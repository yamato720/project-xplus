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
// 发送命令、消费 SpMV 结果和维护 PCG 向量状态。x0/p 的 SpMV 输入
// 由 packed float_v16 HBM 副本喂给 vector loader，避免 controller
// 在 SpMV 热路径里逐元素打包。
void Pcg_Controller(tapa::ostreams<CuperSpmvCommand, 2> &Command_out,
                    tapa::ostreams<CuperSpmvCommand, HBM_CHANNEL_NUM> &Matrix_Command_out,
                    tapa::ostreams<INDEX_TYPE, 8> &Checker_Stop_out,
                    tapa::ostream<INDEX_TYPE> &Sort_Stop_out,
                    tapa::ostream<INDEX_TYPE> &Vector_Destroy_Stop_out,
                    tapa::ostream<PcgStageEvent> &Stage_Event_out,
                    tapa::istream<ap_uint<64>> &Stage_Ticks_in,
                    tapa::istream<float_v16> &Spmv_in,
                    tapa::mmap<double> B,
                    tapa::mmap<double> M_inv,
                    tapa::mmap<double> X,
                    tapa::mmap<double> R,
                    tapa::mmap<double> Z,
                    tapa::mmap<double> P,
                    tapa::mmap<float_v16> AP_spmv,
                    tapa::mmap<float_v16> P_spmv,
                    tapa::mmap<double> Metrics,
                    tapa::mmap<INDEX_TYPE> Status,
                    const INDEX_TYPE Row_num,
                    const INDEX_TYPE Max_iters,
                    const double Tau) {
    // B/M_inv/X/R/Z/P 是 FP64 PCG 状态；AP_spmv/P_spmv 是为了贴近 Cuper
    // SpMV 的 packed FP32 辅助副本。最终解仍写在 X 里。
    //
    // AP_spmv 只缓存最近一次 A*p 的 SpMV 输出；P_spmv 缓存当前搜索方向 p。
    // X_spmv 不传入 controller，由 Pcg_Vector_Loader 在 init SpMV 时直接读。
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
    command.stop = 0;
    command.vector_source = kPcgVectorSourceX;
    // controller_total 覆盖从参数检查到 stop 广播、metrics 写回前的主体时间。
    pcg_stage_mark(Stage_Event_out, kPcgStageControllerTotal, kPcgStageBegin);

    // 非法参数直接报 breakdown，避免后续常驻 SpMV 服务读取无效范围。
    if (Row_num <= 0 || Max_iters < 0 || Tau <= 0.0 || pcg_invalid(Tau)) {
        status_code = kPcgStatusBreakdown;
    } else {
        // 初始化 SpMV：先用当前 X_spmv 计算 A*x0。
        pcg_stage_mark(Stage_Event_out, kPcgStageInitSpmv, kPcgStageBegin);
send_init_command:
        for (INDEX_TYPE index = 0; index < 2; ++index) {
#pragma HLS unroll
        // Command_out[0] 给 ptr loader，Command_out[1] 给 vector loader。
        Command_out[index].write(command);
    }
send_init_matrix_command:
    for (INDEX_TYPE index = 0; index < HBM_CHANNEL_NUM; ++index) {
#pragma HLS unroll
        // 每个 matrix loader 独立一条命令流，保证 16 路 HBM loader 同步启动。
        Matrix_Command_out[index].write(command);
    }

    init_spmv_stream:
        // x0 已由 host 预打包到 X_spmv，Pcg_Vector_Loader 会按 float_v16
        // packed HBM 顺序读入。controller 只负责消费 A*x0，避免在 SpMV
        // 计时段内逐元素读取 double X 并临时打包。
        for (INDEX_TYPE received_packets = 0; received_packets < packet_count;) {
#pragma HLS loop_tripcount min=1 max=500000
            ++init_spmv_ticks;
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
        // 第二段再读 R/M_inv，初始化 Z/P 并累计 rz/rr。这里同时维护
        // P_spmv 的 float_v16 packed 副本，下一轮 A*p 可直接走 Cuper
        // 风格向量 loader，不再由 controller 从 double P 标量打包。
        for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
            float_v16 p_packet;
    init_zp_lanes:
            for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
#pragma HLS pipeline II=4
                const INDEX_TYPE index = (packet << 4) + lane;
                double z_value = 0.0;
                if (index < Row_num) {
                    const double r_value = R[index];
                    const double minv_value = M_inv[index];
                    z_value = minv_value * r_value;
                    Z[index] = z_value;
                    P[index] = z_value;
                    rz += r_value * z_value;
                    rr += r_value * r_value;
                }
                p_packet[lane] = static_cast<VALUE_TYPE>(z_value);
            }
            P_spmv[packet] = p_packet;
        }
        init_zp_ticks += static_cast<unsigned long long>(Row_num) * 4ULL +
                          static_cast<unsigned long long>(packet_count);
        pcg_stage_mark(Stage_Event_out, kPcgStageInitZp, kPcgStageEnd);

pcg_loop:
        for (INDEX_TYPE iter = 0; iter < Max_iters && rr > Tau; ++iter) {
#pragma HLS loop_tripcount min=1 max=1000
    // 每轮 SpMV：将当前搜索方向 p 送入 Cuper 流水，计算 AP=A*p。
            pcg_stage_mark(Stage_Event_out, kPcgStageIterSpmv, kPcgStageBegin);
            command.vector_source = kPcgVectorSourceP;
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
            // p 已经在 init_zp/update_p 阶段维护为 P_spmv packed 副本。
            // 这里不再从 double P 逐元素读和打包，controller 只消费 A*p。
            for (INDEX_TYPE received_packets = 0; received_packets < packet_count;) {
#pragma HLS loop_tripcount min=1 max=500000
                ++iter_spmv_ticks;
                if (!Spmv_in.empty()) {
                    const INDEX_TYPE packet = received_packets;
                    float_v16 ap_packet;
                    Spmv_in.try_read(ap_packet);
                    // 只按 Cuper 原生 packed 形态回收 AP。旧版这里把一包
                    // float_v16 拆成 16 个 double 写入 AP HBM，会在 SpMV
                    // 输出侧重新形成 controller 标量瓶颈。
                    AP_spmv[packet] = ap_packet;
                    ++received_packets;
                }
            }
            pcg_stage_mark(Stage_Event_out, kPcgStageIterSpmv, kPcgStageEnd);

            pcg_stage_mark(Stage_Event_out, kPcgStageDotPAp, kPcgStageBegin);
    dot_p_ap:
            for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
                // AP 已按 Cuper 输出粒度缓存为 float_v16；P 仍保留 FP64，
                // 因此这里做 FP32->FP64 转换后参与 p^T AP。
                const float_v16 ap_packet = AP_spmv[packet];
        dot_p_ap_lanes:
                for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
#pragma HLS pipeline II=4
                    const INDEX_TYPE index = (packet << 4) + lane;
                    if (index < Row_num) {
                        p_ap += P[index] * static_cast<double>(ap_packet[lane]);
                    }
                }
            }
            dot_p_ap_ticks += static_cast<unsigned long long>(Row_num) * 4ULL +
                              static_cast<unsigned long long>(packet_count);
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
            for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
                const float_v16 ap_packet = AP_spmv[packet];
        update_xr_lanes:
                for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
#pragma HLS pipeline II=4
                    const INDEX_TYPE index = (packet << 4) + lane;
                    if (index < Row_num) {
                        const double x_value = X[index];
                        const double p_value = P[index];
                        const double r_value = R[index];
                        const double ap_value = static_cast<double>(ap_packet[lane]);
                        const double x_new = x_value + alpha * p_value;
                        const double r_new = r_value - alpha * ap_value;
                        X[index] = x_new;
                        R[index] = r_new;
                    }
                }
            }
            update_xr_ticks += static_cast<unsigned long long>(Row_num) * 4ULL +
                               static_cast<unsigned long long>(packet_count);
            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateXr, kPcgStageEnd);

            double rz_new = 0.0;
            double rr_new = 0.0;
            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateZ, kPcgStageBegin);
    update_z_reduce:
            // 再更新 z 并累计新残差。该段只读 R/M_inv、写 Z，避免和
            // update_xr 的 X/P/AP_spmv 访问以及 alpha 乘法挤在同一条流水里。
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
            // p = z + beta * p。
            // 这里同步更新 P_spmv packed 副本，把下一轮 SpMV 的向量输入
            // 准备成 single Cuper 相同的 float_v16 HBM 形态。
            for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
                float_v16 p_packet;
        update_p_lanes:
                for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
#pragma HLS pipeline II=2
                    const INDEX_TYPE index = (packet << 4) + lane;
                    double p_new = 0.0;
                    if (index < Row_num) {
                        const double z_value = Z[index];
                        const double p_value = P[index];
                        p_new = z_value + beta * p_value;
                        P[index] = p_new;
                    }
                    p_packet[lane] = static_cast<VALUE_TYPE>(p_new);
                }
                P_spmv[packet] = p_packet;
            }
            update_p_ticks += static_cast<unsigned long long>(Row_num) * 2ULL +
                              static_cast<unsigned long long>(packet_count);
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
    stop_command.stop = 1;
    stop_command.vector_source = kPcgVectorSourceX;
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
    //   [4..14] work-tick 估算：packet_count 和各阶段手工计数
    //   [16..24] stage timer 实测 cycle：init/iter SpMV、PCG 更新、总时间
    // Status[0] 是 kPcgStatus*，Status[1] 是实际完成的 PCG 迭代数。
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
