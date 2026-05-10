#include "../include/cg_kernels.hpp"

namespace {

using data_t = project_xplus::cgsolver::data_t;
using index_t = project_xplus::cgsolver::index_t;
constexpr int kMaxN = project_xplus::cgsolver::kMaxN;

inline bool valid_n(const int n) {
    // 本地“kernel 函数”版本也保持和 HLS 顶层一致的边界检查。
    return n >= 0 && n <= kMaxN;
}

inline void load_vector_local(const data_t* src, data_t* dst, const int n) {
    // 这个辅助函数对应硬件版里先把 x 缓存到片上本地数组的动作。
    for (int index = 0; index < n; ++index) {
        dst[index] = src[index];
    }
}

inline void spmv_reference_body(const index_t* row_ptr,
                                const index_t* col_idx,
                                const data_t* values,
                                const data_t* x,
                                data_t* y,
                                const int n) {
    data_t x_local[kMaxN];
    load_vector_local(x, x_local, n);

    for (int row = 0; row < n; ++row) {
        data_t acc = 0.0;
        for (int offset = row_ptr[row]; offset < row_ptr[row + 1]; ++offset) {
            acc += values[offset] * x_local[col_idx[offset]];
        }
        y[row] = acc;
    }
}

}  // namespace

extern "C" {

void spmv_csr_kernel(const index_t* row_ptr,
                     const index_t* col_idx,
                     const data_t* values,
                     const data_t* x,
                     data_t* y,
                     int n) {
    // 这是本地验证路径里的“同名 kernel 函数”实现。
    // 它不经过 XRT，但算法行为和 HLS 顶层 kernel 保持一致。
    if (!valid_n(n)) {
        return;
    }

    spmv_reference_body(row_ptr, col_idx, values, x, y, n);
}

void spmv_blocked_kernel(const index_t* row_ptr,
                         const index_t* col_idx,
                         const data_t* values,
                         const data_t* x,
                         data_t* y,
                         int n) {
    // 这是给后续“分块 SpMV”替换预留的本地占位实现。
    // 当前先保持接口稳定，并退化为与 CSR 版一致的行为。
    if (!valid_n(n)) {
        return;
    }

    spmv_reference_body(row_ptr, col_idx, values, x, y, n);
}

void init_pcg_kernel(const data_t* b,
                     const data_t* ax,
                     const data_t* m_inv,
                     data_t* r,
                     data_t* z,
                     data_t* p,
                     data_t* metrics,
                     int n) {
    // 初始化阶段把 Jacobi-PCG 的前缀步骤合并在一起，
    // 这样后面的 host/本地编排器都能只拿 rz/rr 两个标量进入主循环。
    if (!valid_n(n)) {
        return;
    }

    data_t rz = 0.0;
    data_t rr = 0.0;

    for (int index = 0; index < n; ++index) {
        // r / z / p 的写入和 rz / rr 的 reduction 一次完成。
        const data_t r_value = b[index] - ax[index];
        const data_t z_value = m_inv[index] * r_value;
        r[index] = r_value;
        z[index] = z_value;
        p[index] = z_value;
        rz += r_value * z_value;
        rr += r_value * r_value;
    }

    metrics[0] = rz;
    metrics[1] = rr;
}

void dot_kernel(const data_t* a, const data_t* b, data_t* out, int n) {
    // 本地版点积和硬件版一样，只负责一个标量 reduction。
    if (!valid_n(n)) {
        return;
    }

    data_t acc = 0.0;
    for (int index = 0; index < n; ++index) {
        acc += a[index] * b[index];
    }
    out[0] = acc;
}

void update_xrz_kernel(data_t* x,
                       const data_t* p,
                       data_t* r,
                       const data_t* ap,
                       const data_t* m_inv,
                       data_t* z,
                       data_t* metrics,
                       data_t alpha,
                       int n) {
    // 这一层对应一轮 PCG 中与 alpha 绑定的全部更新：
    //   x / r / z / rz_new / rr
    if (!valid_n(n)) {
        return;
    }

    data_t rz_new = 0.0;
    data_t rr = 0.0;

    for (int index = 0; index < n; ++index) {
        // 先更新 x / r，再基于新 r 做 Jacobi 和新的两个标量。
        x[index] += alpha * p[index];
        r[index] -= alpha * ap[index];
        z[index] = m_inv[index] * r[index];
        rz_new += r[index] * z[index];
        rr += r[index] * r[index];
    }

    metrics[0] = rz_new;
    metrics[1] = rr;
}

void update_p_kernel(const data_t* z, data_t* p, data_t beta, int n) {
    // 方向更新单独留成一步，保持和 XRT 多 kernel 版完全同构。
    if (!valid_n(n)) {
        return;
    }

    for (int index = 0; index < n; ++index) {
        p[index] = z[index] + beta * p[index];
    }
}

}
