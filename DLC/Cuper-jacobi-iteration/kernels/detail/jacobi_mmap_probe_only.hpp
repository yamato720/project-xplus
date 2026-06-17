#pragma once

// 最小 mmap 写回探针 top。
//
// 这个 top 不接入 Cuper SpMV、Jacobi update、stage timer、debug event stream 或
// feedback token。它只写 Status/Metrics/Debug 的固定槽位并等待 write response 后
// 返回，用来隔离 TAPA/FRT Finish、m_axi 写回、HBM bank 分配和 timing 问题。

#include <tapa.h>

#include "jacobi_common.hpp"
#include "jacobi_deadlock_debug.hpp"

inline void Jacobi_MmapProbeWriteInt(tapa::async_mmap<INDEX_TYPE> &Out,
                                     const INDEX_TYPE addr,
                                     const INDEX_TYPE value) {
#pragma HLS inline
    Out.write_addr.write(addr);
    Out.write_data.write(value);
    uint8_t num_responses = 0;
wait_probe_int_resp:
    while (!Out.write_resp.try_read(num_responses)) {
#pragma HLS pipeline II=1
    }
}

inline void Jacobi_MmapProbeWriteDouble(tapa::async_mmap<double> &Out,
                                        const INDEX_TYPE addr,
                                        const double value) {
#pragma HLS inline
    Out.write_addr.write(addr);
    Out.write_data.write(value);
    uint8_t num_responses = 0;
wait_probe_double_resp:
    while (!Out.write_resp.try_read(num_responses)) {
#pragma HLS pipeline II=1
    }
}

void Jacobi_MmapProbeOnlyTask(tapa::async_mmap<INDEX_TYPE> &Status,
                              tapa::async_mmap<double> &Metrics,
                              tapa::async_mmap<INDEX_TYPE> &Debug,
                              const INDEX_TYPE Row_num,
                              const INDEX_TYPE Max_iters,
                              const INDEX_TYPE Column_num) {
    const INDEX_TYPE packet_count = spmv_service_num_float_v16_packets(Row_num);

    // Status[0..3] 作为 micro top 自身的返回标记；Status[8..11] 复用 full
    // graph 入口 probe 的槽位，方便直接对比两种 top 的上板日志。
    Jacobi_MmapProbeWriteInt(Status, 0, kJacobiDebugProbeMagic);
    Jacobi_MmapProbeWriteInt(Status, 1, Row_num);
    Jacobi_MmapProbeWriteInt(Status, 2, Max_iters);
    Jacobi_MmapProbeWriteInt(Status, 3, Column_num);
    Jacobi_MmapProbeWriteInt(Status, 8, kJacobiDebugProbeMagic);
    Jacobi_MmapProbeWriteInt(Status, 9, Row_num);
    Jacobi_MmapProbeWriteInt(Status, 10, Max_iters);
    Jacobi_MmapProbeWriteInt(Status, 11, packet_count);

    Jacobi_MmapProbeWriteDouble(Metrics, 0, static_cast<double>(kJacobiDebugProbeMagic));
    Jacobi_MmapProbeWriteDouble(Metrics, 1, static_cast<double>(Row_num));
    Jacobi_MmapProbeWriteDouble(Metrics, 2, static_cast<double>(Max_iters));
    Jacobi_MmapProbeWriteDouble(Metrics, 3, static_cast<double>(Column_num));
    Jacobi_MmapProbeWriteDouble(Metrics, 8, static_cast<double>(kJacobiDebugProbeMagic));
    Jacobi_MmapProbeWriteDouble(Metrics, 9, static_cast<double>(Row_num));
    Jacobi_MmapProbeWriteDouble(Metrics, 10, static_cast<double>(Max_iters));
    Jacobi_MmapProbeWriteDouble(Metrics, 11, static_cast<double>(packet_count));

    Jacobi_MmapProbeWriteInt(Debug, 0, kJacobiDebugProbeMagic);
    Jacobi_MmapProbeWriteInt(Debug, kJacobiDebugProbeSlotMagic, kJacobiDebugProbeMagic);
    Jacobi_MmapProbeWriteInt(Debug, kJacobiDebugProbeSlotStreamCount, kJacobiDebugStreamCount);
    Jacobi_MmapProbeWriteInt(Debug, kJacobiDebugProbeSlotPhase, kJacobiDebugPhaseEnterRound);
    Jacobi_MmapProbeWriteInt(Debug, kJacobiDebugProbeSlotStopDrain, kJacobiDebugStopDrainCycles);
}

void CuperJacobiMmapProbeOnly(tapa::mmap<INDEX_TYPE> Status,
                              tapa::mmap<double> Metrics,
                              tapa::mmap<INDEX_TYPE> Debug,
                              const INDEX_TYPE Row_num,
                              const INDEX_TYPE Max_iters,
                              const INDEX_TYPE Column_num) {
    tapa::task().invoke(Jacobi_MmapProbeOnlyTask,
                        Status,
                        Metrics,
                        Debug,
                        Row_num,
                        Max_iters,
                        Column_num);
}
