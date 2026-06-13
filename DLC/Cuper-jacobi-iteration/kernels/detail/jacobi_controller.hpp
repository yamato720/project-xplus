#pragma once

// Jacobi 数据流控制 task。
// 这里不再用集中 controller 主动逐轮控制；正常轮次由 JacobiRoundToken 在数据流里传播。
// Source 只种第一枚 token，Output_Update 写完本轮后反馈下一枚 token，Dispatcher 按 token
// 扇出矩阵预取、SpMV compute command/frame，并在 stop token 到达时统一停机和写回状态。

#include <tapa.h>

#include "jacobi_common.hpp"
#ifdef JACOBI_DEADLOCK_DEBUG
#include "jacobi_deadlock_debug.hpp"
#endif

void Jacobi_RoundTokenSource(tapa::ostream<JacobiRoundToken> &Initial_Token_out,
                             const INDEX_TYPE Row_num,
                             const INDEX_TYPE Max_iters) {
    if (Max_iters <= 0) {
        Initial_Token_out.write(Jacobi_MakeStopToken(Row_num, Max_iters, 0));
    } else {
        Initial_Token_out.write(Jacobi_MakeRoundToken(Row_num, Max_iters, 0));
    }
}

void Jacobi_RoundTokenMux(tapa::istream<JacobiRoundToken> &Initial_Token_in,
                          tapa::istream<JacobiRoundToken> &Feedback_Token_in,
                          tapa::ostream<JacobiRoundToken> &Round_Token_out) {
    JacobiRoundToken token = Initial_Token_in.read();
    for (;;) {
#pragma HLS loop_flatten off
        Round_Token_out.write(token);
        if (token.stop != 0) {
            return;
        }
        token = Feedback_Token_in.read();
    }
}

inline void Jacobi_WriteStatus(tapa::async_mmap<INDEX_TYPE> &Status,
                               const INDEX_TYPE status,
                               const INDEX_TYPE iterations_done) {
#pragma HLS inline
    Status.write_addr.write(0);
    Status.write_data.write(status);
    Status.write_addr.write(1);
    Status.write_data.write(0);
    Status.write_addr.write(2);
    Status.write_data.write(iterations_done);
write_status_resp:
    for (INDEX_TYPE response_count = 0; response_count < 3;) {
#pragma HLS pipeline II=1
        uint8_t num_responses = 0;
        if (Status.write_resp.try_read(num_responses)) {
            response_count += int(num_responses) + 1;
        }
    }
}

inline void Jacobi_WriteMetrics(tapa::async_mmap<double> &Metrics,
                                const INDEX_TYPE Row_num,
                                const INDEX_TYPE iterations_done,
                                const unsigned long long spmv_update_work_packets,
                                const ap_uint<64> stage_cycles[kJacobiStageCount + 1]) {
#pragma HLS inline
    Metrics.write_addr.write(0);
    Metrics.write_data.write(0.0);
    Metrics.write_addr.write(1);
    Metrics.write_data.write(static_cast<double>(iterations_done));
    Metrics.write_addr.write(2);
    Metrics.write_data.write(static_cast<double>(spmv_service_num_float_v16_packets(Row_num)));
    Metrics.write_addr.write(3);
    Metrics.write_data.write(static_cast<double>(spmv_update_work_packets));
    Metrics.write_addr.write(4);
    Metrics.write_data.write(static_cast<double>(stage_cycles[kJacobiStageSpmvUpdate].to_uint64()));
    Metrics.write_addr.write(5);
    Metrics.write_data.write(static_cast<double>(stage_cycles[kJacobiStageControllerTotal].to_uint64()));
    Metrics.write_addr.write(6);
    Metrics.write_data.write(static_cast<double>(stage_cycles[kJacobiStageCount].to_uint64()));
    Metrics.write_addr.write(7);
    Metrics.write_data.write((iterations_done > 0)
                                 ? static_cast<double>(stage_cycles[kJacobiStageSpmvUpdate].to_uint64()) /
                                       static_cast<double>(iterations_done)
                                 : 0.0f);
write_metrics_resp:
    for (INDEX_TYPE response_count = 0; response_count < 8;) {
#pragma HLS pipeline II=1
        uint8_t num_responses = 0;
        if (Metrics.write_resp.try_read(num_responses)) {
            response_count += int(num_responses) + 1;
        }
    }
}

#ifdef JACOBI_DEADLOCK_DEBUG
inline void Jacobi_WriteStatusProbe(tapa::async_mmap<INDEX_TYPE> &Status,
                                    const INDEX_TYPE Row_num,
                                    const INDEX_TYPE Max_iters) {
#pragma HLS inline
    // Status[8..11] 是 debug-only 入口探针，避开 Status[0..2] 的正式结果。
    Status.write_addr.write(8);
    Status.write_data.write(kJacobiDebugProbeMagic);
    Status.write_addr.write(9);
    Status.write_data.write(Row_num);
    Status.write_addr.write(10);
    Status.write_data.write(Max_iters);
    Status.write_addr.write(11);
    Status.write_data.write(spmv_service_num_float_v16_packets(Row_num));
write_status_probe_resp:
    for (INDEX_TYPE response_count = 0; response_count < 4;) {
#pragma HLS pipeline II=1
        uint8_t num_responses = 0;
        if (Status.write_resp.try_read(num_responses)) {
            response_count += int(num_responses) + 1;
        }
    }
}

inline void Jacobi_WriteMetricsProbe(tapa::async_mmap<double> &Metrics,
                                     const INDEX_TYPE Row_num,
                                     const INDEX_TYPE Max_iters) {
#pragma HLS inline
    // Metrics[8..11] 镜像 Status probe。若 Status 可见但 Metrics 不可见，优先看
    // double mmap ABI 或 HBM[24] 上多个 BO 的连接。
    Metrics.write_addr.write(8);
    Metrics.write_data.write(static_cast<double>(kJacobiDebugProbeMagic));
    Metrics.write_addr.write(9);
    Metrics.write_data.write(static_cast<double>(Row_num));
    Metrics.write_addr.write(10);
    Metrics.write_data.write(static_cast<double>(Max_iters));
    Metrics.write_addr.write(11);
    Metrics.write_data.write(static_cast<double>(spmv_service_num_float_v16_packets(Row_num)));
write_metrics_probe_resp:
    for (INDEX_TYPE response_count = 0; response_count < 4;) {
#pragma HLS pipeline II=1
        uint8_t num_responses = 0;
        if (Metrics.write_resp.try_read(num_responses)) {
            response_count += int(num_responses) + 1;
        }
    }
}
#endif

void Jacobi_RoundDispatcher(
    tapa::istream<JacobiRoundToken> &Round_Token_in,
    tapa::ostreams<CuperSpmvServiceCommand, 2> &Command_out,
    tapa::ostreams<CuperSpmvServiceCommand, HBM_CHANNEL_NUM> &Matrix_Prefetch_Command_out,
    tapa::ostream<INDEX_TYPE> &Vector_Destroy_Stop_out,
#ifdef JACOBI_DEADLOCK_DEBUG
    tapa::ostream<JacobiDebugEvent> &Debug_Event_out,
    tapa::ostream<INDEX_TYPE> &Debug_Stop_out,
#endif
    tapa::ostream<JacobiStageEvent> &Stage_Event_out,
    tapa::istream<ap_uint<64>> &Stage_Ticks_in,
    tapa::ostream<JacobiFrame> &Update_Frame_out,
    tapa::async_mmap<INDEX_TYPE> &Status,
    tapa::async_mmap<double> &Metrics,
    const INDEX_TYPE Row_num,
    const INDEX_TYPE Max_iters,
    const float Tau) {
    // 当前 fixed-count Jacobi 不使用 Tau，保留 ABI。
    (void)Tau;
    unsigned long long spmv_update_work_packets = 0;

#ifdef JACOBI_DEADLOCK_DEBUG
    // Dispatcher 是 Status/Metrics 的唯一 writer。入口阻塞写可以确认该 task 已启动、
    // HBM[24] 上 Status/Metrics BO 可写，且写响应能回到 kernel。
    Jacobi_WriteStatusProbe(Status, Row_num, Max_iters);
    Jacobi_WriteMetricsProbe(Metrics, Row_num, Max_iters);
#endif

    Jacobi_StageMark(Stage_Event_out, kJacobiStageControllerTotal, kJacobiStageBegin);

dispatch_loop:
    for (;;) {
#pragma HLS loop_flatten off
        const JacobiRoundToken token = Round_Token_in.read();
        // 每个非首轮 token 都来自 HBM writer 的反馈，说明上一轮 x_next 写回响应已收齐。
        // 因此在 dispatcher 这里打 SpMV+update end 点，避免 Stage_Event_Stream 出现多生产者。
        if (token.iter > 0) {
            Jacobi_StageMark(Stage_Event_out, kJacobiStageSpmvUpdate, kJacobiStageEnd);
        }
        if (token.stop != 0) {
#ifdef JACOBI_DEADLOCK_DEBUG
            Jacobi_DebugTryWrite(Debug_Event_out,
                                 kJacobiDebugSourceDispatcher,
                                 kJacobiDebugPhaseStop,
                                 0,
                                 token.iterations_done);
#endif
            spmv_service_send_compute_stop(Command_out);
            spmv_service_send_matrix_stop(Matrix_Prefetch_Command_out);
            Update_Frame_out.write(Jacobi_MakeStopFrame());
            Vector_Destroy_Stop_out.write(1);
#ifdef JACOBI_DEADLOCK_DEBUG
            Debug_Stop_out.write(1);
#endif

            Jacobi_StageMark(Stage_Event_out, kJacobiStageControllerTotal, kJacobiStageEnd);
            Jacobi_StageMark(Stage_Event_out, 0, kJacobiStageStop);

            ap_uint<64> stage_cycles[kJacobiStageCount + 1];
#pragma HLS array_partition variable=stage_cycles complete
        read_jacobi_stage_timer_metrics:
            for (INDEX_TYPE index = 0; index < kJacobiStageCount + 1; ++index) {
#pragma HLS pipeline II=1
                stage_cycles[index] = Stage_Ticks_in.read();
            }

            Jacobi_WriteStatus(Status,
                               kJacobiStatusMaxIter,
                               token.iterations_done);
            Jacobi_WriteMetrics(Metrics,
                                Row_num,
                                token.iterations_done,
                                spmv_update_work_packets,
                                stage_cycles);
            return;
        }

        Jacobi_StageMark(Stage_Event_out, kJacobiStageSpmvUpdate, kJacobiStageBegin);
#ifdef JACOBI_DEADLOCK_DEBUG
        Jacobi_DebugTryWrite(Debug_Event_out,
                             kJacobiDebugSourceDispatcher,
                             kJacobiDebugPhaseEnterRound,
                             token.iter,
                             token.packet_count);
#endif
        // 第 0 轮没有上一轮可提前预取，所以这里先发当前轮矩阵预取。
        // 后续轮次的矩阵预取在上一轮 compute 启动后已经发出。
        if (token.iter == 0) {
            spmv_service_send_matrix_command(Matrix_Prefetch_Command_out);
        }

        // Core 启动仍由 ptr/vector compute command 控制，从而保证单 X 原地更新时
        // 不会在 X 完整写回前读到半旧半新的向量。
        spmv_service_send_compute_command(Command_out);
        Update_Frame_out.write(Jacobi_MakeFrame(token));
#ifdef JACOBI_DEADLOCK_DEBUG
        Jacobi_DebugTryWrite(Debug_Event_out,
                             kJacobiDebugSourceDispatcher,
                             kJacobiDebugPhaseProgress,
                             token.iter,
                             spmv_update_work_packets);
#endif

        // 当前轮 Core 开始后，Matrix_A_Stream 前部会被逐步消费；若还有下一轮，
        // 立即把下一轮矩阵读入同一组 FIFO。FIFO 满时 matrix loader 会自然反压。
        if ((token.iter + 1) < token.max_iters) {
            spmv_service_send_matrix_command(Matrix_Prefetch_Command_out);
        }
        spmv_update_work_packets += static_cast<unsigned long long>(token.packet_count);
    }
}
