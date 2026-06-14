#pragma once

// Jacobi deadlock debug 辅助模块。
// 只有定义 JACOBI_DEADLOCK_DEBUG 时才会被顶层接入。所有业务 task 只用
// try_write 发事件，避免 debug 通路反过来阻塞正常数据流。
//
// 注意：完整 full graph 默认不再做入口阻塞式 mmap probe。mmap-only micro top 已经
// 证明 Status/Metrics/Debug 写回链路可用；full graph 里继续等待 Debug write response
// 会把调试路径变成新的 Finish 风险点。若确实要恢复旧入口阻塞探针，可以额外定义
// JACOBI_BLOCKING_ENTRY_PROBE。

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

// 入口级 mmap 探针。下一版上板若 pre-Finish dump 仍读不到这些槽位，优先怀疑
// kernel task 未启动、Debug BO ABI/mmap 写回路径，或 XRT/FRT 的迁移顺序。
static constexpr INDEX_TYPE kJacobiDebugProbeMagic = 0x4a434231;
static constexpr INDEX_TYPE kJacobiDebugProbeSlotMagic = 48;
static constexpr INDEX_TYPE kJacobiDebugProbeSlotStreamCount = 49;
static constexpr INDEX_TYPE kJacobiDebugProbeSlotPhase = 50;
static constexpr INDEX_TYPE kJacobiDebugProbeSlotStopDrain = 51;

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
#ifdef JACOBI_BLOCKING_ENTRY_PROBE
    // 先做阻塞写并等待 AXI write response。它只阻塞 debug task，不会反压业务流；
    // 但可以证明 Debug mmap 端口是否在 kernel 入口阶段已经可写。
    Jacobi_DebugWriteBlocking(Debug, 0, kJacobiDebugProbeMagic);
    Jacobi_DebugWriteBlocking(Debug,
                              kJacobiDebugProbeSlotMagic,
                              kJacobiDebugProbeMagic);
    Jacobi_DebugWriteBlocking(Debug,
                              kJacobiDebugProbeSlotStreamCount,
                              kJacobiDebugStreamCount);
    Jacobi_DebugWriteBlocking(Debug,
                              kJacobiDebugProbeSlotPhase,
                              kJacobiDebugPhaseEnterRound);
    Jacobi_DebugWriteBlocking(Debug,
                              kJacobiDebugProbeSlotStopDrain,
                              kJacobiDebugStopDrainCycles);
#endif

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

        if (!stop_seen && !Debug_Stop_in.empty()) {
            INDEX_TYPE stop = 0;
            Debug_Stop_in.try_read(stop);
            stop_seen = true;
            stop_drain_count = 0;
#ifndef JACOBI_BLOCKING_ENTRY_PROBE
            // full graph debug 默认是 best-effort。收到 stop 后丢弃未发出的
            // debug 写，避免 Debug mmap 端口异常反过来阻止整个 graph Finish。
            pending_count = 0;
            pending_index = 0;
#endif
        }

        if (stop_seen && stop_drain_count >= kJacobiDebugStopDrainCycles) {
#ifdef JACOBI_BLOCKING_ENTRY_PROBE
            Jacobi_DebugWriteBlocking(Debug, 0, heartbeat);
            Jacobi_DebugWriteBlocking(Debug, 15, kJacobiDebugPhaseStop);
#endif
            return;
        }

#ifndef JACOBI_BLOCKING_ENTRY_PROBE
        if (stop_seen) {
        drain_debug_events_after_stop:
            for (INDEX_TYPE offset = 0; offset < kJacobiDebugStreamCount; ++offset) {
#pragma HLS unroll
                JacobiDebugEvent dropped_event;
                (void)Debug_Event_in[offset].try_read(dropped_event);
            }
            ++stop_drain_count;
            continue;
        }
#endif

        if (pending_index < pending_count) {
            if (!Debug.write_addr.full() && !Debug.write_data.full()) {
                Debug.write_addr.try_write(pending_addr[pending_index]);
                Debug.write_data.try_write(pending_data[pending_index]);
                ++pending_index;
            }
        } else {
            pending_count = 0;
            pending_index = 0;

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
