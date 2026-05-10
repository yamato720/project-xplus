#include "cpu_reference.hpp"
#include "multi_kernel_solver.hpp"

#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct CliOptions {
    std::filesystem::path dataset_dir;
    double tau = 1.0e-10;
    int max_iters = 0;
};

void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <dataset_dir> [--tau value] [--max-iters value]\n";
}

CliOptions parse_args(int argc, char** argv) {
    if (argc < 2) {
        throw std::runtime_error("missing dataset_dir");
    }

    CliOptions options;
    options.dataset_dir = std::filesystem::path(argv[1]);

    for (int index = 2; index < argc; ++index) {
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

    return options;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions options = parse_args(argc, argv);
        // 本地路径不经过 XRT，只是直接调用同名“kernel 函数”，
        // 用来验证多阶段拆分本身的数值逻辑。
        const project_xplus::cgsolver::Dataset dataset =
            project_xplus::cgsolver::Dataset::load(options.dataset_dir);

        project_xplus::cgsolver::SolverConfig config;
        config.tau = options.tau;
        config.max_iters = options.max_iters;

        std::cout << "[xplus] dataset=" << options.dataset_dir << "\n";

        // 如需切到分块 SpMV 占位实现，只需把这里改成 spmv_blocked_kernel。
        const auto spmv_kernel_entry = spmv_csr_kernel;
        const project_xplus::cgsolver::MultiKernelResult xplus_result =
            project_xplus::cgsolver::run_local_multi_kernel_solver(
                dataset, config, &std::cout, spmv_kernel_entry);
        // 仍然保留 CPU golden，确保本地多-kernel 逻辑没有偏离参考实现。
        const project_xplus::cgsolver::CpuReferenceResult golden_result =
            project_xplus::cgsolver::run_cpu_reference(dataset, config);

        double max_abs_diff = 0.0;
        for (std::size_t index = 0; index < xplus_result.solution.size(); ++index) {
            const double diff =
                std::fabs(xplus_result.solution[index] - golden_result.golden.solution[index]);
            if (diff > max_abs_diff) {
                max_abs_diff = diff;
            }
        }

        std::cout << std::scientific << std::setprecision(12);
        std::cout << "[done] iter=" << xplus_result.iterations
                  << " residual_abs=" << xplus_result.residual_l2
                  << " residual_rel=" << xplus_result.residual_rel
                  << " status=" << project_xplus::cgsolver::to_string(xplus_result.status) << "\n";
        std::cout << "[check] cpu_residual_abs=" << golden_result.summary.residual_l2
                  << " fpga_residual_abs=" << xplus_result.residual_l2
                  << " max_abs_diff=" << max_abs_diff << "\n";

        if (!xplus_result.converged) {
            return 2;
        }
        if (max_abs_diff > 1.0e-9) {
            return 3;
        }
        return 0;
    } catch (const std::exception& error) {
        usage(argv[0]);
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
