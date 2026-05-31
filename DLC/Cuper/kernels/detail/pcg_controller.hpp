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
//
// 阶段归属速记：
//   - 共用 SpMV service：init_spmv / iter_spmv 都走同一套 Pcg_* loader/core/
//     accumulator/checker/sort-tree，只是 vector_source 分别选择 X_spmv/P_spmv。
//   - init 专用：init_spmv 生成初始 R，init_zp 生成 Z/P/P_spmv 并累计初始 rz/rr。
    //   - PCG 迭代专用：iter_spmv_recv_dot、update_xr、update_z、update_p。
//   - 收尾控制：stop 广播、timer 读回、Metrics/Status 写回；它们不属于算法阶段。
void Pcg_Controller(tapa::ostreams<CuperSpmvCommand, 2> &Command_out,
                    tapa::ostreams<CuperSpmvCommand, HBM_CHANNEL_NUM> &Matrix_Command_out,
                    tapa::ostreams<INDEX_TYPE, 8> &Checker_Stop_out,
                    tapa::ostream<INDEX_TYPE> &Sort_Stop_out,
                    tapa::ostream<INDEX_TYPE> &Vector_Destroy_Stop_out,
                    tapa::ostream<PcgStageEvent> &Stage_Event_out,
                    tapa::istream<ap_uint<64>> &Stage_Ticks_in,
                    tapa::istream<float_v16> &Spmv_in,
                    tapa::mmap<double_v8> B,
                    tapa::mmap<double_v8> M_inv,
                    tapa::mmap<double_v8> X,
                    tapa::mmap<double_v8> R,
                    tapa::mmap<double_v8> Z,
                    tapa::mmap<double_v8> P,
                    tapa::mmap<float_v16> AP_spmv,
                    tapa::mmap<float_v16> P_spmv,
                    tapa::mmap<double> Metrics,
                    tapa::mmap<INDEX_TYPE> Status,
                    const INDEX_TYPE Row_num,
                    const INDEX_TYPE Max_iters,
                    const double Tau) {
    // B/M_inv/X/R/Z/P 是 512-bit packed FP64 PCG 状态；AP_spmv/P_spmv
    // 是为了贴近 Cuper SpMV 的 packed FP32 辅助副本。最终解仍写在 X 里。
    //
    // AP_spmv 只缓存最近一次 A*p 的 SpMV 输出；P_spmv 缓存当前搜索方向 p。
    // X_spmv 不传入 controller，由 Pcg_Vector_Loader 在 init SpMV 时直接读。
    // controller 和 SpMV 数据流之间用 float_v16 作为向量包。
    // packet_count 只描述 PCG 向量长度，和 Cuper 内部 18-bit row 编码
    // 不是一回事。
    const INDEX_TYPE packet_count = pcg_num_float_v16_packets(Row_num);
    const INDEX_TYPE double_packet_count = pcg_num_double_v8_packets(Row_num);
    INDEX_TYPE status_code = kPcgStatusMaxIter;
    INDEX_TYPE iterations = 0;
    double rz = 0.0;
    double rr = 0.0;
    double p_ap = 0.0;
    double alpha = 0.0;
    const unsigned long long float_packet_work = static_cast<unsigned long long>(packet_count);
    const unsigned long long double_packet_work = static_cast<unsigned long long>(double_packet_count);
    unsigned long long init_spmv_work_packets = 0;
    unsigned long long init_zp_work_packets = 0;
    unsigned long long iter_spmv_work_packets = 0;
    unsigned long long update_xr_work_packets = 0;
    unsigned long long update_z_work_packets = 0;
    unsigned long long update_p_work_packets = 0;
    // controller_total 覆盖从参数检查到 stop 广播、metrics 写回前的主体时间。
    pcg_stage_mark(Stage_Event_out, kPcgStageControllerTotal, kPcgStageBegin);

    // 非法参数直接报 breakdown，避免后续常驻 SpMV 服务读取无效范围。
    if (Row_num <= 0 || Max_iters < 0 || Tau <= 0.0 || pcg_invalid(Tau)) {
        status_code = kPcgStatusBreakdown;
    } else {
        // [共用 SpMV service / init 调用] 初始化 SpMV：先用当前 X_spmv
        // 计算 A*x0。硬件数据通路和每轮 A*p 相同，区别只在 vector_source。
        pcg_stage_mark(Stage_Event_out, kPcgStageInitSpmv, kPcgStageBegin);
        pcg_send_spmv_command(Command_out,
                              Matrix_Command_out,
                              kPcgVectorSourceX);

    init_spmv_stream:
        // [init 专用后处理] x0 已由 host 预打包到 X_spmv，
        // Pcg_Vector_Loader 会按 float_v16 packed HBM 顺序读入。
        // controller 只负责消费 A*x0，并把它转成初始残差 R。
        // 这段的 SpMV 输入/矩阵读取是共用 service；R = B - A*x0 是 init 专用。
        for (INDEX_TYPE received_packets = 0; received_packets < packet_count;) {
#pragma HLS loop_tripcount min=1 max=500000
            if (!Spmv_in.empty()) {
                const INDEX_TYPE packet = received_packets;
                float_v16 ap_packet;
                Spmv_in.try_read(ap_packet);
                const INDEX_TYPE double_packet_index = packet << 1;
                const double_v8 b_packet_lo = B[double_packet_index];
                double_v8 r_packet_lo;
                double_v8 r_packet_hi;
                VALUE_TYPE ap_lanes[16];
#pragma HLS array_partition variable=ap_lanes complete
        init_r_unpack_ap:
                for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
#pragma HLS unroll
                    ap_lanes[lane] = ap_packet[lane];
                }
        init_r_lanes:
                // 第一段只消费 A*x0 并生成初始残差 R。上一版 init_vectors 同时读 B/M_inv、
                // 写 R/Z/P、做 rz/rr 归约，route 最后 4 根冲突线集中在这条大流水。
                // 拆开后用一次额外 R 读取换取更小的局部 FP64/HBM 访问压力。
                for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS pipeline II=1
                    const INDEX_TYPE index = (packet << 4) + lane;
                    double r_value = 0.0;
                    if (index < Row_num) {
                        const double b_value = b_packet_lo[lane];
                        const double ap_value = static_cast<double>(ap_lanes[lane]);
                        r_value = b_value - ap_value;
                    }
                    r_packet_lo[lane] = r_value;
                }
                if (double_packet_index + 1 < double_packet_count) {
                    const double_v8 b_packet_hi = B[double_packet_index + 1];
        init_r_lanes_hi:
                    for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS pipeline II=1
                        const INDEX_TYPE index = (packet << 4) + lane + 8;
                        double r_value = 0.0;
                        if (index < Row_num) {
                            const double b_value = b_packet_hi[lane];
                            const double ap_value = static_cast<double>(ap_lanes[lane + 8]);
                            r_value = b_value - ap_value;
                        }
                        r_packet_hi[lane] = r_value;
                    }
                }
                R[double_packet_index] = r_packet_lo;
                if (double_packet_index + 1 < double_packet_count) {
                    R[double_packet_index + 1] = r_packet_hi;
                }
                ++received_packets;
            }
        }
        // packed work packets: AP stream packets + B reads + R writes.
        init_spmv_work_packets += float_packet_work + 2ULL * double_packet_work;
        pcg_stage_mark(Stage_Event_out, kPcgStageInitSpmv, kPcgStageEnd);

        pcg_stage_mark(Stage_Event_out, kPcgStageInitZp, kPcgStageBegin);
init_zp_reduce:
        // [init 专用] 第二段再读 R/M_inv，初始化 Z/P 并累计初始 rz/rr。
        // 这里同时维护 P_spmv 的 float_v16 packed 副本，为后续 PCG 迭代
        // 的 A*p 准备输入。它和 update_z/update_p 有相似访问模式，但目前
        // 是独立 loop；优化 update_p 不会自动改到这里。
        for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
            VALUE_TYPE p_lanes[16];
#pragma HLS array_partition variable=p_lanes complete
            const INDEX_TYPE double_packet_index = packet << 1;
            const double_v8 r_packet_lo = R[double_packet_index];
            const double_v8 minv_packet_lo = M_inv[double_packet_index];
            double_v8 z_packet_lo;
            double_v8 p_packet_lo;
            double_v8 z_packet_hi;
            double_v8 p_packet_hi;
    init_zp_lanes:
            for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS pipeline II=1
                const INDEX_TYPE index = (packet << 4) + lane;
                double z_value = 0.0;
                if (index < Row_num) {
                    const double r_value = r_packet_lo[lane];
                    const double minv_value = minv_packet_lo[lane];
                    z_value = minv_value * r_value;
                    rz += r_value * z_value;
                    rr += r_value * r_value;
                }
                z_packet_lo[lane] = z_value;
                p_packet_lo[lane] = z_value;
                p_lanes[lane] = static_cast<VALUE_TYPE>(z_value);
            }
            if (double_packet_index + 1 < double_packet_count) {
                const double_v8 r_packet_hi = R[double_packet_index + 1];
                const double_v8 minv_packet_hi = M_inv[double_packet_index + 1];
    init_zp_lanes_hi:
                for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS pipeline II=1
                    const INDEX_TYPE index = (packet << 4) + lane + 8;
                    double z_value = 0.0;
                    if (index < Row_num) {
                        const double r_value = r_packet_hi[lane];
                        const double minv_value = minv_packet_hi[lane];
                        z_value = minv_value * r_value;
                        rz += r_value * z_value;
                        rr += r_value * r_value;
                    }
                    z_packet_hi[lane] = z_value;
                    p_packet_hi[lane] = z_value;
                    p_lanes[lane + 8] = static_cast<VALUE_TYPE>(z_value);
                }
            } else {
    init_zp_pad_hi:
                for (INDEX_TYPE lane = 8; lane < 16; ++lane) {
#pragma HLS unroll
                    p_lanes[lane] = 0.0f;
                }
            }
            Z[double_packet_index] = z_packet_lo;
            P[double_packet_index] = p_packet_lo;
            if (double_packet_index + 1 < double_packet_count) {
                Z[double_packet_index + 1] = z_packet_hi;
                P[double_packet_index + 1] = p_packet_hi;
            }
            float_v16 p_packet;
    init_zp_pack_p:
            for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
#pragma HLS unroll
                p_packet[lane] = p_lanes[lane];
            }
            P_spmv[packet] = p_packet;
        }
        // packed work packets: R/M_inv reads + Z/P writes + P_spmv write.
        init_zp_work_packets += 4ULL * double_packet_work + float_packet_work;
        pcg_stage_mark(Stage_Event_out, kPcgStageInitZp, kPcgStageEnd);

pcg_loop:
        for (INDEX_TYPE iter = 0; iter < Max_iters && rr > Tau; ++iter) {
#pragma HLS loop_tripcount min=1 max=1000
            // [共用 SpMV service / PCG 迭代调用] 每轮 SpMV：将当前搜索方向 p
            // 送入 Cuper 流水，计算 AP=A*p。硬件 service 与 init_spmv 共用，
            // 只是 Pcg_Vector_Loader 这次从 P_spmv 读 packed 输入。
            pcg_stage_mark(Stage_Event_out, kPcgStageIterSpmv, kPcgStageBegin);
            pcg_send_spmv_command(Command_out,
                                  Matrix_Command_out,
                                  kPcgVectorSourceP);

            p_ap = 0.0;
    iter_spmv_stream:
            // [PCG 迭代专用接收] p 已经在 init_zp/update_p 阶段维护为
            // P_spmv packed 副本。这里不再从 double P 逐元素读和打包，
            // controller 只消费 A*p，并把 packed AP 暂存到 AP_spmv。
            // p^T AP 在接收 A*p 时顺手计算，避免后面再完整扫描一遍
            // P 和 AP_spmv；AP_spmv 仍保留给 update_xr 使用。
            for (INDEX_TYPE received_packets = 0; received_packets < packet_count;) {
#pragma HLS loop_tripcount min=1 max=500000
                if (!Spmv_in.empty()) {
                    const INDEX_TYPE packet = received_packets;
                    float_v16 ap_packet;
                    Spmv_in.try_read(ap_packet);
                    // 只按 Cuper 原生 packed 形态回收 AP。旧版这里把一包
                    // float_v16 拆成 16 个 double 写入 AP HBM，会在 SpMV
                    // 输出侧重新形成 controller 标量瓶颈。
                    AP_spmv[packet] = ap_packet;
                    const INDEX_TYPE double_packet_index = packet << 1;
                    const double_v8 p_packet_lo = P[double_packet_index];
                    VALUE_TYPE ap_lanes[16];
#pragma HLS array_partition variable=ap_lanes complete
            iter_dot_unpack_ap:
                    for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
#pragma HLS unroll
                        ap_lanes[lane] = ap_packet[lane];
                    }
            iter_dot_p_ap_lanes:
                    for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS pipeline II=1
                        const INDEX_TYPE index = (packet << 4) + lane;
                        if (index < Row_num) {
                            p_ap += p_packet_lo[lane] * static_cast<double>(ap_lanes[lane]);
                        }
                    }
                    if (double_packet_index + 1 < double_packet_count) {
                        const double_v8 p_packet_hi = P[double_packet_index + 1];
            iter_dot_p_ap_lanes_hi:
                        for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS pipeline II=1
                            const INDEX_TYPE index = (packet << 4) + lane + 8;
                            if (index < Row_num) {
                                p_ap += p_packet_hi[lane] *
                                        static_cast<double>(ap_lanes[lane + 8]);
                            }
                        }
                    }
                    ++received_packets;
                }
            }
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

            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateXr, kPcgStageBegin);
    update_xr:
            // [PCG 迭代专用] 用 alpha 更新 x/r。这里读写 FP64 X/R，
            // 读 FP64 P，并读 packed FP32 AP_spmv 后转 double。
            // 它是当前 full-PCG 里最大的向量更新热点之一，不参与 init-only。
            // 只更新 x/r。上一版把 x/r/z 更新、M_inv 读取、rz/rr 归约
            // 都塞在同一个 update_xrz pipeline 里，route 失败集中在该
            // pipeline 的 FP64 乘法和 AXI 读写附近。这里把它拆成两段，
            // 用一次额外 R 读取换取更小的局部布线热点。
            for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
                const float_v16 ap_packet = AP_spmv[packet];
                const INDEX_TYPE double_packet_index = packet << 1;
                const double_v8 x_packet_lo = X[double_packet_index];
                const double_v8 p_packet_lo = P[double_packet_index];
                const double_v8 r_packet_lo = R[double_packet_index];
                double_v8 x_new_packet_lo;
                double_v8 r_new_packet_lo;
                double_v8 x_new_packet_hi;
                double_v8 r_new_packet_hi;
                VALUE_TYPE ap_lanes[16];
#pragma HLS array_partition variable=ap_lanes complete
        update_xr_unpack_ap:
                for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
#pragma HLS unroll
                    ap_lanes[lane] = ap_packet[lane];
                }
        update_xr_compute_lanes:
                for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS pipeline II=1
                    const INDEX_TYPE index = (packet << 4) + lane;
                    const bool valid = index < Row_num;
                    double x_new = 0.0;
                    double r_new = 0.0;
                    if (valid) {
                        const double x_value = x_packet_lo[lane];
                        const double p_value = p_packet_lo[lane];
                        const double r_value = r_packet_lo[lane];
                        const double ap_value = static_cast<double>(ap_lanes[lane]);
                        x_new = x_value + alpha * p_value;
                        r_new = r_value - alpha * ap_value;
                    }
                    x_new_packet_lo[lane] = x_new;
                    r_new_packet_lo[lane] = r_new;
                }
                if (double_packet_index + 1 < double_packet_count) {
                    const double_v8 x_packet_hi = X[double_packet_index + 1];
                    const double_v8 p_packet_hi = P[double_packet_index + 1];
                    const double_v8 r_packet_hi = R[double_packet_index + 1];
        update_xr_compute_lanes_hi:
                    for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS pipeline II=1
                        const INDEX_TYPE index = (packet << 4) + lane + 8;
                        double x_new = 0.0;
                        double r_new = 0.0;
                        if (index < Row_num) {
                            const double x_value = x_packet_hi[lane];
                            const double p_value = p_packet_hi[lane];
                            const double r_value = r_packet_hi[lane];
                            const double ap_value = static_cast<double>(ap_lanes[lane + 8]);
                            x_new = x_value + alpha * p_value;
                            r_new = r_value - alpha * ap_value;
                        }
                        x_new_packet_hi[lane] = x_new;
                        r_new_packet_hi[lane] = r_new;
                    }
                }
                X[double_packet_index] = x_new_packet_lo;
                R[double_packet_index] = r_new_packet_lo;
                if (double_packet_index + 1 < double_packet_count) {
                    X[double_packet_index + 1] = x_new_packet_hi;
                    R[double_packet_index + 1] = r_new_packet_hi;
                }
            }
            // packed work packets: AP read + X/P/R reads + X/R writes.
            update_xr_work_packets += float_packet_work + 5ULL * double_packet_work;
            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateXr, kPcgStageEnd);

            double rz_new = 0.0;
            double rr_new = 0.0;
            pcg_stage_mark(Stage_Event_out, kPcgStageUpdateZ, kPcgStageBegin);
    update_z_reduce:
            // [PCG 迭代专用] 根据更新后的 R 重新计算 Z 和新的 rz/rr。
            // 访问模式类似 init_zp 的 R/M_inv/Z 片段，但这里不初始化 P，
            // 也不写 P_spmv；后者留给 update_p。
            // 再更新 z 并累计新残差。该段只读 R/M_inv、写 Z，避免和
            // update_xr 的 X/P/AP_spmv 访问以及 alpha 乘法挤在同一条流水里。
            for (INDEX_TYPE packet = 0; packet < double_packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=1000000
                const double_v8 r_packet = R[packet];
                const double_v8 minv_packet = M_inv[packet];
                double_v8 z_packet;
        update_z_lanes:
                for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS pipeline II=1
                    const INDEX_TYPE index = (packet << 3) + lane;
                    double z_new = 0.0;
                    if (index < Row_num) {
                        const double r_new = r_packet[lane];
                        const double minv_value = minv_packet[lane];
                        z_new = minv_value * r_new;
                        rz_new += r_new * z_new;
                        rr_new += r_new * r_new;
                    }
                    z_packet[lane] = z_new;
                }
                Z[packet] = z_packet;
            }
            // packed work packets: R/M_inv reads + Z write.
            update_z_work_packets += 3ULL * double_packet_work;
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
            // [PCG 迭代专用] 用 beta 更新搜索方向 P，并同步维护下一轮
            // A*p 所需的 packed FP32 P_spmv。这里是 FP64 主状态和 FP32
            // Cuper 输入副本之间的关键同步点。
            // p = z + beta * p。
            // 这里同步更新 P_spmv packed 副本，把下一轮 SpMV 的向量输入
            // 准备成 single Cuper 相同的 float_v16 HBM 形态。
            for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
                VALUE_TYPE p_lanes[16];
#pragma HLS array_partition variable=p_lanes complete
                const INDEX_TYPE double_packet_index = packet << 1;
                const double_v8 z_packet_lo = Z[double_packet_index];
                const double_v8 p_packet_lo_old = P[double_packet_index];
                double_v8 p_packet_lo_new;
                double_v8 p_packet_hi_new;
        update_p_compute_lanes:
                for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS pipeline II=1
                    const INDEX_TYPE index = (packet << 4) + lane;
                    const bool valid = index < Row_num;
                    double p_new = 0.0;
                    if (valid) {
                        const double z_value = z_packet_lo[lane];
                        const double p_value = p_packet_lo_old[lane];
                        p_new = z_value + beta * p_value;
                    }
                    p_packet_lo_new[lane] = p_new;
                    p_lanes[lane] = static_cast<VALUE_TYPE>(p_new);
                }
                if (double_packet_index + 1 < double_packet_count) {
                    const double_v8 z_packet_hi = Z[double_packet_index + 1];
                    const double_v8 p_packet_hi_old = P[double_packet_index + 1];
        update_p_compute_lanes_hi:
                    for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS pipeline II=1
                        const INDEX_TYPE index = (packet << 4) + lane + 8;
                        double p_new = 0.0;
                        if (index < Row_num) {
                            const double z_value = z_packet_hi[lane];
                            const double p_value = p_packet_hi_old[lane];
                            p_new = z_value + beta * p_value;
                        }
                        p_packet_hi_new[lane] = p_new;
                        p_lanes[lane + 8] = static_cast<VALUE_TYPE>(p_new);
                    }
                } else {
        update_p_pad_hi:
                    for (INDEX_TYPE lane = 8; lane < 16; ++lane) {
#pragma HLS unroll
                        p_lanes[lane] = 0.0f;
                    }
                }
                P[double_packet_index] = p_packet_lo_new;
                if (double_packet_index + 1 < double_packet_count) {
                    P[double_packet_index + 1] = p_packet_hi_new;
                }
                float_v16 p_packet;
        update_p_pack_p:
                for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
#pragma HLS unroll
                    p_packet[lane] = p_lanes[lane];
                }
                P_spmv[packet] = p_packet;
            }
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
    Metrics[8] = static_cast<double>(update_xr_work_packets);
    Metrics[9] = static_cast<double>(update_z_work_packets);
    Metrics[10] = static_cast<double>(update_p_work_packets);
    Metrics[11] = static_cast<double>(init_spmv_work_packets + init_zp_work_packets +
                                      iter_spmv_work_packets + update_xr_work_packets +
                                      update_z_work_packets + update_p_work_packets);
    Metrics[12] = static_cast<double>(Row_num);
    Metrics[13] = static_cast<double>(Max_iters);
    Metrics[14] = 0.0;  // p^T AP is fused into Metrics[7] / kPcgStageIterSpmv.
    Metrics[15] = static_cast<double>(double_packet_count);
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
