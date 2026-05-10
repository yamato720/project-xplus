#include "../include/cg_common.hpp"

extern "C" {

void update_xrz_kernel(project_xplus::cgsolver::data_t* x,
                       const project_xplus::cgsolver::data_t* p,
                       project_xplus::cgsolver::data_t* r,
                       const project_xplus::cgsolver::data_t* ap,
                       const project_xplus::cgsolver::data_t* m_inv,
                       project_xplus::cgsolver::data_t* z,
                       project_xplus::cgsolver::data_t* metrics,
                       project_xplus::cgsolver::data_t alpha,
                       int n) {
// 这一层把同一轮 alpha 相关的更新合在一个 kernel：
//   x      = x + alpha p
//   r      = r - alpha ap
//   z      = M^{-1} r
//   rz_new = r^T z
//   rr     = r^T r
//
// 这样 host 每轮只需要拿回两个标量 rz_new / rr。
#pragma HLS INTERFACE s_axilite port = x bundle = control
#pragma HLS INTERFACE s_axilite port = p bundle = control
#pragma HLS INTERFACE s_axilite port = r bundle = control
#pragma HLS INTERFACE s_axilite port = ap bundle = control
#pragma HLS INTERFACE s_axilite port = m_inv bundle = control
#pragma HLS INTERFACE s_axilite port = z bundle = control
#pragma HLS INTERFACE s_axilite port = metrics bundle = control
#pragma HLS INTERFACE s_axilite port = alpha bundle = control
#pragma HLS INTERFACE s_axilite port = n bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x
#pragma HLS INTERFACE m_axi port = p offset = slave bundle = gmem_p
#pragma HLS INTERFACE m_axi port = r offset = slave bundle = gmem_r
#pragma HLS INTERFACE m_axi port = ap offset = slave bundle = gmem_ap
#pragma HLS INTERFACE m_axi port = m_inv offset = slave bundle = gmem_minv
#pragma HLS INTERFACE m_axi port = z offset = slave bundle = gmem_z
#pragma HLS INTERFACE m_axi port = metrics offset = slave bundle = gmem_metrics

    using data_t = project_xplus::cgsolver::data_t;
    constexpr int kMaxN = project_xplus::cgsolver::kMaxN;

    if (n < 0 || n > kMaxN) {
        return;
    }

    data_t rz_new = 0.0;
    data_t rr = 0.0;

update_loop:
    for (int index = 0; index < n; ++index) {
#pragma HLS PIPELINE II = 1
        // 这一步对应 Jacobi-PCG 一轮中最重的向量更新部分。
        x[index] += alpha * p[index];
        r[index] -= alpha * ap[index];
        z[index] = m_inv[index] * r[index];
        rz_new += r[index] * z[index];
        rr += r[index] * r[index];
    }

    metrics[0] = rz_new;
    metrics[1] = rr;
}

}
