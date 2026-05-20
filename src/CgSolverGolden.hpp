#ifndef PROJECT_XPLUS_CG_SOLVER_GOLDEN_HPP
#define PROJECT_XPLUS_CG_SOLVER_GOLDEN_HPP

#include "CsrDataset.hpp"
#include "../include/cg_common.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace project_xplus::cgsolver {

struct GoldenIterationTrace {
    int iteration = 0;
    double alpha = 0.0;
    double beta = 0.0;
    double rz = 0.0;
    double rr = 0.0;
    double residual = 0.0;
};

struct GoldenResult {
    std::vector<double> solution;
    std::vector<double> jacobi_diag;
    std::vector<GoldenIterationTrace> iteration_trace;
    int iterations = 0;
    double final_rr = 0.0;
    bool converged = false;
};

inline void apply_jacobi_inverse(const std::vector<double>& diag,
                                 const std::vector<double>& r,
                                 std::vector<double>& z) {
    z.resize(r.size());
    for (std::size_t index = 0; index < r.size(); ++index) {
        z[index] = r[index] / diag[index];
    }
}

inline GoldenResult run_jacobi_pcg(const CsrDataset& dataset, const SolverConfig& config) {
    if (config.tau <= 0.0) {
        throw std::runtime_error("tau must be positive");
    }

    const int max_iters = config.max_iters > 0 ? config.max_iters : std::max(4 * dataset.n(), 1000);
    const std::vector<double> jacobi_diag = dataset.extract_jacobi_diag();

    GoldenResult result;
    result.solution = dataset.x0();
    result.jacobi_diag = jacobi_diag;

    std::vector<double> ax0;
    std::vector<double> r(static_cast<std::size_t>(dataset.n()), 0.0);
    std::vector<double> z;
    std::vector<double> p;
    std::vector<double> ap;

    dataset.spmv(result.solution, ax0);

    for (int index = 0; index < dataset.n(); ++index) {
        r[static_cast<std::size_t>(index)] =
            dataset.b()[static_cast<std::size_t>(index)] - ax0[static_cast<std::size_t>(index)];
    }

    apply_jacobi_inverse(jacobi_diag, r, z);
    p = z;

    double rz = dot(r, z);
    double rr = dot(r, r);

    result.final_rr = rr;
    result.converged = rr <= config.tau;

    for (int iteration = 0; iteration < max_iters && rr > config.tau; ++iteration) {
        dataset.spmv(p, ap);

        const double pap = dot(p, ap);
        if (std::fabs(pap) <= std::numeric_limits<double>::min()) {
            throw std::runtime_error("breakdown: p^T A p is too small");
        }

        const double alpha = rz / pap;

        for (int index = 0; index < dataset.n(); ++index) {
            result.solution[static_cast<std::size_t>(index)] += alpha * p[static_cast<std::size_t>(index)];
            r[static_cast<std::size_t>(index)] -= alpha * ap[static_cast<std::size_t>(index)];
        }

        apply_jacobi_inverse(jacobi_diag, r, z);

        const double rz_new = dot(r, z);
        const double rr_new = dot(r, r);
        const double beta = rz_new / rz;

        result.iteration_trace.push_back(GoldenIterationTrace{
            iteration,
            alpha,
            beta,
            rz_new,
            rr_new,
            std::sqrt(std::max(rr_new, 0.0)),
        });

        rz = rz_new;
        rr = rr_new;

        result.iterations = iteration + 1;
        result.final_rr = rr;

        if (rr <= config.tau) {
            break;
        }

        for (int index = 0; index < dataset.n(); ++index) {
            p[static_cast<std::size_t>(index)] =
                z[static_cast<std::size_t>(index)] + beta * p[static_cast<std::size_t>(index)];
        }
    }

    result.converged = result.final_rr <= config.tau;
    return result;
}

inline double compute_residual_norm(const CsrDataset& dataset, const std::vector<double>& x) {
    std::vector<double> ax;
    dataset.spmv(x, ax);

    for (int index = 0; index < dataset.n(); ++index) {
        ax[static_cast<std::size_t>(index)] -= dataset.b()[static_cast<std::size_t>(index)];
    }

    return l2_norm(ax);
}

}  // namespace project_xplus::cgsolver

#endif
