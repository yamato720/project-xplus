#include "../include/cg_common.hpp"

namespace {

using data_t = project_xplus::cgsolver::data_t;
using index_t = project_xplus::cgsolver::index_t;
constexpr int kMaxN = project_xplus::cgsolver::kMaxN;

constexpr int BLOCK_SIZE = 128;

inline void spmv_block_csc_coo_body(
    const index_t* b_col_ptr,
    const index_t* b_row_idx,
    const index_t* b_nnz_ptr,
    const unsigned char* local_rows,
    const unsigned char* local_cols,
    const data_t* values,
    const data_t* x,
    data_t* y,
    const int num_block_cols,
    const int total_n) 
{
    // 1. 将全量 x 和 y 缓存在片上，避免块间 CSC 对 y 的随机写直接打到外存。
    data_t x_local[kMaxN];
    data_t y_local[kMaxN];
#pragma HLS BIND_STORAGE variable = x_local type = ram_2p impl = bram
#pragma HLS BIND_STORAGE variable = y_local type = ram_2p impl = bram

load_xy:
    for (int index = 0; index < total_n; ++index) {
#pragma HLS PIPELINE II = 1
        x_local[index] = x[index];
        y_local[index] = 0.0;
    }

    // 2. 块间按 CSC 遍历：先走 block-column，再走该列中的非零 block。
block_cols:
    for (int bc = 0; bc < num_block_cols; ++bc) {
    blocks_in_col:
        for (int bi = b_col_ptr[bc]; bi < b_col_ptr[bc + 1]; ++bi) {
            const int br = b_row_idx[bi];
            const int value_begin = b_nnz_ptr[bi];
            const int value_end = b_nnz_ptr[bi + 1];

        block_entries:
            for (int vi = value_begin; vi < value_end; ++vi) {
#pragma HLS PIPELINE II = 1
                const int row = br * BLOCK_SIZE + static_cast<int>(local_rows[vi]);
                const int col = bc * BLOCK_SIZE + static_cast<int>(local_cols[vi]);
                if (row < total_n && col < total_n) {
                    y_local[row] += values[vi] * x_local[col];
                }
            }
        }
    }

write_y:
    for (int index = 0; index < total_n; ++index) {
#pragma HLS PIPELINE II = 1
        y[index] = y_local[index];
    }
}

// inline void spmv_blocked_body(const index_t* row_ptr,
//                               const index_t* col_idx,
//                               const data_t* values,
//                               const data_t* x,
//                               data_t* y,
//                               const int n) {
//     if (n < 0 || n > kMaxN) {
//         return;
//     }

//     data_t x_local[kMaxN];
// #pragma HLS BIND_STORAGE variable = x_local type = ram_2p impl = bram

// load_x:
//     for (int index = 0; index < n; ++index) {
// #pragma HLS PIPELINE II = 1
//         x_local[index] = x[index];
//     }

// row_blocks:
//     for (int row_block_begin = 0; row_block_begin < n; row_block_begin += kRowBlockSize) {
//         const int row_block_end =
//             (row_block_begin + kRowBlockSize < n) ? (row_block_begin + kRowBlockSize) : n;

//     block_rows:
//         for (int row = row_block_begin; row < row_block_end; ++row) {
// #pragma HLS PIPELINE II = 1
//             data_t acc = 0.0;
//             for (int offset = row_ptr[row]; offset < row_ptr[row + 1]; ++offset) {
//                 acc += values[offset] * x_local[col_idx[offset]];
//             }
//             y[row] = acc;
//         }
//     }
// }

}  // namespace

extern "C" {

void spmv_blocked_kernel(const index_t* b_col_ptr,
                         const index_t* b_row_idx,
                         const index_t* b_nnz_ptr,
                         const unsigned char* local_rows,
                         const unsigned char* local_cols,
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
#pragma HLS INTERFACE s_axilite port = b_col_ptr bundle = control
#pragma HLS INTERFACE s_axilite port = b_row_idx bundle = control
#pragma HLS INTERFACE s_axilite port = b_nnz_ptr bundle = control
#pragma HLS INTERFACE s_axilite port = local_rows bundle = control
#pragma HLS INTERFACE s_axilite port = local_cols bundle = control
#pragma HLS INTERFACE s_axilite port = values bundle = control
#pragma HLS INTERFACE s_axilite port = x bundle = control
#pragma HLS INTERFACE s_axilite port = y bundle = control
#pragma HLS INTERFACE s_axilite port = n bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS INTERFACE m_axi port = b_col_ptr offset = slave bundle = gmem_colptr depth=50000
#pragma HLS INTERFACE m_axi port = b_row_idx offset = slave bundle = gmem_rowidx depth=50000
#pragma HLS INTERFACE m_axi port = b_nnz_ptr offset = slave bundle = gmem_nnz depth=50000
#pragma HLS INTERFACE m_axi port = local_rows offset = slave bundle = gmem_local_rows depth=50000
#pragma HLS INTERFACE m_axi port = local_cols offset = slave bundle = gmem_local_cols depth=50000
#pragma HLS INTERFACE m_axi port = values offset = slave bundle = gmem_val depth=50000
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x depth=50000
#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y depth=50000

    int num_block_cols = (n + BLOCK_SIZE - 1) / BLOCK_SIZE;
    spmv_block_csc_coo_body(b_col_ptr, b_row_idx, b_nnz_ptr, local_rows, local_cols, values, x, y, num_block_cols, n);
}

void spmv_csr_kernel(const index_t* b_col_ptr,
                     const index_t* b_row_idx,
                     const index_t* b_nnz_ptr,
                     const unsigned char* local_rows,
                     const unsigned char* local_cols,
                     const data_t* values,
                     const data_t* x,
                     data_t* y,
                     int n) {
// 这个兼容顶层保留原来的 kernel 外部名字，
// 这样 xrt_host / connectivity / BO group_id 都不需要重写，
// 只通过编译时切换源文件就能把硬件实现换成 blocked 版本。

#pragma HLS INTERFACE s_axilite port = b_col_ptr bundle = control
#pragma HLS INTERFACE s_axilite port = b_row_idx bundle = control
#pragma HLS INTERFACE s_axilite port = b_nnz_ptr bundle = control
#pragma HLS INTERFACE s_axilite port = local_rows bundle = control
#pragma HLS INTERFACE s_axilite port = local_cols bundle = control
#pragma HLS INTERFACE s_axilite port = values bundle = control
#pragma HLS INTERFACE s_axilite port = x bundle = control
#pragma HLS INTERFACE s_axilite port = y bundle = control
#pragma HLS INTERFACE s_axilite port = n bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS INTERFACE m_axi port = b_col_ptr offset = slave bundle = gmem_colptr depth=50000
#pragma HLS INTERFACE m_axi port = b_row_idx offset = slave bundle = gmem_rowidx depth=50000
#pragma HLS INTERFACE m_axi port = b_nnz_ptr offset = slave bundle = gmem_nnz depth=50000
#pragma HLS INTERFACE m_axi port = local_rows offset = slave bundle = gmem_local_rows depth=50000
#pragma HLS INTERFACE m_axi port = local_cols offset = slave bundle = gmem_local_cols depth=50000
#pragma HLS INTERFACE m_axi port = values offset = slave bundle = gmem_val depth=50000
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x depth=50000
#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y depth=50000

    int num_block_cols = (n + BLOCK_SIZE - 1) / BLOCK_SIZE;
    spmv_block_csc_coo_body(b_col_ptr, b_row_idx, b_nnz_ptr, local_rows, local_cols, values, x, y, num_block_cols, n);
// #pragma HLS INTERFACE s_axilite port = row_ptr bundle = control
// #pragma HLS INTERFACE s_axilite port = col_idx bundle = control
// #pragma HLS INTERFACE s_axilite port = values bundle = control
// #pragma HLS INTERFACE s_axilite port = x bundle = control
// #pragma HLS INTERFACE s_axilite port = y bundle = control
// #pragma HLS INTERFACE s_axilite port = n bundle = control
// #pragma HLS INTERFACE s_axilite port = return bundle = control

// #pragma HLS INTERFACE m_axi port = row_ptr offset = slave bundle = gmem_row
// #pragma HLS INTERFACE m_axi port = col_idx offset = slave bundle = gmem_col
// #pragma HLS INTERFACE m_axi port = values offset = slave bundle = gmem_val
// #pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x
// #pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y

//     spmv_blocked_body(row_ptr, col_idx, values, x, y, n);
     }

}
