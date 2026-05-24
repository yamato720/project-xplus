#ifndef PROJECT_XPLUS_CUPER_PCG_SOLVER_HPP
#define PROJECT_XPLUS_CUPER_PCG_SOLVER_HPP

#include "../include/cg_common.hpp"
#include "dataset_bridge.hpp"
#include "pcg_common.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <vector>

namespace project_xplus::cgsolver {

namespace cuper_pcg {

using value_t = float;
using index_t = project_xplus::cgsolver::index_t;

static constexpr int kPeNum = 8;
static constexpr int kHbmChannelNum = 16;
static constexpr int kRowHbmNum = 4;
static constexpr int kSliceSize = kHbmChannelNum * kRowHbmNum;
static constexpr int kBatchSize = 8192 / kSliceSize;
static constexpr int kSliceWidth = kSliceSize * kBatchSize;

struct Element {
    index_t row = 0;
    index_t col = 0;
    value_t value = 0.0f;
};

struct ColumnBatch {
    int col_begin = 0;
    int col_end = 0;
    std::vector<Element> elements;
};

struct MatrixPlan {
    int n = 0;
    int nnz = 0;
    int slice_size = kSliceSize;
    int batch_size = kBatchSize;
    int slice_width = kSliceWidth;
    int num_col_slices = 0;
    std::vector<ColumnBatch> batches;
};

struct TimingSummary {
    double plan_ms = 0.0;
    double spmv_ms = 0.0;
    int spmv_calls = 0;
};

}  // namespace cuper_pcg

struct CuperPcgResult {
    std::vector<data_t> solution;
    std::vector<data_t> jacobi_inverse;
    int iterations = 0;
    int effective_max_iters = 0;
    data_t final_rr = 0.0;
    data_t residual_l2 = 0.0;
    data_t residual_rel = 0.0;
    SolverStatus status = SolverStatus::kMaxIter;
    bool converged = false;
    cuper_pcg::TimingSummary timing;
};

struct CuperPcgBackendInfo {
    const char* dtype = "unknown";
    int slice_width = 0;
    std::size_t batches = 0;
};

inline void log_cuper_iteration(std::ostream& output,
                                const int iteration,
                                const data_t alpha,
                                const data_t beta,
                                const data_t rz,
                                const data_t rr) {
    log_iteration(output, iteration, alpha, beta, rz, rr);
}

inline cuper_pcg::MatrixPlan build_cuper_matrix_plan(const Dataset& dataset) {
    using namespace cuper_pcg;

    MatrixPlan plan;
    plan.n = dataset.n();
    plan.nnz = dataset.nnz();
    plan.num_col_slices = (plan.n + plan.slice_size - 1) / plan.slice_size;

    const int num_batches = (plan.num_col_slices + plan.batch_size - 1) / plan.batch_size;
    plan.batches.resize(static_cast<std::size_t>(num_batches));

    std::vector<int> counts(static_cast<std::size_t>(num_batches), 0);
    for (int row = 0; row < dataset.n(); ++row) {
        for (int offset = dataset.row_ptr()[static_cast<std::size_t>(row)];
             offset < dataset.row_ptr()[static_cast<std::size_t>(row + 1)];
             ++offset) {
            const int col = dataset.col_idx()[static_cast<std::size_t>(offset)];
            const int batch = (col / plan.slice_size) / plan.batch_size;
            ++counts[static_cast<std::size_t>(batch)];
        }
    }

    for (int batch = 0; batch < num_batches; ++batch) {
        ColumnBatch& column_batch = plan.batches[static_cast<std::size_t>(batch)];
        column_batch.col_begin = batch * plan.slice_width;
        column_batch.col_end = std::min(column_batch.col_begin + plan.slice_width, plan.n);
        column_batch.elements.reserve(static_cast<std::size_t>(counts[static_cast<std::size_t>(batch)]));
    }

    for (int row = 0; row < dataset.n(); ++row) {
        for (int offset = dataset.row_ptr()[static_cast<std::size_t>(row)];
             offset < dataset.row_ptr()[static_cast<std::size_t>(row + 1)];
             ++offset) {
            const int col = dataset.col_idx()[static_cast<std::size_t>(offset)];
            const int batch = (col / plan.slice_size) / plan.batch_size;
            plan.batches[static_cast<std::size_t>(batch)].elements.push_back(cuper_pcg::Element{
                row,
                col,
                static_cast<cuper_pcg::value_t>(dataset.values()[static_cast<std::size_t>(offset)]),
            });
        }
    }

    return plan;
}

inline void cuper_style_spmv(const cuper_pcg::MatrixPlan& plan,
                             const std::vector<data_t>& x,
                             std::vector<data_t>& y) {
    using namespace cuper_pcg;

    y.assign(static_cast<std::size_t>(plan.n), 0.0);
    std::vector<value_t> x_window(static_cast<std::size_t>(plan.slice_width), 0.0f);

    for (const ColumnBatch& batch : plan.batches) {
        const int width = batch.col_end - batch.col_begin;
        std::fill(x_window.begin(), x_window.end(), 0.0f);
        for (int index = 0; index < width; ++index) {
            x_window[static_cast<std::size_t>(index)] =
                static_cast<value_t>(x[static_cast<std::size_t>(batch.col_begin + index)]);
        }

        for (const Element& element : batch.elements) {
            const int local_col = element.col - batch.col_begin;
            const value_t product =
                element.value * x_window[static_cast<std::size_t>(local_col)];
            y[static_cast<std::size_t>(element.row)] += static_cast<data_t>(product);
        }
    }
}

inline data_t dot_vec(const std::vector<data_t>& lhs, const std::vector<data_t>& rhs) {
    data_t acc = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        acc += lhs[index] * rhs[index];
    }
    return acc;
}

template <typename SpmvRunner>
// Cuper-PCG 的 host-side 主循环。
//
// 模板参数 run_spmv 可以是：
//   - 软件版 Cuper 风格 SpMV
//   - TAPA Cuper kernel 包装后的 SpMV 后端
//
// 但无论后端是哪一种，这个函数都在 host 侧完成 PCG 控制：
//   init r/z/p
//   dot(r, z), dot(p, A*p)
//   alpha / beta
//   x/r/z/p 更新
//   tau 收敛判断和 breakdown 判断
//
// 因此 Cuper-PCG 软件版和 Cuper-PCG TAPA 版都不是“PCG 进 kernel”。
// 真正把 PCG 控制放入 FPGA kernel 的版本是 kernels/cuper_pcg_control_kernel.cpp。
inline CuperPcgResult run_cuper_pcg_solver_with_backend(const Dataset& dataset,
                                                        const SolverConfig& config,
                                                        const CuperPcgBackendInfo& backend,
                                                        const double plan_ms,
                                                        SpmvRunner run_spmv,
                                                        std::ostream* log_output = nullptr) {
    if (config.tau <= 0.0) {
        throw std::runtime_error("tau must be positive");
    }

    CuperPcgResult result;
    result.solution.assign(dataset.x0().begin(), dataset.x0().end());
    result.jacobi_inverse = build_jacobi_inverse(dataset);
    result.effective_max_iters =
        config.max_iters > 0 ? config.max_iters : std::max(4 * dataset.n(), 1000);
    result.timing.plan_ms = plan_ms;

    std::vector<data_t> r(static_cast<std::size_t>(dataset.n()), 0.0);
    std::vector<data_t> z(static_cast<std::size_t>(dataset.n()), 0.0);
    std::vector<data_t> p(static_cast<std::size_t>(dataset.n()), 0.0);
    std::vector<data_t> spmv_out(static_cast<std::size_t>(dataset.n()), 0.0);

    if (log_output != nullptr) {
        *log_output << "[init] n=" << dataset.n()
                    << " nnz=" << dataset.nnz()
                    << " tau=" << std::scientific << std::setprecision(12) << config.tau
                    << " max_iters=" << std::defaultfloat << result.effective_max_iters
                    << " dtype=" << backend.dtype;
        if (backend.slice_width > 0) {
            *log_output << " cuper_slice_width=" << backend.slice_width;
        }
        if (backend.batches > 0) {
            *log_output << " batches=" << backend.batches;
        }
        *log_output << "\n";
    }

    // 初始 SpMV：spmv_out = A * x0，用于构造 r = b - A*x0。
    run_spmv(result.solution, spmv_out, result);

    for (int index = 0; index < dataset.n(); ++index) {
        const std::size_t pos = static_cast<std::size_t>(index);
        r[pos] = dataset.b()[pos] - spmv_out[pos];
        z[pos] = result.jacobi_inverse[pos] * r[pos];
        p[pos] = z[pos];
    }

    data_t rz = dot_vec(r, z);
    data_t rr = dot_vec(r, r);
    result.final_rr = rr;

    for (int iteration = 0;
         iteration < result.effective_max_iters && rr > config.tau;
         ++iteration) {
        // 每轮 PCG 只把 SpMV 交给后端；其余 dot、alpha/beta 和向量更新
        // 仍在 host 侧执行。
        run_spmv(p, spmv_out, result);

        const data_t p_ap = dot_vec(p, spmv_out);
        if (!std::isfinite(p_ap) || std::fabs(p_ap) <= kBreakdownEps ||
            !std::isfinite(rz) || std::fabs(rz) <= kBreakdownEps) {
            result.status = SolverStatus::kBreakdown;
            break;
        }

        // alpha = (r, z) / (p, A*p)
        const data_t alpha = rz / p_ap;
        if (!std::isfinite(alpha)) {
            result.status = SolverStatus::kBreakdown;
            break;
        }

        for (int index = 0; index < dataset.n(); ++index) {
            const std::size_t pos = static_cast<std::size_t>(index);
            result.solution[pos] += alpha * p[pos];
            r[pos] -= alpha * spmv_out[pos];
            z[pos] = result.jacobi_inverse[pos] * r[pos];
        }

        const data_t rz_new = dot_vec(r, z);
        const data_t rr_new = dot_vec(r, r);
        if (!std::isfinite(rz_new) || !std::isfinite(rr_new)) {
            result.status = SolverStatus::kBreakdown;
            break;
        }

        // beta = (r_new, z_new) / (r_old, z_old)
        const data_t beta = rz_new / rz;
        if (!std::isfinite(beta)) {
            result.status = SolverStatus::kBreakdown;
            break;
        }

        for (int index = 0; index < dataset.n(); ++index) {
            const std::size_t pos = static_cast<std::size_t>(index);
            p[pos] = z[pos] + beta * p[pos];
        }

        rz = rz_new;
        rr = rr_new;
        result.iterations = iteration + 1;
        result.final_rr = rr;

        if (log_output != nullptr) {
            log_cuper_iteration(*log_output, iteration, alpha, beta, rz, rr);
        }
    }

    std::vector<data_t> ax;
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

inline CuperPcgResult run_cuper_pcg_solver(const Dataset& dataset,
                                           const SolverConfig& config,
                                           std::ostream* log_output = nullptr) {
    if (config.tau <= 0.0) {
        throw std::runtime_error("tau must be positive");
    }

    CuperPcgResult result;
    result.solution.assign(dataset.x0().begin(), dataset.x0().end());
    result.jacobi_inverse = build_jacobi_inverse(dataset);
    result.effective_max_iters =
        config.max_iters > 0 ? config.max_iters : std::max(4 * dataset.n(), 1000);

    const auto plan_start = std::chrono::steady_clock::now();
    const cuper_pcg::MatrixPlan plan = build_cuper_matrix_plan(dataset);
    const auto plan_end = std::chrono::steady_clock::now();
    result.timing.plan_ms =
        std::chrono::duration<double, std::milli>(plan_end - plan_start).count();

    std::vector<data_t> r(static_cast<std::size_t>(dataset.n()), 0.0);
    std::vector<data_t> z(static_cast<std::size_t>(dataset.n()), 0.0);
    std::vector<data_t> p(static_cast<std::size_t>(dataset.n()), 0.0);
    std::vector<data_t> spmv_out(static_cast<std::size_t>(dataset.n()), 0.0);

    if (log_output != nullptr) {
        *log_output << "[init] n=" << dataset.n()
                    << " nnz=" << dataset.nnz()
                    << " tau=" << std::scientific << std::setprecision(12) << config.tau
                    << " max_iters=" << std::defaultfloat << result.effective_max_iters
                    << " dtype=fp32-spmv+fp64-pcg"
                    << " cuper_slice_width=" << plan.slice_width
                    << " batches=" << plan.batches.size() << "\n";
    }

    auto run_spmv = [&](const std::vector<data_t>& input, std::vector<data_t>& output) {
        const auto start = std::chrono::steady_clock::now();
        cuper_style_spmv(plan, input, output);
        const auto end = std::chrono::steady_clock::now();
        result.timing.spmv_ms += std::chrono::duration<double, std::milli>(end - start).count();
        ++result.timing.spmv_calls;
    };

    run_spmv(result.solution, spmv_out);

    for (int index = 0; index < dataset.n(); ++index) {
        const std::size_t pos = static_cast<std::size_t>(index);
        r[pos] = dataset.b()[pos] - spmv_out[pos];
        z[pos] = result.jacobi_inverse[pos] * r[pos];
        p[pos] = z[pos];
    }

    data_t rz = dot_vec(r, z);
    data_t rr = dot_vec(r, r);
    result.final_rr = rr;

    for (int iteration = 0;
         iteration < result.effective_max_iters && rr > config.tau;
         ++iteration) {
        run_spmv(p, spmv_out);

        const data_t p_ap = dot_vec(p, spmv_out);
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

        for (int index = 0; index < dataset.n(); ++index) {
            const std::size_t pos = static_cast<std::size_t>(index);
            result.solution[pos] += alpha * p[pos];
            r[pos] -= alpha * spmv_out[pos];
            z[pos] = result.jacobi_inverse[pos] * r[pos];
        }

        const data_t rz_new = dot_vec(r, z);
        const data_t rr_new = dot_vec(r, r);
        if (!std::isfinite(rz_new) || !std::isfinite(rr_new)) {
            result.status = SolverStatus::kBreakdown;
            break;
        }

        const data_t beta = rz_new / rz;
        if (!std::isfinite(beta)) {
            result.status = SolverStatus::kBreakdown;
            break;
        }

        for (int index = 0; index < dataset.n(); ++index) {
            const std::size_t pos = static_cast<std::size_t>(index);
            p[pos] = z[pos] + beta * p[pos];
        }

        rz = rz_new;
        rr = rr_new;
        result.iterations = iteration + 1;
        result.final_rr = rr;

        if (log_output != nullptr) {
            log_cuper_iteration(*log_output, iteration, alpha, beta, rz, rr);
        }
    }

    std::vector<data_t> ax;
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
