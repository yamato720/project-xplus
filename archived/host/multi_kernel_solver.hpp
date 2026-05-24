#ifndef PROJECT_XPLUS_MULTI_KERNEL_SOLVER_HPP
#define PROJECT_XPLUS_MULTI_KERNEL_SOLVER_HPP

#include "../../include/cg_common.hpp"
#include "../include/cg_kernels.hpp"
#include "dataset_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
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

struct MultiKernelResult {
    std::vector<data_t> solution;
    std::vector<data_t> jacobi_inverse;
    int iterations = 0;
    int effective_max_iters = 0;
    data_t final_rr = 0.0;
    data_t residual_l2 = 0.0;
    data_t residual_rel = 0.0;
    SolverStatus status = SolverStatus::kMaxIter;
    bool converged = false;
};

using SpmvKernelFn = void (*)(const index_t* row_ptr,
                              const index_t* col_idx,
                              const data_t* values,
                              const data_t* x,
                              data_t* y,
                              int n);

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

inline MultiKernelResult run_local_multi_kernel_solver(const Dataset& dataset,
                                                       const SolverConfig& config,
                                                       std::ostream* log_output = nullptr,
                                                       SpmvKernelFn spmv_kernel_fn = spmv_csr_kernel) {
    // 这个函数是“host 直接调用 kernel 函数”的本地等价版：
    // 不涉及 XRT / BO / xclbin，但执行顺序与硬件版 host orchestration 保持一致。
    if (config.tau <= 0.0) {
        throw std::runtime_error("tau must be positive");
    }
    if (dataset.n() > kMaxN) {
        throw std::runtime_error("dataset size exceeds kMaxN");
    }

    MultiKernelResult result;
    result.solution.assign(dataset.x0().begin(), dataset.x0().end());
    result.jacobi_inverse = build_jacobi_inverse(dataset);
    result.effective_max_iters =
        config.max_iters > 0 ? config.max_iters : std::max(4 * dataset.n(), 1000);

    std::vector<data_t> r(static_cast<std::size_t>(dataset.n()), 0.0);
    std::vector<data_t> z(static_cast<std::size_t>(dataset.n()), 0.0);
    std::vector<data_t> p(static_cast<std::size_t>(dataset.n()), 0.0);
    std::vector<data_t> spmv_out(static_cast<std::size_t>(dataset.n()), 0.0);
    data_t metrics[2] = {0.0, 0.0};
    data_t dot_out[1] = {0.0};

    if (log_output != nullptr) {
        *log_output << "[init] n=" << dataset.n()
                    << " nnz=" << dataset.nnz()
                    << " tau=" << std::scientific << std::setprecision(12) << config.tau
                    << " max_iters=" << std::defaultfloat << result.effective_max_iters
                    << " dtype=double\n";
    }

    // 初始化前两步：
    // 1. SpMV 算 ax = A*x0
    // 2. init kernel 生成 r/z/p 以及 rz/rr
    spmv_kernel_fn(dataset.row_ptr().data(),
                   dataset.col_idx().data(),
                   dataset.values().data(),
                   result.solution.data(),
                   spmv_out.data(),
                   dataset.n());
    init_pcg_kernel(dataset.b().data(),
                    spmv_out.data(),
                    result.jacobi_inverse.data(),
                    r.data(),
                    z.data(),
                    p.data(),
                    metrics,
                    dataset.n());

    data_t rz = metrics[0];
    data_t rr = metrics[1];
    result.final_rr = rr;

    for (int iteration = 0;
         iteration < result.effective_max_iters && rr > config.tau;
         ++iteration) {
        // 主循环严格跟硬件版一样：
        //   spmv -> dot -> host算alpha -> update_xrz -> host算beta -> update_p
        spmv_kernel_fn(dataset.row_ptr().data(),
                       dataset.col_idx().data(),
                       dataset.values().data(),
                       p.data(),
                       spmv_out.data(),
                       dataset.n());
        dot_kernel(p.data(), spmv_out.data(), dot_out, dataset.n());

        const data_t p_ap = dot_out[0];
        if (!std::isfinite(p_ap) || std::fabs(p_ap) <= kBreakdownEps ||
            !std::isfinite(rz) || std::fabs(rz) <= kBreakdownEps) {
            result.status = SolverStatus::kBreakdown;
            break;
        }

        const data_t alpha = rz / p_ap;
        if (!std::isfinite(alpha)) {
            result.status = SolverStatus::kBreakdown;
            break;
        }

        update_xrz_kernel(result.solution.data(),
                          p.data(),
                          r.data(),
                          spmv_out.data(),
                          result.jacobi_inverse.data(),
                          z.data(),
                          metrics,
                          alpha,
                          dataset.n());

        const data_t rz_new = metrics[0];
        const data_t rr_new = metrics[1];
        if (!std::isfinite(rz_new) || !std::isfinite(rr_new)) {
            result.status = SolverStatus::kBreakdown;
            break;
        }

        const data_t beta = rz_new / rz;
        if (!std::isfinite(beta)) {
            result.status = SolverStatus::kBreakdown;
            break;
        }

        update_p_kernel(z.data(), p.data(), beta, dataset.n());

        rz = rz_new;
        rr = rr_new;
        result.iterations = iteration + 1;
        result.final_rr = rr;

        if (log_output != nullptr) {
            log_iteration(*log_output, iteration, alpha, beta, rz, rr);
        }
    }

    std::vector<data_t> ax;
    // 最后仍然用 CPU SpMV 回算残差，作为统一验证口径。
    dataset.spmv(result.solution, ax);
    for (int index = 0; index < dataset.n(); ++index) {
        ax[static_cast<std::size_t>(index)] -= dataset.b()[static_cast<std::size_t>(index)];
    }

    result.residual_l2 = l2_norm_vec(ax);
    result.residual_rel =
        result.residual_l2 / std::max(l2_norm_vec(dataset.b()), data_t{kBreakdownEps});
    result.converged = (result.final_rr <= config.tau);

    if (result.status != SolverStatus::kBreakdown) {
        result.status = result.converged ? SolverStatus::kConverged : SolverStatus::kMaxIter;
    }

    return result;
}

}  // namespace project_xplus::cgsolver

#endif
