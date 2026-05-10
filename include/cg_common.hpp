#ifndef PROJECT_XPLUS_CG_COMMON_HPP
#define PROJECT_XPLUS_CG_COMMON_HPP

#include <cstddef>

namespace project_xplus::cgsolver {

using data_t = double;
using index_t = int;

static constexpr int kMaxN = 1024;
static constexpr data_t kBreakdownEps = 1.0e-30;

struct SolverConfig {
    data_t tau = 1.0e-10;
    int max_iters = 0;
};

struct RunSummary {
    int n = 0;
    int nnz = 0;
    int iterations = 0;
    data_t final_rr = 0.0;
    data_t residual_l2 = 0.0;
    bool converged = false;
};

}  // namespace project_xplus::cgsolver

#endif
