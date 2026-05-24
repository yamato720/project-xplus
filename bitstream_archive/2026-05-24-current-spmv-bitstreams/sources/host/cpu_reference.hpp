#ifndef PROJECT_XPLUS_CPU_REFERENCE_HPP
#define PROJECT_XPLUS_CPU_REFERENCE_HPP

#include "../include/cg_common.hpp"
#include "dataset_bridge.hpp"
#include "../src/CgSolverGolden.hpp"

namespace project_xplus::cgsolver {

struct CpuReferenceResult {
    project_xplus::cgsolver::GoldenResult golden;
    RunSummary summary;
};

inline CpuReferenceResult run_cpu_reference(const Dataset& dataset, const SolverConfig& config) {
    project_xplus::cgsolver::SolverConfig golden_config;
    golden_config.tau = config.tau;
    golden_config.max_iters = config.max_iters;

    CpuReferenceResult result;
    result.golden = project_xplus::cgsolver::run_jacobi_pcg(dataset, golden_config);
    result.summary.n = dataset.n();
    result.summary.nnz = dataset.nnz();
    result.summary.iterations = result.golden.iterations;
    result.summary.final_rr = result.golden.final_rr;
    result.summary.residual_l2 =
        project_xplus::cgsolver::compute_residual_norm(dataset, result.golden.solution);
    result.summary.converged = result.golden.converged;
    return result;
}

}  // namespace project_xplus::cgsolver

#endif
