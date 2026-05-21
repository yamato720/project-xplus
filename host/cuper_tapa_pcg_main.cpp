#include "cuper_pcg_solver.hpp"
#include "cpu_reference.hpp"
#include "run_defaults.hpp"

#include <ap_int.h>
#include <tapa.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Cuper.h"
#include "Cuper_common.h"

namespace {

template <typename T>
using AlignedVector = std::vector<T, tapa::aligned_allocator<T>>;

using project_xplus::cgsolver::CuperPcgBackendInfo;
using project_xplus::cgsolver::CuperPcgResult;
using project_xplus::cgsolver::Dataset;
using project_xplus::cgsolver::SolverConfig;
using project_xplus::cgsolver::data_t;

struct CliOptions {
    std::filesystem::path dataset_dir;
    std::string bitstream;
    double tau = project_xplus::cgsolver::run_defaults::kTau;
    int max_iters = project_xplus::cgsolver::run_defaults::kMaxIters;
    double diff_tol = 1.0e-3;
};

void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " [dataset_dir] [--bitstream path] [--tau value] [--max-iters value]"
              << " [--diff-tol value]\n";
}

std::string env_or_empty(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

CliOptions parse_args(int argc, char** argv) {
    CliOptions options;
    options.dataset_dir = project_xplus::cgsolver::run_defaults::dataset_dir(argv[0]);
    options.bitstream = env_or_empty("BITFILE");

    int index = 1;
    if (index < argc && std::string(argv[index]).rfind("--", 0) != 0) {
        options.dataset_dir = std::filesystem::path(argv[index++]);
    }

    for (; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--bitstream") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--bitstream requires a value");
            }
            options.bitstream = argv[++index];
        } else if (arg == "--tau") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--tau requires a value");
            }
            options.tau = std::stod(argv[++index]);
        } else if (arg == "--max-iters") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--max-iters requires a value");
            }
            options.max_iters = std::stoi(argv[++index]);
        } else if (arg == "--diff-tol") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--diff-tol requires a value");
            }
            options.diff_tol = std::stod(argv[++index]);
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }

    if (options.tau <= 0.0) {
        throw std::runtime_error("--tau must be positive");
    }
    if (options.max_iters < 0) {
        throw std::runtime_error("--max-iters must be non-negative");
    }
    if (options.diff_tol <= 0.0) {
        throw std::runtime_error("--diff-tol must be positive");
    }

    return options;
}

int round_up(const int value, const int align) {
    return ((value + align - 1) / align) * align;
}

// 把 DLC/Cuper 的 TAPA kernel 包装成 PCG 求解器可调用的 SpMV 后端。
//
// 这个类只负责：
//   1. 把 Project-XPlus 的 Dataset 转成 Cuper/TAPA 需要的 HBM 数据布局
//   2. 把 host 侧 PCG 当前输入向量拷到 X buffer
//   3. 调用 TAPA 顶层 Cuper kernel 做一次 y = A * x
//   4. 把 Y_out 拷回 host 侧 PCG 的 output 向量
//
// PCG 的 alpha/beta、r/z/p 更新、收敛判断都不在这里；
// 它们在 host/cuper_pcg_solver.hpp 的 run_cuper_pcg_solver_with_backend 中。
class CuperTapaSpmv {
  public:
    CuperTapaSpmv(const Dataset& dataset, std::string bitstream)
        : n_(dataset.n()), bitstream_(std::move(bitstream)) {
        const auto start = std::chrono::steady_clock::now();
        build_matrix(dataset);
        allocate_vectors();
        const auto end = std::chrono::steady_clock::now();
        plan_ms_ = std::chrono::duration<double, std::milli>(end - start).count();
    }

    CuperPcgBackendInfo backend_info() const {
        return CuperPcgBackendInfo{
            bitstream_.empty() ? "tapa-cuper-sw-sim-fp32-spmv+fp64-pcg"
                               : "tapa-cuper-hw-fp32-spmv+fp64-pcg",
            Slice_WIDTH,
            static_cast<std::size_t>(batch_num_),
        };
    }

    double plan_ms() const { return plan_ms_; }

    void operator()(const std::vector<data_t>& input,
                    std::vector<data_t>& output,
                    CuperPcgResult& result) {
        std::fill(x_fpga_data_.begin(), x_fpga_data_.end(), 0.0f);
        std::fill(y_fpga_data_out_.begin(), y_fpga_data_out_.end(), 0.0f);

        for (int index = 0; index < n_; ++index) {
            x_fpga_data_[static_cast<std::size_t>(index)] =
                static_cast<VALUE_TYPE>(input[static_cast<std::size_t>(index)]);
        }

        // 每次 operator() 对应 host-side PCG 的一次 SpMV 调用。
        // Iteration_num 固定传 1，避免把 Cuper kernel 内部的性能重复计数
        // 和 PCG 迭代次数混在一起。
        const double kernel_ns = tapa::invoke(
            Cuper,
            bitstream_,
            tapa::read_only_mmap<INDEX_TYPE>(sp_element_list_ptr_fpga_),
            tapa::read_only_mmaps<unsigned long, HBM_CHANNEL_NUM>(matrix_fpga_data_)
                .reinterpret<ap_uint<512>>(),
            tapa::read_only_mmap<float>(x_fpga_data_).reinterpret<float_v16>(),
            tapa::write_only_mmap<float>(y_fpga_data_out_).reinterpret<float_v16>(),
            batch_num_,
            matrix_len_,
            n_,
            n_,
            1);

        output.assign(static_cast<std::size_t>(n_), 0.0);
        for (int index = 0; index < n_; ++index) {
            output[static_cast<std::size_t>(index)] =
                static_cast<data_t>(y_fpga_data_out_[static_cast<std::size_t>(index)]);
        }

        result.timing.spmv_ms += kernel_ns * 1.0e-6;
        ++result.timing.spmv_calls;
    }

  private:
    void build_matrix(const Dataset& dataset) {
        // 这里把 Project-XPlus 的 CSR 数据集转换为 DLC/Cuper 的输入格式。
        // 转换链路是：
        //   CSR -> COO -> SparseSlice -> 每个 PE 的 SpElement list -> 16 个 HBM channel
        // 这只是 TAPA SpMV 的矩阵预处理，不包含 PCG 向量 r/z/p 或 Jacobi 对角项。
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

        batch_num_ = static_cast<INDEX_TYPE>(sp_element_list_ptr.size()) - 1;
        matrix_len_ = sp_element_list_ptr[static_cast<std::size_t>(batch_num_)];

        const INDEX_TYPE ptr_size = round_up(static_cast<int>(sp_element_list_ptr.size()), 16);
        const INDEX_TYPE ptr_channel_size = round_up(ptr_size, 1024);
        sp_element_list_ptr_fpga_.assign(static_cast<std::size_t>(ptr_channel_size), 0);
        for (std::size_t index = 0; index < sp_element_list_ptr.size(); ++index) {
            sp_element_list_ptr_fpga_[index] = sp_element_list_ptr[index];
        }

        matrix_fpga_data_.resize(HBM_CHANNEL_NUM);
        Create_SpElement_list_for_all_channels(sp_element_list_pes,
                                               sp_element_list_ptr,
                                               matrix_fpga_data_,
                                               HBM_CHANNEL_NUM);
    }

    void allocate_vectors() {
        const int x_column_size = round_up(n_, 16);
        const int x_channel_size = round_up(x_column_size, 1024);
        const int y_column_size = round_up(n_, 16);
        const int y_channel_size = round_up(y_column_size, 1024);

        x_fpga_data_.assign(static_cast<std::size_t>(x_channel_size), 0.0f);
        y_fpga_data_out_.assign(static_cast<std::size_t>(y_channel_size), 0.0f);
    }

    int n_ = 0;
    std::string bitstream_;
    double plan_ms_ = 0.0;
    INDEX_TYPE batch_num_ = 0;
    INDEX_TYPE matrix_len_ = 0;
    AlignedVector<INDEX_TYPE> sp_element_list_ptr_fpga_;
    std::vector<AlignedVector<unsigned long>> matrix_fpga_data_;
    AlignedVector<VALUE_TYPE> x_fpga_data_;
    AlignedVector<VALUE_TYPE> y_fpga_data_out_;
};

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions options = parse_args(argc, argv);
        const Dataset dataset = Dataset::load(options.dataset_dir);

        SolverConfig config;
        config.tau = options.tau;
        config.max_iters = options.max_iters;

        std::cout << "[xplus] dataset=" << options.dataset_dir
                  << " mode=cuper-pcg-tapa"
                  << " spmv=tapa-cuper"
                  << " bitstream=" << (options.bitstream.empty() ? "<software-sim>" : options.bitstream)
                  << "\n";

        // TAPA 版 PCG 的硬件部分到这里为止只是一个 SpMV 后端。
        // 下面的 run_cuper_pcg_solver_with_backend 会在 host 侧循环调用 spmv。
        CuperTapaSpmv spmv(dataset, options.bitstream);
        CuperPcgResult cuper_result =
            project_xplus::cgsolver::run_cuper_pcg_solver_with_backend(
                dataset, config, spmv.backend_info(), spmv.plan_ms(), spmv, &std::cout);
        const project_xplus::cgsolver::CpuReferenceResult golden_result =
            project_xplus::cgsolver::run_cpu_reference(dataset, config);

        double max_abs_diff = 0.0;
        double max_rel_diff = 0.0;
        for (std::size_t index = 0; index < cuper_result.solution.size(); ++index) {
            const double expected = golden_result.golden.solution[index];
            const double actual = cuper_result.solution[index];
            const double abs_diff = std::fabs(actual - expected);
            const double rel_diff = abs_diff / std::max(std::fabs(expected), 1.0e-12);
            max_abs_diff = std::max(max_abs_diff, abs_diff);
            max_rel_diff = std::max(max_rel_diff, rel_diff);
        }

        std::cout << std::scientific << std::setprecision(12);
        std::cout << "[done] iter=" << cuper_result.iterations
                  << " residual_abs=" << cuper_result.residual_l2
                  << " residual_rel=" << cuper_result.residual_rel
                  << " status=" << project_xplus::cgsolver::to_string(cuper_result.status) << "\n";
        std::cout << "[check] cpu_residual_abs=" << golden_result.summary.residual_l2
                  << " cuper_residual_abs=" << cuper_result.residual_l2
                  << " max_abs_diff=" << max_abs_diff
                  << " max_rel_diff=" << max_rel_diff
                  << " diff_tol=" << options.diff_tol << "\n";
        std::cout << "[timing-ms] plan=" << cuper_result.timing.plan_ms
                  << " spmv_total=" << cuper_result.timing.spmv_ms
                  << " spmv_calls=" << std::defaultfloat << cuper_result.timing.spmv_calls
                  << std::scientific
                  << " spmv_avg="
                  << (cuper_result.timing.spmv_calls > 0
                          ? cuper_result.timing.spmv_ms / cuper_result.timing.spmv_calls
                          : 0.0)
                  << "\n";

        if (!cuper_result.converged) {
            return 2;
        }
        if (max_abs_diff > options.diff_tol && max_rel_diff > options.diff_tol) {
            return 3;
        }
        return 0;
    } catch (const std::exception& error) {
        usage(argv[0]);
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
