#include "cpu_reference.hpp"
#include "pcg_common.hpp"
#include "run_defaults.hpp"

#include <ap_int.h>
#include <tapa.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "Cuper.h"
#include "Cuper_common.h"

#include "experimental/xrt_bo.h"
#include "experimental/xrt_device.h"
#include "experimental/xrt_ip.h"

namespace {

template <typename T>
using AlignedVector = std::vector<T, tapa::aligned_allocator<T>>;

using project_xplus::cgsolver::Dataset;
using project_xplus::cgsolver::data_t;

struct CliOptions {
    std::filesystem::path dataset_dir;
    std::string bitstream;
    double tau = project_xplus::cgsolver::run_defaults::kTau;
    int max_iters = 0;
    double diff_tol = 1.0e-3;
    int kernel_timeout_sec = 60;
    int live_status_poll_sec = 0;
};

struct CuperTapaMatrix {
    INDEX_TYPE batch_num = 0;
    INDEX_TYPE matrix_len = 0;
    INDEX_TYPE stripped_matrix_len_total = 0;
    INDEX_TYPE stripped_matrix_len_min = 0;
    INDEX_TYPE stripped_matrix_len_max = 0;
    bool strip_padding = false;
    AlignedVector<INDEX_TYPE> sp_element_list_ptr;
    std::vector<AlignedVector<unsigned long>> matrix_data;
    double plan_ms = 0.0;
};

struct CpuExactResult {
    std::vector<double> solution;
    int iterations = 0;
    double final_rr = 0.0;
    bool converged = false;
};

constexpr double kKernelClockPeriodNs = 3.3;
constexpr INDEX_TYPE kHostStatusConverged = 0;
constexpr INDEX_TYPE kHostStatusMaxIter = 1;
constexpr INDEX_TYPE kHostStatusBreakdown = 2;

constexpr std::array<int, 28> kMemoryGroups = {
    16,  // SpElement_list_ptr
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 10, 11, 12, 13, 14, 15,
    17, 18,  // X[0], X[1]
    19, 20,  // P[0], P[1]
    21,      // AP
    22, 23,  // R[0], R[1]
    24,      // M_inv
    25,      // Residuals
    30,      // Status
    31,      // Metrics
};

void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " [dataset_dir] [--bitstream path] [--tau value]"
              << " [--max-iters value] [--diff-tol value]"
              << " [--kernel-timeout-sec value] [--live-status-poll-sec value]\n";
}

std::string env_or_empty(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

CliOptions parse_args(int argc, char** argv) {
    CliOptions options;
    options.dataset_dir = project_xplus::cgsolver::run_defaults::dataset_dir(argv[0]);
    options.bitstream = env_or_empty("BITFILE");
    if (const char* env = std::getenv("MAX_ITERS")) {
        options.max_iters = std::atoi(env);
    }
    if (const char* env = std::getenv("TAU")) {
        options.tau = std::atof(env);
    }
    if (const char* env = std::getenv("DIFF_TOL")) {
        options.diff_tol = std::atof(env);
    }
    if (const char* env = std::getenv("KERNEL_TIMEOUT_SEC")) {
        options.kernel_timeout_sec = std::atoi(env);
    }
    if (const char* env = std::getenv("LIVE_STATUS_POLL_SEC")) {
        options.live_status_poll_sec = std::atoi(env);
    }

    int index = 1;
    if (index < argc && std::string(argv[index]).rfind("--", 0) != 0) {
        options.dataset_dir = argv[index++];
    }
    for (; index < argc; ++index) {
        const std::string arg = argv[index];
        auto require_value = [&](const char* name) -> const char* {
            if (index + 1 >= argc) {
                throw std::runtime_error(std::string(name) + " requires a value");
            }
            return argv[++index];
        };
        if (arg == "--bitstream") {
            options.bitstream = require_value("--bitstream");
        } else if (arg == "--tau") {
            options.tau = std::stod(require_value("--tau"));
        } else if (arg == "--max-iters") {
            options.max_iters = std::stoi(require_value("--max-iters"));
        } else if (arg == "--diff-tol") {
            options.diff_tol = std::stod(require_value("--diff-tol"));
        } else if (arg == "--kernel-timeout-sec") {
            options.kernel_timeout_sec = std::stoi(require_value("--kernel-timeout-sec"));
        } else if (arg == "--live-status-poll-sec") {
            options.live_status_poll_sec = std::stoi(require_value("--live-status-poll-sec"));
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }

    if (options.tau <= 0.0 || !std::isfinite(options.tau)) {
        throw std::runtime_error("--tau must be positive and finite");
    }
    if (options.max_iters < 0) {
        throw std::runtime_error("--max-iters must be non-negative");
    }
    if (options.diff_tol <= 0.0 || !std::isfinite(options.diff_tol)) {
        throw std::runtime_error("--diff-tol must be positive and finite");
    }
    if (options.kernel_timeout_sec < 0 || options.live_status_poll_sec < 0) {
        throw std::runtime_error("timeout/poll values must be non-negative");
    }
    return options;
}

int round_up(const int value, const int align) {
    return ((value + align - 1) / align) * align;
}

double elapsed_ms(const std::chrono::steady_clock::time_point start,
                  const std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

CuperTapaMatrix build_tapa_matrix(const Dataset& dataset) {
    const auto start = std::chrono::steady_clock::now();
    CuperTapaMatrix matrix;

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

#ifdef JACOBI_SPMV_STRIP_PADDING
    matrix.matrix_data.resize(HBM_CHANNEL_NUM);
    std::vector<INDEX_TYPE> matrix_len_per_hbm;
    std::vector<INDEX_TYPE> sp_element_list_ptr_per_hbm;
    Create_SpElement_list_for_all_channels_strip_hbm_padding(sp_element_list_pes,
                                                             sp_element_list_ptr,
                                                             matrix.matrix_data,
                                                             sp_element_list_ptr_per_hbm,
                                                             matrix_len_per_hbm,
                                                             HBM_CHANNEL_NUM);

    const INDEX_TYPE ptr_payload_size = static_cast<INDEX_TYPE>(
        HBM_CHANNEL_NUM + sp_element_list_ptr_per_hbm.size());
    const INDEX_TYPE ptr_size = round_up(static_cast<int>(ptr_payload_size), 16);
    const INDEX_TYPE ptr_channel_size = round_up(ptr_size, 1024);
    matrix.sp_element_list_ptr.assign(static_cast<std::size_t>(ptr_channel_size), 0);
    for (INDEX_TYPE channel = 0; channel < HBM_CHANNEL_NUM; ++channel) {
        matrix.sp_element_list_ptr[static_cast<std::size_t>(channel)] =
            matrix_len_per_hbm[static_cast<std::size_t>(channel)];
    }
    for (INDEX_TYPE index = 0;
         index < static_cast<INDEX_TYPE>(sp_element_list_ptr_per_hbm.size());
         ++index) {
        matrix.sp_element_list_ptr[static_cast<std::size_t>(HBM_CHANNEL_NUM + index)] =
            sp_element_list_ptr_per_hbm[static_cast<std::size_t>(index)];
    }

    matrix.strip_padding = true;
    matrix.stripped_matrix_len_total = 0;
    matrix.stripped_matrix_len_min = matrix_len_per_hbm.empty() ? 0 : matrix_len_per_hbm[0];
    matrix.stripped_matrix_len_max = matrix_len_per_hbm.empty() ? 0 : matrix_len_per_hbm[0];
    for (INDEX_TYPE channel = 0; channel < HBM_CHANNEL_NUM; ++channel) {
        const INDEX_TYPE len = matrix_len_per_hbm[static_cast<std::size_t>(channel)];
        matrix.stripped_matrix_len_total += len;
        matrix.stripped_matrix_len_min = std::min(matrix.stripped_matrix_len_min, len);
        matrix.stripped_matrix_len_max = std::max(matrix.stripped_matrix_len_max, len);
    }
#else
    const INDEX_TYPE ptr_size =
        round_up(static_cast<int>(sp_element_list_ptr.size()), 16);
    const INDEX_TYPE ptr_channel_size = round_up(ptr_size, 1024);
    matrix.sp_element_list_ptr.assign(static_cast<std::size_t>(ptr_channel_size), 0);
    std::copy(sp_element_list_ptr.begin(),
              sp_element_list_ptr.end(),
              matrix.sp_element_list_ptr.begin());

    matrix.matrix_data.resize(HBM_CHANNEL_NUM);
    Create_SpElement_list_for_all_channels(sp_element_list_pes,
                                           sp_element_list_ptr,
                                           matrix.matrix_data,
                                           HBM_CHANNEL_NUM);
#endif

    const auto end = std::chrono::steady_clock::now();
    matrix.plan_ms = elapsed_ms(start, end);
    return matrix;
}

template <typename T>
AlignedVector<T> aligned_copy(const std::vector<T>& input, const int align = 1024) {
    AlignedVector<T> output(static_cast<std::size_t>(round_up(static_cast<int>(input.size()), align)), T{});
    std::copy(input.begin(), input.end(), output.begin());
    return output;
}

AlignedVector<double_v8> pack_double_v8(const std::vector<double>& input,
                                        const int valid_size,
                                        const int align_packets = 1024) {
    const int packet_count = (valid_size + 7) >> 3;
    AlignedVector<double_v8> output(static_cast<std::size_t>(round_up(packet_count, align_packets)));
    for (double_v8& packet : output) {
        for (int lane = 0; lane < 8; ++lane) {
            packet[lane] = 0.0;
        }
    }
    for (int packet_index = 0; packet_index < packet_count; ++packet_index) {
        double_v8 packet;
        for (int lane = 0; lane < 8; ++lane) {
            const int index = (packet_index << 3) + lane;
            packet[lane] = index < valid_size ? input[static_cast<std::size_t>(index)] : 0.0;
        }
        output[static_cast<std::size_t>(packet_index)] = packet;
    }
    return output;
}

void unpack_double_v8(const AlignedVector<double_v8>& input,
                      const int valid_size,
                      std::vector<double>& output) {
    output.assign(static_cast<std::size_t>(valid_size), 0.0);
    const int packet_count = (valid_size + 7) >> 3;
    for (int packet_index = 0; packet_index < packet_count; ++packet_index) {
        const double_v8 packet = input[static_cast<std::size_t>(packet_index)];
        for (int lane = 0; lane < 8; ++lane) {
            const int index = (packet_index << 3) + lane;
            if (index < valid_size) {
                output[static_cast<std::size_t>(index)] = packet[lane];
            }
        }
    }
}

AlignedVector<float_v16> make_zero_float_v16(const int valid_size,
                                             const int align_packets = 1024) {
    const int packet_count = (valid_size + 15) >> 4;
    AlignedVector<float_v16> output(static_cast<std::size_t>(round_up(packet_count, align_packets)));
    for (float_v16& packet : output) {
        for (int lane = 0; lane < 16; ++lane) {
            packet[lane] = 0.0f;
        }
    }
    return output;
}

std::vector<double> validate_jacobi_inverse(const Dataset& dataset) {
    std::vector<double> minv = project_xplus::cgsolver::build_jacobi_inverse(dataset);
    for (std::size_t index = 0; index < minv.size(); ++index) {
        if (!std::isfinite(minv[index])) {
            throw std::runtime_error("non-finite Jacobi inverse at row " +
                                     std::to_string(index));
        }
    }
    return minv;
}

double dot(const std::vector<double>& a, const std::vector<double>& b) {
    double acc = 0.0;
    for (std::size_t index = 0; index < a.size(); ++index) {
        acc += a[index] * b[index];
    }
    return acc;
}

CpuExactResult run_cpu_exact(const Dataset& dataset,
                             const std::vector<double>& minv,
                             const double tau,
                             const int max_iters) {
    CpuExactResult result;
    result.solution = dataset.x0();

    std::vector<double> ax0;
    dataset.spmv(result.solution, ax0);
    std::vector<double> r(static_cast<std::size_t>(dataset.n()), 0.0);
    std::vector<double> z(static_cast<std::size_t>(dataset.n()), 0.0);
    std::vector<double> p(static_cast<std::size_t>(dataset.n()), 0.0);
    for (int index = 0; index < dataset.n(); ++index) {
        r[static_cast<std::size_t>(index)] =
            dataset.b()[static_cast<std::size_t>(index)] - ax0[static_cast<std::size_t>(index)];
        z[static_cast<std::size_t>(index)] =
            minv[static_cast<std::size_t>(index)] * r[static_cast<std::size_t>(index)];
        p[static_cast<std::size_t>(index)] = z[static_cast<std::size_t>(index)];
    }

    double rz = dot(r, z);
    double rr = dot(r, r);
    result.final_rr = rr;
    result.converged = rr <= tau;

    for (int iter = 0; iter < max_iters && rr > tau; ++iter) {
        std::vector<double> ap;
        dataset.spmv(p, ap);
        const double pap = dot(p, ap);
        if (std::fabs(pap) <= project_xplus::cgsolver::kBreakdownEps) {
            throw std::runtime_error("CPU reference breakdown: p^T A p is too small");
        }
        const double alpha = rz / pap;
        for (int index = 0; index < dataset.n(); ++index) {
            result.solution[static_cast<std::size_t>(index)] +=
                alpha * p[static_cast<std::size_t>(index)];
            r[static_cast<std::size_t>(index)] -=
                alpha * ap[static_cast<std::size_t>(index)];
            z[static_cast<std::size_t>(index)] =
                minv[static_cast<std::size_t>(index)] * r[static_cast<std::size_t>(index)];
        }
        const double rz_new = dot(r, z);
        const double rr_new = dot(r, r);
        const double beta = rz_new / rz;
        rz = rz_new;
        rr = rr_new;
        result.iterations = iter + 1;
        result.final_rr = rr;
        if (rr <= tau) {
            break;
        }
        for (int index = 0; index < dataset.n(); ++index) {
            p[static_cast<std::size_t>(index)] =
                z[static_cast<std::size_t>(index)] +
                beta * p[static_cast<std::size_t>(index)];
        }
    }
    result.converged = result.final_rr <= tau;
    return result;
}

template <typename Vector>
xrt::bo make_bo(xrt::device& device,
                const int arg_index,
                const Vector& data,
                const bool initialize) {
    using T = typename Vector::value_type;
    xrt::bo bo(device,
               data.size() * sizeof(T),
               kMemoryGroups[static_cast<std::size_t>(arg_index)]);
    auto mapped = bo.map<T*>();
    if (initialize) {
        std::copy(data.begin(), data.end(), mapped);
    } else {
        std::fill(mapped, mapped + data.size(), T{});
    }
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data.size() * sizeof(T), 0);
    return bo;
}

template <typename Vector>
void sync_from_bo(xrt::bo& bo, Vector& data) {
    using T = typename Vector::value_type;
    bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, data.size() * sizeof(T), 0);
    const auto mapped = bo.map<T*>();
    std::copy(mapped, mapped + data.size(), data.begin());
}

void write_reg_u64(xrt::ip& ip, const uint32_t offset, const uint64_t value) {
    ip.write_register(offset, static_cast<uint32_t>(value));
    ip.write_register(offset + 4, static_cast<uint32_t>(value >> 32));
}

void write_reg_i32(xrt::ip& ip, const uint32_t offset, const int value) {
    ip.write_register(offset, static_cast<uint32_t>(value));
}

void write_reg_double(xrt::ip& ip, const uint32_t offset, const double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    write_reg_u64(ip, offset, bits);
}

xrt::ip open_ip(const xrt::device& device, const xrt::uuid& uuid) {
    const std::array<std::string, 3> names = {
        "CuperPcgCallipepla:CuperPcgCallipepla_1",
        "CuperPcgCallipepla_1",
        "CuperPcgCallipepla",
    };
    std::string errors;
    for (const std::string& name : names) {
        try {
            return xrt::ip(device, uuid, name);
        } catch (const std::exception& error) {
            errors += " [" + name + ": " + error.what() + "]";
        }
    }
    throw std::runtime_error("failed to open CuperPcgCallipepla xrt::ip:" + errors);
}

double run_xrt(const CliOptions& options,
               const Dataset& dataset,
               const CuperTapaMatrix& matrix,
               std::vector<AlignedVector<double_v8>>& x_banks,
               std::vector<AlignedVector<double_v8>>& p_banks,
               AlignedVector<float_v16>& ap,
               std::vector<AlignedVector<double_v8>>& r_banks,
               AlignedVector<double_v8>& minv,
               AlignedVector<double>& residuals,
               AlignedVector<INDEX_TYPE>& status,
               AlignedVector<double>& metrics) {
    xrt::device device(project_xplus::cgsolver::run_defaults::kDeviceIndex);
    auto uuid = device.load_xclbin(options.bitstream);

    auto sp_ptr_bo = make_bo(device, 0, matrix.sp_element_list_ptr, true);
    std::vector<xrt::bo> matrix_bos;
    matrix_bos.reserve(HBM_CHANNEL_NUM);
    for (int channel = 0; channel < HBM_CHANNEL_NUM; ++channel) {
        matrix_bos.emplace_back(make_bo(device, 1 + channel, matrix.matrix_data[channel], true));
    }
    auto x0_bo = make_bo(device, 17, x_banks[0], true);
    auto x1_bo = make_bo(device, 18, x_banks[1], true);
    auto p0_bo = make_bo(device, 19, p_banks[0], true);
    auto p1_bo = make_bo(device, 20, p_banks[1], true);
    auto ap_bo = make_bo(device, 21, ap, true);
    auto r0_bo = make_bo(device, 22, r_banks[0], true);
    auto r1_bo = make_bo(device, 23, r_banks[1], true);
    auto minv_bo = make_bo(device, 24, minv, true);
    auto residuals_bo = make_bo(device, 25, residuals, false);
    auto status_bo = make_bo(device, 26, status, false);
    auto metrics_bo = make_bo(device, 27, metrics, false);

    xrt::ip ip = open_ip(device, uuid);
    const std::array<std::pair<uint32_t, uint64_t>, 28> pointer_args = {{
        {0x010, sp_ptr_bo.address()},
        {0x01c, matrix_bos[0].address()},
        {0x028, matrix_bos[1].address()},
        {0x034, matrix_bos[2].address()},
        {0x040, matrix_bos[3].address()},
        {0x04c, matrix_bos[4].address()},
        {0x058, matrix_bos[5].address()},
        {0x064, matrix_bos[6].address()},
        {0x070, matrix_bos[7].address()},
        {0x07c, matrix_bos[8].address()},
        {0x088, matrix_bos[9].address()},
        {0x094, matrix_bos[10].address()},
        {0x0a0, matrix_bos[11].address()},
        {0x0ac, matrix_bos[12].address()},
        {0x0b8, matrix_bos[13].address()},
        {0x0c4, matrix_bos[14].address()},
        {0x0d0, matrix_bos[15].address()},
        {0x0dc, x0_bo.address()},
        {0x0e8, x1_bo.address()},
        {0x0f4, p0_bo.address()},
        {0x100, p1_bo.address()},
        {0x10c, ap_bo.address()},
        {0x118, r0_bo.address()},
        {0x124, r1_bo.address()},
        {0x130, minv_bo.address()},
        {0x13c, residuals_bo.address()},
        {0x148, status_bo.address()},
        {0x154, metrics_bo.address()},
    }};
    for (const auto& [offset, address] : pointer_args) {
        write_reg_u64(ip, offset, address);
    }
    write_reg_i32(ip, 0x160, matrix.batch_num);
    write_reg_i32(ip, 0x168, matrix.matrix_len);
    write_reg_i32(ip, 0x170, dataset.n());
    write_reg_i32(ip, 0x178, dataset.n());
    write_reg_i32(ip, 0x180, options.max_iters);
    write_reg_double(ip, 0x188, options.tau);

    const auto kernel_start = std::chrono::steady_clock::now();
    ip.write_register(0x00, 0x01);

    bool completed = false;
    uint32_t last_ctrl = 0;
    auto next_poll = kernel_start + std::chrono::seconds(options.live_status_poll_sec);
    while (true) {
        last_ctrl = ip.read_register(0x00);
        if ((last_ctrl & 0x02) != 0 || (last_ctrl & 0x04) != 0) {
            completed = true;
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        if (options.live_status_poll_sec > 0 && now >= next_poll) {
            sync_from_bo(status_bo, status);
            sync_from_bo(metrics_bo, metrics);
            sync_from_bo(residuals_bo, residuals);
            std::cerr << "[live-status] phase=" << status[8]
                      << " iter=" << status[9]
                      << " x=" << status[10]
                      << " r=" << status[11]
                      << " p=" << status[12]
                      << " spmv_rounds=" << status[13]
                      << " rr=" << metrics[1]
                      << " residual0=" << residuals[0]
                      << " ctrl=0x" << std::hex << last_ctrl << std::dec << "\n";
            next_poll = now + std::chrono::seconds(options.live_status_poll_sec);
        }
        if (options.kernel_timeout_sec > 0 &&
            now - kernel_start > std::chrono::seconds(options.kernel_timeout_sec)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    sync_from_bo(status_bo, status);
    sync_from_bo(metrics_bo, metrics);
    sync_from_bo(residuals_bo, residuals);
    sync_from_bo(x0_bo, x_banks[0]);
    sync_from_bo(x1_bo, x_banks[1]);
    sync_from_bo(p0_bo, p_banks[0]);
    sync_from_bo(p1_bo, p_banks[1]);
    sync_from_bo(r0_bo, r_banks[0]);
    sync_from_bo(r1_bo, r_banks[1]);

    if (!completed) {
        throw std::runtime_error("kernel timeout before completion, ctrl=0x" +
                                 std::to_string(last_ctrl));
    }

    return elapsed_ms(kernel_start, std::chrono::steady_clock::now());
}

double stage_cycles_to_ms(const double cycles) {
    return cycles * kKernelClockPeriodNs * 1.0e-6;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto total_start = std::chrono::steady_clock::now();
        const CliOptions options = parse_args(argc, argv);
        const Dataset dataset = Dataset::load(options.dataset_dir);
        const std::vector<double> minv_host = validate_jacobi_inverse(dataset);
        const CpuExactResult reference =
            run_cpu_exact(dataset, minv_host, options.tau, options.max_iters);

        CuperTapaMatrix matrix = build_tapa_matrix(dataset);
        std::vector<double> zero_vec(static_cast<std::size_t>(dataset.n()), 0.0);

        std::vector<AlignedVector<double_v8>> x_banks(2);
        std::vector<AlignedVector<double_v8>> p_banks(2);
        std::vector<AlignedVector<double_v8>> r_banks(2);
        x_banks[0] = pack_double_v8(dataset.x0(), dataset.n());
        x_banks[1] = pack_double_v8(zero_vec, dataset.n());
        p_banks[0] = pack_double_v8(dataset.x0(), dataset.n());
        p_banks[1] = pack_double_v8(zero_vec, dataset.n());
        r_banks[0] = pack_double_v8(dataset.b(), dataset.n());
        r_banks[1] = pack_double_v8(zero_vec, dataset.n());
        AlignedVector<double_v8> minv = pack_double_v8(minv_host, dataset.n());
        AlignedVector<float_v16> ap = make_zero_float_v16(dataset.n());
        AlignedVector<double> residuals(
            static_cast<std::size_t>(round_up(std::max(options.max_iters + 2, 16), 1024)),
            0.0);
        AlignedVector<INDEX_TYPE> status(1024, 0);
        AlignedVector<double> metrics(1024, 0.0);

        std::cout << "[xplus] dataset=" << options.dataset_dir
                  << " mode=cuper-tapa-pcg-callipepla"
                  << " kernel=CuperPcgCallipepla"
                  << " bitstream=" << (options.bitstream.empty() ? "<software-sim>" : options.bitstream)
                  << " batches=" << matrix.batch_num
                  << " matrix_len=" << matrix.matrix_len
                  << " strip_padding=" << (matrix.strip_padding ? 1 : 0)
                  << " max_iters=" << options.max_iters
                  << "\n";
        if (matrix.strip_padding) {
            const INDEX_TYPE original_read_beats = matrix.matrix_len * HBM_CHANNEL_NUM;
            const INDEX_TYPE saved_beats =
                original_read_beats - matrix.stripped_matrix_len_total;
            const double saved_pct =
                original_read_beats == 0
                    ? 0.0
                    : 100.0 * static_cast<double>(saved_beats) /
                          static_cast<double>(original_read_beats);
            std::cout << "[spmv-strip-padding] original_read_beats="
                      << original_read_beats
                      << " stripped_read_beats=" << matrix.stripped_matrix_len_total
                      << " saved_beats=" << saved_beats
                      << " saved_pct=" << saved_pct
                      << " per_hbm_len_min=" << matrix.stripped_matrix_len_min
                      << " per_hbm_len_max=" << matrix.stripped_matrix_len_max
                      << "\n";
        }

        const auto kernel_start = std::chrono::steady_clock::now();
        double kernel_reported_ms = 0.0;
        if (options.bitstream.empty()) {
            const double kernel_ns = tapa::invoke(
                CuperPcgCallipepla,
                options.bitstream,
                tapa::read_only_mmap<INDEX_TYPE>(matrix.sp_element_list_ptr),
                tapa::read_only_mmaps<unsigned long, HBM_CHANNEL_NUM>(matrix.matrix_data)
                    .reinterpret<ap_uint<512>>(),
                tapa::read_write_mmap<double_v8>(x_banks[0]),
                tapa::read_write_mmap<double_v8>(x_banks[1]),
                tapa::read_write_mmap<double_v8>(p_banks[0]),
                tapa::read_write_mmap<double_v8>(p_banks[1]),
                tapa::read_write_mmap<float_v16>(ap),
                tapa::read_write_mmap<double_v8>(r_banks[0]),
                tapa::read_write_mmap<double_v8>(r_banks[1]),
                tapa::read_only_mmap<double_v8>(minv),
                tapa::write_only_mmap<double>(residuals),
                tapa::write_only_mmap<INDEX_TYPE>(status),
                tapa::write_only_mmap<double>(metrics),
                matrix.batch_num,
                matrix.matrix_len,
                dataset.n(),
                dataset.n(),
                options.max_iters,
                options.tau);
            kernel_reported_ms = kernel_ns * 1.0e-6;
        } else {
            kernel_reported_ms = run_xrt(options,
                                         dataset,
                                         matrix,
                                         x_banks,
                                         p_banks,
                                         ap,
                                         r_banks,
                                         minv,
                                         residuals,
                                         status,
                                         metrics);
        }
        const auto kernel_end = std::chrono::steady_clock::now();

        const int final_x_bank = status[2] == 1 ? 1 : 0;
        std::vector<double> solution;
        unpack_double_v8(x_banks[static_cast<std::size_t>(final_x_bank)],
                         dataset.n(),
                         solution);

        double max_abs_diff = 0.0;
        double max_rel_diff = 0.0;
        for (std::size_t index = 0; index < solution.size(); ++index) {
            const double expected = reference.solution[index];
            const double abs_diff = std::fabs(solution[index] - expected);
            const double rel_diff = abs_diff / std::max(std::fabs(expected), 1.0e-12);
            max_abs_diff = std::max(max_abs_diff, abs_diff);
            max_rel_diff = std::max(max_rel_diff, rel_diff);
        }
        const double residual_l2 =
            project_xplus::cgsolver::compute_residual_norm(dataset, solution);
        const auto total_end = std::chrono::steady_clock::now();

        const char* status_name =
            status[0] == kHostStatusConverged ? "converged" :
            status[0] == kHostStatusMaxIter ? "max_iter" :
            status[0] == kHostStatusBreakdown ? "breakdown" : "unknown";

        std::cout << std::scientific << std::setprecision(12);
        std::cout << "[done] iter=" << status[1]
                  << " status=" << status_name
                  << " final_x_bank=" << status[2]
                  << " final_r_bank=" << status[3]
                  << " final_p_bank=" << status[4]
                  << " rr=" << metrics[1]
                  << "\n";
        std::cout << "[check] cpu_iter=" << reference.iterations
                  << " cpu_rr=" << reference.final_rr
                  << " cpu_residual_abs=" << project_xplus::cgsolver::compute_residual_norm(dataset, reference.solution)
                  << " cuper_residual_abs=" << residual_l2
                  << " max_abs_diff=" << max_abs_diff
                  << " max_rel_diff=" << max_rel_diff
                  << " diff_tol=" << options.diff_tol
                  << "\n";
        std::cout << "[timing-ms] plan=" << matrix.plan_ms
                  << " kernel_reported=" << kernel_reported_ms
                  << " kernel_wall=" << elapsed_ms(kernel_start, kernel_end)
                  << " total=" << elapsed_ms(total_start, total_end)
                  << "\n";
        std::cout << std::fixed << std::setprecision(0);
        std::cout << "[stage-work-packets]"
                  << " float_v16_packets=" << metrics[5]
                  << " double_v8_packets=" << metrics[6]
                  << " spmv_rounds=" << metrics[7]
                  << " init_spmv=" << metrics[8]
                  << " init_zp=" << metrics[9]
                  << " iter_spmv=" << metrics[10]
                  << " vector_update_total=" << metrics[11]
                  << " residual_writes=" << metrics[12]
                  << " total_work=" << metrics[15]
                  << "\n";
        std::cout << std::scientific << std::setprecision(12);
        std::cout << "[stage-ms]"
                  << " init_spmv=" << stage_cycles_to_ms(metrics[16])
                  << " init_zp=" << stage_cycles_to_ms(metrics[17])
                  << " iter_spmv=" << stage_cycles_to_ms(metrics[18])
                  << " dot_alpha=" << stage_cycles_to_ms(metrics[19])
                  << " update_x=" << stage_cycles_to_ms(metrics[20])
                  << " update_r=" << stage_cycles_to_ms(metrics[21])
                  << " apply_m_inv=" << stage_cycles_to_ms(metrics[22])
                  << " dot_rz=" << stage_cycles_to_ms(metrics[23])
                  << " update_p=" << stage_cycles_to_ms(metrics[24])
                  << " dot_residual=" << stage_cycles_to_ms(metrics[25])
                  << " round=" << stage_cycles_to_ms(metrics[26])
                  << " total=" << stage_cycles_to_ms(metrics[27])
                  << " timer_total=" << stage_cycles_to_ms(metrics[28])
                  << "\n";
        std::cout << "[residuals] init=" << residuals[0];
        const int residual_count = std::min(options.max_iters + 1, 8);
        for (int index = 1; index < residual_count; ++index) {
            std::cout << " r" << index << "=" << residuals[static_cast<std::size_t>(index)];
        }
        std::cout << "\n";

        if (status[0] != kHostStatusConverged &&
            status[0] != kHostStatusMaxIter) {
            return 2;
        }
        if (status[1] != reference.iterations) {
            return 3;
        }
        if (max_abs_diff > options.diff_tol && max_rel_diff > options.diff_tol) {
            return 4;
        }
        return 0;
    } catch (const std::exception& error) {
        usage(argv[0]);
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
