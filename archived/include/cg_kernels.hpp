#ifndef PROJECT_XPLUS_CG_KERNELS_HPP
#define PROJECT_XPLUS_CG_KERNELS_HPP

#include "cg_common.hpp"

using project_xplus_data_t = project_xplus::cgsolver::data_t;
using project_xplus_index_t = project_xplus::cgsolver::index_t;

extern "C" {

void spmv_csr_kernel(const project_xplus_index_t* row_ptr,
                     const project_xplus_index_t* col_idx,
                     const project_xplus_data_t* values,
                     const project_xplus_data_t* x,
                     project_xplus_data_t* y,
                     int n);

void spmv_blocked_kernel(const project_xplus_index_t* row_ptr,
                         const project_xplus_index_t* col_idx,
                         const project_xplus_data_t* values,
                         const project_xplus_data_t* x,
                         project_xplus_data_t* y,
                         int n);

void init_pcg_kernel(const project_xplus_data_t* b,
                     const project_xplus_data_t* ax,
                     const project_xplus_data_t* m_inv,
                     project_xplus_data_t* r,
                     project_xplus_data_t* z,
                     project_xplus_data_t* p,
                     project_xplus_data_t* metrics,
                     int n);

void dot_kernel(const project_xplus_data_t* a,
                const project_xplus_data_t* b,
                project_xplus_data_t* out,
                int n);

void update_xrz_kernel(project_xplus_data_t* x,
                       const project_xplus_data_t* p,
                       project_xplus_data_t* r,
                       const project_xplus_data_t* ap,
                       const project_xplus_data_t* m_inv,
                       project_xplus_data_t* z,
                       project_xplus_data_t* metrics,
                       project_xplus_data_t alpha,
                       int n);

void update_p_kernel(const project_xplus_data_t* z,
                     project_xplus_data_t* p,
                     project_xplus_data_t beta,
                     int n);

}

#endif
