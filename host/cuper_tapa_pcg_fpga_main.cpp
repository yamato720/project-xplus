#include "cpu_reference.hpp"
#include "pcg_common.hpp"
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

// 主线 3：TAPA Cuper / FPGA-PCG。
//
// 这个 host 启动 DLC/Cuper/kernels/Cuper.cpp 里的 CuperPcg 顶层。
// 与 host/cuper_tapa_pcg_main.cpp 不同，CuperPcg 会在 FPGA/TAPA task graph
// 内完成初始 SpMV、PCG 迭代、alpha/beta、向量更新和收敛判断。
// host 只负责矩阵/向量打包、一次 kernel launch、结果读回和 CPU reference 校验。
namespace {

template <typename T>
using AlignedVector = std::vector<T, tapa::aligned_allocator<T>>;

using project_xplus::cgsolver::CpuReferenceResult;
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

struct CuperTapaMatrix {
    // CuperPcg 和原 TAPA Cuper SpMV 复用同一套矩阵打包格式：
    // sp_element_list_ptr 给出 batch 边界，matrix_data[0..15] 对应 16 路 HBM。
    INDEX_TYPE batch_num = 0;
    INDEX_TYPE matrix_len = 0;
    AlignedVector<INDEX_TYPE> sp_element_list_ptr;
    std::vector<AlignedVector<unsigned long>> matrix_data;
    double plan_ms = 0.0;
};

CuperTapaMatrix build_tapa_matrix(const Dataset& dataset) {
    const auto start = std::chrono::steady_clock::now();
    CuperTapaMatrix matrix;

    // TAPA Cuper 原生工具链以 fp32 矩阵值和 COO/SparseSlice 为入口。
    // Project-XPlus 数据集是 double CSR，因此这里先做类型转换和格式转换。
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

    const INDEX_TYPE ptr_size = round_up(static_cast<int>(sp_element_list_ptr.size()), 16);
    const INDEX_TYPE ptr_channel_size = round_up(ptr_size, 1024);
    matrix.sp_element_list_ptr.assign(static_cast<std::size_t>(ptr_channel_size), 0);
    for (std::size_t index = 0; index < sp_element_list_ptr.size(); ++index) {
        matrix.sp_element_list_ptr[index] = sp_element_list_ptr[index];
    }

    matrix.matrix_data.resize(HBM_CHANNEL_NUM);
    Create_SpElement_list_for_all_channels(sp_element_list_pes,
                                           sp_element_list_ptr,
                                           matrix.matrix_data,
                                           HBM_CHANNEL_NUM);

    // plan_ms 只衡量 host 端矩阵打包时间，后面的 kernel_reported/kernel_wall
    // 才是 CuperPcg 自身运行时间。
    const auto end = std::chrono::steady_clock::now();
    matrix.plan_ms = std::chrono::duration<double, std::milli>(end - start).count();
    return matrix;
}

template <typename T>
AlignedVector<T> aligned_copy(const std::vector<T>& input, int align = 1024) {
    // TAPA mmap 需要 aligned_allocator，并且向量长度按 1024 对齐，
    // 这样和原 Cuper host 的 buffer 约定保持一致。
    AlignedVector<T> output(static_cast<std::size_t>(round_up(static_cast<int>(input.size()), align)), T{});
    std::copy(input.begin(), input.end(), output.begin());
    return output;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto total_start = std::chrono::steady_clock::now();
        const CliOptions options = parse_args(argc, argv);
        const Dataset dataset = Dataset::load(options.dataset_dir);

        SolverConfig config;
        config.tau = options.tau;
        config.max_iters = options.max_iters;
        const int effective_max_iters =
            config.max_iters > 0 ? config.max_iters : std::max(4 * dataset.n(), 1000);

        CuperTapaMatrix matrix = build_tapa_matrix(dataset);
        std::vector<data_t> minv_host = project_xplus::cgsolver::build_jacobi_inverse(dataset);

        // 这些 BO/mmap 是 FPGA 内 PCG 的完整状态：
        // B/M_inv 是只读输入，X/R/Z/P/AP 是 kernel 内迭代更新的向量，
        // Metrics/Status 是收敛状态和调试信息的最小写回口。
        AlignedVector<double> b = aligned_copy(dataset.b());
        AlignedVector<double> minv = aligned_copy(minv_host);
        AlignedVector<double> x = aligned_copy(dataset.x0());
        AlignedVector<double> r(static_cast<std::size_t>(round_up(dataset.n(), 1024)), 0.0);
        AlignedVector<double> z(static_cast<std::size_t>(round_up(dataset.n(), 1024)), 0.0);
        AlignedVector<double> p(static_cast<std::size_t>(round_up(dataset.n(), 1024)), 0.0);
        AlignedVector<double> ap(static_cast<std::size_t>(round_up(dataset.n(), 1024)), 0.0);
        AlignedVector<double> metrics(1024, 0.0);
        AlignedVector<INDEX_TYPE> status(1024, 0);

        std::cout << "[xplus] dataset=" << options.dataset_dir
                  << " mode=cuper-pcg-tapa-fpga"
                  << " kernel=CuperPcg"
                  << " bitstream=" << (options.bitstream.empty() ? "<software-sim>" : options.bitstream)
                  << " batches=" << matrix.batch_num
                  << " matrix_len=" << matrix.matrix_len << "\n";

        // 主线入口：一次 launch 执行完整 TAPA Cuper FPGA-PCG。
        // CuperPcg 内部每轮都会通过原 Cuper 数据流做 A*p，
        // host 不再逐轮参与 PCG 控制。
        const auto kernel_start = std::chrono::steady_clock::now();
        const double kernel_ns = tapa::invoke(
            CuperPcg,
            options.bitstream,
            tapa::read_only_mmap<INDEX_TYPE>(matrix.sp_element_list_ptr),
            tapa::read_only_mmaps<unsigned long, HBM_CHANNEL_NUM>(matrix.matrix_data)
                .reinterpret<ap_uint<512>>(),
            tapa::read_only_mmap<double>(b),
            tapa::read_only_mmap<double>(minv),
            tapa::read_write_mmap<double>(x),
            tapa::read_write_mmap<double>(r),
            tapa::read_write_mmap<double>(z),
            tapa::read_write_mmap<double>(p),
            tapa::read_write_mmap<double>(ap),
            tapa::write_only_mmap<double>(metrics),
            tapa::write_only_mmap<INDEX_TYPE>(status),
            matrix.batch_num,
            matrix.matrix_len,
            dataset.n(),
            dataset.n(),
            effective_max_iters,
            config.tau);
        const auto kernel_end = std::chrono::steady_clock::now();

        // kernel 返回后 X 即为最终解向量。只拷贝有效 n 项，
        // 对齐 padding 不参与残差和 diff 校验。
        std::vector<double> solution(static_cast<std::size_t>(dataset.n()), 0.0);
        std::copy(x.begin(), x.begin() + dataset.n(), solution.begin());

        const CpuReferenceResult golden_result =
            project_xplus::cgsolver::run_cpu_reference(dataset, config);
        double max_abs_diff = 0.0;
        double max_rel_diff = 0.0;
        for (std::size_t index = 0; index < solution.size(); ++index) {
            const double expected = golden_result.golden.solution[index];
            const double abs_diff = std::fabs(solution[index] - expected);
            const double rel_diff = abs_diff / std::max(std::fabs(expected), 1.0e-12);
            max_abs_diff = std::max(max_abs_diff, abs_diff);
            max_rel_diff = std::max(max_rel_diff, rel_diff);
        }

        const double residual_l2 =
            project_xplus::cgsolver::compute_residual_norm(dataset, solution);
        const auto total_end = std::chrono::steady_clock::now();

        std::cout << std::scientific << std::setprecision(12);
        std::cout << "[done] iter=" << status[1]
                  << " residual_abs=" << residual_l2
                  << " status=" << (status[0] == 0 ? "converged"
                                   : status[0] == 1 ? "max_iter"
                                                    : "breakdown")
                  << "\n";
        std::cout << "[check] cpu_residual_abs=" << golden_result.summary.residual_l2
                  << " cuper_tapa_pcg_residual_abs=" << residual_l2
                  << " max_abs_diff=" << max_abs_diff
                  << " max_rel_diff=" << max_rel_diff
                  << " diff_tol=" << options.diff_tol
                  << " rr=" << metrics[1] << "\n";
        std::cout << "[timing-ms] plan=" << matrix.plan_ms
                  << " kernel_reported=" << kernel_ns * 1.0e-6
                  << " kernel_wall="
                  << std::chrono::duration<double, std::milli>(kernel_end - kernel_start).count()
                  << " total="
                  << std::chrono::duration<double, std::milli>(total_end - total_start).count()
                  << "\n";

        if (status[0] != 0) {
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
