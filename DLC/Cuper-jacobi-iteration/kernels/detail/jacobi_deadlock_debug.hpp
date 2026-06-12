#pragma once

// Jacobi deadlock debug 辅助模块。
// 只有定义 JACOBI_DEADLOCK_DEBUG 时才会被顶层接入。所有业务 task 只用
// try_write 发事件，避免 debug 通路反过来阻塞正常数据流。

#include <ap_int.h>
#include <tapa.h>

#include "jacobi_common.hpp"

static constexpr INDEX_TYPE kJacobiDebugStreamCount = 11;

static constexpr INDEX_TYPE kJacobiDebugSourceDispatcher = 1;
static constexpr INDEX_TYPE kJacobiDebugSourcePackWriter = 2;
static constexpr INDEX_TYPE kJacobiDebugSourceHbmWriter = 3;
static constexpr INDEX_TYPE kJacobiDebugSourcePairBase = 16;

static constexpr INDEX_TYPE kJacobiDebugPhaseEnterRound = 1;
static constexpr INDEX_TYPE kJacobiDebugPhaseProgress = 2;
static constexpr INDEX_TYPE kJacobiDebugPhaseWait = 3;
static constexpr INDEX_TYPE kJacobiDebugPhaseDoneRound = 4;
static constexpr INDEX_TYPE kJacobiDebugPhaseStop = 255;

static constexpr INDEX_TYPE kJacobiDebugStreamDispatcher = 0;
static constexpr INDEX_TYPE kJacobiDebugStreamPackWriter = 1;
static constexpr INDEX_TYPE kJacobiDebugStreamHbmWriter = 2;
static constexpr INDEX_TYPE kJacobiDebugStreamPairBase = 3;
static constexpr INDEX_TYPE kJacobiDebugStopDrainCycles = 8192;

struct JacobiDebugEvent {
    INDEX_TYPE source;
    INDEX_TYPE phase;
    INDEX_TYPE lane;
    INDEX_TYPE value;
};

inline INDEX_TYPE Jacobi_DebugPackEvent(const JacobiDebugEvent &event) {
#pragma HLS inline
    return ((event.source & 0xff) << 24) |
           ((event.phase & 0xff) << 16) |
           (event.lane & 0xffff);
}

inline void Jacobi_DebugTryWrite(tapa::ostream<JacobiDebugEvent> &Debug_Event_out,
                                 const INDEX_TYPE source,
                                 const INDEX_TYPE phase,
                                 const INDEX_TYPE lane,
                                 const INDEX_TYPE value) {
#pragma HLS inline
    JacobiDebugEvent event;
    event.source = source;
    event.phase = phase;
    event.lane = lane;
    event.value = value;
    Debug_Event_out.try_write(event);
}

inline void Jacobi_DebugWriteBlocking(tapa::async_mmap<INDEX_TYPE> &Debug,
                                      const INDEX_TYPE addr,
                                      const INDEX_TYPE value) {
#pragma HLS inline
    Debug.write_addr.write(addr);
    Debug.write_data.write(value);
    uint8_t num_responses = 0;
wait_debug_write_resp:
    while (!Debug.write_resp.try_read(num_responses)) {
#pragma HLS pipeline II=1
    }
}

void Jacobi_DebugMonitor(
    tapa::istreams<JacobiDebugEvent, kJacobiDebugStreamCount> &Debug_Event_in,
    tapa::istream<INDEX_TYPE> &Debug_Stop_in,
    tapa::async_mmap<INDEX_TYPE> &Debug) {
    INDEX_TYPE heartbeat = 0;
    INDEX_TYPE event_count = 0;
    INDEX_TYPE poll_index = 0;
    bool stop_seen = false;
    INDEX_TYPE stop_drain_count = 0;

    INDEX_TYPE pending_addr[4];
    INDEX_TYPE pending_data[4];
#pragma HLS array_partition variable=pending_addr complete
#pragma HLS array_partition variable=pending_data complete
    INDEX_TYPE pending_count = 0;
    INDEX_TYPE pending_index = 0;

debug_monitor_loop:
    for (;;) {
#pragma HLS pipeline II=1
        ++heartbeat;

        uint8_t num_responses = 0;
        (void)Debug.write_resp.try_read(num_responses);

        if (pending_index < pending_count) {
            if (!Debug.write_addr.full() && !Debug.write_data.full()) {
                Debug.write_addr.try_write(pending_addr[pending_index]);
                Debug.write_data.try_write(pending_data[pending_index]);
                ++pending_index;
            }
        } else {
            pending_count = 0;
            pending_index = 0;

            if (!stop_seen && !Debug_Stop_in.empty()) {
                INDEX_TYPE stop = 0;
                Debug_Stop_in.try_read(stop);
                stop_seen = true;
                stop_drain_count = 0;
            }

            JacobiDebugEvent event;
            bool has_event = false;
        poll_debug_event_streams:
            for (INDEX_TYPE offset = 0; offset < kJacobiDebugStreamCount; ++offset) {
                const INDEX_TYPE stream_index = (poll_index + offset) % kJacobiDebugStreamCount;
                if (!has_event && Debug_Event_in[stream_index].try_read(event)) {
                    has_event = true;
                    poll_index = (stream_index + 1) % kJacobiDebugStreamCount;
                }
            }

            if (has_event) {
                ++event_count;
                pending_addr[0] = 1;
                pending_data[0] = event_count;
                pending_addr[1] = 2;
                pending_data[1] = Jacobi_DebugPackEvent(event);
                pending_addr[2] = 3;
                pending_data[2] = event.value;
                pending_addr[3] = 16 + (event.source & 0x1f);
                pending_data[3] = event.value;
                pending_count = 4;
            } else if (stop_seen && stop_drain_count >= kJacobiDebugStopDrainCycles) {
                Jacobi_DebugWriteBlocking(Debug, 0, heartbeat);
                Jacobi_DebugWriteBlocking(Debug, 15, kJacobiDebugPhaseStop);
                return;
            } else if ((heartbeat & 0x3ff) == 0) {
                pending_addr[0] = 0;
                pending_data[0] = heartbeat;
                pending_count = 1;
            }

            if (stop_seen && stop_drain_count < kJacobiDebugStopDrainCycles) {
                ++stop_drain_count;
            }
        }
    }
}
