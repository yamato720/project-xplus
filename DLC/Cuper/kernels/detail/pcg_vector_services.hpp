#pragma once

// Private implementation header for kernels/Cuper.cpp.
// It contains task definitions and should not be included by another translation unit.

#include <ap_int.h>
#include <tapa.h>

#include "pcg_common.hpp"

// update_z 的 HBM reader。它只负责把 R/M_inv 读成 512-bit stream；
// FP64 乘法、归约和 Z 写回都在后续 task，便于综合/布局单独观察各段压力。
void Pcg_UpdateZ_Read_Service(tapa::istream<PcgVectorCommand> &Command_in,
                              tapa::ostream<PcgVectorCommand> &Compute_Command_out,
                              tapa::ostream<PcgVectorCommand> &Write_Command_out,
                              tapa::ostream<PcgUpdateZReadPacket> &Packet_out,
                              tapa::mmap<double_v8> R,
                              tapa::mmap<double_v8> M_inv) {
    for (;;) {
#pragma HLS loop_flatten off
        const PcgVectorCommand command = Command_in.read();
        Compute_Command_out.write(command);
        Write_Command_out.write(command);
        if (command.stop != 0) {
            return;
        }

        const INDEX_TYPE row_num = command.row_num;
        const INDEX_TYPE double_packet_count = pcg_num_double_v8_packets(row_num);

    update_z_read_packets:
        for (INDEX_TYPE packet = 0; packet < double_packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=1000000
#pragma HLS pipeline II=1
            PcgUpdateZReadPacket out;
            out.r = R[packet];
            out.minv = M_inv[packet];
            Packet_out.write(out);
        }
    }
}

// update_z 的纯计算 task。它不直接访问 HBM，和 Read/Write 拆开后可以独立看
// FP64 乘法、归约和 FIFO 压力。若做 SLR floorplan，优先保持整条 update_z 链同 SLR，
// 避免宽 FIFO 跨 SLR 形成局部 SLL 拥塞。
void Pcg_UpdateZ_Compute_Service(tapa::istream<PcgVectorCommand> &Command_in,
                                 tapa::istream<PcgUpdateZReadPacket> &Packet_in,
                                 tapa::ostream<PcgUpdateZWritePacket> &Packet_out,
                                 tapa::ostream<PcgUpdateZResult> &Result_out) {
    double rz_acc[8][kPcgReductionBanks];
    double rr_acc[8][kPcgReductionBanks];
#pragma HLS array_partition variable=rz_acc complete
#pragma HLS array_partition variable=rr_acc complete

    for (;;) {
#pragma HLS loop_flatten off
        const PcgVectorCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }

        const INDEX_TYPE row_num = command.row_num;
        const INDEX_TYPE double_packet_count = pcg_num_double_v8_packets(row_num);

    update_z_clear_acc_bank:
        for (INDEX_TYPE bank = 0; bank < kPcgReductionBanks; ++bank) {
#pragma HLS unroll
    update_z_clear_acc_lane:
            for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                rz_acc[lane][bank] = 0.0;
                rr_acc[lane][bank] = 0.0;
            }
        }

    update_z_compute_packets:
        for (INDEX_TYPE packet = 0; packet < double_packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=1000000
#pragma HLS pipeline II=1
            const INDEX_TYPE reduction_bank = packet & (kPcgReductionBanks - 1);
            const PcgUpdateZReadPacket in = Packet_in.read();
            PcgUpdateZWritePacket out;
    update_z_compute_lanes:
            for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                const INDEX_TYPE index = (packet << 3) + lane;
                double z_new = 0.0;
                double rz_term = 0.0;
                double rr_term = 0.0;
                if (index < row_num) {
                    const double r_new = in.r[lane];
                    const double minv_value = in.minv[lane];
                    z_new = minv_value * r_new;
                    rz_term = r_new * z_new;
                    rr_term = r_new * r_new;
                }
                rz_acc[lane][reduction_bank] += rz_term;
                rr_acc[lane][reduction_bank] += rr_term;
                out.z[lane] = z_new;
            }
            Packet_out.write(out);
        }

        double rz_new = 0.0;
        double rr_new = 0.0;
    update_z_reduce_acc_lane:
        for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
    update_z_reduce_acc_bank:
            for (INDEX_TYPE bank = 0; bank < kPcgReductionBanks; ++bank) {
#pragma HLS unroll
                rz_new += rz_acc[lane][bank];
                rr_new += rr_acc[lane][bank];
            }
        }

        PcgUpdateZResult result;
        result.rz = rz_new;
        result.rr = rr_new;
        result.breakdown = (pcg_invalid(rz_new) || pcg_invalid(rr_new)) ? 1 : 0;
        Result_out.write(result);
    }
}

// update_z 的 HBM writer。它只把 compute 输出写回 Z。
void Pcg_UpdateZ_Write_Service(tapa::istream<PcgVectorCommand> &Command_in,
                               tapa::istream<PcgUpdateZWritePacket> &Packet_in,
                               tapa::istream<PcgUpdateZResult> &Result_in,
                               tapa::ostream<PcgUpdateZResult> &Result_out,
                               tapa::mmap<double_v8> Z) {
    for (;;) {
#pragma HLS loop_flatten off
        const PcgVectorCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }

        const INDEX_TYPE row_num = command.row_num;
        const INDEX_TYPE double_packet_count = pcg_num_double_v8_packets(row_num);

    update_z_write_packets:
        for (INDEX_TYPE packet = 0; packet < double_packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=1000000
#pragma HLS pipeline II=1
            const PcgUpdateZWritePacket in = Packet_in.read();
            Z[packet] = in.z;
        }

        // controller 只能在 Z 已全部写回后继续发 update_p，否则 update_p reader
        // 可能读到 writer FIFO 尾部尚未落到 HBM 的旧 Z。
        Result_out.write(Result_in.read());
    }
}

// update_p 的 HBM reader。一个输出包对应一包 P_spmv，因此内部读
// 最多两个 double_v8 的 Z/P。
void Pcg_UpdateP_Read_Service(tapa::istream<PcgVectorCommand> &Command_in,
                              tapa::ostream<PcgVectorCommand> &Compute_Command_out,
                              tapa::ostream<PcgVectorCommand> &Write_Command_out,
                              tapa::ostream<PcgUpdatePReadPacket> &Packet_out,
                              tapa::mmap<double_v8> Z,
                              tapa::mmap<double_v8> P) {
    for (;;) {
#pragma HLS loop_flatten off
        const PcgVectorCommand command = Command_in.read();
        Compute_Command_out.write(command);
        Write_Command_out.write(command);
        if (command.stop != 0) {
            return;
        }

        const INDEX_TYPE row_num = command.row_num;
        const INDEX_TYPE packet_count = pcg_num_float_v16_packets(row_num);
        const INDEX_TYPE double_packet_count = pcg_num_double_v8_packets(row_num);

    update_p_read_packets:
        for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
            const INDEX_TYPE double_packet_index = packet << 1;
            PcgUpdatePReadPacket out;
            out.z_lo = Z[double_packet_index];
            out.p_lo = P[double_packet_index];
            out.has_hi = (double_packet_index + 1 < double_packet_count) ? 1 : 0;
            if (out.has_hi != 0) {
                out.z_hi = Z[double_packet_index + 1];
                out.p_hi = P[double_packet_index + 1];
            }
            Packet_out.write(out);
        }
    }
}

// update_p 的纯计算 task。它只做 p = z + beta * p 和 FP32 packed 副本生成。
// 若做 SLR floorplan，优先保持整条 update_p 链同 SLR，避免 Read/Write 与
// Compute 之间的宽 FIFO 跨 SLR。
void Pcg_UpdateP_Compute_Service(tapa::istream<PcgVectorCommand> &Command_in,
                                 tapa::istream<PcgUpdatePReadPacket> &Packet_in,
                                 tapa::ostream<PcgUpdatePWritePacket> &Packet_out) {
    for (;;) {
#pragma HLS loop_flatten off
        const PcgVectorCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }

        const INDEX_TYPE row_num = command.row_num;
        const double beta = command.scalar;
        const INDEX_TYPE packet_count = pcg_num_float_v16_packets(row_num);

    update_p_compute_packets:
        for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
            const PcgUpdatePReadPacket in = Packet_in.read();
            PcgUpdatePWritePacket out;
            VALUE_TYPE p_lanes[16];
#pragma HLS array_partition variable=p_lanes complete
    update_p_compute_lanes:
            for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                const INDEX_TYPE index = (packet << 4) + lane;
                const bool valid = index < row_num;
                double p_new = 0.0;
                if (valid) {
                    const double z_value = in.z_lo[lane];
                    const double p_value = in.p_lo[lane];
                    p_new = z_value + beta * p_value;
                }
                out.p_lo[lane] = p_new;
                p_lanes[lane] = static_cast<VALUE_TYPE>(p_new);
            }
            out.has_hi = in.has_hi;
            if (in.has_hi != 0) {
    update_p_compute_lanes_hi:
                for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                    const INDEX_TYPE index = (packet << 4) + lane + 8;
                    double p_new = 0.0;
                    if (index < row_num) {
                        const double z_value = in.z_hi[lane];
                        const double p_value = in.p_hi[lane];
                        p_new = z_value + beta * p_value;
                    }
                    out.p_hi[lane] = p_new;
                    p_lanes[lane + 8] = static_cast<VALUE_TYPE>(p_new);
                }
            } else {
    update_p_pad_hi:
                for (INDEX_TYPE lane = 8; lane < 16; ++lane) {
#pragma HLS unroll
                    p_lanes[lane] = 0.0f;
                }
            }
    update_p_pack_p:
            for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
#pragma HLS unroll
                out.p_spmv[lane] = p_lanes[lane];
            }
            Packet_out.write(out);
        }
    }
}

// update_p 的 HBM writer。它更新 FP64 P 和下一轮 SpMV 所需 P_spmv。
void Pcg_UpdateP_Write_Service(tapa::istream<PcgVectorCommand> &Command_in,
                               tapa::istream<PcgUpdatePWritePacket> &Packet_in,
                               tapa::ostream<INDEX_TYPE> &Done_out,
                               tapa::mmap<double_v8> P,
                               tapa::mmap<float_v16> P_spmv) {
    for (;;) {
#pragma HLS loop_flatten off
        const PcgVectorCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }

        const INDEX_TYPE row_num = command.row_num;
        const INDEX_TYPE packet_count = pcg_num_float_v16_packets(row_num);

    update_p_write_packets:
        for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
            const INDEX_TYPE double_packet_index = packet << 1;
            const PcgUpdatePWritePacket in = Packet_in.read();
            P[double_packet_index] = in.p_lo;
            if (in.has_hi != 0) {
                P[double_packet_index + 1] = in.p_hi;
            }
            P_spmv[packet] = in.p_spmv;
        }

        Done_out.write(1);
    }
}
