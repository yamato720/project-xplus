#pragma once

// 可复用的 service-mode Cuper SpMV task。
// 这些 task 把原 one-shot Cuper SpMV 包成“收到 command 跑一轮，收到 stop 退出”的常驻服务。

#include <algorithm>

#include <ap_int.h>
#include <tapa.h>

#include "cuper_spmv_tasks.hpp"
#include "spmv_service_common.hpp"

// 本文件把原始一次性 Cuper SpMV task 改造成可反复调用的常驻服务。
// 它不包含 PCG 语义；Jacobi controller 只把它当作可重复触发的 SpMV 计算服务。

// 读取 SpElement_list_ptr 边界表，并把 Batch/Row/Column 参数送入 16 级 Core 链。
// 每收到一条非 stop command，就完整发起一轮 SpMV 的边界表读取。
void SpmvService_SpElementPtrLoader(const INDEX_TYPE Batch_num,
                                    const INDEX_TYPE Row_num,
                                    const INDEX_TYPE Column_num,
                                    tapa::async_mmap<INDEX_TYPE> &SpElement_list_ptr,
                                    tapa::istream<CuperSpmvServiceCommand> &Command_in,
                                    tapa::ostream<INDEX_TYPE> &PE_Param) {
    for (;;) {
#pragma HLS loop_flatten off
        const CuperSpmvServiceCommand command = Command_in.read();
        if (command.stop != 0) {
            PE_Param.write(kSpmvServiceStopToken);
            return;
        }
        PE_Param.write(Batch_num);
        PE_Param.write(Row_num);
        PE_Param.write(Column_num);

        // SpElement_list_ptr 长度是 Batch_num + 1，Core 用相邻两个边界确定
        // 每个 column batch 需要消费的 Matrix_data beat 范围。
        const INDEX_TYPE batch_num_plus_1 = Batch_num + 1;
        Cuper_ReadSpElementPtrPackets(batch_num_plus_1,
                                      SpElement_list_ptr,
                                      PE_Param);
    }
}

// 单路 HBM 矩阵 loader。顶层会 join 出 16 个实例，各自读取自己的 Matrix_data[channel]。
// Matrix_len 是每路都要读取的 512-bit beat 数，由 host 预处理后的最大边界给出。
void SpmvService_MatrixLoader(const INDEX_TYPE Matrix_len,
                              tapa::async_mmap<ap_uint<512>> &Matrix_data,
                              tapa::istream<CuperSpmvServiceCommand> &Command_in,
                              tapa::ostream<ap_uint<512>> &Matrix_A_Stream) {
    for (;;) {
#pragma HLS loop_flatten off
        const CuperSpmvServiceCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }

        // Matrix_data 的布局仍沿用 Cuper：一个 512-bit beat 内含 8 个 64-bit SpElement slot。
        Cuper_ReadMatrixPackets(Matrix_len,
                                Matrix_data,
                                Matrix_A_Stream);
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
// 它输出的是按硬件对齐顺序排列的 float_v2，后面还需要 checker/sort 恢复连续 SpMV 结果。
void SpmvService_Accumulator(tapa::istream<INDEX_TYPE>    &Vector_Y_Param,
                             tapa::istream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream,
                             tapa::ostream<float_v2>      &Vector_Y_Stream) {
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
            return;
        }
        const INDEX_TYPE Row_num = Vector_Y_Param.read();
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
    }
}

// Checker 过滤 accumulator 为对齐产生的 padding 输出。
// 这里是常驻 task：没有数据时会同时检查 Stop_in，避免 controller stop 后仍然空等。
void SpmvService_VectorChecker(const INDEX_TYPE Row_num,
                               tapa::istreams<float_v2, HBM_CHANNEL_NUM_DIV_8> &Vector_Y_Stream,
                               tapa::ostream<float_v2> &Vector_Y_Stream_Aftck,
                               tapa::istream<INDEX_TYPE> &Stop_in) {
    const INDEX_TYPE num_pe_output = spmv_service_num_checker_pe_outputs(Row_num);
    const INDEX_TYPE num_out = spmv_service_num_float_v16_packets(Row_num);

    for (;;) {
#pragma HLS loop_flatten off
    wait_round:
        for (;;) {
#pragma HLS pipeline II=1
            // 以第 0 路输出作为“一轮已有数据”的轻量触发；真正转发时
            // Cuper_TryForwardCheckerValue 会按固定顺序轮询各路输入。
            if (!Vector_Y_Stream[0].empty()) {
                break;
            }
            if (!Stop_in.empty()) {
                INDEX_TYPE stop;
                Stop_in.try_read(stop);
                return;
            }
        }
    out:
        for (INDEX_TYPE i = 0, c_idx = 0, o_idx = 0; i < num_pe_output;) {
#pragma HLS loop_tripcount min=1 max=1800
#pragma HLS pipeline II=1
            (void)Cuper_TryForwardCheckerValue(num_pe_output,
                                                num_out,
                                                i,
                                                c_idx,
                                                o_idx,
                                                Vector_Y_Stream,
                                                Vector_Y_Stream_Aftck);
        }
    }
}

// Sort tree 把 8 路 float_v2 checker 输出重新拼成连续 float_v16 的 SpMV 结果包。
// 输出直接进入 Jacobi_Update_Service，不再写 standalone Cuper 的 Y_out。
void SpmvService_MultSortTree(tapa::istreams<float_v2, 8> &Vector_Y_Stream_Aftck,
                              tapa::ostream<float_v16> &Vector_Y_Stream_Ans,
                              tapa::istream<INDEX_TYPE> &Stop_in) {
    for (;;) {
#pragma HLS pipeline II=1
        if (!Stop_in.empty()) {
            INDEX_TYPE stop;
            Stop_in.try_read(stop);
            return;
        }

        // 非阻塞尝试拼包；输入未齐时本周期不输出，等待下一拍继续轮询。
        (void)Cuper_TryPackFloatV16(Vector_Y_Stream_Aftck, Vector_Y_Stream_Ans);
    }
}
