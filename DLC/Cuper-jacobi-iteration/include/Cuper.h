#ifndef CUPER_H
#define CUPER_H

#include <ap_int.h>
#include <tapa.h>

#define VALUE_TYPE float
#define INDEX_TYPE int

// #define BINARY_READ

#define PINGPONG

#define FLEX_REUSE

//#define X_TABLE


// Cuper TAPA SpMV 的硬件结构常量。
// 这些常量描述 SpMV 的 slice/batch/HBM 并行度，不是 Jacobi 迭代参数。
// CuperJacobiIteration 内部会由 controller 多次触发 service 化 SpMV，
// 每次触发完成一次 y = R * (-x_old)；R 是 host 侧从 A 中去掉对角项后的矩阵。
constexpr INDEX_TYPE PE_NUM                 = 8;
constexpr INDEX_TYPE HBM_CHANNEL_NUM        = 16;
constexpr INDEX_TYPE ROW_HBM_NUM            = 4;
// SparseSlice 的二维块边长。当前为 16 * 4 = 64，也就是 host 会先把
// 矩阵按 64 x 64 的 slice 块归类，再继续映射到 PE/HBM。
constexpr INDEX_TYPE Slice_SIZE             = HBM_CHANNEL_NUM * ROW_HBM_NUM;
constexpr INDEX_TYPE BATCH_SIZE             = 8192 / Slice_SIZE;
constexpr INDEX_TYPE WINDOWS                = 10;
constexpr INDEX_TYPE X_PARTITION_FACTOR     = 8;
constexpr INDEX_TYPE URAM_DEPTH             = (48 / HBM_CHANNEL_NUM) * 4096 / 2;
// TAPA stream 模板参数里的 FIFO 深度，当前值为 2。
// CuperJacobiIteration(...) 和 service 化 SpMV task 引用的是同一个常量。
constexpr INDEX_TYPE FIFO_DEPTH             = 2;
constexpr INDEX_TYPE X_BRAM_DEPTH           = 4;
constexpr INDEX_TYPE X_TABLE_DEPTH          = 200;
// standalone Cuper benchmark 的默认重复次数。
// 当前 Jacobi 顶层不使用这个常量控制迭代轮数；真正的 Jacobi 轮数来自
// CuperJacobiIteration(...) 的 Max_iters。
constexpr INDEX_TYPE ITERATION_NUM          = 2;
constexpr INDEX_TYPE X_TABLE_ITERATION_NUM  = 1;
constexpr double     THRESHOLD              = 1e-10;

const     INDEX_TYPE HBM_CHANNEL_NUM_DIV_8    = HBM_CHANNEL_NUM >> 3;
const     INDEX_TYPE HBM_CHANNEL_NUM_MULT_16  = HBM_CHANNEL_NUM << 4;
const     INDEX_TYPE HBM_CHANNEL_NUM_MULT_2   = HBM_CHANNEL_NUM << 1;
const     INDEX_TYPE Slice_WIDTH            = Slice_SIZE * BATCH_SIZE;
const     INDEX_TYPE Slice_WIDTH_DIV_16     = Slice_WIDTH >> 4;

inline INDEX_TYPE Cuper_NumFloatV16Packets(const INDEX_TYPE element_count) {
#pragma HLS inline
    return (element_count + 15) >> 4;
}

inline INDEX_TYPE Cuper_NumDoubleV8Packets(const INDEX_TYPE element_count) {
#pragma HLS inline
    return (element_count + 7) >> 3;
}

inline INDEX_TYPE Cuper_NumAccumulatorInitGroups(const INDEX_TYPE row_num) {
#pragma HLS inline
    return (row_num + HBM_CHANNEL_NUM_MULT_16 - 1) / HBM_CHANNEL_NUM_MULT_16;
}

inline INDEX_TYPE Cuper_NumAccumulatorOutputs(const INDEX_TYPE row_num) {
#pragma HLS inline
    return (row_num + HBM_CHANNEL_NUM_MULT_2 - 1) / HBM_CHANNEL_NUM_MULT_2;
}

inline INDEX_TYPE Cuper_NumCheckerPeOutputs(const INDEX_TYPE row_num) {
#pragma HLS inline
    return Cuper_NumAccumulatorOutputs(row_num) * HBM_CHANNEL_NUM_DIV_8;
}

using int_v2    = tapa::vec_t<INDEX_TYPE, 2>;

using float_v2  = tapa::vec_t<VALUE_TYPE, 2>;
using float_v4  = tapa::vec_t<VALUE_TYPE, 4>;
using float_v8  = tapa::vec_t<VALUE_TYPE, 8>;
using float_v16 = tapa::vec_t<VALUE_TYPE, 16>;
using double_v8 = tapa::vec_t<double, 8>;

//using row_v8    = tapa::vec_t<ap_uint<18>, 8>;

// Jacobi iteration 实验顶层。
//
// 当前 demo 在 host 侧拆 A = D + R，Jacobi vector loader 读 X0/X1 时取负，
// Cuper service 因此计算 -R*x_old：
//   x_next = (b + (-R*x_old)) * diag_inv
// X0/X1 是双缓冲；Status[1] 返回最终解所在 buffer。
void CuperJacobiIteration(tapa::mmap<INDEX_TYPE> SpElement_list_ptr,
                          tapa::mmaps<ap_uint<512>, HBM_CHANNEL_NUM> Matrix_data,
                          tapa::mmap<float_v16> B,
                          tapa::mmap<float_v16> Diag_inv,
                          tapa::mmap<float_v16> X0,
                          tapa::mmap<float_v16> X1,
                          tapa::mmap<INDEX_TYPE> Status,
                          tapa::mmap<double> Metrics,
                          const INDEX_TYPE Batch_num,
                          const INDEX_TYPE Matrix_len,
                          const INDEX_TYPE Row_num,
                          const INDEX_TYPE Column_num,
                          const INDEX_TYPE Max_iters,
                          const float Tau
                         );

#endif
