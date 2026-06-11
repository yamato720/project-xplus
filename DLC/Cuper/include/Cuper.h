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
// 这些常量描述 SpMV 的 slice/batch/HBM 并行度，不是 PCG 迭代参数。
// Project-XPlus 的 Cuper-PCG TAPA 版会从 host 侧多次调用 Cuper，
// 每次调用完成一次 y = A * x。
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
// Cuper(...)、CuperPcgSpmv(...) 和 CuperPcg(...) 引用的是同一个常量。
constexpr INDEX_TYPE FIFO_DEPTH             = 2;
constexpr INDEX_TYPE X_BRAM_DEPTH           = 4;
constexpr INDEX_TYPE X_TABLE_DEPTH          = 200;
// standalone Cuper benchmark 的默认重复次数。
// 在 Project-XPlus 的 Cuper-PCG TAPA 路径中，PCG 迭代由 host 控制，
// 调用 Cuper 时通常把 Iteration_num 传 1，避免和 PCG 迭代次数混淆。
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

// TAPA Cuper 顶层只做 SpMV：
//   Matrix_data + X -> Y_out
// 不接收/更新 PCG 的 r/z/p，也不计算 alpha/beta。
void Cuper(tapa::mmap<INDEX_TYPE> SpElement_list_ptr,
           tapa::mmaps<ap_uint<512>, HBM_CHANNEL_NUM> Matrix_data,
           tapa::mmap<float_v16> X,
           tapa::mmap<float_v16> Y_out,
          
           const INDEX_TYPE Batch_num,
           const INDEX_TYPE Matrix_len,
           const INDEX_TYPE Row_num,
           const INDEX_TYPE Column_num,
           const INDEX_TYPE Iteration_num
          );

// Cuper 兼容 single SpMV demo kernel。
//
// ABI 刻意保持为 single SpMV 形态：
//   Matrix_data + X -> Y_out
// 这样 host 可以用和 Cuper(...) 相同的输入/输出缓冲做 demo-only 对比。
// 当前内部刻意采用和 Cuper(...) 一样的一次性 SpMV task graph，不接 PCG
// service controller/command/stop。PCG 的服务控制优化只在 CuperPcg(...) 路径处理。
void CuperPcgSpmv(tapa::mmap<INDEX_TYPE> SpElement_list_ptr,
                  tapa::mmaps<ap_uint<512>, HBM_CHANNEL_NUM> Matrix_data,
                  tapa::mmap<float_v16> X,
                  tapa::mmap<float_v16> Y_out,
                  const INDEX_TYPE Batch_num,
                  const INDEX_TYPE Matrix_len,
                  const INDEX_TYPE Row_num,
                  const INDEX_TYPE Column_num,
                  const INDEX_TYPE Iteration_num
                 );

// TAPA Cuper + FPGA-side Jacobi-PCG 顶层。
//
// 这个版本保留 Cuper 的 TAPA SpMV task graph，但不再让 host 每轮调用
// SpMV。PCG controller 在 FPGA 内部发起每次 SpMV、消费 y=A*x/A*p，
// 并更新 x/r/z/p、metrics/status。
//
// 参数分组：
//   - SpElement_list_ptr / Matrix_data[0..15]：原 Cuper 矩阵格式；
//   - B/M_inv/X/R/Z/P：FP64 Jacobi-PCG 状态，按 double_v8 512-bit 包传输；
//   - AP_spmv/X_spmv/P_spmv：FP32 float_v16 packed SpMV 辅助缓冲；
//   - Metrics/Status：host 读取的调试计时和收敛状态。
//
// TAPA 端口类型：
//   - tapa::mmap<T> 表示一个连续的全局内存映射端口，host 侧通常对应一个
//     xrt::bo；kernel 内用数组下标读写 T 类型元素。
//   - tapa::mmaps<T, N> 表示 N 个同类型 mmap 端口的数组，常用来把同一类数据
//     分散到 N 个 HBM bank。这里 Matrix_data[0..15] 对应 16 路矩阵 HBM 端口。
void CuperPcg(tapa::mmap<INDEX_TYPE> SpElement_list_ptr,                // Cuper 预处理后的稀疏元素/批次索引表，驱动 16 路 SpMV 调度
              tapa::mmaps<ap_uint<512>, HBM_CHANNEL_NUM> Matrix_data,   // 16 个 HBM 通道上的 512-bit packed 矩阵数据
              tapa::mmap<double_v8> B,                                  // PCG 右端项 b，FP64 主状态，512-bit packed
              tapa::mmap<double_v8> M_inv,                              // Jacobi 预条件器对角逆 M^{-1}，512-bit packed
              tapa::mmap<double_v8> X,                                  // 解向量 x，输入初值 x0，kernel 内更新并写回最终解
              tapa::mmap<double_v8> R,                                  // 残差向量 r = b - A*x
              tapa::mmap<double_v8> Z,                                  // 预条件残差 z = M^{-1}*r
              tapa::mmap<double_v8> P,                                  // PCG 搜索方向 p，FP64 权威状态
              tapa::mmap<float_v16> AP_spmv,                            // packed FP32 的 A*p 缓冲，供 dot/update 阶段复用 SpMV 输出
              tapa::mmap<float_v16> X_spmv,                             // packed FP32 的 x0 副本，初始化 A*x0 时喂给 Cuper vector loader
              tapa::mmap<float_v16> P_spmv,                             // packed FP32 的 p 副本，每轮 A*p 时喂给 Cuper vector loader
              tapa::mmap<double> Metrics,                               // kernel 写回的阶段计时/调试统计数组
              tapa::mmap<INDEX_TYPE> Status,                            // kernel 写回的收敛、max-iter、breakdown 等状态码
              const INDEX_TYPE Batch_num,                               // Cuper SpMV 批次数/任务批数量
              const INDEX_TYPE Matrix_len,                              // Cuper 编码后的矩阵数据长度
              const INDEX_TYPE Row_num,                                 // 矩阵行数，也是 PCG 向量长度 n
              const INDEX_TYPE Column_num,                              // 矩阵列数，PCG 方阵场景通常等于 Row_num
              const INDEX_TYPE Max_iters,                               // PCG 最大迭代次数
              const double Tau                                          // PCG 收敛阈值
             );

#endif
