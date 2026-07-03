#include "cpu_reference.hpp"
#include "dataset_bridge.hpp"
#include "run_defaults.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "experimental/xrt_bo.h"
#include "experimental/xrt_device.h"
#include "experimental/xrt_kernel.h"

#include "Cuper_common.h"

// Native XRT host for the standalone Chisel RTL SpMV kernel.
//
// The current RTL top is an entry probe, so Y correctness is opt-in.  It still
// prepares the final ownerbank8 ABI: 8 Matrix_data HBM ports, lane-static real
// scalar Y_out, ptr[0..7] per-HBM lengths, and boundary-major batch pointers.
namespace {

template <typename T>
using aligned_vector = std::vector<T, tapa::aligned_allocator<T>>;

using project_xplus::cgsolver::Dataset;
using project_xplus::cgsolver::data_t;

struct HostOptions {
    std::filesystem::path xclbin_path;
    std::filesystem::path dataset_dir;
    unsigned int device_index = project_xplus::cgsolver::run_defaults::kDeviceIndex;
    double diff_tol = 1.0e-3;
    int iteration_num = 1;
    bool check_y = false;
    std::string kernel_name = "CuperSpmvChisel8";
};

struct Chisel8Matrix {
    INDEX_TYPE batch_num = 0;
    INDEX_TYPE matrix_len = 0;
    INDEX_TYPE lane_static_len_total = 0;
    INDEX_TYPE lane_static_len_min = 0;
    INDEX_TYPE lane_static_len_max = 0;
    aligned_vector<INDEX_TYPE> ptr;
    std::vector<aligned_vector<unsigned long>> matrix_data;
    double plan_ms = 0.0;
};

int round_up(const int value, const int align) {
    return ((value + align - 1) / align) * align;
}

double elapsed_ms(const std::chrono::steady_clock::time_point start,
                  const std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

unsigned int parse_uint(const char* text, const char* name) {
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0') {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    return static_cast<unsigned int>(value);
}

int parse_int(const char* text, const char* name) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0') {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    return static_cast<int>(value);
}

double parse_double(const char* text, const char* name) {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || *end != '\0') {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    return value;
}

void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " [xclbin] [dataset_dir] [--device-index value]"
              << " [--iteration-num value] [--diff-tol value]"
              << " [--check-y|--skip-y-check] [--kernel-name value]\n";
}

HostOptions parse_args(int argc, char** argv) {
    HostOptions options;
    options.xclbin_path =
        project_xplus::cgsolver::run_defaults::project_root(argv[0]) /
        "cuper-spmv-chisel8-build" /
        project_xplus::cgsolver::run_defaults::xrt_target() /
        "CuperSpmvChisel8.xclbin";
    options.dataset_dir = project_xplus::cgsolver::run_defaults::dataset_dir(argv[0]);

    int index = 1;
    if (index < argc && std::string(argv[index]).rfind("--", 0) != 0) {
        options.xclbin_path = std::filesystem::path(argv[index++]);
    }
    if (index < argc && std::string(argv[index]).rfind("--", 0) != 0) {
        options.dataset_dir = std::filesystem::path(argv[index++]);
    }

    for (; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--device-index") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--device-index requires a value");
            }
            options.device_index = parse_uint(argv[++index], "device_index");
        } else if (arg == "--iteration-num") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--iteration-num requires a value");
            }
            options.iteration_num = parse_int(argv[++index], "iteration_num");
        } else if (arg == "--diff-tol") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--diff-tol requires a value");
            }
            options.diff_tol = parse_double(argv[++index], "diff_tol");
        } else if (arg == "--check-y") {
            options.check_y = true;
        } else if (arg == "--skip-y-check") {
            options.check_y = false;
        } else if (arg == "--kernel-name") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--kernel-name requires a value");
            }
            options.kernel_name = argv[++index];
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }

    if (options.diff_tol <= 0.0) {
        throw std::runtime_error("--diff-tol must be positive");
    }
    if (options.iteration_num < 0) {
        throw std::runtime_error("--iteration-num must be non-negative");
    }
    if (options.kernel_name.empty()) {
        throw std::runtime_error("--kernel-name must not be empty");
    }
    return options;
}

Chisel8Matrix build_ownerbank8_matrix(const Dataset& dataset) {
    const auto start = std::chrono::steady_clock::now();
    Chisel8Matrix matrix;

    std::vector<INDEX_TYPE> row_ptr(dataset.row_ptr().begin(), dataset.row_ptr().end());
    std::vector<INDEX_TYPE> col_idx(dataset.col_idx().begin(), dataset.col_idx().end());
    std::vector<VALUE_TYPE> values(dataset.values().size(), 0.0f);
    for (std::size_t index = 0; index < dataset.values().size(); ++index) {
        values[index] = static_cast<VALUE_TYPE>(dataset.values()[index]);
    }

    std::vector<INDEX_TYPE> row_idx_coo(static_cast<std::size_t>(dataset.nnz()));
    std::vector<INDEX_TYPE> col_idx_coo(static_cast<std::size_t>(dataset.nnz()));
    std::vector<VALUE_TYPE> val_coo(static_cast<std::size_t>(dataset.nnz()));
    CSR_2_COO(dataset.n(),
              dataset.n(),
              dataset.nnz(),
              row_ptr,
              col_idx,
              values,
              row_idx_coo,
              col_idx_coo,
              val_coo);

    SparseSlice slice_matrix;
    Create_SparseSlice(dataset.n(),
                       dataset.n(),
                       dataset.nnz(),
                       Slice_SIZE,
                       row_idx_coo,
                       col_idx_coo,
                       val_coo,
                       slice_matrix);

    std::vector<std::vector<SpElement>> sp_element_list_pes;
    std::vector<INDEX_TYPE> sp_element_list_ptr;
    Create_SpElement_list_for_all_PEs(HBM_CHANNEL_NUM * PE_NUM,
                                      dataset.n(),
                                      dataset.n(),
                                      Slice_SIZE,
                                      BATCH_SIZE,
                                      slice_matrix,
                                      sp_element_list_pes,
                                      sp_element_list_ptr,
                                      WINDOWS);

    matrix.batch_num = static_cast<INDEX_TYPE>(sp_element_list_ptr.size()) - 1;
    matrix.matrix_len = sp_element_list_ptr[static_cast<std::size_t>(matrix.batch_num)];
    matrix.matrix_data.resize(HBM_CHANNEL_NUM);

    std::vector<INDEX_TYPE> ptr_per_hbm;
    std::vector<INDEX_TYPE> len_per_hbm;
    Create_SpElement_list_for_all_channels_lane_static_real_batch(dataset.n(),
                                                                  Slice_SIZE,
                                                                  BATCH_SIZE,
                                                                  slice_matrix,
                                                                  matrix.matrix_data,
                                                                  ptr_per_hbm,
                                                                  len_per_hbm,
                                                                  HBM_CHANNEL_NUM);

    const INDEX_TYPE ptr_payload_size =
        static_cast<INDEX_TYPE>(HBM_CHANNEL_NUM + ptr_per_hbm.size());
    const INDEX_TYPE ptr_size = ((ptr_payload_size + 15) / 16) * 16;
    const INDEX_TYPE ptr_channel_size = ((ptr_size + 1023) / 1024) * 1024;
    matrix.ptr.assign(static_cast<std::size_t>(ptr_channel_size), 0);

    matrix.lane_static_len_min = len_per_hbm.empty() ? 0 : len_per_hbm.front();
    matrix.lane_static_len_max = len_per_hbm.empty() ? 0 : len_per_hbm.front();
    matrix.lane_static_len_total = 0;
    for (INDEX_TYPE channel = 0; channel < HBM_CHANNEL_NUM; ++channel) {
        matrix.ptr[static_cast<std::size_t>(channel)] = len_per_hbm[static_cast<std::size_t>(channel)];
        matrix.lane_static_len_total += len_per_hbm[static_cast<std::size_t>(channel)];
        matrix.lane_static_len_min =
            std::min(matrix.lane_static_len_min, len_per_hbm[static_cast<std::size_t>(channel)]);
        matrix.lane_static_len_max =
            std::max(matrix.lane_static_len_max, len_per_hbm[static_cast<std::size_t>(channel)]);
    }
    for (std::size_t index = 0; index < ptr_per_hbm.size(); ++index) {
        matrix.ptr[static_cast<std::size_t>(HBM_CHANNEL_NUM) + index] = ptr_per_hbm[index];
    }

    const auto end = std::chrono::steady_clock::now();
    matrix.plan_ms = elapsed_ms(start, end);
    return matrix;
}

aligned_vector<float> pack_float_vector(const std::vector<data_t>& input, const int valid_size) {
    const int column_size = round_up(valid_size, 16);
    const int channel_size = round_up(column_size, 1024);
    aligned_vector<float> output(static_cast<std::size_t>(channel_size), 0.0f);
    for (int index = 0; index < valid_size; ++index) {
        output[static_cast<std::size_t>(index)] =
            static_cast<float>(input[static_cast<std::size_t>(index)]);
    }
    return output;
}

aligned_vector<float> make_zero_float_buffer(const int valid_size) {
    const int column_size = round_up(valid_size, 16);
    const int channel_size = round_up(column_size, 1024);
    return aligned_vector<float>(static_cast<std::size_t>(channel_size), 0.0f);
}

template <typename T, typename Alloc>
xrt::bo make_input_bo(xrt::device& device,
                      xrt::kernel& kernel,
                      const int arg_index,
                      const std::vector<T, Alloc>& data) {
    xrt::bo bo(device, data.size() * sizeof(T), kernel.group_id(arg_index));
    auto mapped = bo.map<T*>();
    std::copy(data.begin(), data.end(), mapped);
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data.size() * sizeof(T), 0);
    return bo;
}

template <typename T, typename Alloc>
xrt::bo make_inout_bo(xrt::device& device,
                      xrt::kernel& kernel,
                      const int arg_index,
                      std::vector<T, Alloc>& data) {
    xrt::bo bo(device, data.size() * sizeof(T), kernel.group_id(arg_index));
    auto mapped = bo.map<T*>();
    std::copy(data.begin(), data.end(), mapped);
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data.size() * sizeof(T), 0);
    return bo;
}

void print_status_raw(const std::vector<std::uint32_t>& status) {
    std::cout << "[status-raw-u32]";
    for (std::size_t index = 0; index < status.size(); ++index) {
        std::cout << " " << index << ":0x"
                  << std::hex << std::setw(8) << std::setfill('0')
                  << status[index]
                  << std::dec << std::setfill(' ');
    }
    std::cout << "\n";
}

void print_metrics_raw(const std::vector<std::uint64_t>& metrics) {
    std::cout << "[metrics-raw-u64]";
    for (std::size_t index = 0; index < metrics.size(); ++index) {
        std::cout << " " << index << ":0x"
                  << std::hex << std::setw(16) << std::setfill('0')
                  << metrics[index]
                  << std::dec << std::setfill(' ');
    }
    std::cout << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto total_start = std::chrono::steady_clock::now();
        const HostOptions options = parse_args(argc, argv);

        const Dataset dataset = Dataset::load(options.dataset_dir);
        Chisel8Matrix matrix = build_ownerbank8_matrix(dataset);

        std::cout << "[xplus-xrt] xclbin=" << options.xclbin_path
                  << " dataset=" << options.dataset_dir
                  << " kernel=" << options.kernel_name
                  << " mode=cuper-spmv-chisel8-entry-probe"
                  << " hbm_channels=" << HBM_CHANNEL_NUM
                  << " batches=" << matrix.batch_num
                  << " matrix_len=" << matrix.matrix_len
                  << " lane_static_read_beats=" << matrix.lane_static_len_total
                  << " per_hbm_len_min=" << matrix.lane_static_len_min
                  << " per_hbm_len_max=" << matrix.lane_static_len_max
                  << "\n";

        const auto xrt_setup_start = std::chrono::steady_clock::now();
        xrt::device device(options.device_index);
        auto uuid = device.load_xclbin(options.xclbin_path.string());
        xrt::kernel kernel(device, uuid, options.kernel_name.c_str());
        const auto xrt_setup_end = std::chrono::steady_clock::now();

        aligned_vector<float> x = pack_float_vector(dataset.b(), dataset.n());
        aligned_vector<float> y = make_zero_float_buffer(dataset.n());
        std::vector<std::uint32_t> status(64, 0x51510000U);
        std::vector<std::uint64_t> metrics(64, 0x4d54524300000000ULL);

        const auto bo_setup_start = std::chrono::steady_clock::now();
        auto ptr_bo = make_input_bo(device, kernel, 0, matrix.ptr);
        auto matrix0_bo = make_input_bo(device, kernel, 1, matrix.matrix_data[0]);
        auto matrix1_bo = make_input_bo(device, kernel, 2, matrix.matrix_data[1]);
        auto matrix2_bo = make_input_bo(device, kernel, 3, matrix.matrix_data[2]);
        auto matrix3_bo = make_input_bo(device, kernel, 4, matrix.matrix_data[3]);
        auto matrix4_bo = make_input_bo(device, kernel, 5, matrix.matrix_data[4]);
        auto matrix5_bo = make_input_bo(device, kernel, 6, matrix.matrix_data[5]);
        auto matrix6_bo = make_input_bo(device, kernel, 7, matrix.matrix_data[6]);
        auto matrix7_bo = make_input_bo(device, kernel, 8, matrix.matrix_data[7]);
        auto x_bo = make_input_bo(device, kernel, 9, x);
        auto y_bo = make_inout_bo(device, kernel, 10, y);
        auto status_bo = make_inout_bo(device, kernel, 11, status);
        auto metrics_bo = make_inout_bo(device, kernel, 12, metrics);
        const auto bo_setup_end = std::chrono::steady_clock::now();

        const auto kernel_start = std::chrono::steady_clock::now();
        auto run = kernel(ptr_bo,
                          matrix0_bo,
                          matrix1_bo,
                          matrix2_bo,
                          matrix3_bo,
                          matrix4_bo,
                          matrix5_bo,
                          matrix6_bo,
                          matrix7_bo,
                          x_bo,
                          y_bo,
                          status_bo,
                          metrics_bo,
                          matrix.batch_num,
                          matrix.matrix_len,
                          dataset.n(),
                          dataset.n(),
                          options.iteration_num);
        run.wait();
        const auto kernel_end = std::chrono::steady_clock::now();

        const auto readback_start = std::chrono::steady_clock::now();
        y_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, y.size() * sizeof(float), 0);
        status_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, status.size() * sizeof(std::uint32_t), 0);
        metrics_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, metrics.size() * sizeof(std::uint64_t), 0);
        auto y_mapped = y_bo.map<float*>();
        auto status_mapped = status_bo.map<std::uint32_t*>();
        auto metrics_mapped = metrics_bo.map<std::uint64_t*>();
        std::copy(y_mapped, y_mapped + y.size(), y.begin());
        std::copy(status_mapped, status_mapped + status.size(), status.begin());
        std::copy(metrics_mapped, metrics_mapped + metrics.size(), metrics.begin());
        const auto readback_end = std::chrono::steady_clock::now();

        std::cout << std::scientific << std::setprecision(12);
        std::cout << "[done] status0=" << std::defaultfloat << status[0]
                  << " magic=0x" << std::hex << status[1] << std::dec
                  << " y0=" << std::scientific << static_cast<double>(y[0])
                  << "\n";
        print_status_raw(status);
        print_metrics_raw(metrics);

        int rc = 0;
        if (options.check_y) {
            std::vector<data_t> expected;
            dataset.spmv(dataset.b(), expected);
            double max_abs_diff = 0.0;
            double max_rel_diff = 0.0;
            for (std::size_t index = 0; index < expected.size(); ++index) {
                const double actual = y[index];
                const double abs_diff = std::fabs(actual - expected[index]);
                const double rel_diff = abs_diff / std::max(std::fabs(expected[index]), 1.0e-12);
                max_abs_diff = std::max(max_abs_diff, abs_diff);
                max_rel_diff = std::max(max_rel_diff, rel_diff);
            }
            std::cout << "[check] max_abs_diff=" << max_abs_diff
                      << " max_rel_diff=" << max_rel_diff
                      << " diff_tol=" << options.diff_tol << "\n";
            if (max_abs_diff > options.diff_tol && max_rel_diff > options.diff_tol) {
                rc = 3;
            }
        } else {
            std::cout << "[check] skipped_y_correctness=1"
                      << " reason=entry_probe_only\n";
        }

        const auto total_end = std::chrono::steady_clock::now();
        std::cout << "[timing-ms] plan=" << matrix.plan_ms
                  << " xrt_setup=" << elapsed_ms(xrt_setup_start, xrt_setup_end)
                  << " bo_setup=" << elapsed_ms(bo_setup_start, bo_setup_end)
                  << " kernel=" << elapsed_ms(kernel_start, kernel_end)
                  << " readback=" << elapsed_ms(readback_start, readback_end)
                  << " total=" << elapsed_ms(total_start, total_end)
                  << "\n";
        return rc;
    } catch (const std::exception& error) {
        usage(argv[0]);
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
