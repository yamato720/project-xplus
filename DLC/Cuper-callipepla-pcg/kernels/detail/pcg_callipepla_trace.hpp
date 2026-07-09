#pragma once

#include <ap_int.h>
#include <tapa.h>

#include "Cuper.h"

#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED

static constexpr INDEX_TYPE kPcgCallipeplaTraceSourceCount = 13;
static constexpr INDEX_TYPE kPcgCallipeplaTraceSourceController = 0;
static constexpr INDEX_TYPE kPcgCallipeplaTraceSourcePtrLoader = 1;
static constexpr INDEX_TYPE kPcgCallipeplaTraceSourceVectorLoader = 2;
static constexpr INDEX_TYPE kPcgCallipeplaTraceSourceMatrix0 = 3;
static constexpr INDEX_TYPE kPcgCallipeplaTraceSourceMatrix15 = 4;
static constexpr INDEX_TYPE kPcgCallipeplaTraceSourceCore0 = 5;
static constexpr INDEX_TYPE kPcgCallipeplaTraceSourceCore15 = 6;
static constexpr INDEX_TYPE kPcgCallipeplaTraceSourceAcc0 = 7;
static constexpr INDEX_TYPE kPcgCallipeplaTraceSourceAcc15 = 8;
static constexpr INDEX_TYPE kPcgCallipeplaTraceSourceChecker0 = 9;
static constexpr INDEX_TYPE kPcgCallipeplaTraceSourceChecker7 = 10;
static constexpr INDEX_TYPE kPcgCallipeplaTraceSourceSortTree = 11;
static constexpr INDEX_TYPE kPcgCallipeplaTraceSourceVectorPhases = 12;

static constexpr INDEX_TYPE kPcgCallipeplaTracePhaseEntry = 1;
static constexpr INDEX_TYPE kPcgCallipeplaTracePhaseRecv = 2;
static constexpr INDEX_TYPE kPcgCallipeplaTracePhaseSend = 3;
static constexpr INDEX_TYPE kPcgCallipeplaTracePhaseProgress = 4;
static constexpr INDEX_TYPE kPcgCallipeplaTracePhaseDone = 5;
static constexpr INDEX_TYPE kPcgCallipeplaTracePhaseWait = 6;
static constexpr INDEX_TYPE kPcgCallipeplaTracePhaseStop = 255;

static constexpr INDEX_TYPE kPcgCallipeplaTraceMagic = 0x434c5452;
static constexpr INDEX_TYPE kPcgCallipeplaTraceStopDrainCycles = 8192;
static constexpr INDEX_TYPE kPcgCallipeplaTraceHeartbeatMask = 0x3ffff;

struct PcgCallipeplaDebugEvent {
    INDEX_TYPE source;
    INDEX_TYPE phase;
    INDEX_TYPE lane;
    INDEX_TYPE value;
};

struct PcgCallipeplaStatusWrite {
    INDEX_TYPE addr;
    INDEX_TYPE value;
};

inline INDEX_TYPE PcgCallipepla_DebugPackEvent(
    const PcgCallipeplaDebugEvent &event) {
#pragma HLS inline
    return ((event.source & 0xff) << 24) |
           ((event.phase & 0xff) << 16) |
           (event.lane & 0xffff);
}

inline bool PcgCallipepla_DebugTryWrite(
    tapa::ostream<PcgCallipeplaDebugEvent> &Debug_Event_out,
    const INDEX_TYPE source,
    const INDEX_TYPE phase,
    const INDEX_TYPE lane,
    const INDEX_TYPE value) {
#pragma HLS inline
    PcgCallipeplaDebugEvent event;
    event.source = source;
    event.phase = phase;
    event.lane = lane;
    event.value = value;
    return Debug_Event_out.try_write(event);
}

inline void PcgCallipepla_StatusTryEnqueue(
    INDEX_TYPE pending_addr[12],
    INDEX_TYPE pending_data[12],
    INDEX_TYPE &pending_count,
    const INDEX_TYPE addr,
    const INDEX_TYPE value) {
#pragma HLS inline
    pending_addr[pending_count] = addr;
    pending_data[pending_count] = value;
    ++pending_count;
}

inline bool PcgCallipepla_DebugTryReadByIndex(
    tapa::istream<PcgCallipeplaDebugEvent> &Debug_Controller_in,
    tapa::istream<PcgCallipeplaDebugEvent> &Debug_PtrLoader_in,
    tapa::istream<PcgCallipeplaDebugEvent> &Debug_VectorLoader_in,
    tapa::istreams<PcgCallipeplaDebugEvent, HBM_CHANNEL_NUM> &Debug_MatrixLoader_in,
    tapa::istreams<PcgCallipeplaDebugEvent, HBM_CHANNEL_NUM> &Debug_Core_in,
    tapa::istreams<PcgCallipeplaDebugEvent, HBM_CHANNEL_NUM> &Debug_Accumulator_in,
    tapa::istreams<PcgCallipeplaDebugEvent, 8> &Debug_Checker_in,
    tapa::istream<PcgCallipeplaDebugEvent> &Debug_SortTree_in,
    tapa::istream<PcgCallipeplaDebugEvent> &Debug_VectorPhases_in,
    const INDEX_TYPE stream_index,
    PcgCallipeplaDebugEvent &event) {
#pragma HLS inline
    switch (stream_index) {
        case kPcgCallipeplaTraceSourceController:
            return Debug_Controller_in.try_read(event);
        case kPcgCallipeplaTraceSourcePtrLoader:
            return Debug_PtrLoader_in.try_read(event);
        case kPcgCallipeplaTraceSourceVectorLoader:
            return Debug_VectorLoader_in.try_read(event);
        case kPcgCallipeplaTraceSourceMatrix0:
            return Debug_MatrixLoader_in[0].try_read(event);
        case kPcgCallipeplaTraceSourceMatrix15:
            return Debug_MatrixLoader_in[15].try_read(event);
        case kPcgCallipeplaTraceSourceCore0:
            return Debug_Core_in[0].try_read(event);
        case kPcgCallipeplaTraceSourceCore15:
            return Debug_Core_in[15].try_read(event);
        case kPcgCallipeplaTraceSourceAcc0:
            return Debug_Accumulator_in[0].try_read(event);
        case kPcgCallipeplaTraceSourceAcc15:
            return Debug_Accumulator_in[15].try_read(event);
        case kPcgCallipeplaTraceSourceChecker0:
            return Debug_Checker_in[0].try_read(event);
        case kPcgCallipeplaTraceSourceChecker7:
            return Debug_Checker_in[7].try_read(event);
        case kPcgCallipeplaTraceSourceSortTree:
            return Debug_SortTree_in.try_read(event);
        case kPcgCallipeplaTraceSourceVectorPhases:
            return Debug_VectorPhases_in.try_read(event);
        default:
            return false;
    }
}

void PcgCallipepla_StatusMonitor(
    tapa::istream<PcgCallipeplaStatusWrite> &Status_Write_in,
    tapa::istream<PcgCallipeplaDebugEvent> &Debug_Controller_in,
    tapa::istream<PcgCallipeplaDebugEvent> &Debug_PtrLoader_in,
    tapa::istream<PcgCallipeplaDebugEvent> &Debug_VectorLoader_in,
    tapa::istreams<PcgCallipeplaDebugEvent, HBM_CHANNEL_NUM> &Debug_MatrixLoader_in,
    tapa::istreams<PcgCallipeplaDebugEvent, HBM_CHANNEL_NUM> &Debug_Core_in,
    tapa::istreams<PcgCallipeplaDebugEvent, HBM_CHANNEL_NUM> &Debug_Accumulator_in,
    tapa::istreams<PcgCallipeplaDebugEvent, 8> &Debug_Checker_in,
    tapa::istream<PcgCallipeplaDebugEvent> &Debug_SortTree_in,
    tapa::istream<PcgCallipeplaDebugEvent> &Debug_VectorPhases_in,
    tapa::istream<INDEX_TYPE> &Debug_Stop_in,
    tapa::async_mmap<INDEX_TYPE> &Status) {
    INDEX_TYPE heartbeat = 1;
    INDEX_TYPE event_count = 0;
    INDEX_TYPE drop_count = 0;
    INDEX_TYPE poll_index = 0;
    INDEX_TYPE write_issue_count = 0;
    INDEX_TYPE write_response_count = 0;
    INDEX_TYPE status_write_count = 0;
    INDEX_TYPE stop_drain_count = 0;
    bool stop_seen = false;

    INDEX_TYPE per_source_count[16];
#pragma HLS array_partition variable=per_source_count complete
init_source_counts:
    for (INDEX_TYPE index = 0; index < 16; ++index) {
#pragma HLS unroll
        per_source_count[index] = 0;
    }

    INDEX_TYPE pending_addr[12];
    INDEX_TYPE pending_data[12];
#pragma HLS array_partition variable=pending_addr complete
#pragma HLS array_partition variable=pending_data complete
    INDEX_TYPE pending_count = 0;
    INDEX_TYPE pending_index = 0;

    PcgCallipepla_StatusTryEnqueue(
        pending_addr, pending_data, pending_count, 48, heartbeat);
    PcgCallipepla_StatusTryEnqueue(
        pending_addr, pending_data, pending_count, 49, drop_count);
    PcgCallipepla_StatusTryEnqueue(
        pending_addr, pending_data, pending_count, 50,
        kPcgCallipeplaTraceMagic);
    PcgCallipepla_StatusTryEnqueue(
        pending_addr, pending_data, pending_count, 51,
        kPcgCallipeplaTraceSourceCount);

status_monitor_loop:
    for (;;) {
#pragma HLS pipeline II=1
        ++heartbeat;

        uint8_t num_responses = 0;
        if (Status.write_resp.try_read(num_responses)) {
            write_response_count += int(num_responses) + 1;
        }

        if (pending_index < pending_count) {
            if (!Status.write_addr.full() && !Status.write_data.full()) {
                Status.write_addr.try_write(pending_addr[pending_index]);
                Status.write_data.try_write(pending_data[pending_index]);
                ++pending_index;
                ++write_issue_count;
            }
        } else {
            pending_count = 0;
            pending_index = 0;

            if (!Status_Write_in.empty()) {
                PcgCallipeplaStatusWrite write;
                Status_Write_in.try_read(write);
                PcgCallipepla_StatusTryEnqueue(
                    pending_addr, pending_data, pending_count, write.addr, write.value);
                ++status_write_count;
            } else {
                if (!stop_seen && !Debug_Stop_in.empty()) {
                    INDEX_TYPE stop = 0;
                    Debug_Stop_in.try_read(stop);
                    stop_seen = true;
                    stop_drain_count = 0;
                    PcgCallipepla_StatusTryEnqueue(
                        pending_addr, pending_data, pending_count, 55, 1);
                }

                if (stop_seen) {
                    ++stop_drain_count;
                }

                const bool emit_heartbeat =
                    ((heartbeat & kPcgCallipeplaTraceHeartbeatMask) == 0);
                if (emit_heartbeat) {
                    PcgCallipepla_StatusTryEnqueue(
                        pending_addr, pending_data, pending_count, 48, heartbeat);
                    PcgCallipepla_StatusTryEnqueue(
                        pending_addr, pending_data, pending_count, 49, drop_count);
                    PcgCallipepla_StatusTryEnqueue(
                        pending_addr, pending_data, pending_count, 56, write_issue_count);
                    PcgCallipepla_StatusTryEnqueue(
                        pending_addr, pending_data, pending_count, 57, write_response_count);
                    PcgCallipepla_StatusTryEnqueue(
                        pending_addr, pending_data, pending_count, 58, status_write_count);
                    PcgCallipepla_StatusTryEnqueue(
                        pending_addr, pending_data, pending_count, 59, poll_index);
                } else {
                    PcgCallipeplaDebugEvent event;
                    const INDEX_TYPE current_poll_index = poll_index;
                    const bool has_event = PcgCallipepla_DebugTryReadByIndex(
                        Debug_Controller_in,
                        Debug_PtrLoader_in,
                        Debug_VectorLoader_in,
                        Debug_MatrixLoader_in,
                        Debug_Core_in,
                        Debug_Accumulator_in,
                        Debug_Checker_in,
                        Debug_SortTree_in,
                        Debug_VectorPhases_in,
                        current_poll_index,
                        event);
                    poll_index =
                        current_poll_index == kPcgCallipeplaTraceSourceCount - 1
                            ? 0
                            : current_poll_index + 1;
                    if (has_event) {
                        ++event_count;
                        const INDEX_TYPE source = event.source & 0x0f;
                        if (source < 16) {
                            ++per_source_count[source];
                            PcgCallipepla_StatusTryEnqueue(
                                pending_addr,
                                pending_data,
                                pending_count,
                                16 + source,
                                PcgCallipepla_DebugPackEvent(event));
                            PcgCallipepla_StatusTryEnqueue(
                                pending_addr,
                                pending_data,
                                pending_count,
                                32 + source,
                                per_source_count[source]);
                        } else {
                            ++drop_count;
                        }
                        PcgCallipepla_StatusTryEnqueue(
                            pending_addr, pending_data, pending_count, 49, drop_count);
                        PcgCallipepla_StatusTryEnqueue(
                            pending_addr, pending_data, pending_count, 52, event_count);
                        PcgCallipepla_StatusTryEnqueue(
                            pending_addr,
                            pending_data,
                            pending_count,
                            53,
                            PcgCallipepla_DebugPackEvent(event));
                        PcgCallipepla_StatusTryEnqueue(
                            pending_addr, pending_data, pending_count, 54, event.value);
                    }
                }
            }
        }

        if (stop_seen &&
            stop_drain_count >= kPcgCallipeplaTraceStopDrainCycles &&
            pending_index >= pending_count &&
            Status_Write_in.empty()) {
            return;
        }
    }
}

#endif
