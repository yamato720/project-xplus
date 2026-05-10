#include "../include/cg_common.hpp"

extern "C" {

void init_pcg_kernel(const project_xplus::cgsolver::data_t* b,
                     const project_xplus::cgsolver::data_t* ax,
                     const project_xplus::cgsolver::data_t* m_inv,
                     project_xplus::cgsolver::data_t* r,
                     project_xplus::cgsolver::data_t* z,
                     project_xplus::cgsolver::data_t* p,
                     project_xplus::cgsolver::data_t* metrics,
                     int n) {
// 初始化 kernel 把 Jacobi-PCG 的前缀步骤打包在一起：
//   r  = b - ax
//   z  = M^{-1} r
//   p  = z
//   rz = r^T z
//   rr = r^T r
#pragma HLS INTERFACE s_axilite port = b bundle = control
#pragma HLS INTERFACE s_axilite port = ax bundle = control
#pragma HLS INTERFACE s_axilite port = m_inv bundle = control
#pragma HLS INTERFACE s_axilite port = r bundle = control
#pragma HLS INTERFACE s_axilite port = z bundle = control
#pragma HLS INTERFACE s_axilite port = p bundle = control
#pragma HLS INTERFACE s_axilite port = metrics bundle = control
#pragma HLS INTERFACE s_axilite port = n bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS INTERFACE m_axi port = b offset = slave bundle = gmem_b
#pragma HLS INTERFACE m_axi port = ax offset = slave bundle = gmem_ax
#pragma HLS INTERFACE m_axi port = m_inv offset = slave bundle = gmem_minv
#pragma HLS INTERFACE m_axi port = r offset = slave bundle = gmem_r
#pragma HLS INTERFACE m_axi port = z offset = slave bundle = gmem_z
#pragma HLS INTERFACE m_axi port = p offset = slave bundle = gmem_p
#pragma HLS INTERFACE m_axi port = metrics offset = slave bundle = gmem_metrics

    using data_t = project_xplus::cgsolver::data_t;
    constexpr int kMaxN = project_xplus::cgsolver::kMaxN;

    if (n < 0 || n > kMaxN) {
        return;
    }

    data_t rz = 0.0;
    data_t rr = 0.0;

init_loop:
    for (int index = 0; index < n; ++index) {
#pragma HLS PIPELINE II = 1
        // 一次循环同时完成向量更新和两个 reduction，
        // 这样 host 不需要再单独调 kernel 去算 rz / rr。
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

}
