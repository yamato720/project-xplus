#include "cuper_pcg_solver.hpp"
#include "cpu_reference.hpp"
#include "run_defaults.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

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

        project_xplus::cgsolver::SolverConfig config;
        config.tau = options.tau;
        config.max_iters = options.max_iters;

        std::cout << "[xplus] dataset=" << options.dataset_dir
                  << " mode=cuper-pcg"
                  << " spmv=cuper-slice-fp32\n";

        const project_xplus::cgsolver::CuperPcgResult cuper_result =
            project_xplus::cgsolver::run_cuper_pcg_solver(dataset, config, &std::cout);
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
