#include "../include/cg_common.hpp"

namespace {

using data_t = project_xplus::cgsolver::data_t;
using index_t = project_xplus::cgsolver::index_t;
// 和 host / pcg_control_kernel 共用同一个 block 结构，避免两边结构体
// 字段顺序或大小不一致。
using Block = project_xplus::cgsolver::SpmvBlock;
constexpr int kMaxN = project_xplus::cgsolver::kMaxN;
constexpr int kRowBlockSize = 32;

// 独立 spmv_blocked_kernel 也使用 4x4 block/bitmap 格式。
// 当前默认 PCG xclbin 不链接这个独立 kernel；真正默认路径在
// pcg_control_kernel.cpp 里调用 spmv_blocked_local(...)。
constexpr int BLOCK_SIZE = project_xplus::cgsolver::kSpmvBlockSize;
constexpr unsigned char FORMAT_BITMAP = 1;

inline void spmv_blocked_bitmap_body(
    const index_t* b_row_ptr,
    const index_t* b_col_idx,
    const Block* blocks,
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
        
        // 局部累加器，用于暂存这 4 行的结果
        data_t y_accum[BLOCK_SIZE] = {0.0, 0.0, 0.0, 0.0};
#pragma HLS ARRAY_PARTITION variable=y_accum complete

        int block_start = b_row_ptr[br];
        int block_end = b_row_ptr[br + 1];

        // 3. 遍历该块行中的所有非零块
    blocks_in_row:
        for (int bi = block_start; bi < block_end; ++bi) {
#pragma HLS PIPELINE II = 1
            // 注意：为了达到 II=1，这个循环体内必须能在一个时钟周期内发起所有运算
            
            int bc = b_col_idx[bi];
            Block blk = blocks[bi];
            
            // 解析 16位 Mask
            unsigned short mask = (unsigned short)blk.indices[2] | ((unsigned short)blk.indices[3] << 8);
            
            int v_idx = 0;
            // 4. 解析 Bitmap 并累加
            // 这里循环界限是确定的 16，HLS 会自动完全展开 (UNROLL) 这个内层循环
            for (int pos = 0; pos < 16; pos++) {
                if ((mask >> pos) & 1) {
                    int local_r = pos / BLOCK_SIZE;
                    int local_c = pos % BLOCK_SIZE;
                    int global_c = bc * BLOCK_SIZE + local_c;
                    
                    // 安全访问：只有当列索引有效时才读取
                    data_t x_val = (global_c < total_n) ? x_local[global_c] : 0.0;
                    
                    y_accum[local_r] += blk.values[v_idx] * x_val;
                    v_idx++; // 只有该位为 1 时，才去取下一个紧凑存储的 value
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
                         const Block* blocks,
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
#pragma HLS INTERFACE s_axilite port = blocks bundle = control
#pragma HLS INTERFACE s_axilite port = x bundle = control
#pragma HLS INTERFACE s_axilite port = y bundle = control
#pragma HLS INTERFACE s_axilite port = n bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS INTERFACE m_axi port = b_row_ptr offset = slave bundle = gmem_row depth=50000
#pragma HLS INTERFACE m_axi port = b_col_idx offset = slave bundle = gmem_col depth=50000
#pragma HLS INTERFACE m_axi port = blocks offset = slave bundle = gmem_val depth=50000
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x depth=50000
#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y depth=50000

    int num_block_rows = (n + BLOCK_SIZE - 1) / BLOCK_SIZE;
    spmv_blocked_bitmap_body(b_row_ptr, b_col_idx, blocks, x, y, num_block_rows, n);
}

void spmv_csr_kernel(const index_t* b_row_ptr,
                     const index_t* b_col_idx,
                     const Block* blocks,
                     const data_t* x,
                     data_t* y,
                     int n) {
// 这个兼容顶层保留原来的 kernel 外部名字，
// 这样 xrt_host / connectivity / BO group_id 都不需要重写，
// 只通过编译时切换源文件就能把硬件实现换成 blocked 版本。

#pragma HLS INTERFACE s_axilite port = b_row_ptr bundle = control
#pragma HLS INTERFACE s_axilite port = b_col_idx bundle = control
#pragma HLS INTERFACE s_axilite port = blocks bundle = control
#pragma HLS INTERFACE s_axilite port = x bundle = control
#pragma HLS INTERFACE s_axilite port = y bundle = control
#pragma HLS INTERFACE s_axilite port = n bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS INTERFACE m_axi port = b_row_ptr offset = slave bundle = gmem_row depth=50000
#pragma HLS INTERFACE m_axi port = b_col_idx offset = slave bundle = gmem_col depth=50000
#pragma HLS INTERFACE m_axi port = blocks offset = slave bundle = gmem_val depth=50000
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x depth=50000
#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y depth=50000

    int num_block_rows = (n + BLOCK_SIZE - 1) / BLOCK_SIZE;
    spmv_blocked_bitmap_body(b_row_ptr, b_col_idx, blocks, x, y, num_block_rows, n);
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
