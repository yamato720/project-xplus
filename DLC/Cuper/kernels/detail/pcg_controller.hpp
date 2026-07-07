#pragma once

// Private implementation header for kernels/Cuper.cpp.
// It contains task definitions and should not be included by another translation unit.

#include <ap_int.h>
#include <tapa.h>

#include "pcg_common.hpp"
#include "pcg_vector_phases.hpp"

// FPGA 内 PCG 主控。
//
// controller 现在只保留 Callipepla 风格的标量调度职责：
//   1. 发起 init/iter SpMV command。
//   2. 发起向量阶段 command，并用 result 作为阶段完成边界。
//   3. 做收敛判断、alpha/beta 计算、breakdown 检查。
//   4. 维护 stage timer 和 Metrics/Status 写回。
//
// 大段 B/M_inv/X/R/Z/P/AP_spmv/P_spmv HBM 向量访问已经下沉到
// Pcg_Vector_Phases，避免 controller 同时承担控制和宽向量 datapath。
void Pcg_Controller(tapa::ostreams<CuperSpmvCommand, 2> &Command_out,
                    tapa::ostreams<CuperSpmvCommand, HBM_CHANNEL_NUM> &Matrix_Command_out,
                    tapa::ostreams<INDEX_TYPE, 8> &Checker_Stop_out,
                    tapa::ostream<INDEX_TYPE> &Sort_Stop_out,
                    tapa::ostream<INDEX_TYPE> &Vector_Destroy_Stop_out,
                    tapa::ostream<PcgStageEvent> &Stage_Event_out,
                    tapa::istream<ap_uint<64>> &Stage_Ticks_in,
                    tapa::ostream<PcgVectorCommand> &Vector_Command_out,
                    tapa::istream<PcgVectorResult> &Vector_Result_in,
                    tapa::mmap<double> Metrics,
                    tapa::mmap<INDEX_TYPE> Status,
                    const INDEX_TYPE Row_num,
                    const INDEX_TYPE Max_iters,
                    const double Tau) {
    const INDEX_TYPE packet_count = pcg_num_float_v16_packets(Row_num);
    const INDEX_TYPE double_packet_count = pcg_num_double_v8_packets(Row_num);
    INDEX_TYPE status_code = kPcgStatusMaxIter;
    INDEX_TYPE iterations = 0;
    double rz = 0.0;
    double rr = 0.0;
    double p_ap = 0.0;
    double alpha = 0.0;
    const unsigned long long float_packet_work =
        static_cast<unsigned long long>(packet_count);
    const unsigned long long double_packet_work =
        static_cast<unsigned long long>(double_packet_count);
    unsigned long long init_spmv_work_packets = 0;
    unsigned long long init_zp_work_packets = 0;
    unsigned long long iter_spmv_work_packets = 0;
    unsigned long long update_x_work_packets = 0;
    unsigned long long update_rz_work_packets = 0;
    unsigned long long update_p_work_packets = 0;

    // controller_total 覆盖从参数检查到 stop 广播、metrics 写回前的主体时间。
    pcg_stage_mark(Stage_Event_out, kPcgStageControllerTotal, kPcgStageBegin);

    // 非法参数直接报 breakdown，避免后续常驻 SpMV 服务读取无效范围。
    if (Row_num <= 0 || Max_iters < 0 || Tau <= 0.0 || pcg_invalid(Tau)) {
        status_code = kPcgStatusBreakdown;
    } else {
        // [共用 SpMV service / init 调用] 初始化 SpMV：先用当前 X_spmv
        // 计算 A*x0。向量 worker 消费 A*x0 stream，并生成初始残差 R。
        pcg_stage_mark(Stage_Event_out, kPcgStageInitSpmv, kPcgStageBegin);
        pcg_send_spmv_command(Command_out,
                              Matrix_Command_out,
                              kPcgVectorSourceX);
        Vector_Command_out.write(
            pcg_make_vector_command(kPcgVectorPhaseInitSpmv, 0.0, 0.0));
        (void)Vector_Result_in.read();
        // packed work packets: AP stream packets + B reads + R writes.
        init_spmv_work_packets += float_packet_work + 2ULL * double_packet_work;
        pcg_stage_mark(Stage_Event_out, kPcgStageInitSpmv, kPcgStageEnd);

        pcg_stage_mark(Stage_Event_out, kPcgStageInitZp, kPcgStageBegin);
        Vector_Command_out.write(
            pcg_make_vector_command(kPcgVectorPhaseInitZp, 0.0, 0.0));
        const PcgVectorResult init_zp_result = Vector_Result_in.read();
        rz = init_zp_result.rz;
        rr = init_zp_result.rr;
        // packed work packets: R/M_inv reads + Z/P writes + P_spmv write.
        init_zp_work_packets += 4ULL * double_packet_work + float_packet_work;
        pcg_stage_mark(Stage_Event_out, kPcgStageInitZp, kPcgStageEnd);

    pcg_loop:
        for (INDEX_TYPE iter = 0; iter < Max_iters && rr > Tau; ++iter) {
#pragma HLS loop_tripcount min=1 max=1000
            // [共用 SpMV service / PCG 迭代调用] 每轮 SpMV：将当前搜索方向 p
            // 送入 Cuper 流水，计算 AP=A*p。向量 worker 缓存 AP_spmv 并返回 p^T AP。
            pcg_stage_mark(Stage_Event_out, kPcgStageIterSpmv, kPcgStageBegin);
            pcg_send_spmv_command(Command_out,
                                  Matrix_Command_out,
                                  kPcgVectorSourceP);
            Vector_Command_out.write(
                pcg_make_vector_command(kPcgVectorPhaseIterDot, 0.0, 0.0));
            const PcgVectorResult iter_dot_result = Vector_Result_in.read();
            p_ap = iter_dot_result.p_ap;
            // packed work packets: AP stream read + AP_spmv write + P read for fused dot.
            iter_spmv_work_packets += 2ULL * float_packet_work + double_packet_work;
            pcg_stage_mark(Stage_Event_out, kPcgStageIterSpmv, kPcgStageEnd);

            if (pcg_invalid(p_ap) || pcg_abs(p_ap) <= kPcgBreakdownEps ||
                pcg_invalid(rz) || pcg_abs(rz) <= kPcgBreakdownEps) {
                status_code = kPcgStatusBreakdown;
                break;
            }

            alpha = rz / p_ap;
            if (pcg_invalid(alpha)) {
                status_code = kPcgStatusBreakdown;
                break;
            }

            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateX, kPcgStageBegin);
            Vector_Command_out.write(
                pcg_make_vector_command(kPcgVectorPhaseUpdateX, alpha, 0.0));
            (void)Vector_Result_in.read();
            // packed work packets: X/P reads + X write.
            update_x_work_packets += 3ULL * double_packet_work;
            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateX, kPcgStageEnd);

            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateRz, kPcgStageBegin);
            Vector_Command_out.write(
                pcg_make_vector_command(kPcgVectorPhaseUpdateRzReduce, alpha, 0.0));
            const PcgVectorResult rz_result = Vector_Result_in.read();
            const double rz_new = rz_result.rz;
            const double rr_new = rz_result.rr;
            // packed work packets: AP read + R/M_inv reads + R/Z writes.
            update_rz_work_packets += float_packet_work + 4ULL * double_packet_work;
            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateRz, kPcgStageEnd);

            if (pcg_invalid(rz_new) || pcg_invalid(rr_new)) {
                status_code = kPcgStatusBreakdown;
                break;
            }

            const double beta = rz_new / rz;
            if (pcg_invalid(beta)) {
                status_code = kPcgStatusBreakdown;
                break;
            }

            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateP, kPcgStageBegin);
            Vector_Command_out.write(
                pcg_make_vector_command(kPcgVectorPhaseUpdateP, 0.0, beta));
            (void)Vector_Result_in.read();
            // packed work packets: Z/P reads + P write + P_spmv write.
            update_p_work_packets += 3ULL * double_packet_work + float_packet_work;
            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateP, kPcgStageEnd);

            rz = rz_new;
            rr = rr_new;
            iterations = iter + 1;
        }

        if (status_code != kPcgStatusBreakdown) {
            status_code = (rr <= Tau) ? kPcgStatusConverged : kPcgStatusMaxIter;
        }
    }

    pcg_send_spmv_stop(Command_out, Matrix_Command_out);
    Vector_Command_out.write(pcg_make_vector_stop_command());
send_checker_stop:
    for (INDEX_TYPE index = 0; index < 8; ++index) {
#pragma HLS unroll
        Checker_Stop_out[index].write(1);
    }
    Sort_Stop_out.write(1);
    Vector_Destroy_Stop_out.write(1);
    // 先让所有服务任务收到 stop，再通知 timer 停止；否则 host 可能等不到
    // AP_CTRL_HS done，或 timer 还没把 stage_cycles 写完。
    pcg_stage_mark(Stage_Event_out, kPcgStageControllerTotal, kPcgStageEnd);
    pcg_stage_mark(Stage_Event_out, 0, kPcgStageStop);

    ap_uint<64> stage_cycles[kPcgStageCount + 1];
#pragma HLS array_partition variable=stage_cycles complete
read_stage_timer_metrics:
    for (INDEX_TYPE index = 0; index < kPcgStageCount + 1; ++index) {
#pragma HLS pipeline II=1
        stage_cycles[index] = Stage_Ticks_in.read();
    }

    // Metrics/Status 是 host 侧判断运行结果和调试数值稳定性的最小输出。
    // Metrics 布局：
    //   [0..3]  数值状态：rz, rr, p_ap, alpha
    //   [4..15] packed work：float_v16/double_v8 包数量和各阶段 memory packet work
    //   [16..24] stage timer 实测 cycle：init/iter SpMV、PCG 更新、总时间
    // Status[0] 是 kPcgStatus*，Status[1] 是实际完成的 PCG 迭代数。
    Metrics[0] = rz;
    Metrics[1] = rr;
    Metrics[2] = p_ap;
    Metrics[3] = alpha;
    Metrics[4] = static_cast<double>(packet_count);
    Metrics[5] = static_cast<double>(init_spmv_work_packets);
    Metrics[6] = static_cast<double>(init_zp_work_packets);
    Metrics[7] = static_cast<double>(iter_spmv_work_packets);
    Metrics[8] = static_cast<double>(update_x_work_packets);
    Metrics[9] = static_cast<double>(update_rz_work_packets);
    Metrics[10] = static_cast<double>(update_p_work_packets);
    Metrics[11] = static_cast<double>(init_spmv_work_packets + init_zp_work_packets +
                                      iter_spmv_work_packets + update_x_work_packets +
                                      update_rz_work_packets + update_p_work_packets);
    Metrics[12] = static_cast<double>(Row_num);
    Metrics[13] = static_cast<double>(Max_iters);
    Metrics[14] = 0.0;  // p^T AP is fused into Metrics[7] / kPcgStageIterSpmv.
    Metrics[15] = static_cast<double>(double_packet_count);
    Metrics[16] = static_cast<double>(stage_cycles[kPcgStageInitSpmv].to_uint64());
    Metrics[17] = static_cast<double>(stage_cycles[kPcgStageInitZp].to_uint64());
    Metrics[18] = static_cast<double>(stage_cycles[kPcgStageIterSpmv].to_uint64());
    Metrics[19] = static_cast<double>(stage_cycles[kPcgStageDotPAp].to_uint64());
    Metrics[20] = static_cast<double>(stage_cycles[kPcgStageUpdateX].to_uint64());
    Metrics[21] = static_cast<double>(stage_cycles[kPcgStageUpdateRz].to_uint64());
    Metrics[22] = static_cast<double>(stage_cycles[kPcgStageUpdateP].to_uint64());
    Metrics[23] = static_cast<double>(stage_cycles[kPcgStageControllerTotal].to_uint64());
    Metrics[24] = static_cast<double>(stage_cycles[kPcgStageCount].to_uint64());
    Status[0] = status_code;
    Status[1] = iterations;
}
