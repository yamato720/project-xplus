#pragma once

#include <tapa.h>

#include "pcg_callipepla_common.hpp"

inline double_v8 PcgCallipepla_ReadBank(tapa::mmap<double_v8> &B0,
                                        tapa::mmap<double_v8> &B1,
                                        const INDEX_TYPE bank,
                                        const INDEX_TYPE index) {
#pragma HLS inline
    return bank == 0 ? B0[index] : B1[index];
}

inline void PcgCallipepla_WriteBank(tapa::mmap<double_v8> &B0,
                                    tapa::mmap<double_v8> &B1,
                                    const INDEX_TYPE bank,
                                    const INDEX_TYPE index,
                                    const double_v8 value) {
#pragma HLS inline
    if (bank == 0) {
        B0[index] = value;
    } else {
        B1[index] = value;
    }
}

void PcgCallipepla_Vector_Phases(
    tapa::istream<PcgCallipeplaVectorCommand> &Command_in,
    tapa::ostream<PcgCallipeplaVectorResult> &Result_out,
    tapa::istream<float_v16> &Spmv_in,
    tapa::mmap<double_v8> X0,
    tapa::mmap<double_v8> X1,
    tapa::mmap<double_v8> P0,
    tapa::mmap<double_v8> P1,
    tapa::mmap<float_v16> AP,
    tapa::mmap<double_v8> R0,
    tapa::mmap<double_v8> R1,
    tapa::mmap<double_v8> M_inv,
    const INDEX_TYPE Row_num) {
    const INDEX_TYPE float_packet_count =
        Row_num > 0 ? pcg_callipepla_num_float_v16_packets(Row_num) : 0;
    const INDEX_TYPE double_packet_count =
        Row_num > 0 ? pcg_callipepla_num_double_v8_packets(Row_num) : 0;

vector_phase_loop:
    for (;;) {
#pragma HLS loop_flatten off
        const PcgCallipeplaVectorCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }

        PcgCallipeplaVectorResult result =
            pcg_callipepla_make_vector_result(command.phase);

        if (command.phase == kPcgCallipeplaPhaseInitSpmv) {
        init_spmv:
            for (INDEX_TYPE packet = 0; packet < float_packet_count;) {
#pragma HLS loop_tripcount min=1 max=500000
                if (!Spmv_in.empty()) {
                    float_v16 ap_packet;
                    Spmv_in.try_read(ap_packet);
                    AP[packet] = ap_packet;
                    const INDEX_TYPE double_index = packet << 1;
                    const double_v8 b_lo =
                        PcgCallipepla_ReadBank(R0, R1, command.r_read_bank, double_index);
                    double_v8 r_lo;
                    double_v8 r_hi;
                init_r_lo:
                    for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                        const INDEX_TYPE index = (packet << 4) + lane;
                        r_lo[lane] = index < Row_num
                                         ? b_lo[lane] - static_cast<double>(ap_packet[lane])
                                         : 0.0;
                    }
                    if (double_index + 1 < double_packet_count) {
                        const double_v8 b_hi =
                            PcgCallipepla_ReadBank(R0, R1, command.r_read_bank, double_index + 1);
                    init_r_hi:
                        for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                            const INDEX_TYPE index = (packet << 4) + lane + 8;
                            r_hi[lane] =
                                index < Row_num
                                    ? b_hi[lane] - static_cast<double>(ap_packet[lane + 8])
                                    : 0.0;
                        }
                    }
                    PcgCallipepla_WriteBank(R0, R1, command.r_write_bank, double_index, r_lo);
                    if (double_index + 1 < double_packet_count) {
                        PcgCallipepla_WriteBank(R0, R1, command.r_write_bank, double_index + 1, r_hi);
                    }
                    ++packet;
                }
            }
            Result_out.write(result);
        } else if (command.phase == kPcgCallipeplaPhaseInitZp) {
            double rz = 0.0;
            double rr = 0.0;
        init_zp:
            for (INDEX_TYPE packet = 0; packet < double_packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
                const double_v8 r_packet =
                    PcgCallipepla_ReadBank(R0, R1, command.r_read_bank, packet);
                const double_v8 minv_packet = M_inv[packet];
                double_v8 p_packet;
            init_zp_lanes:
                for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                    const INDEX_TYPE index = (packet << 3) + lane;
                    double z_value = 0.0;
                    if (index < Row_num) {
                        const double r_value = r_packet[lane];
                        z_value = minv_packet[lane] * r_value;
                        rz += r_value * z_value;
                        rr += r_value * r_value;
                    }
                    p_packet[lane] = z_value;
                }
                PcgCallipepla_WriteBank(P0, P1, command.p_write_bank, packet, p_packet);
            }
            result.rz = rz;
            result.rr = rr;
            Result_out.write(result);
        } else if (command.phase == kPcgCallipeplaPhaseIterDot) {
            double p_ap = 0.0;
        iter_dot:
            for (INDEX_TYPE packet = 0; packet < float_packet_count;) {
#pragma HLS loop_tripcount min=1 max=500000
                if (!Spmv_in.empty()) {
                    float_v16 ap_packet;
                    Spmv_in.try_read(ap_packet);
                    AP[packet] = ap_packet;
                    const INDEX_TYPE double_index = packet << 1;
                    const double_v8 p_lo =
                        PcgCallipepla_ReadBank(P0, P1, command.p_read_bank, double_index);
                dot_lo:
                    for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                        const INDEX_TYPE index = (packet << 4) + lane;
                        if (index < Row_num) {
                            p_ap += p_lo[lane] * static_cast<double>(ap_packet[lane]);
                        }
                    }
                    if (double_index + 1 < double_packet_count) {
                        const double_v8 p_hi =
                            PcgCallipepla_ReadBank(P0, P1, command.p_read_bank, double_index + 1);
                    dot_hi:
                        for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                            const INDEX_TYPE index = (packet << 4) + lane + 8;
                            if (index < Row_num) {
                                p_ap += p_hi[lane] *
                                        static_cast<double>(ap_packet[lane + 8]);
                            }
                        }
                    }
                    ++packet;
                }
            }
            result.p_ap = p_ap;
            Result_out.write(result);
        } else if (command.phase == kPcgCallipeplaPhaseUpdateX) {
            const double alpha = command.alpha;
        update_x:
            for (INDEX_TYPE packet = 0; packet < double_packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
                const double_v8 x_old =
                    PcgCallipepla_ReadBank(X0, X1, command.x_read_bank, packet);
                const double_v8 p_packet =
                    PcgCallipepla_ReadBank(P0, P1, command.p_read_bank, packet);
                double_v8 x_new;
            update_x_lanes:
                for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                    const INDEX_TYPE index = (packet << 3) + lane;
                    x_new[lane] =
                        index < Row_num ? x_old[lane] + alpha * p_packet[lane] : 0.0;
                }
                PcgCallipepla_WriteBank(X0, X1, command.x_write_bank, packet, x_new);
            }
            Result_out.write(result);
        } else if (command.phase == kPcgCallipeplaPhaseUpdateR) {
            const double alpha = command.alpha;
        update_r:
            for (INDEX_TYPE packet = 0; packet < float_packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
                const float_v16 ap_packet = AP[packet];
                const INDEX_TYPE double_index = packet << 1;
                const double_v8 r_lo_old =
                    PcgCallipepla_ReadBank(R0, R1, command.r_read_bank, double_index);
                double_v8 r_lo_new;
                double_v8 r_hi_new;
            update_r_lo:
                for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                    const INDEX_TYPE index = (packet << 4) + lane;
                    r_lo_new[lane] =
                        index < Row_num
                            ? r_lo_old[lane] - alpha * static_cast<double>(ap_packet[lane])
                            : 0.0;
                }
                if (double_index + 1 < double_packet_count) {
                    const double_v8 r_hi_old =
                        PcgCallipepla_ReadBank(R0, R1, command.r_read_bank, double_index + 1);
                update_r_hi:
                    for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                        const INDEX_TYPE index = (packet << 4) + lane + 8;
                        r_hi_new[lane] =
                            index < Row_num
                                ? r_hi_old[lane] -
                                      alpha * static_cast<double>(ap_packet[lane + 8])
                                : 0.0;
                    }
                }
                PcgCallipepla_WriteBank(R0, R1, command.r_write_bank, double_index, r_lo_new);
                if (double_index + 1 < double_packet_count) {
                    PcgCallipepla_WriteBank(R0, R1, command.r_write_bank, double_index + 1, r_hi_new);
                }
            }
            Result_out.write(result);
        } else if (command.phase == kPcgCallipeplaPhaseApplyMInvDot) {
            double rz = 0.0;
            double rr = 0.0;
        apply_m_inv:
            for (INDEX_TYPE packet = 0; packet < double_packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
                const double_v8 r_packet =
                    PcgCallipepla_ReadBank(R0, R1, command.r_read_bank, packet);
                const double_v8 minv_packet = M_inv[packet];
                double_v8 z_packet;
            apply_m_inv_lanes:
                for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                    const INDEX_TYPE index = (packet << 3) + lane;
                    double z_value = 0.0;
                    if (index < Row_num) {
                        const double r_value = r_packet[lane];
                        z_value = minv_packet[lane] * r_value;
                        rz += r_value * z_value;
                        rr += r_value * r_value;
                    }
                    z_packet[lane] = z_value;
                }
                // The new ABI has no Z port.  P[p_write_bank] is a temporary Z
                // buffer until update_p overwrites it with the next P vector.
                PcgCallipepla_WriteBank(P0, P1, command.p_write_bank, packet, z_packet);
            }
            result.rz = rz;
            result.rr = rr;
            Result_out.write(result);
        } else if (command.phase == kPcgCallipeplaPhaseUpdateP) {
            const double beta = command.beta;
        update_p:
            for (INDEX_TYPE packet = 0; packet < double_packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
                const double_v8 z_packet =
                    PcgCallipepla_ReadBank(P0, P1, command.p_write_bank, packet);
                const double_v8 p_old =
                    PcgCallipepla_ReadBank(P0, P1, command.p_read_bank, packet);
                double_v8 p_new;
            update_p_lanes:
                for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                    const INDEX_TYPE index = (packet << 3) + lane;
                    p_new[lane] =
                        index < Row_num ? z_packet[lane] + beta * p_old[lane] : 0.0;
                }
                PcgCallipepla_WriteBank(P0, P1, command.p_write_bank, packet, p_new);
            }
            Result_out.write(result);
        } else {
            Result_out.write(result);
        }
    }
}
