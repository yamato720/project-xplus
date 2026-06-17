#pragma once

// 可复用的 service-mode Cuper SpMV task。
// 这些 task 把原 one-shot Cuper SpMV 包成“收到 command 跑一轮，收到 stop 退出”的常驻服务。

#include <algorithm>

#include <ap_int.h>
#include <tapa.h>

#include "cuper_spmv_tasks.hpp"
#include "spmv_service_common.hpp"
#ifdef JACOBI_TRACE_ENABLED
#include "jacobi_deadlock_debug.hpp"
#endif

// 本文件把原始一次性 Cuper SpMV task 改造成可反复调用的常驻服务。
// 它不包含 PCG 语义；Jacobi controller 只把它当作可重复触发的 SpMV 计算服务。

// 读取 SpElement_list_ptr 边界表，并把 Batch/Row/Column 参数送入 16 级 Core 链。
// 每收到一条非 stop command，就完整发起一轮 SpMV 的边界表读取。
void SpmvService_SpElementPtrLoader(const INDEX_TYPE Batch_num,
                                    const INDEX_TYPE Row_num,
                                    const INDEX_TYPE Column_num,
                                    tapa::async_mmap<INDEX_TYPE> &SpElement_list_ptr,
                                    tapa::istream<CuperSpmvServiceCommand> &Command_in,
                                    tapa::ostream<INDEX_TYPE> &PE_Param
#ifdef JACOBI_TRACE_ENABLED
                                    ,
                                    tapa::ostream<JacobiDebugEvent> &Debug_Event_out
#endif
                                    ) {
    for (;;) {
#pragma HLS loop_flatten off
        const CuperSpmvServiceCommand command = Command_in.read();
        if (command.stop != 0) {
#ifdef JACOBI_TRACE_ENABLED
            Jacobi_DebugTryWrite(Debug_Event_out,
                                 kJacobiDebugSourcePtrLoader,
                                 kJacobiDebugPhaseStop,
                                 0,
                                 Batch_num);
#endif
            PE_Param.write(kSpmvServiceStopToken);
            return;
        }
#ifdef JACOBI_TRACE_ENABLED
        Jacobi_DebugTryWrite(Debug_Event_out,
                             kJacobiDebugSourcePtrLoader,
                             kJacobiDebugPhaseRecv,
                             Batch_num,
                             Row_num);
#endif
        PE_Param.write(Batch_num);
        PE_Param.write(Row_num);
        PE_Param.write(Column_num);
#ifdef JACOBI_TRACE_ENABLED
        Jacobi_DebugTryWrite(Debug_Event_out,
                             kJacobiDebugSourcePtrLoader,
                             kJacobiDebugPhaseSend,
                             0,
                             Column_num);
#endif

        // SpElement_list_ptr 长度是 Batch_num + 1，Core 用相邻两个边界确定
        // 每个 column batch 需要消费的 Matrix_data beat 范围。
        const INDEX_TYPE batch_num_plus_1 = Batch_num + 1;
        Cuper_ReadSpElementPtrPackets(batch_num_plus_1,
                                      SpElement_list_ptr,
                                      PE_Param);
#ifdef JACOBI_TRACE_ENABLED
        Jacobi_DebugTryWrite(Debug_Event_out,
                             kJacobiDebugSourcePtrLoader,
                             kJacobiDebugPhaseDoneRound,
                             0,
                             batch_num_plus_1);
#endif
    }
}

// 单路 HBM 矩阵预取 loader。顶层会 join 出 16 个实例，各自读取自己的 Matrix_data[channel]。
// Matrix_len 是每路都要读取的 512-bit beat 数，由 host 预处理后的最大边界给出。
// 它只把矩阵 beat 灌入 Matrix_A_Stream FIFO；Core 是否启动由 PE_Param/X command 决定。
void SpmvService_MatrixLoader(const INDEX_TYPE Matrix_len,
                              tapa::async_mmap<ap_uint<512>> &Matrix_data,
                              tapa::istream<CuperSpmvServiceCommand> &Command_in,
                              tapa::ostream<ap_uint<512>> &Matrix_A_Stream
                              ,
                              const INDEX_TYPE Debug_channel
#ifdef JACOBI_TRACE_FULL
                              ,
                              tapa::ostream<JacobiDebugEvent> &Debug_Event_out
#endif
                              ) {
#ifdef JACOBI_TRACE_FULL
    const INDEX_TYPE Debug_source = kJacobiDebugSourceMatrixLoaderBase + Debug_channel;
#endif
    for (;;) {
#pragma HLS loop_flatten off
        const CuperSpmvServiceCommand command = Command_in.read();
        if (command.stop != 0) {
#ifdef JACOBI_TRACE_FULL
            Jacobi_DebugTryWrite(Debug_Event_out,
                                 Debug_source,
                                 kJacobiDebugPhaseStop,
                                 0,
                                 Matrix_len);
#endif
            return;
        }

#ifdef JACOBI_TRACE_FULL
        Jacobi_DebugTryWrite(Debug_Event_out,
                             Debug_source,
                             kJacobiDebugPhaseRecv,
                             0,
                             Matrix_len);
#endif
        // Matrix_data 的布局仍沿用 Cuper：一个 512-bit beat 内含 8 个 64-bit SpElement slot。
        Cuper_ReadMatrixPackets(Matrix_len,
                                Matrix_data,
                                Matrix_A_Stream);
#ifdef JACOBI_TRACE_FULL
        Jacobi_DebugTryWrite(Debug_Event_out,
                             Debug_source,
                             kJacobiDebugPhaseDoneRound,
                             0,
                             Matrix_len);
#endif
    }
}

// 单级 Cuper Core service 壳。
// 它读取并转发本轮全局参数，然后调用原 Cuper_Core_Compute_Round 处理所有 batch。
void SpmvService_Core(tapa::istream<INDEX_TYPE>    &PE_Param_in,
                      tapa::istream<ap_uint<512> >  &Matrix_A_Stream,
                      tapa::istream<float_v16>     &Vector_X_Stream_in,
                      tapa::ostream<INDEX_TYPE>    &PE_Param_out,
                      tapa::ostream<float_v16>     &Vector_X_Stream_out,
                      tapa::ostream<INDEX_TYPE>    &Vector_Y_Param,
                      tapa::ostream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream) {
    for (;;) {
#pragma HLS loop_flatten off
        // PE_Param_in 的第一项是 Batch_num 或停止令牌。停止令牌沿 core 链
        // 继续传到 PE_Param_out，让链尾 drain 也能退出。
        const INDEX_TYPE Batch_num = PE_Param_in.read();
        if (Batch_num == kSpmvServiceStopToken) {
            PE_Param_out.write(kSpmvServiceStopToken);
            Vector_Y_Param.write(kSpmvServiceStopToken);
            return;
        }
        const INDEX_TYPE Row_num = PE_Param_in.read();
        const INDEX_TYPE Column_num = PE_Param_in.read();

        PE_Param_out.write(Batch_num);
        PE_Param_out.write(Row_num);
        PE_Param_out.write(Column_num);

        Vector_Y_Param.write(Batch_num);
        Vector_Y_Param.write(Row_num);

        // 底层 round 负责：
        //   - 缓存本 column batch 的 X 窗口；
        //   - 解码当前 HBM channel 的 SpElement；
        //   - 输出 row 编码和 val*x[col] 局部乘积。
        Cuper_Core_Compute_Round(Batch_num,
                                 Column_num,
                                 PE_Param_in,
                                 Matrix_A_Stream,
                                 Vector_X_Stream_in,
                                 PE_Param_out,
                                 Vector_X_Stream_out,
                                 Vector_Y_Param,
                                 Matrix_Mult_Vector_Stream);
    }
}

// Accumulator 按 Cuper 内部 row 编码累加每路 Core 的局部乘积。
// 它输出的是按硬件对齐顺序排列的 float_v2；Jacobi update stage 直接消费该顺序，
// 并在做 x_next 更新时顺手丢弃 padding。
void SpmvService_Accumulator(tapa::istream<INDEX_TYPE>    &Vector_Y_Param,
                             tapa::istream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream,
                             tapa::ostream<float_v2>      &Vector_Y_Stream
                             ,
                             const INDEX_TYPE Debug_channel
#ifdef JACOBI_TRACE_FULL
                             ,
                             tapa::ostream<JacobiDebugEvent> &Debug_Event_out
#endif
                             ) {
#ifdef JACOBI_TRACE_FULL
    const INDEX_TYPE Debug_source = kJacobiDebugSourceAccumulatorBase + Debug_channel;
#endif
#ifdef PINGPONG
    // ping/pong 分别保存偶数行和奇数行的部分和。row 编码来自 host 侧
    // Reordering：bit0 表示奇偶，bit[17:1] 是局部累加地址，bit17=1
    // 表示空元素。这里不是按原始全局 row 直接索引。
    ap_uint<32> local_part_Y_ping[8][URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_ping type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_ping dim=1
    ap_uint<32> local_part_Y_pong[8][URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_pong type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_pong dim=1
#else
    // 非 PINGPONG 时一个 URAM word 保存偶/奇两路部分和，沿用原 Cuper helper 的格式。
    ap_uint<64> local_part_Y_ping[8][URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_ping type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_ping dim=1
#endif

    for (;;) {
#pragma HLS loop_flatten off
        // Vector_Y_Param 的第一项是 Batch_num 或停止令牌。正常轮次随后还有 Row_num，
        // 后续 batch 边界由 Core 写入同一条 stream。
        const INDEX_TYPE Batch_num = Vector_Y_Param.read();
        if (Batch_num == kSpmvServiceStopToken) {
#ifdef JACOBI_TRACE_FULL
            Jacobi_DebugTryWrite(Debug_Event_out,
                                 Debug_source,
                                 kJacobiDebugPhaseStop,
                                 0,
                                 0);
#endif
            return;
        }
        const INDEX_TYPE Row_num = Vector_Y_Param.read();
#ifdef JACOBI_TRACE_FULL
        Jacobi_DebugTryWrite(Debug_Event_out,
                             Debug_source,
                             kJacobiDebugPhaseRecv,
                             Batch_num,
                             Row_num);
#endif
#ifdef PINGPONG
        Cuper_Accumulator_Compute_Round(Batch_num,
                                        Row_num,
                                        Vector_Y_Param,
                                        Matrix_Mult_Vector_Stream,
                                        Vector_Y_Stream,
                                        local_part_Y_ping,
                                        local_part_Y_pong);
#else
        Cuper_Accumulator_Compute_Round(Batch_num,
                                        Row_num,
                                        Vector_Y_Param,
                                        Matrix_Mult_Vector_Stream,
                                        Vector_Y_Stream,
                                        local_part_Y_ping);
#endif
#ifdef JACOBI_TRACE_FULL
        Jacobi_DebugTryWrite(Debug_Event_out,
                             Debug_source,
                             kJacobiDebugPhaseDoneRound,
                             Batch_num,
                             spmv_service_num_accumulator_outputs(Row_num));
#endif
    }
}
