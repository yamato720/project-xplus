#include "../include/cg_common.hpp"

namespace {

using data_t = project_xplus::cgsolver::data_t;
using index_t = project_xplus::cgsolver::index_t;

constexpr int BLOCK_SIZE = 4096;

inline void spmv_block_csc_coo_body(
    const index_t* b_col_ptr,
    const index_t* b_row_idx,
    const index_t* b_nnz_ptr,
    const unsigned short* local_rows,
    const unsigned short* local_cols,
    const data_t* values,
    const data_t* x,
    data_t* y,
    const int num_block_cols,
    const int total_n) 
{
    // 输出向量每次 SpMV 都从 0 开始累加。这里顺序清外存，避免使用 y_local[kMaxN]。
clear_y:
    for (int index = 0; index < total_n; ++index) {
#pragma HLS PIPELINE II = 1
        y[index] = 0.0;
    }

    // 块间按 CSC 遍历。每次只缓存当前 block-column 对应的 4096 段 x。
block_cols:
    for (int bc = 0; bc < num_block_cols; ++bc) {
        data_t x_tile[BLOCK_SIZE];
#pragma HLS BIND_STORAGE variable = x_tile type = ram_2p impl = bram

    load_x_tile:
        for (int local_col = 0; local_col < BLOCK_SIZE; ++local_col) {
#pragma HLS PIPELINE II = 1
            const int col = bc * BLOCK_SIZE + local_col;
            x_tile[local_col] = (col < total_n) ? x[col] : 0.0;
        }

    blocks_in_col:
        for (int bi = b_col_ptr[bc]; bi < b_col_ptr[bc + 1]; ++bi) {
            const int br = b_row_idx[bi];
            const int value_begin = b_nnz_ptr[bi];
            const int value_end = b_nnz_ptr[bi + 1];

            data_t y_tile[BLOCK_SIZE];
#pragma HLS BIND_STORAGE variable = y_tile type = ram_2p impl = bram

        load_y_tile:
            for (int local_row = 0; local_row < BLOCK_SIZE; ++local_row) {
#pragma HLS PIPELINE II = 1
                const int row = br * BLOCK_SIZE + local_row;
                y_tile[local_row] = (row < total_n) ? y[row] : 0.0;
            }

        block_entries:
            for (int vi = value_begin; vi < value_end; ++vi) {
#pragma HLS PIPELINE II = 1
                const int local_row = static_cast<int>(local_rows[vi]);
                const int local_col = static_cast<int>(local_cols[vi]);
                if (local_row < BLOCK_SIZE && local_col < BLOCK_SIZE) {
                    y_tile[local_row] += values[vi] * x_tile[local_col];
                }
            }

        write_y_tile:
            for (int local_row = 0; local_row < BLOCK_SIZE; ++local_row) {
#pragma HLS PIPELINE II = 1
                const int row = br * BLOCK_SIZE + local_row;
                if (row < total_n) {
                    y[row] = y_tile[local_row];
                }
            }
        }
    }
}

}  // namespace

extern "C" {

void spmv_blocked_kernel(const index_t* b_col_ptr,
                         const index_t* b_row_idx,
                         const index_t* b_nnz_ptr,
                         const unsigned short* local_rows,
                         const unsigned short* local_cols,
                         const data_t* values,
                         const data_t* x,
                         data_t* y,
                         int n) {
// 4096x4096 块间 CSC、块内 COO 的 SpMV 顶层入口。
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

#pragma HLS INTERFACE m_axi port = b_col_ptr offset = slave bundle = gmem_colptr depth=200001
#pragma HLS INTERFACE m_axi port = b_row_idx offset = slave bundle = gmem_rowidx depth=200000
#pragma HLS INTERFACE m_axi port = b_nnz_ptr offset = slave bundle = gmem_nnz depth=200001
#pragma HLS INTERFACE m_axi port = local_rows offset = slave bundle = gmem_local_rows depth=2000000
#pragma HLS INTERFACE m_axi port = local_cols offset = slave bundle = gmem_local_cols depth=2000000
#pragma HLS INTERFACE m_axi port = values offset = slave bundle = gmem_val depth=2000000
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x depth=200000
#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y depth=200000

    int num_block_cols = (n + BLOCK_SIZE - 1) / BLOCK_SIZE;
    spmv_block_csc_coo_body(b_col_ptr, b_row_idx, b_nnz_ptr, local_rows, local_cols, values, x, y, num_block_cols, n);
}

void spmv_csr_kernel(const index_t* b_col_ptr,
                     const index_t* b_row_idx,
                     const index_t* b_nnz_ptr,
                     const unsigned short* local_rows,
                     const unsigned short* local_cols,
                     const data_t* values,
                     const data_t* x,
                     data_t* y,
                     int n) {
// 兼容旧的 kernel 名称，host 和 connectivity 仍然实例化 spmv_csr_kernel。

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

#pragma HLS INTERFACE m_axi port = b_col_ptr offset = slave bundle = gmem_colptr depth=200001
#pragma HLS INTERFACE m_axi port = b_row_idx offset = slave bundle = gmem_rowidx depth=200000
#pragma HLS INTERFACE m_axi port = b_nnz_ptr offset = slave bundle = gmem_nnz depth=200001
#pragma HLS INTERFACE m_axi port = local_rows offset = slave bundle = gmem_local_rows depth=2000000
#pragma HLS INTERFACE m_axi port = local_cols offset = slave bundle = gmem_local_cols depth=2000000
#pragma HLS INTERFACE m_axi port = values offset = slave bundle = gmem_val depth=2000000
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x depth=200000
#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y depth=200000

    int num_block_cols = (n + BLOCK_SIZE - 1) / BLOCK_SIZE;
    spmv_block_csc_coo_body(b_col_ptr, b_row_idx, b_nnz_ptr, local_rows, local_cols, values, x, y, num_block_cols, n);
}

}
