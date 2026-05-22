#include "cuper_control_matrix.hpp"
#include "cpu_reference.hpp"
#include "multi_kernel_solver.hpp"
#include "run_defaults.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
// 本地可执行文件直接链接 HLS C++ kernel 函数，用同一套 host 打包数据
// 验证 cuper_pcg_control_kernel 的数学结果。参数顺序必须和 kernel 定义一致。
void cuper_pcg_control_kernel(const project_xplus::cgsolver::index_t* sp_element_list_ptr,
                              const unsigned long* matrix_data_0,
                              const unsigned long* matrix_data_1,
                              const unsigned long* matrix_data_2,
                              const unsigned long* matrix_data_3,
                              const unsigned long* matrix_data_4,
                              const unsigned long* matrix_data_5,
                              const unsigned long* matrix_data_6,
                              const unsigned long* matrix_data_7,
                              const unsigned long* matrix_data_8,
                              const unsigned long* matrix_data_9,
                              const unsigned long* matrix_data_10,
                              const unsigned long* matrix_data_11,
                              const unsigned long* matrix_data_12,
                              const unsigned long* matrix_data_13,
                              const unsigned long* matrix_data_14,
                              const unsigned long* matrix_data_15,
                              const project_xplus::cgsolver::data_t* b,
                              const project_xplus::cgsolver::data_t* m_inv,
                              project_xplus::cgsolver::data_t* x,
                              project_xplus::cgsolver::data_t* r,
                              project_xplus::cgsolver::data_t* z,
                              project_xplus::cgsolver::data_t* p,
                              project_xplus::cgsolver::data_t* ap,
                              project_xplus::cgsolver::data_t* metrics,
                              int* status,
                              project_xplus::cgsolver::data_t tau,
                              int max_iters,
                              int n,
                              int batch_count);
}

namespace {

struct CliOptions {
    std::filesystem::path dataset_dir;
    double tau = project_xplus::cgsolver::run_defaults::kTau;
    int max_iters = project_xplus::cgsolver::run_defaults::kMaxIters;
    double diff_tol = 1.0e-3;
};

void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " [dataset_dir] [--tau value] [--max-iters value] [--diff-tol value]\n";
}

CliOptions parse_args(int argc, char** argv) {
    CliOptions options;
    options.dataset_dir = project_xplus::cgsolver::run_defaults::dataset_dir(argv[0]);

    int index = 1;
    if (index < argc && std::string(argv[index]).rfind("--", 0) != 0) {
        options.dataset_dir = std::filesystem::path(argv[index++]);
    }

    for (; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--tau") {
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

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions options = parse_args(argc, argv);
        const project_xplus::cgsolver::Dataset dataset =
            project_xplus::cgsolver::Dataset::load(options.dataset_dir);
        // CSR 数据在 host 侧转换成 Cuper packed 16-HBM 格式，
        // 后续 local 调用和 XRT 调用都复用这个布局。
        const project_xplus::cgsolver::CuperControlMatrix matrix =
            project_xplus::cgsolver::build_cuper_control_matrix(dataset);

        project_xplus::cgsolver::SolverConfig config;
        config.tau = options.tau;
        config.max_iters = options.max_iters;

        std::vector<project_xplus::cgsolver::data_t> x = dataset.x0();
        std::vector<project_xplus::cgsolver::data_t> r(static_cast<std::size_t>(dataset.n()), 0.0);
        std::vector<project_xplus::cgsolver::data_t> z(static_cast<std::size_t>(dataset.n()), 0.0);
        std::vector<project_xplus::cgsolver::data_t> p(static_cast<std::size_t>(dataset.n()), 0.0);
        std::vector<project_xplus::cgsolver::data_t> ap(static_cast<std::size_t>(dataset.n()), 0.0);
        std::vector<project_xplus::cgsolver::data_t> m_inv =
            project_xplus::cgsolver::build_jacobi_inverse(dataset);
        std::vector<project_xplus::cgsolver::data_t> metrics(4, 0.0);
        std::vector<int> status(2, 0);

        std::cout << "[xplus] dataset=" << options.dataset_dir
                  << " mode=cuper-pcg-control-local"
                  << " spmv=cuper-packed-16hbm"
                  << " batches=" << matrix.batch_count
                  << " matrix_len=" << matrix.matrix_len << "\n";

        // local 路径没有 XRT BO，直接把 std::vector 的底层指针传给 kernel。
        // matrix_data[0..15] 对应 kernel 的 matrix_data_0..15。
        cuper_pcg_control_kernel(matrix.sp_element_list_ptr.data(),
                                 matrix.matrix_data[0].data(),
                                 matrix.matrix_data[1].data(),
                                 matrix.matrix_data[2].data(),
                                 matrix.matrix_data[3].data(),
                                 matrix.matrix_data[4].data(),
                                 matrix.matrix_data[5].data(),
                                 matrix.matrix_data[6].data(),
                                 matrix.matrix_data[7].data(),
                                 matrix.matrix_data[8].data(),
                                 matrix.matrix_data[9].data(),
                                 matrix.matrix_data[10].data(),
                                 matrix.matrix_data[11].data(),
                                 matrix.matrix_data[12].data(),
                                 matrix.matrix_data[13].data(),
                                 matrix.matrix_data[14].data(),
                                 matrix.matrix_data[15].data(),
                                 dataset.b().data(),
                                 m_inv.data(),
                                 x.data(),
                                 r.data(),
                                 z.data(),
                                 p.data(),
                                 ap.data(),
                                 metrics.data(),
                                 status.data(),
                                 config.tau,
                                 config.max_iters > 0 ? config.max_iters
                                                      : std::max(4 * dataset.n(), 1000),
                                 dataset.n(),
                                 matrix.batch_count);

        // 用 CPU reference 校验本地 kernel 函数结果，主要检查 Cuper 打包和
        // PCG 控制流是否和原始 solver 一致。
        const project_xplus::cgsolver::CpuReferenceResult golden_result =
            project_xplus::cgsolver::run_cpu_reference(dataset, config);

        double max_abs_diff = 0.0;
        double max_rel_diff = 0.0;
        for (std::size_t index = 0; index < x.size(); ++index) {
            const double expected = golden_result.golden.solution[index];
            const double abs_diff = std::fabs(x[index] - expected);
            const double rel_diff = abs_diff / std::max(std::fabs(expected), 1.0e-12);
            max_abs_diff = std::max(max_abs_diff, abs_diff);
            max_rel_diff = std::max(max_rel_diff, rel_diff);
        }

        const double residual_l2 = project_xplus::cgsolver::compute_residual_norm(dataset, x);
        const bool converged = status[0] == 0;

        std::cout << std::scientific << std::setprecision(12);
        std::cout << "[done] iter=" << status[1]
                  << " residual_abs=" << residual_l2
                  << " status=" << (status[0] == 0 ? "converged"
                                   : status[0] == 1 ? "max_iter"
                                                    : "breakdown")
                  << "\n";
        std::cout << "[check] cpu_residual_abs=" << golden_result.summary.residual_l2
                  << " cuper_control_residual_abs=" << residual_l2
                  << " max_abs_diff=" << max_abs_diff
                  << " max_rel_diff=" << max_rel_diff
                  << " diff_tol=" << options.diff_tol << "\n";

        if (!converged) {
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
