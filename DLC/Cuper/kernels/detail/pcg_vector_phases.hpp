#pragma once

// Private implementation header for kernels/Cuper.cpp.
// It contains task definitions and should not be included by another translation unit.

#include <ap_int.h>
#include <tapa.h>

#include "pcg_common.hpp"

struct PcgVectorCommand {
    // controller 发给向量 worker 的阶段命令。stop 非 0 表示有限退出。
    // alpha/beta 只在对应 update 阶段有效；其它阶段保留为 0，便于波形排查。
    INDEX_TYPE phase;
    INDEX_TYPE stop;
    double alpha;
    double beta;
};

struct PcgVectorResult {
    // 每条非 stop command 都回一条 result，controller 用它作为阶段完成边界。
    // init_zp/update_rz_reduce 返回 rz/rr，iter_dot 返回 p_ap。
    INDEX_TYPE phase;
    double rz;
    double rr;
    double p_ap;
};

static constexpr INDEX_TYPE kPcgVectorPhaseInitSpmv = 0;
static constexpr INDEX_TYPE kPcgVectorPhaseInitZp = 1;
static constexpr INDEX_TYPE kPcgVectorPhaseIterDot = 2;
static constexpr INDEX_TYPE kPcgVectorPhaseUpdateX = 3;
static constexpr INDEX_TYPE kPcgVectorPhaseUpdateRzReduce = 4;
static constexpr INDEX_TYPE kPcgVectorPhaseUpdateP = 5;

inline PcgVectorCommand pcg_make_vector_command(const INDEX_TYPE phase,
                                                const double alpha,
                                                const double beta) {
#pragma HLS inline
    PcgVectorCommand command;
    command.phase = phase;
    command.stop = 0;
    command.alpha = alpha;
    command.beta = beta;
    return command;
}

inline PcgVectorCommand pcg_make_vector_stop_command() {
#pragma HLS inline
    PcgVectorCommand command;
    command.phase = kPcgVectorPhaseInitSpmv;
    command.stop = 1;
    command.alpha = 0.0;
    command.beta = 0.0;
    return command;
}

inline PcgVectorResult pcg_make_vector_result(const INDEX_TYPE phase) {
#pragma HLS inline
    PcgVectorResult result;
    result.phase = phase;
    result.rz = 0.0;
    result.rr = 0.0;
    result.p_ap = 0.0;
    return result;
}

// CuperPcg 向量阶段 worker。
//
// controller 只负责发 SpMV 命令、计算 alpha/beta、收敛判断和写回 status/metrics；
// 这里常驻等待 PcgVectorCommand，接管所有大段 HBM 向量访问：
//   - init_spmv       : 消费 A*x0 stream，读 B，写 R。
//   - init_zp         : 读 R/M_inv，写 Z/P/P_spmv，返回 rz/rr。
//   - iter_dot        : 消费 A*p stream，写 AP_spmv，读 P，返回 p_ap。
//   - update_x        : 读 X/P，写 X。
//   - update_rz_reduce: 读 R/AP_spmv/M_inv，写 R/Z，返回 rz_new/rr_new。
//   - update_p        : 读 Z/P，写 P/P_spmv。
void Pcg_Vector_Phases(tapa::istream<PcgVectorCommand> &Command_in,
                       tapa::ostream<PcgVectorResult> &Result_out,
                       tapa::istream<float_v16> &Spmv_in,
                       tapa::mmap<double_v8> B,
                       tapa::mmap<double_v8> M_inv,
                       tapa::mmap<double_v8> X,
                       tapa::mmap<double_v8> R,
                       tapa::mmap<double_v8> Z,
                       tapa::mmap<double_v8> P,
                       tapa::mmap<float_v16> AP_spmv,
                       tapa::mmap<float_v16> P_spmv,
                       const INDEX_TYPE Row_num) {
    const INDEX_TYPE packet_count =
        Row_num > 0 ? pcg_num_float_v16_packets(Row_num) : 0;
    const INDEX_TYPE double_packet_count =
        Row_num > 0 ? pcg_num_double_v8_packets(Row_num) : 0;

vector_phase_loop:
    for (;;) {
#pragma HLS loop_flatten off
        const PcgVectorCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }

        PcgVectorResult result = pcg_make_vector_result(command.phase);

        if (command.phase == kPcgVectorPhaseInitSpmv) {
    init_spmv_stream:
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
                                const double ap_value =
                                    static_cast<double>(ap_lanes[lane + 8]);
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
            Result_out.write(result);
        } else if (command.phase == kPcgVectorPhaseInitZp) {
            double rz = 0.0;
            double rr = 0.0;
    init_zp_reduce:
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
            result.rz = rz;
            result.rr = rr;
            Result_out.write(result);
        } else if (command.phase == kPcgVectorPhaseIterDot) {
            double p_ap = 0.0;
    iter_spmv_stream:
            for (INDEX_TYPE received_packets = 0; received_packets < packet_count;) {
#pragma HLS loop_tripcount min=1 max=500000
                if (!Spmv_in.empty()) {
                    const INDEX_TYPE packet = received_packets;
                    float_v16 ap_packet;
                    Spmv_in.try_read(ap_packet);
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
                            p_ap += p_packet_lo[lane] *
                                    static_cast<double>(ap_lanes[lane]);
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
            result.p_ap = p_ap;
            Result_out.write(result);
        } else if (command.phase == kPcgVectorPhaseUpdateX) {
            const double alpha = command.alpha;
    update_x:
            for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
                const INDEX_TYPE double_packet_index = packet << 1;
                const double_v8 x_packet_lo = X[double_packet_index];
                const double_v8 p_packet_lo = P[double_packet_index];
                double_v8 x_new_packet_lo;
                double_v8 x_new_packet_hi;
        update_x_compute_lanes:
                for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS pipeline II=1
                    const INDEX_TYPE index = (packet << 4) + lane;
                    double x_new = 0.0;
                    if (index < Row_num) {
                        const double x_value = x_packet_lo[lane];
                        const double p_value = p_packet_lo[lane];
                        x_new = x_value + alpha * p_value;
                    }
                    x_new_packet_lo[lane] = x_new;
                }
                if (double_packet_index + 1 < double_packet_count) {
                    const double_v8 x_packet_hi = X[double_packet_index + 1];
                    const double_v8 p_packet_hi = P[double_packet_index + 1];
        update_x_compute_lanes_hi:
                    for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS pipeline II=1
                        const INDEX_TYPE index = (packet << 4) + lane + 8;
                        double x_new = 0.0;
                        if (index < Row_num) {
                            const double x_value = x_packet_hi[lane];
                            const double p_value = p_packet_hi[lane];
                            x_new = x_value + alpha * p_value;
                        }
                        x_new_packet_hi[lane] = x_new;
                    }
                }
                X[double_packet_index] = x_new_packet_lo;
                if (double_packet_index + 1 < double_packet_count) {
                    X[double_packet_index + 1] = x_new_packet_hi;
                }
            }
            Result_out.write(result);
        } else if (command.phase == kPcgVectorPhaseUpdateRzReduce) {
            const double alpha = command.alpha;
            double rz_new = 0.0;
            double rr_new = 0.0;
    update_rz_reduce:
            for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
                const float_v16 ap_packet = AP_spmv[packet];
                const INDEX_TYPE double_packet_index = packet << 1;
                const double_v8 r_packet_lo = R[double_packet_index];
                const double_v8 minv_packet_lo = M_inv[double_packet_index];
                double_v8 r_new_packet_lo;
                double_v8 z_packet_lo;
                double_v8 r_new_packet_hi;
                double_v8 z_packet_hi;
                VALUE_TYPE ap_lanes[16];
#pragma HLS array_partition variable=ap_lanes complete
        update_rz_unpack_ap:
                for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
#pragma HLS unroll
                    ap_lanes[lane] = ap_packet[lane];
                }
        update_rz_lanes:
                for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS pipeline II=1
                    const INDEX_TYPE index = (packet << 4) + lane;
                    double r_new = 0.0;
                    double z_new = 0.0;
                    if (index < Row_num) {
                        const double r_value = r_packet_lo[lane];
                        const double ap_value = static_cast<double>(ap_lanes[lane]);
                        const double minv_value = minv_packet_lo[lane];
                        r_new = r_value - alpha * ap_value;
                        z_new = minv_value * r_new;
                        rz_new += r_new * z_new;
                        rr_new += r_new * r_new;
                    }
                    r_new_packet_lo[lane] = r_new;
                    z_packet_lo[lane] = z_new;
                }
                if (double_packet_index + 1 < double_packet_count) {
                    const double_v8 r_packet_hi = R[double_packet_index + 1];
                    const double_v8 minv_packet_hi = M_inv[double_packet_index + 1];
        update_rz_lanes_hi:
                    for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS pipeline II=1
                        const INDEX_TYPE index = (packet << 4) + lane + 8;
                        double r_new = 0.0;
                        double z_new = 0.0;
                        if (index < Row_num) {
                            const double r_value = r_packet_hi[lane];
                            const double ap_value =
                                static_cast<double>(ap_lanes[lane + 8]);
                            const double minv_value = minv_packet_hi[lane];
                            r_new = r_value - alpha * ap_value;
                            z_new = minv_value * r_new;
                            rz_new += r_new * z_new;
                            rr_new += r_new * r_new;
                        }
                        r_new_packet_hi[lane] = r_new;
                        z_packet_hi[lane] = z_new;
                    }
                }
                R[double_packet_index] = r_new_packet_lo;
                Z[double_packet_index] = z_packet_lo;
                if (double_packet_index + 1 < double_packet_count) {
                    R[double_packet_index + 1] = r_new_packet_hi;
                    Z[double_packet_index + 1] = z_packet_hi;
                }
            }
            result.rz = rz_new;
            result.rr = rr_new;
            Result_out.write(result);
        } else if (command.phase == kPcgVectorPhaseUpdateP) {
            const double beta = command.beta;
    update_p:
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
                    double p_new = 0.0;
                    if (index < Row_num) {
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
            Result_out.write(result);
        } else {
            Result_out.write(result);
        }
    }
}
