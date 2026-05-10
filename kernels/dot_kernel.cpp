#include "../include/cg_common.hpp"

extern "C" {

void dot_kernel(const project_xplus::cgsolver::data_t* a,
                const project_xplus::cgsolver::data_t* b,
                project_xplus::cgsolver::data_t* out,
                int n) {
// dot kernel 当前只负责一个标量 reduction：
//   out[0] = a^T b
//
// 在 PCG 主循环里，它主要用来计算 p^T ap。
#pragma HLS INTERFACE s_axilite port = a bundle = control
#pragma HLS INTERFACE s_axilite port = b bundle = control
#pragma HLS INTERFACE s_axilite port = out bundle = control
#pragma HLS INTERFACE s_axilite port = n bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS INTERFACE m_axi port = a offset = slave bundle = gmem_a
#pragma HLS INTERFACE m_axi port = b offset = slave bundle = gmem_b
#pragma HLS INTERFACE m_axi port = out offset = slave bundle = gmem_out

    using data_t = project_xplus::cgsolver::data_t;
    constexpr int kMaxN = project_xplus::cgsolver::kMaxN;

    if (n < 0 || n > kMaxN) {
        return;
    }

    data_t acc = 0.0;

dot_loop:
    for (int index = 0; index < n; ++index) {
#pragma HLS PIPELINE II = 1
        acc += a[index] * b[index];
    }

    out[0] = acc;
}

}
