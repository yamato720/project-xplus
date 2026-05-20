#include "../include/cg_common.hpp"

namespace {

using data_t = project_xplus::cgsolver::data_t;
using index_t = project_xplus::cgsolver::index_t;
constexpr int kMaxN = project_xplus::cgsolver::kMaxN;
constexpr int kRowBlockSize = 32;

constexpr int BLOCK_SIZE = 128;
constexpr int BLOCK_ELEMENTS = BLOCK_SIZE * BLOCK_SIZE;
constexpr int BITMAP_WORD_BITS = 64;
constexpr int BITMAP_WORDS = (BLOCK_ELEMENTS + BITMAP_WORD_BITS - 1) / BITMAP_WORD_BITS;

// 确保和 Host 端结构一致
struct BlockBitmap {
    unsigned long long bitmap[BITMAP_WORDS];
    int nnz;
};

inline void spmv_blocked_bitmap_body(
    const index_t* b_row_ptr,
    const index_t* b_col_idx,
    const index_t* b_nnz_ptr,
    const BlockBitmap* blocks,
    const data_t* values,
    const data_t* x,
    data_t* y,
    const int num_block_rows,
    const int total_n) 
{
    // 1. 将全量 x 缓存到 BRAM 中，保证后续随机访问 II=1
    data_t x_local[kMaxN];
#pragma HLS BIND_STORAGE variable = x_local type = ram_2p impl = bram

load_x:
    for (int index = 0; index < total_n; ++index) {
#pragma HLS PIPELINE II = 1
        x_local[index] = x[index];
    }

    // 2. 顺序处理每个块行
block_rows:
    for (int br = 0; br < num_block_rows; ++br) {
        
        data_t y_accum[BLOCK_SIZE];
#pragma HLS ARRAY_PARTITION variable=y_accum cyclic factor=16

    clear_accum:
        for (int i = 0; i < BLOCK_SIZE; ++i) {
#pragma HLS PIPELINE II = 1
            y_accum[i] = 0.0;
        }

        int block_start = b_row_ptr[br];
        int block_end = b_row_ptr[br + 1];

        // 3. 遍历该块行中的所有非零块
    blocks_in_row:
        for (int bi = block_start; bi < block_end; ++bi) {
            int bc = b_col_idx[bi];
            int v_idx = b_nnz_ptr[bi];

            // 4. 解析 128x128 bitmap 并按 bitmap 顺序读取紧凑 values。
        bitmap_words:
            for (int word_index = 0; word_index < BITMAP_WORDS; ++word_index) {
                unsigned long long mask = blocks[bi].bitmap[word_index];
                const int word_base = word_index * BITMAP_WORD_BITS;

            bits_in_word:
                for (int bit = 0; bit < BITMAP_WORD_BITS; ++bit) {
#pragma HLS PIPELINE II = 1
                    if ((mask >> bit) & 1ULL) {
                        int pos = word_base + bit;
                        int local_r = pos / BLOCK_SIZE;
                        int local_c = pos % BLOCK_SIZE;
                        int global_c = bc * BLOCK_SIZE + local_c;

                        data_t x_val = (global_c < total_n) ? x_local[global_c] : 0.0;
                        y_accum[local_r] += values[v_idx] * x_val;
                        v_idx++;
                    }
                }
            }
        }

        // 5. 写回全局内存 y，包含边界保护
    write_y:
        for (int i = 0; i < BLOCK_SIZE; ++i) {
#pragma HLS PIPELINE II = 1
            int global_r = br * BLOCK_SIZE + i;
            if (global_r < total_n) {
                y[global_r] = y_accum[i];
            }
        }
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

void spmv_blocked_kernel(const index_t* b_row_ptr,
                         const index_t* b_col_idx,
                         const index_t* b_nnz_ptr,
                         const BlockBitmap* blocks,
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
#pragma HLS INTERFACE s_axilite port = b_row_ptr bundle = control
#pragma HLS INTERFACE s_axilite port = b_col_idx bundle = control
#pragma HLS INTERFACE s_axilite port = b_nnz_ptr bundle = control
#pragma HLS INTERFACE s_axilite port = blocks bundle = control
#pragma HLS INTERFACE s_axilite port = values bundle = control
#pragma HLS INTERFACE s_axilite port = x bundle = control
#pragma HLS INTERFACE s_axilite port = y bundle = control
#pragma HLS INTERFACE s_axilite port = n bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS INTERFACE m_axi port = b_row_ptr offset = slave bundle = gmem_row depth=50000
#pragma HLS INTERFACE m_axi port = b_col_idx offset = slave bundle = gmem_col depth=50000
#pragma HLS INTERFACE m_axi port = b_nnz_ptr offset = slave bundle = gmem_nnz depth=50000
#pragma HLS INTERFACE m_axi port = blocks offset = slave bundle = gmem_bitmap depth=50000
#pragma HLS INTERFACE m_axi port = values offset = slave bundle = gmem_val depth=50000
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x depth=50000
#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y depth=50000

    int num_block_rows = (n + BLOCK_SIZE - 1) / BLOCK_SIZE;
    spmv_blocked_bitmap_body(b_row_ptr, b_col_idx, b_nnz_ptr, blocks, values, x, y, num_block_rows, n);
}

void spmv_csr_kernel(const index_t* b_row_ptr,
                     const index_t* b_col_idx,
                     const index_t* b_nnz_ptr,
                     const BlockBitmap* blocks,
                     const data_t* values,
                     const data_t* x,
                     data_t* y,
                     int n) {
// 这个兼容顶层保留原来的 kernel 外部名字，
// 这样 xrt_host / connectivity / BO group_id 都不需要重写，
// 只通过编译时切换源文件就能把硬件实现换成 blocked 版本。

#pragma HLS INTERFACE s_axilite port = b_row_ptr bundle = control
#pragma HLS INTERFACE s_axilite port = b_col_idx bundle = control
#pragma HLS INTERFACE s_axilite port = b_nnz_ptr bundle = control
#pragma HLS INTERFACE s_axilite port = blocks bundle = control
#pragma HLS INTERFACE s_axilite port = values bundle = control
#pragma HLS INTERFACE s_axilite port = x bundle = control
#pragma HLS INTERFACE s_axilite port = y bundle = control
#pragma HLS INTERFACE s_axilite port = n bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS INTERFACE m_axi port = b_row_ptr offset = slave bundle = gmem_row depth=50000
#pragma HLS INTERFACE m_axi port = b_col_idx offset = slave bundle = gmem_col depth=50000
#pragma HLS INTERFACE m_axi port = b_nnz_ptr offset = slave bundle = gmem_nnz depth=50000
#pragma HLS INTERFACE m_axi port = blocks offset = slave bundle = gmem_bitmap depth=50000
#pragma HLS INTERFACE m_axi port = values offset = slave bundle = gmem_val depth=50000
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x depth=50000
#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y depth=50000

    int num_block_rows = (n + BLOCK_SIZE - 1) / BLOCK_SIZE;
    spmv_blocked_bitmap_body(b_row_ptr, b_col_idx, b_nnz_ptr, blocks, values, x, y, num_block_rows, n);
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
