#pragma once

#include <tapa.h>

#include "cuper_spmv_tasks.hpp"
#include "pcg_callipepla_common.hpp"
#include "pcg_callipepla_trace.hpp"

void PcgCallipepla_DestroyInt(tapa::istream<INDEX_TYPE> &PE_Param) {
    for (;;) {
#pragma HLS pipeline II=1
        const INDEX_TYPE tmp = PE_Param.read();
        if (tmp == kSpmvServiceStopToken) {
            return;
        }
    }
}

void PcgCallipepla_DestroyFloatV16(const INDEX_TYPE Batch_num,
                                   const INDEX_TYPE Column_num,
                                   tapa::istream<float_v16> &Vector_X_Stream,
                                   tapa::istream<INDEX_TYPE> &Expected_Rounds_in) {
    const INDEX_TYPE packet_count =
        Batch_num > 0 ? pcg_callipepla_num_float_v16_packets(Column_num) : 0;
    unsigned long long expected_packets = 0;
    unsigned long long drained_packets = 0;
    bool expected_known = false;

drain_vector_tail:
    for (;;) {
#pragma HLS pipeline II=1
        if (!Vector_X_Stream.empty()) {
            float_v16 tmp;
            Vector_X_Stream.try_read(tmp);
            ++drained_packets;
        }
        if (!expected_known && !Expected_Rounds_in.empty()) {
            INDEX_TYPE expected_rounds = 0;
            Expected_Rounds_in.try_read(expected_rounds);
            expected_packets =
                static_cast<unsigned long long>(packet_count) *
                static_cast<unsigned long long>(expected_rounds);
            expected_known = true;
        }
        if (expected_known && drained_packets >= expected_packets) {
            return;
        }
    }
}

void PcgCallipepla_Vector_Checker(
    const INDEX_TYPE Row_num,
    tapa::istreams<float_v2, HBM_CHANNEL_NUM_DIV_8> &Vector_Y_Stream,
    tapa::ostream<float_v2> &Vector_Y_Stream_Aftck,
    tapa::istream<INDEX_TYPE> &Stop_in,
    const INDEX_TYPE Debug_channel
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
    ,
    tapa::ostream<PcgCallipeplaDebugEvent> &Debug_Event_out
#endif
    ) {
    const INDEX_TYPE num_pe_output = pcg_callipepla_num_checker_pe_outputs(Row_num);
    const INDEX_TYPE num_out = pcg_callipepla_num_float_v16_packets(Row_num);
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
    const INDEX_TYPE Trace_source =
        Debug_channel == 0 ? kPcgCallipeplaTraceSourceChecker0 :
        Debug_channel == 7 ? kPcgCallipeplaTraceSourceChecker7 : -1;
#endif

checker_rounds:
    for (;;) {
#pragma HLS loop_flatten off
    wait_round:
        for (;;) {
#pragma HLS pipeline II=1
            if (!Vector_Y_Stream[0].empty()) {
                break;
            }
            if (!Stop_in.empty()) {
                INDEX_TYPE stop = 0;
                Stop_in.try_read(stop);
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
                if (Trace_source >= 0) {
                    PcgCallipepla_DebugTryWrite(Debug_Event_out,
                                                Trace_source,
                                                kPcgCallipeplaTracePhaseStop,
                                                Debug_channel,
                                                stop);
                }
#endif
                return;
            }
        }

#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
        if (Trace_source >= 0) {
            PcgCallipepla_DebugTryWrite(Debug_Event_out,
                                        Trace_source,
                                        kPcgCallipeplaTracePhaseRecv,
                                        Debug_channel,
                                        num_pe_output);
        }
#endif
    forward_valid_outputs:
        for (INDEX_TYPE i = 0, c_idx = 0, o_idx = 0; i < num_pe_output;) {
#pragma HLS loop_tripcount min=1 max=1800
#pragma HLS pipeline II=1
            (void)Cuper_TryForwardCheckerValue(num_pe_output,
                                               num_out,
                                               i,
                                               c_idx,
                                               o_idx,
                                               Vector_Y_Stream,
                                               Vector_Y_Stream_Aftck);
        }
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
        if (Trace_source >= 0) {
            PcgCallipepla_DebugTryWrite(Debug_Event_out,
                                        Trace_source,
                                        kPcgCallipeplaTracePhaseDone,
                                        Debug_channel,
                                        num_out);
        }
#endif
    }
}

void PcgCallipepla_Mult_Sort_Tree(
    tapa::istreams<float_v2, 8> &Vector_Y_Stream_Aftck,
    tapa::ostream<float_v16> &Vector_Y_Stream_Ans,
    tapa::istream<INDEX_TYPE> &Stop_in
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
    ,
    tapa::ostream<PcgCallipeplaDebugEvent> &Debug_Event_out
#endif
    ) {
    INDEX_TYPE pack_count = 0;
sort_tree_loop:
    for (;;) {
#pragma HLS pipeline II=1
        if (!Stop_in.empty()) {
            INDEX_TYPE stop = 0;
            Stop_in.try_read(stop);
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
            PcgCallipepla_DebugTryWrite(Debug_Event_out,
                                        kPcgCallipeplaTraceSourceSortTree,
                                        kPcgCallipeplaTracePhaseStop,
                                        0,
                                        stop);
#endif
            return;
        }
        if (Cuper_TryPackFloatV16(Vector_Y_Stream_Aftck, Vector_Y_Stream_Ans)) {
            ++pack_count;
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
            if ((pack_count & 0xff) == 1) {
                PcgCallipepla_DebugTryWrite(Debug_Event_out,
                                            kPcgCallipeplaTraceSourceSortTree,
                                            kPcgCallipeplaTracePhaseProgress,
                                            0,
                                            pack_count);
            }
#endif
        }
    }
}
