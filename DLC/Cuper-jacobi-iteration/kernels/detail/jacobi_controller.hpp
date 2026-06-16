#pragma once

// Jacobi 主控制器。
// 当前版本取消 RoundToken/FeedbackToken 自循环，改为单个 controller 显式控制每轮：
//   1. 发矩阵 loader command；
//   2. 发 ptr/vector compute command；
//   3. 向 update 后端广播本轮 command；
//   4. 等 X HBM writer 回传 done ack；
//   5. 进入下一轮或统一广播 stop。

#include <tapa.h>

#include "jacobi_common.hpp"
#ifdef JACOBI_TRACE_ENABLED
#include "jacobi_deadlock_debug.hpp"
#endif

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

#if defined(JACOBI_TRACE_ENABLED) && defined(JACOBI_BLOCKING_ENTRY_PROBE)
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
    // Metrics[8..11] 镜像 Status probe。
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

inline void Jacobi_SendUpdateCommand(
    const JacobiUpdateCommand command,
    tapa::ostream<JacobiUpdateCommand> &Coeff_Command_out,
    tapa::ostream<JacobiUpdateCommand> &Pack_Command_out,
    tapa::ostream<JacobiUpdateCommand> &Hbm_Command_out,
    tapa::ostreams<JacobiUpdateCommand, JACOBI_UPDATE_PAIR_NUM> &Pair_Command_out) {
#pragma HLS inline
    // 主控制器直接把同一轮 command 发给 update 后端所有 task。
    // 这里替代旧版 Jacobi_UpdateFrameFork，避免 frame 在后端自传播。
    Coeff_Command_out.write(command);
    Pack_Command_out.write(command);
    Hbm_Command_out.write(command);
send_pair_update_command:
    for (INDEX_TYPE lane_pair = 0; lane_pair < JACOBI_UPDATE_PAIR_NUM; ++lane_pair) {
#pragma HLS unroll
        Pair_Command_out[lane_pair].write(command);
    }
}

void Jacobi_MasterController(
    tapa::ostreams<CuperSpmvServiceCommand, 2> &Command_out,
    tapa::ostreams<CuperSpmvServiceCommand, HBM_CHANNEL_NUM> &Matrix_Command_out,
    tapa::ostream<INDEX_TYPE> &Vector_Destroy_Stop_out,
#ifdef JACOBI_TRACE_ENABLED
    tapa::ostream<JacobiDebugEvent> &Debug_Event_out,
    tapa::ostream<INDEX_TYPE> &Debug_Stop_out,
#endif
    tapa::ostream<JacobiStageEvent> &Stage_Event_out,
    tapa::istream<ap_uint<64>> &Stage_Ticks_in,
    tapa::ostream<JacobiUpdateCommand> &Update_Coeff_Command_out,
    tapa::ostream<JacobiUpdateCommand> &Update_Pack_Command_out,
    tapa::ostream<JacobiUpdateCommand> &Update_Hbm_Command_out,
    tapa::ostreams<JacobiUpdateCommand, JACOBI_UPDATE_PAIR_NUM> &Update_Pair_Command_out,
    tapa::istream<JacobiUpdateDone> &Update_Done_in,
    tapa::async_mmap<INDEX_TYPE> &Status,
    tapa::async_mmap<double> &Metrics,
    const INDEX_TYPE Row_num,
    const INDEX_TYPE Max_iters,
    const float Tau) {
    // 当前 fixed-count Jacobi 不使用 Tau，保留 ABI。
    (void)Tau;

#if defined(JACOBI_TRACE_ENABLED) && defined(JACOBI_BLOCKING_ENTRY_PROBE)
    // Controller 是 Status/Metrics 的唯一 writer。入口阻塞写可以确认该 task 已启动。
    Jacobi_WriteStatusProbe(Status, Row_num, Max_iters);
    Jacobi_WriteMetricsProbe(Metrics, Row_num, Max_iters);
#endif

    const INDEX_TYPE packet_count = spmv_service_num_float_v16_packets(Row_num);
    unsigned long long spmv_update_work_packets = 0;
    INDEX_TYPE iterations_done = 0;

    Jacobi_StageMark(Stage_Event_out, kJacobiStageControllerTotal, kJacobiStageBegin);

main_round_loop:
    for (INDEX_TYPE iter = 0; iter < Max_iters; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
#ifdef JACOBI_TRACE_ENABLED
        Jacobi_DebugTryWrite(Debug_Event_out,
                             kJacobiDebugSourceController,
                             kJacobiDebugPhaseEnterRound,
                             iter,
                             packet_count);
#endif
        Jacobi_StageMark(Stage_Event_out, kJacobiStageSpmvUpdate, kJacobiStageBegin);

        // 稳定优先：每轮矩阵显式加载一次，不再使用上一轮触发的预取 token。
        spmv_service_send_matrix_command(Matrix_Command_out);
#ifdef JACOBI_TRACE_ENABLED
        Jacobi_DebugTryWrite(Debug_Event_out,
                             kJacobiDebugSourceController,
                             kJacobiDebugPhaseSend,
                             10,
                             iter);
#endif
        spmv_service_send_compute_command(Command_out);
        Jacobi_SendUpdateCommand(Jacobi_MakeUpdateCommand(Row_num, iter),
                                 Update_Coeff_Command_out,
                                 Update_Pack_Command_out,
                                 Update_Hbm_Command_out,
                                 Update_Pair_Command_out);
#ifdef JACOBI_TRACE_ENABLED
        Jacobi_DebugTryWrite(Debug_Event_out,
                             kJacobiDebugSourceController,
                             kJacobiDebugPhaseSend,
                             0,
                             iter);
#endif

        const JacobiUpdateDone done = Update_Done_in.read();
        iterations_done = done.iter + 1;
        spmv_update_work_packets += static_cast<unsigned long long>(done.packet_count);
        Jacobi_StageMark(Stage_Event_out, kJacobiStageSpmvUpdate, kJacobiStageEnd);
#ifdef JACOBI_TRACE_ENABLED
        Jacobi_DebugTryWrite(Debug_Event_out,
                             kJacobiDebugSourceController,
                             kJacobiDebugPhaseDoneRound,
                             done.iter,
                             done.packet_count);
#endif
    }

#ifdef JACOBI_TRACE_ENABLED
    Jacobi_DebugTryWrite(Debug_Event_out,
                         kJacobiDebugSourceController,
                         kJacobiDebugPhaseStop,
                         0,
                         iterations_done);
#endif

    spmv_service_send_compute_stop(Command_out);
    spmv_service_send_matrix_stop(Matrix_Command_out);
    Jacobi_SendUpdateCommand(Jacobi_MakeUpdateStopCommand(),
                             Update_Coeff_Command_out,
                             Update_Pack_Command_out,
                             Update_Hbm_Command_out,
                             Update_Pair_Command_out);
    Vector_Destroy_Stop_out.write(1);
#ifdef JACOBI_TRACE_ENABLED
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
#ifdef JACOBI_TRACE_ENABLED
        Jacobi_DebugTryWrite(Debug_Event_out,
                             kJacobiDebugSourceController,
                             kJacobiDebugPhaseReadResp,
                             index,
                             static_cast<INDEX_TYPE>(stage_cycles[index].to_uint64()));
#endif
    }

#ifdef JACOBI_TRACE_ENABLED
    Jacobi_DebugTryWrite(Debug_Event_out,
                         kJacobiDebugSourceController,
                         kJacobiDebugPhaseWriteIssue,
                         0,
                         iterations_done);
#endif
    Jacobi_WriteStatus(Status,
                       kJacobiStatusMaxIter,
                       iterations_done);
    Jacobi_WriteMetrics(Metrics,
                        Row_num,
                        iterations_done,
                        spmv_update_work_packets,
                        stage_cycles);
#ifdef JACOBI_TRACE_ENABLED
    Jacobi_DebugTryWrite(Debug_Event_out,
                         kJacobiDebugSourceController,
                         kJacobiDebugPhaseDoneRound,
                         0,
                         iterations_done);
#endif
}
