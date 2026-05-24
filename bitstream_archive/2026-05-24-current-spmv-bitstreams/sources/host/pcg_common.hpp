#ifndef PROJECT_XPLUS_PCG_COMMON_HPP
#define PROJECT_XPLUS_PCG_COMMON_HPP

#include "../include/cg_common.hpp"
#include "dataset_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <vector>

namespace project_xplus::cgsolver {

enum class SolverStatus {
    kConverged,
    kMaxIter,
    kBreakdown,
};

inline const char* to_string(const SolverStatus status) {
    switch (status) {
        case SolverStatus::kConverged:
            return "converged";
        case SolverStatus::kMaxIter:
            return "max_iter";
        case SolverStatus::kBreakdown:
            return "breakdown";
    }
    return "unknown";
}

inline data_t l2_norm_vec(const std::vector<data_t>& values) {
    data_t acc = 0.0;
    for (const data_t value : values) {
        acc += value * value;
    }
    return std::sqrt(acc);
}

inline std::vector<data_t> build_jacobi_inverse(const Dataset& dataset) {
    const std::vector<double> diag = dataset.extract_jacobi_diag();
    std::vector<data_t> m_inv(diag.size(), 0.0);

    for (std::size_t index = 0; index < diag.size(); ++index) {
        if (std::fabs(diag[index]) <= kBreakdownEps) {
            throw std::runtime_error("zero diagonal entry while building Jacobi inverse");
        }
        m_inv[index] = 1.0 / diag[index];
    }

    return m_inv;
}

inline void log_iteration(std::ostream& output,
                          const int iteration,
                          const data_t alpha,
                          const data_t beta,
                          const data_t rz,
                          const data_t rr) {
    const std::ios::fmtflags old_flags = output.flags();
    const std::streamsize old_precision = output.precision();
    const char old_fill = output.fill();

    output << "[iter " << std::setw(3) << std::setfill('0') << iteration << "] "
           << std::scientific << std::setprecision(12)
           << "alpha=" << alpha
           << " beta=" << beta
           << " rz=" << rz
           << " rr=" << rr
           << " residual=" << std::sqrt(std::max(rr, data_t{0.0})) << "\n";

    output.flags(old_flags);
    output.precision(old_precision);
    output.fill(old_fill);
}

}  // namespace project_xplus::cgsolver

#endif
