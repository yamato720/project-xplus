#include "../include/cg_common.hpp"

extern "C" {

void update_p_kernel(const project_xplus::cgsolver::data_t* z,
                     project_xplus::cgsolver::data_t* p,
                     project_xplus::cgsolver::data_t beta,
                     int n) {
// 方向更新单独拆成一个很轻量的 kernel：
//   p = z + beta * p
//
// beta 由 host 用 rz_new / rz_old 算好后作为标量下发。
#pragma HLS INTERFACE s_axilite port = z bundle = control
#pragma HLS INTERFACE s_axilite port = p bundle = control
#pragma HLS INTERFACE s_axilite port = beta bundle = control
#pragma HLS INTERFACE s_axilite port = n bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS INTERFACE m_axi port = z offset = slave bundle = gmem_z
#pragma HLS INTERFACE m_axi port = p offset = slave bundle = gmem_p

    constexpr int kMaxN = project_xplus::cgsolver::kMaxN;

    if (n < 0 || n > kMaxN) {
        return;
    }

update_p_loop:
    for (int index = 0; index < n; ++index) {
#pragma HLS PIPELINE II = 1
        p[index] = z[index] + beta * p[index];
    }
}

}
