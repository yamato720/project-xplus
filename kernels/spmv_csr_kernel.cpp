#include "../include/cg_common.hpp"

extern "C" {

void spmv_csr_kernel(const project_xplus::cgsolver::index_t* row_ptr,
                     const project_xplus::cgsolver::index_t* col_idx,
                     const project_xplus::cgsolver::data_t* values,
                     const project_xplus::cgsolver::data_t* x,
                     project_xplus::cgsolver::data_t* y,
                     int n) {
// 这个顶层 kernel 对应硬件中的一次 CSR SpMV：
//   y = A * x
//
// host 会在两个地方复用它：
// 1. 初始化时算 ax = A * x0
// 2. 迭代时算 ap = A * p
#pragma HLS INTERFACE s_axilite port = row_ptr bundle = control
#pragma HLS INTERFACE s_axilite port = col_idx bundle = control
#pragma HLS INTERFACE s_axilite port = values bundle = control
#pragma HLS INTERFACE s_axilite port = x bundle = control
#pragma HLS INTERFACE s_axilite port = y bundle = control
#pragma HLS INTERFACE s_axilite port = n bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS INTERFACE m_axi port = row_ptr offset = slave bundle = gmem_row
#pragma HLS INTERFACE m_axi port = col_idx offset = slave bundle = gmem_col
#pragma HLS INTERFACE m_axi port = values offset = slave bundle = gmem_val
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x
#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y

    using data_t = project_xplus::cgsolver::data_t;
    constexpr int kMaxN = project_xplus::cgsolver::kMaxN;

    if (n < 0 || n > kMaxN) {
        return;
    }

    // 对 n<=kMaxN 的小规模向量，先把输入 x 缓存到片上 BRAM，
    // 避免后面按 CSR col_idx 随机索引时每次都打到外部存储。
    data_t x_local[kMaxN];
#pragma HLS BIND_STORAGE variable = x_local type = ram_2p impl = bram

load_x:
    for (int index = 0; index < n; ++index) {
#pragma HLS PIPELINE II = 1
        x_local[index] = x[index];
    }

spmv_rows:
    for (int row = 0; row < n; ++row) {
#pragma HLS PIPELINE II = 1
        // CSR 一行对应一个输出 y[row]。
        data_t acc = 0.0;
        for (int offset = row_ptr[row]; offset < row_ptr[row + 1]; ++offset) {
            acc += values[offset] * x_local[col_idx[offset]];
        }
        y[row] = acc;
    }
}

}
