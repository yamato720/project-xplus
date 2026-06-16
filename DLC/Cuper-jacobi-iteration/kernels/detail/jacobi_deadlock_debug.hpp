#pragma once

// Jacobi trace/debug 辅助模块。
// JACOBI_TRACE_ISOTOPE 或 JACOBI_DEADLOCK_DEBUG 打开后，顶层会接入 Debug BO 和
// 事件流。所有业务 task 只用 try_write 发事件，避免 debug 通路反过来阻塞正常数据流。
//
// 注意：完整 full graph 默认不再做入口阻塞式 mmap probe。mmap-only micro top 已经
// 证明 Status/Metrics/Debug 写回链路可用；full graph 里继续等待 Debug write response
// 会把调试路径变成新的 Finish 风险点。若确实要恢复旧入口阻塞探针，可以额外定义
// JACOBI_BLOCKING_ENTRY_PROBE。

#include <ap_int.h>
#include <tapa.h>

#include "jacobi_common.hpp"

static constexpr INDEX_TYPE kJacobiDebugBufferWords =
#ifdef JACOBI_WIDE_HBM
    512;
#else
    256;
#endif

static constexpr INDEX_TYPE kJacobiDebugStreamController = 0;
static constexpr INDEX_TYPE kJacobiDebugStreamPtrLoader = 1;
static constexpr INDEX_TYPE kJacobiDebugStreamVectorLoader = 2;
static constexpr INDEX_TYPE kJacobiDebugStreamMatrixLoaderBase = 3;
static constexpr INDEX_TYPE kJacobiDebugStreamAccumulatorBase =
    kJacobiDebugStreamMatrixLoaderBase + HBM_CHANNEL_NUM;
static constexpr INDEX_TYPE kJacobiDebugStreamCoeffLoader =
    kJacobiDebugStreamAccumulatorBase + HBM_CHANNEL_NUM;
static constexpr INDEX_TYPE kJacobiDebugPairStreamCount = JACOBI_UPDATE_PAIR_NUM;
static constexpr INDEX_TYPE kJacobiDebugStreamPairBase = kJacobiDebugStreamCoeffLoader + 1;
static constexpr INDEX_TYPE kJacobiDebugStreamPackWriter =
    kJacobiDebugStreamPairBase + kJacobiDebugPairStreamCount;
static constexpr INDEX_TYPE kJacobiDebugStreamHbmWriter = kJacobiDebugStreamPackWriter + 1;
static constexpr INDEX_TYPE kJacobiDebugFullStreamCount = kJacobiDebugStreamHbmWriter + 1;

static constexpr INDEX_TYPE kJacobiDebugLightStreamController = 0;
static constexpr INDEX_TYPE kJacobiDebugLightStreamPtrLoader = 1;
static constexpr INDEX_TYPE kJacobiDebugLightStreamVectorLoader = 2;
static constexpr INDEX_TYPE kJacobiDebugLightStreamCoeffLoader = 3;
static constexpr INDEX_TYPE kJacobiDebugLightStreamPairBase = 4;
static constexpr INDEX_TYPE kJacobiDebugLightStreamPackWriter =
    kJacobiDebugLightStreamPairBase + kJacobiDebugPairStreamCount;
static constexpr INDEX_TYPE kJacobiDebugLightStreamHbmWriter = kJacobiDebugLightStreamPackWriter + 1;

#ifdef JACOBI_TRACE_FULL
static constexpr INDEX_TYPE kJacobiDebugStreamCount = kJacobiDebugFullStreamCount;
#else
static constexpr INDEX_TYPE kJacobiDebugStreamCount = kJacobiDebugLightStreamHbmWriter + 1;
#endif

static constexpr INDEX_TYPE kJacobiDebugSourceController = 1;
static constexpr INDEX_TYPE kJacobiDebugSourcePtrLoader = 2;
static constexpr INDEX_TYPE kJacobiDebugSourceVectorLoader = 3;
static constexpr INDEX_TYPE kJacobiDebugSourceMatrixLoaderBase = 4;
static constexpr INDEX_TYPE kJacobiDebugSourceAccumulatorBase =
    kJacobiDebugSourceMatrixLoaderBase + HBM_CHANNEL_NUM;
static constexpr INDEX_TYPE kJacobiDebugSourceCoeffLoader =
    kJacobiDebugSourceAccumulatorBase + HBM_CHANNEL_NUM;
static constexpr INDEX_TYPE kJacobiDebugSourcePairBase = kJacobiDebugSourceCoeffLoader + 1;
static constexpr INDEX_TYPE kJacobiDebugSourcePackWriter =
    kJacobiDebugSourcePairBase + kJacobiDebugPairStreamCount;
static constexpr INDEX_TYPE kJacobiDebugSourceHbmWriter = kJacobiDebugSourcePackWriter + 1;

static constexpr INDEX_TYPE kJacobiDebugPhaseEnterRound = 1;
static constexpr INDEX_TYPE kJacobiDebugPhaseProgress = 2;
static constexpr INDEX_TYPE kJacobiDebugPhaseWait = 3;
static constexpr INDEX_TYPE kJacobiDebugPhaseDoneRound = 4;
static constexpr INDEX_TYPE kJacobiDebugPhaseRecv = 5;
static constexpr INDEX_TYPE kJacobiDebugPhaseSend = 6;
static constexpr INDEX_TYPE kJacobiDebugPhaseReadIssue = 7;
static constexpr INDEX_TYPE kJacobiDebugPhaseReadResp = 8;
static constexpr INDEX_TYPE kJacobiDebugPhaseWriteIssue = 9;
static constexpr INDEX_TYPE kJacobiDebugPhaseWriteResp = 10;
static constexpr INDEX_TYPE kJacobiDebugPhaseFeedback = 11;
static constexpr INDEX_TYPE kJacobiDebugPhaseFrame = 12;
static constexpr INDEX_TYPE kJacobiDebugPhaseStop = 255;

static constexpr INDEX_TYPE kJacobiDebugStopDrainCycles = 8192;
static constexpr INDEX_TYPE kJacobiDebugHeartbeatMask = 0x3ffff;
static constexpr INDEX_TYPE kJacobiDebugPerSourceBase = 64;
static constexpr INDEX_TYPE kJacobiDebugPerSourceStride = 4;

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

inline INDEX_TYPE Jacobi_DebugSourceSlot(const INDEX_TYPE source) {
#pragma HLS inline
    return kJacobiDebugPerSourceBase + source * kJacobiDebugPerSourceStride;
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

#ifdef JACOBI_TRACE_FULL
inline bool Jacobi_DebugTryReadStreamByIndex(
    tapa::istream<JacobiDebugEvent> &Debug_Controller_in,
    tapa::istream<JacobiDebugEvent> &Debug_PtrLoader_in,
    tapa::istream<JacobiDebugEvent> &Debug_VectorLoader_in,
    tapa::istreams<JacobiDebugEvent, HBM_CHANNEL_NUM> &Debug_MatrixLoader_in,
    tapa::istreams<JacobiDebugEvent, HBM_CHANNEL_NUM> &Debug_Accumulator_in,
    tapa::istream<JacobiDebugEvent> &Debug_CoeffLoader_in,
    tapa::istreams<JacobiDebugEvent, kJacobiDebugPairStreamCount> &Debug_Pair_in,
    tapa::istream<JacobiDebugEvent> &Debug_PackWriter_in,
    tapa::istream<JacobiDebugEvent> &Debug_HbmWriter_in,
    const INDEX_TYPE stream_index,
    JacobiDebugEvent &event) {
#pragma HLS inline
    // 轻量轮询：每拍只访问一个 trace 输入。这样仍能覆盖全部 source，
    // 但避免 HLS 为 47 路输入综合一个巨大的动态扫描/优先级网络。
    switch (stream_index) {
        case kJacobiDebugStreamController:
            return Debug_Controller_in.try_read(event);
        case kJacobiDebugStreamPtrLoader:
            return Debug_PtrLoader_in.try_read(event);
        case kJacobiDebugStreamVectorLoader:
            return Debug_VectorLoader_in.try_read(event);
        case kJacobiDebugStreamMatrixLoaderBase + 0:
            return Debug_MatrixLoader_in[0].try_read(event);
        case kJacobiDebugStreamMatrixLoaderBase + 1:
            return Debug_MatrixLoader_in[1].try_read(event);
        case kJacobiDebugStreamMatrixLoaderBase + 2:
            return Debug_MatrixLoader_in[2].try_read(event);
        case kJacobiDebugStreamMatrixLoaderBase + 3:
            return Debug_MatrixLoader_in[3].try_read(event);
        case kJacobiDebugStreamMatrixLoaderBase + 4:
            return Debug_MatrixLoader_in[4].try_read(event);
        case kJacobiDebugStreamMatrixLoaderBase + 5:
            return Debug_MatrixLoader_in[5].try_read(event);
        case kJacobiDebugStreamMatrixLoaderBase + 6:
            return Debug_MatrixLoader_in[6].try_read(event);
        case kJacobiDebugStreamMatrixLoaderBase + 7:
            return Debug_MatrixLoader_in[7].try_read(event);
        case kJacobiDebugStreamMatrixLoaderBase + 8:
            return Debug_MatrixLoader_in[8].try_read(event);
        case kJacobiDebugStreamMatrixLoaderBase + 9:
            return Debug_MatrixLoader_in[9].try_read(event);
        case kJacobiDebugStreamMatrixLoaderBase + 10:
            return Debug_MatrixLoader_in[10].try_read(event);
        case kJacobiDebugStreamMatrixLoaderBase + 11:
            return Debug_MatrixLoader_in[11].try_read(event);
        case kJacobiDebugStreamMatrixLoaderBase + 12:
            return Debug_MatrixLoader_in[12].try_read(event);
        case kJacobiDebugStreamMatrixLoaderBase + 13:
            return Debug_MatrixLoader_in[13].try_read(event);
        case kJacobiDebugStreamMatrixLoaderBase + 14:
            return Debug_MatrixLoader_in[14].try_read(event);
        case kJacobiDebugStreamMatrixLoaderBase + 15:
            return Debug_MatrixLoader_in[15].try_read(event);
        case kJacobiDebugStreamAccumulatorBase + 0:
            return Debug_Accumulator_in[0].try_read(event);
        case kJacobiDebugStreamAccumulatorBase + 1:
            return Debug_Accumulator_in[1].try_read(event);
        case kJacobiDebugStreamAccumulatorBase + 2:
            return Debug_Accumulator_in[2].try_read(event);
        case kJacobiDebugStreamAccumulatorBase + 3:
            return Debug_Accumulator_in[3].try_read(event);
        case kJacobiDebugStreamAccumulatorBase + 4:
            return Debug_Accumulator_in[4].try_read(event);
        case kJacobiDebugStreamAccumulatorBase + 5:
            return Debug_Accumulator_in[5].try_read(event);
        case kJacobiDebugStreamAccumulatorBase + 6:
            return Debug_Accumulator_in[6].try_read(event);
        case kJacobiDebugStreamAccumulatorBase + 7:
            return Debug_Accumulator_in[7].try_read(event);
        case kJacobiDebugStreamAccumulatorBase + 8:
            return Debug_Accumulator_in[8].try_read(event);
        case kJacobiDebugStreamAccumulatorBase + 9:
            return Debug_Accumulator_in[9].try_read(event);
        case kJacobiDebugStreamAccumulatorBase + 10:
            return Debug_Accumulator_in[10].try_read(event);
        case kJacobiDebugStreamAccumulatorBase + 11:
            return Debug_Accumulator_in[11].try_read(event);
        case kJacobiDebugStreamAccumulatorBase + 12:
            return Debug_Accumulator_in[12].try_read(event);
        case kJacobiDebugStreamAccumulatorBase + 13:
            return Debug_Accumulator_in[13].try_read(event);
        case kJacobiDebugStreamAccumulatorBase + 14:
            return Debug_Accumulator_in[14].try_read(event);
        case kJacobiDebugStreamAccumulatorBase + 15:
            return Debug_Accumulator_in[15].try_read(event);
        case kJacobiDebugStreamCoeffLoader:
            return Debug_CoeffLoader_in.try_read(event);
        case kJacobiDebugStreamPairBase + 0:
            return Debug_Pair_in[0].try_read(event);
        case kJacobiDebugStreamPairBase + 1:
            return Debug_Pair_in[1].try_read(event);
        case kJacobiDebugStreamPairBase + 2:
            return Debug_Pair_in[2].try_read(event);
        case kJacobiDebugStreamPairBase + 3:
            return Debug_Pair_in[3].try_read(event);
        case kJacobiDebugStreamPairBase + 4:
            return Debug_Pair_in[4].try_read(event);
        case kJacobiDebugStreamPairBase + 5:
            return Debug_Pair_in[5].try_read(event);
        case kJacobiDebugStreamPairBase + 6:
            return Debug_Pair_in[6].try_read(event);
        case kJacobiDebugStreamPairBase + 7:
            return Debug_Pair_in[7].try_read(event);
        case kJacobiDebugStreamPackWriter:
            return Debug_PackWriter_in.try_read(event);
        case kJacobiDebugStreamHbmWriter:
            return Debug_HbmWriter_in.try_read(event);
        default:
            return false;
    }
}
#else
inline bool Jacobi_DebugTryReadStreamByIndex(
    tapa::istream<JacobiDebugEvent> &Debug_Controller_in,
    tapa::istream<JacobiDebugEvent> &Debug_PtrLoader_in,
    tapa::istream<JacobiDebugEvent> &Debug_VectorLoader_in,
    tapa::istream<JacobiDebugEvent> &Debug_CoeffLoader_in,
    tapa::istreams<JacobiDebugEvent, kJacobiDebugPairStreamCount> &Debug_Pair_in,
    tapa::istream<JacobiDebugEvent> &Debug_PackWriter_in,
    tapa::istream<JacobiDebugEvent> &Debug_HbmWriter_in,
    const INDEX_TYPE stream_index,
    JacobiDebugEvent &event) {
#pragma HLS inline
    // 硬件 light trace 只保留关键控制/写回路径，避免 full isotope 的 47 路输入端口。
    switch (stream_index) {
        case kJacobiDebugLightStreamController:
            return Debug_Controller_in.try_read(event);
        case kJacobiDebugLightStreamPtrLoader:
            return Debug_PtrLoader_in.try_read(event);
        case kJacobiDebugLightStreamVectorLoader:
            return Debug_VectorLoader_in.try_read(event);
        case kJacobiDebugLightStreamCoeffLoader:
            return Debug_CoeffLoader_in.try_read(event);
        case kJacobiDebugLightStreamPairBase + 0:
            return Debug_Pair_in[0].try_read(event);
        case kJacobiDebugLightStreamPairBase + 1:
            return Debug_Pair_in[1].try_read(event);
        case kJacobiDebugLightStreamPairBase + 2:
            return Debug_Pair_in[2].try_read(event);
        case kJacobiDebugLightStreamPairBase + 3:
            return Debug_Pair_in[3].try_read(event);
        case kJacobiDebugLightStreamPairBase + 4:
            return Debug_Pair_in[4].try_read(event);
        case kJacobiDebugLightStreamPairBase + 5:
            return Debug_Pair_in[5].try_read(event);
        case kJacobiDebugLightStreamPairBase + 6:
            return Debug_Pair_in[6].try_read(event);
        case kJacobiDebugLightStreamPairBase + 7:
            return Debug_Pair_in[7].try_read(event);
        case kJacobiDebugLightStreamPackWriter:
            return Debug_PackWriter_in.try_read(event);
        case kJacobiDebugLightStreamHbmWriter:
            return Debug_HbmWriter_in.try_read(event);
        default:
            return false;
    }
}
#endif

void Jacobi_DebugMonitor(
    tapa::istream<JacobiDebugEvent> &Debug_Controller_in,
    tapa::istream<JacobiDebugEvent> &Debug_PtrLoader_in,
    tapa::istream<JacobiDebugEvent> &Debug_VectorLoader_in,
#ifdef JACOBI_TRACE_FULL
    tapa::istreams<JacobiDebugEvent, HBM_CHANNEL_NUM> &Debug_MatrixLoader_in,
    tapa::istreams<JacobiDebugEvent, HBM_CHANNEL_NUM> &Debug_Accumulator_in,
#endif
    tapa::istream<JacobiDebugEvent> &Debug_CoeffLoader_in,
    tapa::istreams<JacobiDebugEvent, kJacobiDebugPairStreamCount> &Debug_Pair_in,
    tapa::istream<JacobiDebugEvent> &Debug_PackWriter_in,
    tapa::istream<JacobiDebugEvent> &Debug_HbmWriter_in,
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
    INDEX_TYPE write_issue_count = 0;
    INDEX_TYPE write_response_count = 0;

    INDEX_TYPE pending_addr[12];
    INDEX_TYPE pending_data[12];
#pragma HLS array_partition variable=pending_addr complete
#pragma HLS array_partition variable=pending_data complete
    INDEX_TYPE pending_count = 5;
    INDEX_TYPE pending_index = 0;
    pending_addr[0] = 0;
    pending_data[0] = kJacobiDebugProbeMagic;
    pending_addr[1] = kJacobiDebugProbeSlotMagic;
    pending_data[1] = kJacobiDebugProbeMagic;
    pending_addr[2] = kJacobiDebugProbeSlotStreamCount;
    pending_data[2] = kJacobiDebugStreamCount;
    pending_addr[3] = kJacobiDebugProbeSlotPhase;
    pending_data[3] = kJacobiDebugPhaseEnterRound;
    pending_addr[4] = kJacobiDebugProbeSlotStopDrain;
    pending_data[4] = kJacobiDebugStopDrainCycles;

debug_monitor_loop:
    for (;;) {
#pragma HLS pipeline II=1
        ++heartbeat;

        uint8_t num_responses = 0;
        if (Debug.write_resp.try_read(num_responses)) {
            write_response_count += int(num_responses) + 1;
        }

        if (!stop_seen && !Debug_Stop_in.empty()) {
            INDEX_TYPE stop = 0;
            Debug_Stop_in.try_read(stop);
            stop_seen = true;
            stop_drain_count = 0;
        }

        if (stop_seen) {
            ++stop_drain_count;
        }

        if (stop_seen && stop_drain_count >= kJacobiDebugStopDrainCycles) {
#ifdef JACOBI_BLOCKING_ENTRY_PROBE
            Jacobi_DebugWriteBlocking(Debug, 0, heartbeat);
            Jacobi_DebugWriteBlocking(Debug, 15, kJacobiDebugPhaseStop);
#endif
            return;
        }

        if (pending_index < pending_count) {
            if (!Debug.write_addr.full() && !Debug.write_data.full()) {
                Debug.write_addr.try_write(pending_addr[pending_index]);
                Debug.write_data.try_write(pending_data[pending_index]);
                ++pending_index;
                ++write_issue_count;
            }
        } else {
            pending_count = 0;
            pending_index = 0;

            // heartbeat/progress 槽位必须定期刷新，即使事件流很密也不能只依赖最终 stop。
            // host 会在 Finish 前周期性 sync Debug BO，靠这些槽位判断 kernel 是否仍在推进。
            const bool emit_heartbeat = ((heartbeat & kJacobiDebugHeartbeatMask) == 0);
            if (emit_heartbeat) {
                pending_addr[0] = 0;
                pending_data[0] = heartbeat;
                pending_addr[1] = 5;
                pending_data[1] = write_issue_count;
                pending_addr[2] = 6;
                pending_data[2] = write_response_count;
                pending_addr[3] = 7;
                pending_data[3] = stop_seen ? 1 : 0;
                pending_count = 4;
            } else {
                JacobiDebugEvent event;
                const INDEX_TYPE current_poll_index = poll_index;
#ifdef JACOBI_TRACE_FULL
                bool has_event = Jacobi_DebugTryReadStreamByIndex(Debug_Controller_in,
                                                                  Debug_PtrLoader_in,
                                                                  Debug_VectorLoader_in,
                                                                  Debug_MatrixLoader_in,
                                                                  Debug_Accumulator_in,
                                                                  Debug_CoeffLoader_in,
                                                                  Debug_Pair_in,
                                                                  Debug_PackWriter_in,
                                                                  Debug_HbmWriter_in,
                                                                  current_poll_index,
                                                                  event);
#else
                bool has_event = Jacobi_DebugTryReadStreamByIndex(Debug_Controller_in,
                                                                  Debug_PtrLoader_in,
                                                                  Debug_VectorLoader_in,
                                                                  Debug_CoeffLoader_in,
                                                                  Debug_Pair_in,
                                                                  Debug_PackWriter_in,
                                                                  Debug_HbmWriter_in,
                                                                  current_poll_index,
                                                                  event);
#endif
                poll_index = (current_poll_index == kJacobiDebugStreamCount - 1)
                                 ? 0
                                 : current_poll_index + 1;

                if (has_event) {
                    ++event_count;
                    const INDEX_TYPE source = event.source & 0x3f;
                    const INDEX_TYPE source_slot = Jacobi_DebugSourceSlot(source);
                    pending_addr[0] = 1;
                    pending_data[0] = event_count;
                    pending_addr[1] = 2;
                    pending_data[1] = Jacobi_DebugPackEvent(event);
                    pending_addr[2] = 3;
                    pending_data[2] = event.value;
                    pending_addr[3] = 16 + (event.source & 0x1f);
                    pending_data[3] = event.value;
                    pending_addr[4] = source_slot;
                    pending_data[4] = event.phase;
                    pending_addr[5] = source_slot + 1;
                    pending_data[5] = event.lane;
                    pending_addr[6] = source_slot + 2;
                    pending_data[6] = event.value;
                    pending_addr[7] = source_slot + 3;
                    pending_data[7] = event_count;
                    pending_count = 8;
                }
            }
        }
    }
}
