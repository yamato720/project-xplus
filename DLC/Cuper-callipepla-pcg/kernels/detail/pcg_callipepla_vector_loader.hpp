#pragma once

#include <tapa.h>

#include "pcg_callipepla_common.hpp"
#include "pcg_callipepla_trace.hpp"

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
    tapa::ostream<float_v16> &Vector_X_Stream
#ifdef CUPER_CALLIPEPLA_PROBE_VECTOR_LOADER_EVENTS
    ,
    tapa::ostream<PcgCallipeplaProbeEvent> &Probe_Event_out
#endif
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
    ,
    tapa::ostream<PcgCallipeplaDebugEvent> &Debug_Event_out
#endif
    ) {
    const INDEX_TYPE packet_count =
        pcg_callipepla_num_float_v16_packets(Column_num);
    const INDEX_TYPE double_packet_count =
        pcg_callipepla_num_double_v8_packets(Column_num);
#ifdef CUPER_CALLIPEPLA_PROBE_VECTOR_LOADER_EVENTS
    INDEX_TYPE command_count = 0;
    INDEX_TYPE hbm_word_count = 0;
    INDEX_TYPE probe_event_drop_count = 0;
    pcg_callipepla_probe_try_write_event(
        Probe_Event_out,
        probe_event_drop_count,
        kPcgCallipeplaProbeEventVectorStart,
        -1,
        0,
        0);
#endif

vector_loader_loop:
    for (;;) {
#pragma HLS loop_flatten off
        const PcgCallipeplaSpmvVectorCommand command = Command_in.read();
#ifdef CUPER_CALLIPEPLA_PROBE_VECTOR_LOADER_EVENTS
        ++command_count;
#endif
        if (command.stop != 0) {
#ifdef CUPER_CALLIPEPLA_PROBE_VECTOR_LOADER_EVENTS
            Probe_Event_out.write(pcg_callipepla_make_probe_event(
                kPcgCallipeplaProbeEventVectorStop,
                kPcgCallipeplaProbePhaseStop,
                command_count,
                pcg_callipepla_pack_loader_count_value(
                    hbm_word_count, probe_event_drop_count)));
#endif
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
            PcgCallipepla_DebugTryWrite(Debug_Event_out,
                                        kPcgCallipeplaTraceSourceVectorLoader,
                                        kPcgCallipeplaTracePhaseStop,
                                        command.vector_source,
                                        command.bank);
#endif
            return;
        }
#ifdef CUPER_CALLIPEPLA_PROBE_VECTOR_LOADER_EVENTS
        pcg_callipepla_probe_try_write_event(
            Probe_Event_out,
            probe_event_drop_count,
            kPcgCallipeplaProbeEventVectorCommandReceived,
            command.vector_source,
            command_count,
            command.bank);
#endif
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
        PcgCallipepla_DebugTryWrite(Debug_Event_out,
                                    kPcgCallipeplaTraceSourceVectorLoader,
                                    kPcgCallipeplaTracePhaseRecv,
                                    command.vector_source,
                                    command.bank);
#endif

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
#ifdef CUPER_CALLIPEPLA_PROBE_VECTOR_LOADER_EVENTS
                ++hbm_word_count;
#endif
                if (double_index + 1 < double_packet_count) {
                    hi = PcgCallipepla_ReadPBank(P0, P1, command.bank, double_index + 1);
#ifdef CUPER_CALLIPEPLA_PROBE_VECTOR_LOADER_EVENTS
                    ++hbm_word_count;
#endif
                }
            } else {
                lo = PcgCallipepla_ReadDoubleBank(X0, X1, command.bank, double_index);
#ifdef CUPER_CALLIPEPLA_PROBE_VECTOR_LOADER_EVENTS
                ++hbm_word_count;
#endif
                if (double_index + 1 < double_packet_count) {
                    hi = PcgCallipepla_ReadDoubleBank(X0, X1, command.bank, double_index + 1);
#ifdef CUPER_CALLIPEPLA_PROBE_VECTOR_LOADER_EVENTS
                    ++hbm_word_count;
#endif
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
#ifdef CUPER_CALLIPEPLA_PROBE_VECTOR_LOADER_EVENTS
            if ((packet & 0x0fff) == 0x0fff) {
                pcg_callipepla_probe_try_write_event(
                    Probe_Event_out,
                    probe_event_drop_count,
                    kPcgCallipeplaProbeEventVectorHbmProgress,
                    command.vector_source,
                    command_count,
                    pcg_callipepla_pack_loader_count_value(
                        hbm_word_count, probe_event_drop_count));
            }
#endif
        }
#ifdef CUPER_CALLIPEPLA_PROBE_VECTOR_LOADER_EVENTS
        pcg_callipepla_probe_try_write_event(
            Probe_Event_out,
            probe_event_drop_count,
            kPcgCallipeplaProbeEventVectorRoundDone,
            command.vector_source,
            command_count,
            pcg_callipepla_pack_loader_count_value(
                hbm_word_count, probe_event_drop_count));
#endif
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
        PcgCallipepla_DebugTryWrite(Debug_Event_out,
                                    kPcgCallipeplaTraceSourceVectorLoader,
                                    kPcgCallipeplaTracePhaseDone,
                                    command.vector_source,
                                    packet_count);
#endif
    }
}
