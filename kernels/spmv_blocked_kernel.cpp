#include "../include/cg_common.hpp"

namespace {

using data_t = project_xplus::cgsolver::data_t;
using index_t = project_xplus::cgsolver::index_t;
constexpr int kMaxN = project_xplus::cgsolver::kMaxN;
constexpr int kRowBlockSize = 32;

inline void spmv_blocked_body(const index_t* row_ptr,
                              const index_t* col_idx,
                              const data_t* values,
                              const data_t* x,
                              data_t* y,
                              const int n) {
    if (n < 0 || n > kMaxN) {
        return;
    }

    data_t x_local[kMaxN];
#pragma HLS BIND_STORAGE variable = x_local type = ram_2p impl = bram

load_x:
    for (int index = 0; index < n; ++index) {
#pragma HLS PIPELINE II = 1
        x_local[index] = x[index];
    }

row_blocks:
    for (int row_block_begin = 0; row_block_begin < n; row_block_begin += kRowBlockSize) {
        const int row_block_end =
            (row_block_begin + kRowBlockSize < n) ? (row_block_begin + kRowBlockSize) : n;

    block_rows:
        for (int row = row_block_begin; row < row_block_end; ++row) {
#pragma HLS PIPELINE II = 1
            data_t acc = 0.0;
            for (int offset = row_ptr[row]; offset < row_ptr[row + 1]; ++offset) {
                acc += values[offset] * x_local[col_idx[offset]];
            }
            y[row] = acc;
        }
    }
}

}  // namespace

extern "C" {

void spmv_blocked_kernel(const index_t* row_ptr,
                         const index_t* col_idx,
                         const data_t* values,
                         const data_t* x,
                         data_t* y,
                         int n) {
// 这个文件是给“分块 SpMV”预留的 HLS 顶层入口。
// 接口刻意保持和 spmv_csr_kernel 完全一致，
// 这样后续切换调用点时不需要重新整理 host 侧参数形状。
//
// 当前实现先保留一个可编译、可替换的占位版本：
// 1. 仍然按 CSR 语义输出 y = A * x
// 2. 外层先按 row block 组织结构，方便后续往真正的 blocked dataflow 演进
// 3. 暂时不引入新的数据格式、metadata 或额外端口
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

    spmv_blocked_body(row_ptr, col_idx, values, x, y, n);
}

void spmv_csr_kernel(const index_t* row_ptr,
                     const index_t* col_idx,
                     const data_t* values,
                     const data_t* x,
                     data_t* y,
                     int n) {
// 这个兼容顶层保留原来的 kernel 外部名字，
// 这样 xrt_host / connectivity / BO group_id 都不需要重写，
// 只通过编译时切换源文件就能把硬件实现换成 blocked 版本。
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

    spmv_blocked_body(row_ptr, col_idx, values, x, y, n);
}

}
