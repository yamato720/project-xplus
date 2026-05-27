#pragma once

// Private implementation header for kernels/Cuper.cpp.
// It contains task definitions and should not be included by another translation unit.

#include <ap_int.h>
#include <tapa.h>

#include "pcg_common.hpp"

// FPGA 内 PCG 主控。
//
// 这是 CuperPcg 和 host-PCG 旧版的核心区别：
//   1. 初始化阶段发起 A*x0，计算 r=b-A*x0、z=M_inv*r、p=z。
//   2. 每轮发起 A*p，计算 p_ap、alpha，然后更新 x/r/z。
//   3. 计算 beta 并更新 p，直到 rr<=Tau、达到 Max_iters 或 breakdown。
//
// SpMV 本身仍由下面的 TAPA Cuper 数据流服务完成；controller 只负责
// 发送命令、提供 x/p 向量、消费 SpMV 结果和维护 PCG 向量状态。
void Pcg_Controller(tapa::ostreams<CuperSpmvCommand, 2> &Command_out,
                    tapa::ostreams<CuperSpmvCommand, HBM_CHANNEL_NUM> &Matrix_Command_out,
                    tapa::ostreams<INDEX_TYPE, 8> &Checker_Stop_out,
                    tapa::ostream<INDEX_TYPE> &Sort_Stop_out,
                    tapa::ostream<INDEX_TYPE> &Vector_Destroy_Stop_out,
                    tapa::ostream<PcgStageEvent> &Stage_Event_out,
                    tapa::istream<ap_uint<64>> &Stage_Ticks_in,
                    tapa::ostream<float_v16> &X_to_spmv,
                    tapa::istream<float_v16> &Spmv_in,
                    tapa::mmap<double> B,
                    tapa::mmap<double> M_inv,
                    tapa::mmap<double> X,
                    tapa::mmap<double> R,
                    tapa::mmap<double> Z,
                    tapa::mmap<double> P,
                    tapa::mmap<double> AP,
                    tapa::mmap<double> Metrics,
                    tapa::mmap<INDEX_TYPE> Status,
                    const INDEX_TYPE Row_num,
                    const INDEX_TYPE Max_iters,
                    const double Tau) {
    // controller 和 SpMV 数据流之间用 float_v16 作为向量包。
    // packet_count 只描述 PCG 向量长度，和 Cuper 内部 18-bit row 编码
    // 不是一回事。
    const INDEX_TYPE packet_count = (Row_num + 15) >> 4;
    INDEX_TYPE status_code = kPcgStatusMaxIter;
    INDEX_TYPE iterations = 0;
    double rz = 0.0;
    double rr = 0.0;
    double p_ap = 0.0;
    double alpha = 0.0;
    unsigned long long init_spmv_ticks = 0;
    unsigned long long init_zp_ticks = 0;
    unsigned long long iter_spmv_ticks = 0;
    unsigned long long dot_p_ap_ticks = 0;
    unsigned long long update_xr_ticks = 0;
    unsigned long long update_z_ticks = 0;
    unsigned long long update_p_ticks = 0;
    CuperSpmvCommand command;
    command.iteration_num = 1;
    command.stop = 0;
    pcg_stage_mark(Stage_Event_out, kPcgStageControllerTotal, kPcgStageBegin);

    // 非法参数直接报 breakdown，避免后续常驻 SpMV 服务读取无效范围。
    if (Row_num <= 0 || Max_iters < 0 || Tau <= 0.0 || pcg_invalid(Tau)) {
        status_code = kPcgStatusBreakdown;
    } else {
// 初始化 SpMV：先用当前 X 计算 A*x0。
        pcg_stage_mark(Stage_Event_out, kPcgStageInitSpmv, kPcgStageBegin);
send_init_command:
        for (INDEX_TYPE index = 0; index < 2; ++index) {
#pragma HLS unroll
        Command_out[index].write(command);
    }
send_init_matrix_command:
    for (INDEX_TYPE index = 0; index < HBM_CHANNEL_NUM; ++index) {
#pragma HLS unroll
        Matrix_Command_out[index].write(command);
    }

    init_spmv_stream:
        // 边发送 x0 边消费 A*x0，避免 SpMV 输出 FIFO 填满后反压整条
        // Cuper 数据流，而 controller 仍阻塞在继续写输入向量。
        // 这个循环故意使用 full/empty + try_write/try_read 做非阻塞握手：
        // 大矩阵时输入向量和输出结果会同时在流水里移动，不能拆成
        // “先全部写完，再全部读完”的两段。
        for (INDEX_TYPE sent_packets = 0, received_packets = 0;
             received_packets < packet_count;) {
#pragma HLS loop_tripcount min=1 max=500000
            ++init_spmv_ticks;
            if (sent_packets < packet_count && !X_to_spmv.full()) {
                float_v16 x_packet;
        fill_x0_packet:
                for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
                    const INDEX_TYPE index = (sent_packets << 4) + lane;
                    double value = 0.0;
                    if (index < Row_num) {
                        value = X[index];
                    }
                    x_packet[lane] = static_cast<VALUE_TYPE>(value);
                }
                X_to_spmv.try_write(x_packet);
                ++sent_packets;
            }

            if (!Spmv_in.empty()) {
                const INDEX_TYPE packet = received_packets;
                float_v16 ap_packet;
                Spmv_in.try_read(ap_packet);
        init_r_lanes:
                // 第一段只消费 A*x0 并生成初始残差 R。上一版 init_vectors 同时读 B/M_inv、
                // 写 R/Z/P、做 rz/rr 归约，route 最后 4 根冲突线集中在这条大流水。
                // 拆开后用一次额外 R 读取换取更小的局部 FP64/HBM 访问压力。
                for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
#pragma HLS pipeline II=4
                    const INDEX_TYPE index = (packet << 4) + lane;
                    if (index < Row_num) {
                        const double b_value = B[index];
                        const double ap_value = static_cast<double>(ap_packet[lane]);
                        const double r_value = b_value - ap_value;
                        R[index] = r_value;
                    }
                }
                ++received_packets;
            }
        }
        pcg_stage_mark(Stage_Event_out, kPcgStageInitSpmv, kPcgStageEnd);

        pcg_stage_mark(Stage_Event_out, kPcgStageInitZp, kPcgStageBegin);
init_zp_reduce:
        // 第二段再读 R/M_inv，初始化 Z/P 并累计 rz/rr。它和 update_z_reduce
        // 形态接近，避免把 residual 初始化和 SpMV 输出消费挤在同一条流水里。
        for (INDEX_TYPE index = 0; index < Row_num; ++index) {
#pragma HLS loop_tripcount min=1 max=8000000
#pragma HLS pipeline II=4
            const double r_value = R[index];
            const double minv_value = M_inv[index];
            const double z_value = minv_value * r_value;
            Z[index] = z_value;
            P[index] = z_value;
            rz += r_value * z_value;
            rr += r_value * r_value;
        }
        init_zp_ticks += static_cast<unsigned long long>(Row_num) * 4ULL;
        pcg_stage_mark(Stage_Event_out, kPcgStageInitZp, kPcgStageEnd);

pcg_loop:
        for (INDEX_TYPE iter = 0; iter < Max_iters && rr > Tau; ++iter) {
#pragma HLS loop_tripcount min=1 max=1000
    // 每轮 SpMV：将当前搜索方向 p 送入 Cuper 流水，计算 AP=A*p。
            pcg_stage_mark(Stage_Event_out, kPcgStageIterSpmv, kPcgStageBegin);
    send_iter_command:
            for (INDEX_TYPE index = 0; index < 2; ++index) {
#pragma HLS unroll
                Command_out[index].write(command);
            }
    send_iter_matrix_command:
            for (INDEX_TYPE index = 0; index < HBM_CHANNEL_NUM; ++index) {
#pragma HLS unroll
                Matrix_Command_out[index].write(command);
            }

            p_ap = 0.0;
    iter_spmv_stream:
            // 每轮同样边发送 p 边消费 A*p。否则大矩阵时 controller 可能在
            // X_to_spmv.write() 等下游腾空间，而下游又在等 controller 读取
            // 已经算出的 SpMV 输出，形成硬件死锁。
            // sent_packets 和 received_packets 独立推进，允许 Cuper SpMV
            // 先产出部分 AP，也允许 controller 继续补发后续 p packet。
            for (INDEX_TYPE sent_packets = 0, received_packets = 0;
                 received_packets < packet_count;) {
#pragma HLS loop_tripcount min=1 max=500000
                ++iter_spmv_ticks;
                if (sent_packets < packet_count && !X_to_spmv.full()) {
                    float_v16 p_packet;
            fill_p_packet:
                    for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
                        const INDEX_TYPE index = (sent_packets << 4) + lane;
                        double value = 0.0;
                        if (index < Row_num) {
                            value = P[index];
                        }
                        p_packet[lane] = static_cast<VALUE_TYPE>(value);
                    }
                    X_to_spmv.try_write(p_packet);
                    ++sent_packets;
                }

                if (!Spmv_in.empty()) {
                    const INDEX_TYPE packet = received_packets;
                    float_v16 ap_packet;
                    Spmv_in.try_read(ap_packet);
            ap_lanes:
                    // 只消费 AP 并写回 HBM。p^T AP 放到后面的独立流水里做，
                    // 避免 SpMV 回收路径同时承担 P 读取和 FP64 归约。
                    for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
                        const INDEX_TYPE index = (packet << 4) + lane;
                        if (index < Row_num) {
                            const double ap_value = static_cast<double>(ap_packet[lane]);
                            AP[index] = ap_value;
                        }
                    }
                    ++received_packets;
                }
            }
            pcg_stage_mark(Stage_Event_out, kPcgStageIterSpmv, kPcgStageEnd);

            pcg_stage_mark(Stage_Event_out, kPcgStageDotPAp, kPcgStageBegin);
    dot_p_ap:
            for (INDEX_TYPE index = 0; index < Row_num; ++index) {
#pragma HLS loop_tripcount min=1 max=8000000
#pragma HLS pipeline II=4
                p_ap += P[index] * AP[index];
            }
            dot_p_ap_ticks += static_cast<unsigned long long>(Row_num) * 4ULL;
            pcg_stage_mark(Stage_Event_out, kPcgStageDotPAp, kPcgStageEnd);

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

            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateXr, kPcgStageBegin);
    update_xr:
            // 只更新 x/r。上一版把 x/r/z 更新、M_inv 读取、rz/rr 归约
            // 都塞在同一个 update_xrz pipeline 里，route 失败集中在该
            // pipeline 的 FP64 乘法和 AXI 读写附近。这里把它拆成两段，
            // 用一次额外 R 读取换取更小的局部布线热点。
            for (INDEX_TYPE index = 0; index < Row_num; ++index) {
#pragma HLS loop_tripcount min=1 max=8000000
#pragma HLS pipeline II=4
                const double x_value = X[index];
                const double p_value = P[index];
                const double r_value = R[index];
                const double ap_value = AP[index];
                const double x_new = x_value + alpha * p_value;
                const double r_new = r_value - alpha * ap_value;
                X[index] = x_new;
                R[index] = r_new;
            }
            update_xr_ticks += static_cast<unsigned long long>(Row_num) * 4ULL;
            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateXr, kPcgStageEnd);

            double rz_new = 0.0;
            double rr_new = 0.0;
            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateZ, kPcgStageBegin);
    update_z_reduce:
            // 再更新 z 并累计新残差。该段只读 R/M_inv、写 Z，避免和
            // update_xr 的 X/P/AP 访问以及 alpha 乘法挤在同一条流水里。
            for (INDEX_TYPE index = 0; index < Row_num; ++index) {
#pragma HLS loop_tripcount min=1 max=8000000
#pragma HLS pipeline II=4
                const double r_new = R[index];
                const double minv_value = M_inv[index];
                const double z_new = minv_value * r_new;
                Z[index] = z_new;
                rz_new += r_new * z_new;
                rr_new += r_new * r_new;
            }
            update_z_ticks += static_cast<unsigned long long>(Row_num) * 4ULL;
            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateZ, kPcgStageEnd);

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
    update_p:
            // p = z + beta * p。下一轮 controller 会重新把 P 打包送入 SpMV。
            // 同样放宽 II，避免 beta 更新路径和 update_xrz 争抢同一区域布线。
            for (INDEX_TYPE index = 0; index < Row_num; ++index) {
#pragma HLS loop_tripcount min=1 max=8000000
#pragma HLS pipeline II=2
                const double z_value = Z[index];
                const double p_value = P[index];
                P[index] = z_value + beta * p_value;
            }
            update_p_ticks += static_cast<unsigned long long>(Row_num) * 2ULL;
            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateP, kPcgStageEnd);

            rz = rz_new;
            rr = rr_new;
            iterations = iter + 1;
        }

        if (status_code != kPcgStatusBreakdown) {
            status_code = (rr <= Tau) ? kPcgStatusConverged : kPcgStatusMaxIter;
        }
    }

    CuperSpmvCommand stop_command;
    stop_command.iteration_num = 0;
    stop_command.stop = 1;
send_stop_command:
    for (INDEX_TYPE index = 0; index < 2; ++index) {
#pragma HLS unroll
        Command_out[index].write(stop_command);
    }
send_stop_matrix_command:
    for (INDEX_TYPE index = 0; index < HBM_CHANNEL_NUM; ++index) {
#pragma HLS unroll
        Matrix_Command_out[index].write(stop_command);
    }
send_checker_stop:
    for (INDEX_TYPE index = 0; index < 8; ++index) {
#pragma HLS unroll
        Checker_Stop_out[index].write(1);
    }
    Sort_Stop_out.write(1);
    Vector_Destroy_Stop_out.write(1);
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
    Metrics[0] = rz;
    Metrics[1] = rr;
    Metrics[2] = p_ap;
    Metrics[3] = alpha;
    Metrics[4] = static_cast<double>(packet_count);
    Metrics[5] = static_cast<double>(init_spmv_ticks);
    Metrics[6] = static_cast<double>(init_zp_ticks);
    Metrics[7] = static_cast<double>(iter_spmv_ticks);
    Metrics[8] = static_cast<double>(update_xr_ticks);
    Metrics[9] = static_cast<double>(update_z_ticks);
    Metrics[10] = static_cast<double>(update_p_ticks);
    Metrics[11] = static_cast<double>(init_spmv_ticks + init_zp_ticks +
                                      iter_spmv_ticks + dot_p_ap_ticks + update_xr_ticks +
                                      update_z_ticks + update_p_ticks);
    Metrics[12] = static_cast<double>(Row_num);
    Metrics[13] = static_cast<double>(Max_iters);
    Metrics[14] = static_cast<double>(dot_p_ap_ticks);
    Metrics[16] = static_cast<double>(stage_cycles[kPcgStageInitSpmv].to_uint64());
    Metrics[17] = static_cast<double>(stage_cycles[kPcgStageInitZp].to_uint64());
    Metrics[18] = static_cast<double>(stage_cycles[kPcgStageIterSpmv].to_uint64());
    Metrics[19] = static_cast<double>(stage_cycles[kPcgStageDotPAp].to_uint64());
    Metrics[20] = static_cast<double>(stage_cycles[kPcgStageUpdateXr].to_uint64());
    Metrics[21] = static_cast<double>(stage_cycles[kPcgStageUpdateZ].to_uint64());
    Metrics[22] = static_cast<double>(stage_cycles[kPcgStageUpdateP].to_uint64());
    Metrics[23] = static_cast<double>(stage_cycles[kPcgStageControllerTotal].to_uint64());
    Metrics[24] = static_cast<double>(stage_cycles[kPcgStageCount].to_uint64());
    Status[0] = status_code;
    Status[1] = iterations;
}
