#pragma once

// Jacobi 迭代级 controller。
// 它只负责“每轮什么时候开始/结束”和退出状态，不参与 Rx 数据包本身的逐元素计算。

#include <tapa.h>

#include "jacobi_common.hpp"

void Jacobi_Controller(
    tapa::ostreams<CuperSpmvServiceCommand, 2> &Command_out,
    tapa::ostreams<CuperSpmvServiceCommand, HBM_CHANNEL_NUM> &Matrix_Command_out,
    tapa::ostreams<INDEX_TYPE, 8> &Checker_Stop_out,
    tapa::ostream<INDEX_TYPE> &Sort_Stop_out,
    tapa::ostream<INDEX_TYPE> &Vector_Destroy_Stop_out,
    tapa::ostream<JacobiStageEvent> &Stage_Event_out,
    tapa::istream<ap_uint<64>> &Stage_Ticks_in,
    tapa::ostream<JacobiFrame> &Update_Frame_out,
    tapa::istream<JacobiUpdateResult> &Update_Result_in,
    tapa::async_mmap<INDEX_TYPE> &Status,
    tapa::async_mmap<double> &Metrics,
    const INDEX_TYPE Row_num,
    const INDEX_TYPE Max_iters,
    const float Tau) {
    // Current hardware debug contract is fixed-count Jacobi. Tau is retained in
    // the ABI but no longer drives an early convergence break.
    (void)Tau;
    // read_from_x1 指向本轮旧解 buffer；write_to_x1 每轮取反。
    // last_written_x1 记录最后一次成功写入的新解位置，最终写到 Status[1]。
    INDEX_TYPE read_from_x1 = 0;
    INDEX_TYPE last_written_x1 = 0;
    INDEX_TYPE status = kJacobiStatusMaxIter;
    INDEX_TYPE iterations_done = 0;
    float last_diff = 0.0f;
    unsigned long long spmv_update_work_packets = 0;

    // controller_total 从第一轮控制开始计到 stop 广播前，包含等待 update result。
    Jacobi_StageMark(Stage_Event_out, kJacobiStageControllerTotal, kJacobiStageBegin);

jacobi_iter_loop:
    for (INDEX_TYPE iter = 0; iter < Max_iters; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=1000
        // 一轮 Jacobi 的控制面只有两类消息：
        //   1. 给 SpMV service 的 command，选择 X0/X1 作为 x_old；
        //   2. 给 update service 的 frame，说明 Rx 的行数、包数和写回 buffer。
        const INDEX_TYPE write_to_x1 = (read_from_x1 == 0) ? 1 : 0;
        const JacobiFrame frame = Jacobi_MakeFrame(read_from_x1,
                                                   write_to_x1,
                                                   Row_num,
                                                   iter);

        // spmv_update 覆盖本轮 Cuper SpMV service 运行、sort 输出、Jacobi update 写回，
        // 直到 update service 返回 diff/breakdown。
        Jacobi_StageMark(Stage_Event_out, kJacobiStageSpmvUpdate, kJacobiStageBegin);
        spmv_service_send_command(Command_out,
                                  Matrix_Command_out,
                                  read_from_x1);
        Update_Frame_out.write(frame);

        // update service 处理完本轮所有 float_v16 包后返回 diff/breakdown。
        // controller 在这里形成迭代级同步点，然后决定是否继续下一轮。
        const JacobiUpdateResult result = Update_Result_in.read();
        Jacobi_StageMark(Stage_Event_out, kJacobiStageSpmvUpdate, kJacobiStageEnd);
        last_diff = result.diff_max;
        last_written_x1 = result.wrote_x1;
        iterations_done = iter + 1;
        spmv_update_work_packets += static_cast<unsigned long long>(frame.packet_count);

        if (result.breakdown != 0 || Jacobi_InvalidFloat(result.diff_max)) {
            status = kJacobiStatusBreakdown;
            break;
        }
        read_from_x1 = write_to_x1;
    }

    // controller 退出后要同时停掉两类常驻 task：
    //   - SpMV service loader/core/accumulator 链通过 command/stop token 退出；
    //   - update/checker/sort/drain 这类等待型 task 通过独立 stop stream 退出。
    spmv_service_send_stop(Command_out, Matrix_Command_out);
    Update_Frame_out.write(Jacobi_MakeStopFrame());

stop_checkers:
    for (INDEX_TYPE index = 0; index < 8; ++index) {
#pragma HLS unroll
        Checker_Stop_out[index].write(1);
    }
    Sort_Stop_out.write(1);
    Vector_Destroy_Stop_out.write(1);

    Jacobi_StageMark(Stage_Event_out, kJacobiStageControllerTotal, kJacobiStageEnd);
    Jacobi_StageMark(Stage_Event_out, 0, kJacobiStageStop);

    ap_uint<64> stage_cycles[kJacobiStageCount + 1];
#pragma HLS array_partition variable=stage_cycles complete
read_jacobi_stage_timer_metrics:
    for (INDEX_TYPE index = 0; index < kJacobiStageCount + 1; ++index) {
#pragma HLS pipeline II=1
        stage_cycles[index] = Stage_Ticks_in.read();
    }

    // Status mmap 返回给 host：
    //   Status[0] = 退出状态；
    //   Status[1] = 最终解所在 buffer，0 表示 X0，1 表示 X1；
    //   Status[2] = 已完成迭代轮数。
    Status.write_addr.write(0);
    Status.write_data.write(status);
    Status.write_addr.write(1);
    Status.write_data.write(last_written_x1);
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

    // Metrics mmap 当前只放轻量诊断：
    //   Metrics[0] = 最后一轮 diff_max；
    //   Metrics[1] = 已完成迭代轮数的 float 形式，方便 host/脚本统一读 float 指标。
    //   Metrics[2] = 一轮需要处理的 float_v16 包数；
    //   Metrics[3] = 已处理的 SpMV/update 包数累计；
    //   Metrics[4] = SpMV+update 累计 cycle；
    //   Metrics[5] = controller_total cycle；
    //   Metrics[6] = timer_total cycle；
    //   Metrics[7] = 平均每轮 SpMV+update cycle。
    Metrics.write_addr.write(0);
    Metrics.write_data.write(last_diff);
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
