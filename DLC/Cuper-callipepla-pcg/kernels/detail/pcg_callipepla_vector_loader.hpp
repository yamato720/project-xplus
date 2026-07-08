#pragma once

#include <tapa.h>

#include "pcg_callipepla_common.hpp"

inline double_v8 PcgCallipepla_ReadDoubleBank(tapa::mmap<double_v8> &X0,
                                              tapa::mmap<double_v8> &X1,
                                              const INDEX_TYPE bank,
                                              const INDEX_TYPE index) {
#pragma HLS inline
    return bank == 0 ? X0[index] : X1[index];
}

inline double_v8 PcgCallipepla_ReadPBank(tapa::mmap<double_v8> &P0,
                                         tapa::mmap<double_v8> &P1,
                                         const INDEX_TYPE bank,
                                         const INDEX_TYPE index) {
#pragma HLS inline
    return bank == 0 ? P0[index] : P1[index];
}

void PcgCallipepla_Vector_Loader(
    const INDEX_TYPE Batch_num,
    const INDEX_TYPE Column_num,
    tapa::mmap<double_v8> X0,
    tapa::mmap<double_v8> X1,
    tapa::mmap<double_v8> P0,
    tapa::mmap<double_v8> P1,
    tapa::istream<PcgCallipeplaSpmvVectorCommand> &Command_in,
    tapa::ostream<float_v16> &Vector_X_Stream) {
    const INDEX_TYPE packet_count =
        pcg_callipepla_num_float_v16_packets(Column_num);
    const INDEX_TYPE double_packet_count =
        pcg_callipepla_num_double_v8_packets(Column_num);

vector_loader_loop:
    for (;;) {
#pragma HLS loop_flatten off
        const PcgCallipeplaSpmvVectorCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }

        if (Batch_num == 0) {
            continue;
        }

    pack_double_to_float:
        for (INDEX_TYPE packet = 0; packet < packet_count; ++packet) {
#pragma HLS loop_tripcount min=1 max=500000
            const INDEX_TYPE double_index = packet << 1;
            double_v8 lo;
            double_v8 hi;
            if (command.vector_source == kPcgCallipeplaVectorSourceP) {
                lo = PcgCallipepla_ReadPBank(P0, P1, command.bank, double_index);
                if (double_index + 1 < double_packet_count) {
                    hi = PcgCallipepla_ReadPBank(P0, P1, command.bank, double_index + 1);
                }
            } else {
                lo = PcgCallipepla_ReadDoubleBank(X0, X1, command.bank, double_index);
                if (double_index + 1 < double_packet_count) {
                    hi = PcgCallipepla_ReadDoubleBank(X0, X1, command.bank, double_index + 1);
                }
            }

            float_v16 out;
        pack_lo_lanes:
            for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                const INDEX_TYPE index = (packet << 4) + lane;
                out[lane] = index < Column_num ? static_cast<VALUE_TYPE>(lo[lane]) : 0.0f;
            }
        pack_hi_lanes:
            for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                const INDEX_TYPE index = (packet << 4) + lane + 8;
                out[lane + 8] =
                    (double_index + 1 < double_packet_count && index < Column_num)
                        ? static_cast<VALUE_TYPE>(hi[lane])
                        : 0.0f;
            }
            Vector_X_Stream.write(out);
        }
    }
}
