#pragma once

// Private implementation header for kernels/Cuper.cpp.
// It contains top-level task graph definitions and should not be included by another translation unit.

#include <ap_int.h>
#include <tapa.h>

#include "cuper_pcg_tasks.hpp"

// TAPA Cuper 顶层 kernel：这里只实现 Cuper 风格 SpMV。
//
// 这个 kernel 的职责是：
//   X + Matrix_data_0..15 -> Y_out
// host 传入 Batch_num / Matrix_len / Row_num / Column_num / Iteration_num，
// kernel 内部通过 TAPA task graph 完成向量加载、16 路矩阵读取、Core 乘加、
// Accumulator、检查和写回。
//
// 注意：这里不是 PCG control-kernel。
// 这个顶层没有 r/z/p/ap/m_inv/alpha/beta/status 等 PCG 状态参数，
// 也不做 Jacobi-PCG 的收敛判断。Project-XPlus 的 Cuper-PCG TAPA 版
// 是在 host 侧执行 PCG 主循环，每轮把当前 p/x 向量送进这个 Cuper
// kernel 做一次 SpMV。
void Cuper(tapa::mmap<INDEX_TYPE> SpElement_list_ptr,
           tapa::mmaps<ap_uint<512>, HBM_CHANNEL_NUM> Matrix_data,
           tapa::mmap<float_v16> X,
           tapa::mmap<float_v16> Y_out,
           const INDEX_TYPE Batch_num,
           const INDEX_TYPE Matrix_len,
           const INDEX_TYPE Row_num,
           const INDEX_TYPE Column_num,
           const INDEX_TYPE Iteration_num
          ) {

    // Cuper(...) 是一次性 SpMV task graph，不需要 command/stop stream：
    // 每个 task 按 Iteration_num 固定跑完后自然结束。
    //
    // 参数串接链：ptr loader 写 PE_Param[0]，Core0..Core15 逐级转发到
    // PE_Param[16]，最后由 Destroy_int 消费链尾。
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM + 1, 128>    PE_Param("PE_Param");
    // 向量串接链：Vector_Loader 写 Vector_X_Stream[0]，16 个 core 逐级转发同一份
    // packed X 到 Vector_X_Stream[16]，最后由 Destroy_float_v16 消费链尾。
    tapa::streams<float_v16, HBM_CHANNEL_NUM + 1, 1024>     Vector_X_Stream("Vector_X_Stream");
    // 16 路矩阵输入，每一路对应一个 HBM channel 和一个 Core。
    tapa::streams<ap_uint<512>, HBM_CHANNEL_NUM, 512>      Matrix_A_Stream("Matrix_A_Stream");
    // Core 写给 accumulator 的批次/行边界参数；每个 HBM channel 一路。
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM, 64>         Vector_Y_Param("Vector_Y_Param");
    // 16 个 Core 的局部乘积输出，进入 16 个 Accumulator。
    tapa::streams<Matrix_Mult_X, HBM_CHANNEL_NUM, 1024>     Matrix_Mult_Vector_Stream("Matrix_Mult_Vector_Stream");
    // 16 个 Accumulator 输出的 float_v2 部分结果。
    tapa::streams<float_v2, HBM_CHANNEL_NUM, 1024>          Vector_Y_Stream("Vector_Y_Stream");
    // Vector_Checker 将 16 路 accumulator 输出压到 8 路有效 float_v2 流。
    // FIFO_DEPTH 来自 Cuper.h，当前实际深度为 2。
    tapa::streams<float_v2, 8, FIFO_DEPTH>                  Vector_Y_Stream_Aftck("Vector_Y_Stream_aftck");
    // Mult_Sort_Tree 拼出的最终 float_v16 输出，Vector_Writer 写回 Y_out。
    // FIFO_DEPTH 来自 Cuper.h，当前实际深度为 2。
    tapa::stream<float_v16, FIFO_DEPTH>                    Vector_Y_Stream_Ans("Vector_Y_Stream_Ans");

    tapa::task()
        // 读取 Cuper 预处理后的 SpElement 边界表，把 Batch/Row/Iter/Column 和
        // 每个 batch 的 start/end 写入 PE_Param[0]。
        .invoke(SpElement_list_ptr_Loader, Batch_num, Row_num, Iteration_num, Column_num, SpElement_list_ptr, PE_Param[0])
        // 从 HBM 读取 packed FP32 输入向量 X，写入向量广播链首端。
        .invoke(Vector_Loader, Iteration_num, Column_num, X, Vector_X_Stream[0])
        // 启动 16 个矩阵 loader，每个 loader 读取一个 Matrix_data HBM channel。
        .invoke<tapa::join, HBM_CHANNEL_NUM>(Matrix_Loader, Iteration_num, Matrix_len, Matrix_data, Matrix_A_Stream)
        // Core0：消费 HBM[0] 的矩阵分片，同时转发参数和 X 向量到 Core1。
        .invoke(Core, PE_Param[0], Matrix_A_Stream[0], Vector_X_Stream[0], PE_Param[1], Vector_X_Stream[1], Vector_Y_Param[0], Matrix_Mult_Vector_Stream[0])
        // Core1：消费 HBM[1] 的矩阵分片，同时转发参数和 X 向量到 Core2。
        .invoke(Core, PE_Param[1], Matrix_A_Stream[1], Vector_X_Stream[1], PE_Param[2], Vector_X_Stream[2], Vector_Y_Param[1], Matrix_Mult_Vector_Stream[1])
        // Core2：消费 HBM[2] 的矩阵分片，同时转发参数和 X 向量到 Core3。
        .invoke(Core, PE_Param[2], Matrix_A_Stream[2], Vector_X_Stream[2], PE_Param[3], Vector_X_Stream[3], Vector_Y_Param[2], Matrix_Mult_Vector_Stream[2])
        // Core3：消费 HBM[3] 的矩阵分片，同时转发参数和 X 向量到 Core4。
        .invoke(Core, PE_Param[3], Matrix_A_Stream[3], Vector_X_Stream[3], PE_Param[4], Vector_X_Stream[4], Vector_Y_Param[3], Matrix_Mult_Vector_Stream[3])
        // Core4：消费 HBM[4] 的矩阵分片，同时转发参数和 X 向量到 Core5。
        .invoke(Core, PE_Param[4], Matrix_A_Stream[4], Vector_X_Stream[4], PE_Param[5], Vector_X_Stream[5], Vector_Y_Param[4], Matrix_Mult_Vector_Stream[4])
        // Core5：消费 HBM[5] 的矩阵分片，同时转发参数和 X 向量到 Core6。
        .invoke(Core, PE_Param[5], Matrix_A_Stream[5], Vector_X_Stream[5], PE_Param[6], Vector_X_Stream[6], Vector_Y_Param[5], Matrix_Mult_Vector_Stream[5])
        // Core6：消费 HBM[6] 的矩阵分片，同时转发参数和 X 向量到 Core7。
        .invoke(Core, PE_Param[6], Matrix_A_Stream[6], Vector_X_Stream[6], PE_Param[7], Vector_X_Stream[7], Vector_Y_Param[6], Matrix_Mult_Vector_Stream[6])
        // Core7：消费 HBM[7] 的矩阵分片，同时转发参数和 X 向量到 Core8。
        .invoke(Core, PE_Param[7], Matrix_A_Stream[7], Vector_X_Stream[7], PE_Param[8], Vector_X_Stream[8], Vector_Y_Param[7], Matrix_Mult_Vector_Stream[7])
        // Core8：消费 HBM[8] 的矩阵分片，同时转发参数和 X 向量到 Core9。
        .invoke(Core, PE_Param[8], Matrix_A_Stream[8], Vector_X_Stream[8], PE_Param[9], Vector_X_Stream[9], Vector_Y_Param[8], Matrix_Mult_Vector_Stream[8])
        // Core9：消费 HBM[9] 的矩阵分片，同时转发参数和 X 向量到 Core10。
        .invoke(Core, PE_Param[9], Matrix_A_Stream[9], Vector_X_Stream[9], PE_Param[10], Vector_X_Stream[10], Vector_Y_Param[9], Matrix_Mult_Vector_Stream[9])
        // Core10：消费 HBM[10] 的矩阵分片，同时转发参数和 X 向量到 Core11。
        .invoke(Core, PE_Param[10], Matrix_A_Stream[10], Vector_X_Stream[10], PE_Param[11], Vector_X_Stream[11], Vector_Y_Param[10], Matrix_Mult_Vector_Stream[10])
        // Core11：消费 HBM[11] 的矩阵分片，同时转发参数和 X 向量到 Core12。
        .invoke(Core, PE_Param[11], Matrix_A_Stream[11], Vector_X_Stream[11], PE_Param[12], Vector_X_Stream[12], Vector_Y_Param[11], Matrix_Mult_Vector_Stream[11])
        // Core12：消费 HBM[12] 的矩阵分片，同时转发参数和 X 向量到 Core13。
        .invoke(Core, PE_Param[12], Matrix_A_Stream[12], Vector_X_Stream[12], PE_Param[13], Vector_X_Stream[13], Vector_Y_Param[12], Matrix_Mult_Vector_Stream[12])
        // Core13：消费 HBM[13] 的矩阵分片，同时转发参数和 X 向量到 Core14。
        .invoke(Core, PE_Param[13], Matrix_A_Stream[13], Vector_X_Stream[13], PE_Param[14], Vector_X_Stream[14], Vector_Y_Param[13], Matrix_Mult_Vector_Stream[13])
        // Core14：消费 HBM[14] 的矩阵分片，同时转发参数和 X 向量到 Core15。
        .invoke(Core, PE_Param[14], Matrix_A_Stream[14], Vector_X_Stream[14], PE_Param[15], Vector_X_Stream[15], Vector_Y_Param[14], Matrix_Mult_Vector_Stream[14])
        // Core15：消费 HBM[15] 的矩阵分片，并把参数/X 链尾交给 Destroy_*。
        .invoke(Core, PE_Param[15], Matrix_A_Stream[15], Vector_X_Stream[15], PE_Param[16], Vector_X_Stream[16], Vector_Y_Param[15], Matrix_Mult_Vector_Stream[15])
        // 消费 PE_Param[16] 链尾，防止最后一级 core 写满参数 FIFO。
        .invoke<tapa::detach>(Destroy_int, PE_Param[HBM_CHANNEL_NUM])
        // 消费 Vector_X_Stream[16] 链尾，防止最后一级 core 写满向量 FIFO。
        .invoke<tapa::detach>(Destroy_float_v16, Vector_X_Stream[HBM_CHANNEL_NUM])
        // 16 路 accumulator 累加各 core 的局部乘积，输出 float_v2 部分结果。
        .invoke<tapa::join, HBM_CHANNEL_NUM>(Accumulator, Vector_Y_Param, Matrix_Mult_Vector_Stream, Vector_Y_Stream)
        // 8 路 checker 过滤 Cuper 对齐产生的 padding，只保留 Row_num 有效输出。
        .invoke<tapa::join, 8>(Vector_Checker, Iteration_num, Row_num, Vector_Y_Stream, Vector_Y_Stream_Aftck)
        // sort tree 把 8 路 float_v2 重新拼成单路 float_v16 输出。
        .invoke<tapa::detach>(Mult_Sort_Tree, Vector_Y_Stream_Aftck, Vector_Y_Stream_Ans)
        // 最终 writer 把 float_v16 结果写回 Y_out HBM。
        .invoke(Vector_Writer, Iteration_num, Row_num, Vector_Y_Stream_Ans, Y_out)
    ;
}

// Cuper 兼容 single SpMV demo 顶层。
//
// 外部 ABI 保持历史上的 CuperPcgSpmv kernel 名和端口，方便复用 demo 构建、
// connectivity 和 host 入口；内部不再使用 PCG service controller / command /
// stop / writer-done 这套控制壳，而是和满血 Cuper(...) 一样采用一次性 SpMV
// task graph。这样 single SpMV 只负责测纯 Cuper 风格数据通路；PCG 相关控制
// 优化只在 full CuperPcg(...) 路径里处理。
void CuperPcgSpmv(tapa::mmap<INDEX_TYPE> SpElement_list_ptr,
                  tapa::mmaps<ap_uint<512>, HBM_CHANNEL_NUM> Matrix_data,
                  tapa::mmap<float_v16> X,
                  tapa::mmap<float_v16> Y_out,
                  const INDEX_TYPE Batch_num,
                  const INDEX_TYPE Matrix_len,
                  const INDEX_TYPE Row_num,
                  const INDEX_TYPE Column_num,
                  const INDEX_TYPE Iteration_num
                 ) {
    // 这些 stream 深度和 Cuper(...) 保持一致；这里刻意不复用
    // pcg_spmv_service.hpp 中的 Pcg_* 常驻服务任务。
    //
    // 参数串接链：SpElement_list_ptr_Loader 写 PE_Param[0]，Core0..Core15
    // 逐级转发到 PE_Param[16]，链尾由 Destroy_int 消费。
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM + 1, 128>    PE_Param("PE_Param");
    // 向量串接链：Vector_Loader 写 packed X，16 个 Core 逐级转发同一份
    // float_v16 到链尾；链尾由 Destroy_float_v16 消费。
    tapa::streams<float_v16, HBM_CHANNEL_NUM + 1, 1024>    Vector_X_Stream("Vector_X_Stream");
    // 16 路矩阵输入，每一路对应一个 HBM channel。
    tapa::streams<ap_uint<512>, HBM_CHANNEL_NUM, 512>      Matrix_A_Stream("Matrix_A_Stream");
    // Core 写给 accumulator 的批次/行边界参数。
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM, 64>         Vector_Y_Param("Vector_Y_Param");
    // Core 的局部乘积输出，仍按 16 路 HBM/PE 并行。
    tapa::streams<Matrix_Mult_X, HBM_CHANNEL_NUM, 1024>    Matrix_Mult_Vector_Stream("Matrix_Mult_Vector_Stream");
    // Accumulator 输出的 float_v2 部分和。
    tapa::streams<float_v2, HBM_CHANNEL_NUM, 1024>         Vector_Y_Stream("Vector_Y_Stream");
    // Checker 过滤 padding 后输出 8 路 float_v2；FIFO_DEPTH 当前为 2。
    tapa::streams<float_v2, 8, FIFO_DEPTH>                 Vector_Y_Stream_Aftck("Vector_Y_Stream_aftck");
    // Sort tree 拼出的最终 float_v16 结果，随后写回 Y_out。
    tapa::stream<float_v16, FIFO_DEPTH>                    Vector_Y_Stream_Ans("Vector_Y_Stream_Ans");

    tapa::task()
        // 一次性 ptr loader：按 Iteration_num 重复读取 Cuper 边界表，
        // 生成 Core 链需要的 Batch/Row/Column 和 batch start/end。
        .invoke(SpElement_list_ptr_Loader, Batch_num, Row_num, Iteration_num, Column_num, SpElement_list_ptr, PE_Param[0])
        // 一次性 vector loader：从 X HBM 读取 packed FP32 输入向量。
        .invoke(Vector_Loader, Iteration_num, Column_num, X, Vector_X_Stream[0])
        // 16 个一次性 matrix loader：并行读取 Matrix_data[0..15]。
        .invoke<tapa::join, HBM_CHANNEL_NUM>(Matrix_Loader, Iteration_num, Matrix_len, Matrix_data, Matrix_A_Stream)
        // Core0：消费 HBM[0] 的矩阵分片，同时转发参数和 X 向量到 Core1。
        .invoke(Core, PE_Param[0], Matrix_A_Stream[0], Vector_X_Stream[0], PE_Param[1], Vector_X_Stream[1], Vector_Y_Param[0], Matrix_Mult_Vector_Stream[0])
        // Core1：消费 HBM[1] 的矩阵分片，同时转发参数和 X 向量到 Core2。
        .invoke(Core, PE_Param[1], Matrix_A_Stream[1], Vector_X_Stream[1], PE_Param[2], Vector_X_Stream[2], Vector_Y_Param[1], Matrix_Mult_Vector_Stream[1])
        // Core2：消费 HBM[2] 的矩阵分片，同时转发参数和 X 向量到 Core3。
        .invoke(Core, PE_Param[2], Matrix_A_Stream[2], Vector_X_Stream[2], PE_Param[3], Vector_X_Stream[3], Vector_Y_Param[2], Matrix_Mult_Vector_Stream[2])
        // Core3：消费 HBM[3] 的矩阵分片，同时转发参数和 X 向量到 Core4。
        .invoke(Core, PE_Param[3], Matrix_A_Stream[3], Vector_X_Stream[3], PE_Param[4], Vector_X_Stream[4], Vector_Y_Param[3], Matrix_Mult_Vector_Stream[3])
        // Core4：消费 HBM[4] 的矩阵分片，同时转发参数和 X 向量到 Core5。
        .invoke(Core, PE_Param[4], Matrix_A_Stream[4], Vector_X_Stream[4], PE_Param[5], Vector_X_Stream[5], Vector_Y_Param[4], Matrix_Mult_Vector_Stream[4])
        // Core5：消费 HBM[5] 的矩阵分片，同时转发参数和 X 向量到 Core6。
        .invoke(Core, PE_Param[5], Matrix_A_Stream[5], Vector_X_Stream[5], PE_Param[6], Vector_X_Stream[6], Vector_Y_Param[5], Matrix_Mult_Vector_Stream[5])
        // Core6：消费 HBM[6] 的矩阵分片，同时转发参数和 X 向量到 Core7。
        .invoke(Core, PE_Param[6], Matrix_A_Stream[6], Vector_X_Stream[6], PE_Param[7], Vector_X_Stream[7], Vector_Y_Param[6], Matrix_Mult_Vector_Stream[6])
        // Core7：消费 HBM[7] 的矩阵分片，同时转发参数和 X 向量到 Core8。
        .invoke(Core, PE_Param[7], Matrix_A_Stream[7], Vector_X_Stream[7], PE_Param[8], Vector_X_Stream[8], Vector_Y_Param[7], Matrix_Mult_Vector_Stream[7])
        // Core8：消费 HBM[8] 的矩阵分片，同时转发参数和 X 向量到 Core9。
        .invoke(Core, PE_Param[8], Matrix_A_Stream[8], Vector_X_Stream[8], PE_Param[9], Vector_X_Stream[9], Vector_Y_Param[8], Matrix_Mult_Vector_Stream[8])
        // Core9：消费 HBM[9] 的矩阵分片，同时转发参数和 X 向量到 Core10。
        .invoke(Core, PE_Param[9], Matrix_A_Stream[9], Vector_X_Stream[9], PE_Param[10], Vector_X_Stream[10], Vector_Y_Param[9], Matrix_Mult_Vector_Stream[9])
        // Core10：消费 HBM[10] 的矩阵分片，同时转发参数和 X 向量到 Core11。
        .invoke(Core, PE_Param[10], Matrix_A_Stream[10], Vector_X_Stream[10], PE_Param[11], Vector_X_Stream[11], Vector_Y_Param[10], Matrix_Mult_Vector_Stream[10])
        // Core11：消费 HBM[11] 的矩阵分片，同时转发参数和 X 向量到 Core12。
        .invoke(Core, PE_Param[11], Matrix_A_Stream[11], Vector_X_Stream[11], PE_Param[12], Vector_X_Stream[12], Vector_Y_Param[11], Matrix_Mult_Vector_Stream[11])
        // Core12：消费 HBM[12] 的矩阵分片，同时转发参数和 X 向量到 Core13。
        .invoke(Core, PE_Param[12], Matrix_A_Stream[12], Vector_X_Stream[12], PE_Param[13], Vector_X_Stream[13], Vector_Y_Param[12], Matrix_Mult_Vector_Stream[12])
        // Core13：消费 HBM[13] 的矩阵分片，同时转发参数和 X 向量到 Core14。
        .invoke(Core, PE_Param[13], Matrix_A_Stream[13], Vector_X_Stream[13], PE_Param[14], Vector_X_Stream[14], Vector_Y_Param[13], Matrix_Mult_Vector_Stream[13])
        // Core14：消费 HBM[14] 的矩阵分片，同时转发参数和 X 向量到 Core15。
        .invoke(Core, PE_Param[14], Matrix_A_Stream[14], Vector_X_Stream[14], PE_Param[15], Vector_X_Stream[15], Vector_Y_Param[14], Matrix_Mult_Vector_Stream[14])
        // Core15：消费 HBM[15] 的矩阵分片，并把参数/X 链尾交给 Destroy_*。
        .invoke(Core, PE_Param[15], Matrix_A_Stream[15], Vector_X_Stream[15], PE_Param[16], Vector_X_Stream[16], Vector_Y_Param[15], Matrix_Mult_Vector_Stream[15])
        // 消费 PE_Param[16] 链尾，防止最后一级 core 写满参数 FIFO。
        .invoke<tapa::detach>(Destroy_int, PE_Param[HBM_CHANNEL_NUM])
        // 消费 Vector_X_Stream[16] 链尾，防止最后一级 core 写满向量 FIFO。
        .invoke<tapa::detach>(Destroy_float_v16, Vector_X_Stream[HBM_CHANNEL_NUM])
        // 16 路 accumulator 累加各 core 的局部乘积，输出 float_v2 部分结果。
        .invoke<tapa::join, HBM_CHANNEL_NUM>(Accumulator, Vector_Y_Param, Matrix_Mult_Vector_Stream, Vector_Y_Stream)
        // 8 路 checker 过滤 Cuper 对齐产生的 padding，只保留 Row_num 有效输出。
        .invoke<tapa::join, 8>(Vector_Checker, Iteration_num, Row_num, Vector_Y_Stream, Vector_Y_Stream_Aftck)
        // sort tree 把 8 路 float_v2 重新拼成单路 float_v16 输出。
        .invoke<tapa::detach>(Mult_Sort_Tree, Vector_Y_Stream_Aftck, Vector_Y_Stream_Ans)
        // 最终 writer 把 float_v16 结果写回 Y_out HBM。
        .invoke(Vector_Writer, Iteration_num, Row_num, Vector_Y_Stream_Ans, Y_out)
    ;
}

// TAPA 端口类型速记：
//   - tapa::mmap<T> 是一个连续全局内存端口，host 侧通常绑定一个 xrt::bo；
//     kernel 内用 array[index] 形式访问 T 类型元素。
//   - tapa::mmaps<T, N> 是 N 个同类型 mmap 端口的数组；这里用于把 16 路
//     Matrix_data 分散到 16 个 HBM bank，支撑 Cuper 的 16 路并行 SpMV。
void CuperPcg(tapa::mmap<INDEX_TYPE> SpElement_list_ptr,                // Cuper 预处理后的稀疏元素/批次索引表，驱动 16 路 SpMV 调度
              tapa::mmaps<ap_uint<512>, HBM_CHANNEL_NUM> Matrix_data,   // 16 个 HBM 通道上的 512-bit packed 矩阵数据
              tapa::mmap<double_v8> B,                                  // PCG 右端项 b，FP64 主状态，512-bit packed
              tapa::mmap<double_v8> M_inv,                              // Jacobi 预条件器对角逆 M^{-1}，512-bit packed
              tapa::mmap<double_v8> X,                                  // 解向量 x，输入初值 x0，kernel 内更新并写回最终解
              tapa::mmap<double_v8> R,                                  // 残差向量 r = b - A*x
              tapa::mmap<double_v8> Z,                                  // 预条件残差 z = M^{-1}*r
              tapa::mmap<double_v8> P,                                  // PCG 搜索方向 p，FP64 权威状态
              // AP_spmv/X_spmv/P_spmv 是 full-PCG 版为了贴近 standalone
              // Cuper SpMV 新增的 packed float_v16 缓冲：
              //   X_spmv: host 预打包 x0，初始化 A*x0 时读取
              //   P_spmv: controller 维护 p 的 packed 副本，每轮 A*p 时读取
              //   AP_spmv: controller 缓存 A*p 的 packed 输出，供 dot/update 复用
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
             ) {

    // CuperPcg 顶层数据流：
    //
    //   Pcg_Controller
    //       -> 发送 SpMV 命令到 ptr/vector/matrix loader
    //       <- 从 Pcg_Spmv_Stream 接收 A*x0 或 A*p
    //
    //   Pcg_Vector_Loader
    //       -> 从 X_spmv/P_spmv packed HBM 读 float_v16 向量输入
    //
    //   Pcg_* loader/Core/Accumulator/Checker/Mult_Sort_Tree
    //       -> 基本沿用原始 Cuper 的 16 HBM SpMV 流水
    //
    // 下面这些 stream 数组可以按硬件连线图理解：
    //
    //   1. 参数/向量广播链，长度是 HBM_CHANNEL_NUM + 1：
    //
    //        PE_Param[0]        -> Core0 -> PE_Param[1]
    //        PE_Param[1]        -> Core1 -> PE_Param[2]
    //        ...
    //        PE_Param[15]       -> Core15 -> PE_Param[16]
    //
    //        Vector_X_Stream[0] -> Core0 -> Vector_X_Stream[1]
    //        Vector_X_Stream[1] -> Core1 -> Vector_X_Stream[2]
    //        ...
    //        Vector_X_Stream[15]-> Core15 -> Vector_X_Stream[16]
    //
    //      x0/p 向量不是复制出 16 个独立输入端口，而是通过 16 个 core
    //      串接转发。每个 core 在转发同一份向量的同时，读取自己那一路
    //      HBM 矩阵并计算局部 val * x[col]。
    //
    //   2. 矩阵/局部乘积并行数组，长度是 HBM_CHANNEL_NUM：
    //
    //        Matrix_data_0  -> Matrix_A_Stream[0]  -> Core0  -> Matrix_Mult_Vector_Stream[0]
    //        Matrix_data_1  -> Matrix_A_Stream[1]  -> Core1  -> Matrix_Mult_Vector_Stream[1]
    //        ...
    //        Matrix_data_15 -> Matrix_A_Stream[15] -> Core15 -> Matrix_Mult_Vector_Stream[15]
    //
    //      这部分是真正的 16 路 HBM/SpMV 并行度。
    //
    //   3. SpMV 输出收敛链：
    //
    //        Matrix_Mult_Vector_Stream[0..15]
    //             -> Pcg_Accumulator[0..15]
    //             -> Vector_Y_Stream[0..15]
    //             -> Pcg_Vector_Checker[0..7]
    //             -> Vector_Y_Stream_Aftck[0..7]
    //             -> Mult_Sort_Tree
    //             -> Pcg_Spmv_Stream
    //             -> Pcg_Controller
    //
    //      controller 最终看到的是一包包 float_v16 的 A*x0 或 A*p。
    //
    // 这样 host 只 launch 一次 CuperPcg；PCG 每轮迭代都在这个 TAPA
    // task graph 内部完成，不再走 host 侧循环调用 Cuper。
    //
    // tapa::stream<T, DEPTH> 表示一条 FIFO；tapa::streams<T, N, DEPTH>
    // 表示 N 条同类型 FIFO。T 是每个元素的数据类型，DEPTH 是每条 FIFO
    // 的深度。下面这些 FIFO 就是各个 task 之间的硬件连线。
    //
    // 2 条命令流：controller 分别通知 ptr loader 和 vector loader
    // 启动一次 SpMV，结束时再发 stop 让服务任务退出。
    //
    // 这套 Pcg_* SpMV service 同时服务 init_spmv(A*x0) 和 iter_spmv(A*p)：
    // controller 通过 CuperSpmvCommand::vector_source 指定本轮读 X_spmv
    // 还是 P_spmv。下面的 loader/core/accumulator/checker/sort-tree
    // 都是 init 与 PCG 迭代共用的数据通路。
    tapa::streams<CuperSpmvCommand, 2, 4>                   Command_Stream("Command_Stream");
    // 16 条矩阵命令流：controller 给每个 HBM matrix loader 发同一轮
    // SpMV 命令。HBM_CHANNEL_NUM 当前是 16。
    tapa::streams<CuperSpmvCommand, HBM_CHANNEL_NUM, 4>     Matrix_Command_Stream("Matrix_Command_Stream");
    // 参数广播链：PE_Param[0] 由 ptr loader 写入，随后 Core0..Core15
    // 逐级转发到 PE_Param[16]。链尾由 Destroy_int 消费。
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM + 1, 128>     PE_Param("PE_Param");
    // 向量广播链：Vector_X_Stream[0] 由 vector loader 写入，随后
    // Core0..Core15 逐级转发到 Vector_X_Stream[16]。链尾由
    // Destroy_float_v16 消费。
    tapa::streams<float_v16, HBM_CHANNEL_NUM + 1, 256>      Vector_X_Stream("Vector_X_Stream");
    // 16 路矩阵数据流：每一路对应一个 HBM channel。ap_uint<512>
    // 是一个 512-bit HBM beat，内部打包 8 个 64-bit SpElement。
    tapa::streams<ap_uint<512>, HBM_CHANNEL_NUM, 64>        Matrix_A_Stream("Matrix_A_Stream");
    // 16 路 accumulator 参数流：每个 core 给对应 accumulator 传 Row_num
    // 以及每个 batch 的矩阵边界。
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM, 64>          Vector_Y_Param("Vector_Y_Param");
    // 16 路局部乘积流：每个 core 输出自己 HBM 分片产生的 val * x[col]
    // 及对应的 Cuper 内部 row 编码。
    tapa::streams<Matrix_Mult_X, HBM_CHANNEL_NUM, 256>      Matrix_Mult_Vector_Stream("Matrix_Mult_Vector_Stream");
    // 16 路 accumulator 输出流：每路输出 float_v2，也就是 ping/pong
    // 合并后的两行 y 值。
    tapa::streams<float_v2, HBM_CHANNEL_NUM, 256>           Vector_Y_Stream("Vector_Y_Stream");
    // checker 后的 8 路输出流：过滤 padding 后交给 Mult_Sort_Tree，
    // 最终重新拼成 float_v16 送回 Pcg_Controller。
    // FIFO_DEPTH 来自 Cuper.h，当前实际深度为 2；和 Cuper/CuperPcgSpmv 相同。
    tapa::streams<float_v2, 8, FIFO_DEPTH>                  Vector_Y_Stream_Aftck("Vector_Y_Stream_aftck");
    // 直接把 Cuper 的 float_v16 SpMV 结果接回 controller。
    // 之前额外的 packetizer task 只包装一个未使用的 last 位；板上调试时
    // 该中间层会增加流控不确定性，所以这里保留 128 深度 FIFO 后直接消费。
    // 这里也不再把 y 写回 HBM；CuperPcg 内部直接拿 A*x0/A*p 更新 PCG 状态。
    tapa::stream<float_v16, 128>                            Pcg_Spmv_Stream("Pcg_Spmv_Stream");
    // checker/sort/vector-destroy 都是常驻服务，需要单独 stop 流退出。
    // 这些 stream 深度很小，只承载停止令牌，不承载矩阵/向量数据。
    tapa::streams<INDEX_TYPE, 8, 2>                          Checker_Stop_Stream("Checker_Stop_Stream");
    tapa::stream<INDEX_TYPE, 2>                              Sort_Stop_Stream("Sort_Stop_Stream");
    tapa::stream<INDEX_TYPE, 2>                              Vector_Destroy_Stop_Stream("Vector_Destroy_Stop_Stream");
    // Stage_Event_Stream 是 controller -> timer 的事件流；Stage_Ticks_Stream
    // 是 timer -> controller 的最终 cycle 数组。
    tapa::stream<PcgStageEvent, 16>                          Stage_Event_Stream("Stage_Event_Stream");
    tapa::stream<ap_uint<64>, 16>                             Stage_Ticks_Stream("Stage_Ticks_Stream");

    tapa::task()
        // Controller 完成后广播 stop；所有 Pcg_* 服务任务收到 stop 后
        // 有限退出，避免 host 侧等待 AP_CTRL_HS completion 时卡住。
        .invoke(Pcg_Controller,
                Command_Stream,
                Matrix_Command_Stream,
                Checker_Stop_Stream,
                Sort_Stop_Stream,
                Vector_Destroy_Stop_Stream,
                Stage_Event_Stream,
                Stage_Ticks_Stream,
                Pcg_Spmv_Stream,
                B,
                M_inv,
                X,
                R,
                Z,
                P,
                AP_spmv,
                P_spmv,
                Metrics,
                Status,
                Row_num,
                Max_iters,
                Tau)
        .invoke(Pcg_Stage_Timer, Stage_Event_Stream, Stage_Ticks_Stream)
        // [init+PCG 共用 SpMV service] Cuper SpMV 的参数/向量/矩阵输入端。
        // Command_Stream[0] 给 ptr loader，Command_Stream[1] 给 vector loader；
        // Matrix_Command_Stream 分发到 16 个矩阵 HBM channel。
        .invoke(Pcg_SpElement_list_ptr_Loader,
                Batch_num,
                Row_num,
                Column_num,
                SpElement_list_ptr,
                Command_Stream[0],
                PE_Param[0])
        .invoke(Pcg_Vector_Loader,
                Column_num,
                X_spmv,
                P_spmv,
                Command_Stream[1],
                Vector_X_Stream[0])
        .invoke<tapa::join, HBM_CHANNEL_NUM>(Pcg_Matrix_Loader, Matrix_len, Matrix_data, Matrix_Command_Stream, Matrix_A_Stream)
        // [init+PCG 共用 SpMV service] 16 级 Cuper Core 链。PE_Param 和
        // Vector_X_Stream 在各级之间传递，每级消费一个 Matrix_data[channel]，
        // 输出该 channel 对 y 的部分贡献。
        //
        // 对第 i 级 core，可以按下面的通用形式读：
        //
        //   Pcg_Core(
        //       PE_Param[i],                  // 参数输入，来自上一级或 ptr loader
        //       Matrix_A_Stream[i],           // 第 i 个 HBM channel 的矩阵流
        //       Vector_X_Stream[i],           // 向量输入，来自上一级或 vector loader
        //       PE_Param[i + 1],              // 参数转发给下一级
        //       Vector_X_Stream[i + 1],       // 向量转发给下一级
        //       Vector_Y_Param[i],            // 给 accumulator 的输出行数/边界参数
        //       Matrix_Mult_Vector_Stream[i]) // 第 i 路矩阵分片产生的局部乘积
        //
        // [0..15] 表示 16 路 HBM 矩阵通道和 16 个 SpMV core；[16] 只表示
        // 串接链尾，不再对应新的矩阵通道。
        .invoke(Pcg_Core, PE_Param[0], Matrix_A_Stream[0], Vector_X_Stream[0], PE_Param[1], Vector_X_Stream[1], Vector_Y_Param[0], Matrix_Mult_Vector_Stream[0])
        .invoke(Pcg_Core, PE_Param[1], Matrix_A_Stream[1], Vector_X_Stream[1], PE_Param[2], Vector_X_Stream[2], Vector_Y_Param[1], Matrix_Mult_Vector_Stream[1])
        .invoke(Pcg_Core, PE_Param[2], Matrix_A_Stream[2], Vector_X_Stream[2], PE_Param[3], Vector_X_Stream[3], Vector_Y_Param[2], Matrix_Mult_Vector_Stream[2])
        .invoke(Pcg_Core, PE_Param[3], Matrix_A_Stream[3], Vector_X_Stream[3], PE_Param[4], Vector_X_Stream[4], Vector_Y_Param[3], Matrix_Mult_Vector_Stream[3])
        .invoke(Pcg_Core, PE_Param[4], Matrix_A_Stream[4], Vector_X_Stream[4], PE_Param[5], Vector_X_Stream[5], Vector_Y_Param[4], Matrix_Mult_Vector_Stream[4])
        .invoke(Pcg_Core, PE_Param[5], Matrix_A_Stream[5], Vector_X_Stream[5], PE_Param[6], Vector_X_Stream[6], Vector_Y_Param[5], Matrix_Mult_Vector_Stream[5])
        .invoke(Pcg_Core, PE_Param[6], Matrix_A_Stream[6], Vector_X_Stream[6], PE_Param[7], Vector_X_Stream[7], Vector_Y_Param[6], Matrix_Mult_Vector_Stream[6])
        .invoke(Pcg_Core, PE_Param[7], Matrix_A_Stream[7], Vector_X_Stream[7], PE_Param[8], Vector_X_Stream[8], Vector_Y_Param[7], Matrix_Mult_Vector_Stream[7])
        .invoke(Pcg_Core, PE_Param[8], Matrix_A_Stream[8], Vector_X_Stream[8], PE_Param[9], Vector_X_Stream[9], Vector_Y_Param[8], Matrix_Mult_Vector_Stream[8])
        .invoke(Pcg_Core, PE_Param[9], Matrix_A_Stream[9], Vector_X_Stream[9], PE_Param[10], Vector_X_Stream[10], Vector_Y_Param[9], Matrix_Mult_Vector_Stream[9])
        .invoke(Pcg_Core, PE_Param[10], Matrix_A_Stream[10], Vector_X_Stream[10], PE_Param[11], Vector_X_Stream[11], Vector_Y_Param[10], Matrix_Mult_Vector_Stream[10])
        .invoke(Pcg_Core, PE_Param[11], Matrix_A_Stream[11], Vector_X_Stream[11], PE_Param[12], Vector_X_Stream[12], Vector_Y_Param[11], Matrix_Mult_Vector_Stream[11])
        .invoke(Pcg_Core, PE_Param[12], Matrix_A_Stream[12], Vector_X_Stream[12], PE_Param[13], Vector_X_Stream[13], Vector_Y_Param[12], Matrix_Mult_Vector_Stream[12])
        .invoke(Pcg_Core, PE_Param[13], Matrix_A_Stream[13], Vector_X_Stream[13], PE_Param[14], Vector_X_Stream[14], Vector_Y_Param[13], Matrix_Mult_Vector_Stream[13])
        .invoke(Pcg_Core, PE_Param[14], Matrix_A_Stream[14], Vector_X_Stream[14], PE_Param[15], Vector_X_Stream[15], Vector_Y_Param[14], Matrix_Mult_Vector_Stream[14])
        .invoke(Pcg_Core, PE_Param[15], Matrix_A_Stream[15], Vector_X_Stream[15], PE_Param[16], Vector_X_Stream[16], Vector_Y_Param[15], Matrix_Mult_Vector_Stream[15])
        // 链尾 PE_Param[16] / Vector_X_Stream[16] 已经没有第 16 个 core 消费。
        // Destroy_* 常驻读取尾流，防止最后一级 core 写满 FIFO 后反压整条链。
        .invoke(Pcg_Destroy_int, PE_Param[HBM_CHANNEL_NUM])
        .invoke(Pcg_Destroy_float_v16, Vector_X_Stream[HBM_CHANNEL_NUM], Vector_Destroy_Stop_Stream)
        // [init+PCG 共用 SpMV service] Cuper 输出端：累加各 PE 部分和，
        // 过滤 padding，排序/拼包后直接回到 PCG controller。controller
        // 再根据当前阶段把它解释成 A*x0 或 A*p；这里不写 Y_out HBM。
        .invoke<tapa::join, HBM_CHANNEL_NUM>(Pcg_Accumulator, Vector_Y_Param, Matrix_Mult_Vector_Stream, Vector_Y_Stream)
        .invoke<tapa::join, 8>(Pcg_Vector_Checker, Row_num, Vector_Y_Stream, Vector_Y_Stream_Aftck, Checker_Stop_Stream)
        .invoke(Pcg_Mult_Sort_Tree, Vector_Y_Stream_Aftck, Pcg_Spmv_Stream, Sort_Stop_Stream)
    ;
}
