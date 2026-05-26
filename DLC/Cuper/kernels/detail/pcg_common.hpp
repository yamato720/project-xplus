#pragma once

// Private implementation header for kernels/Cuper.cpp.
// It contains task definitions and should not be included by another translation unit.

#include <ap_int.h>
#include <tapa.h>

#include "Cuper.h"

struct CuperSpmvCommand {
    // 传给 Cuper SpMV 服务流水的一次运行命令。
    // 在 CuperPcg 中每次 PCG 需要 A*x0 或 A*p 时，controller 都发送一次。
    INDEX_TYPE iteration_num;
    INDEX_TYPE stop;
};

struct PcgStageEvent {
    INDEX_TYPE stage;
    INDEX_TYPE op;
};

static constexpr INDEX_TYPE kPcgStatusConverged = 0;
static constexpr INDEX_TYPE kPcgStatusMaxIter = 1;
static constexpr INDEX_TYPE kPcgStatusBreakdown = 2;
static constexpr INDEX_TYPE kPcgStopToken = -1;
static constexpr INDEX_TYPE kPcgStageBegin = 0;
static constexpr INDEX_TYPE kPcgStageEnd = 1;
static constexpr INDEX_TYPE kPcgStageStop = 2;
static constexpr INDEX_TYPE kPcgStageInitSpmv = 0;
static constexpr INDEX_TYPE kPcgStageInitZp = 1;
static constexpr INDEX_TYPE kPcgStageIterSpmv = 2;
static constexpr INDEX_TYPE kPcgStageUpdateXr = 3;
static constexpr INDEX_TYPE kPcgStageUpdateZ = 4;
static constexpr INDEX_TYPE kPcgStageUpdateP = 5;
static constexpr INDEX_TYPE kPcgStageControllerTotal = 6;
static constexpr INDEX_TYPE kPcgStageCount = 7;
static constexpr double kPcgBreakdownEps = 1.0e-30;

inline double pcg_abs(const double value) {
#pragma HLS inline
    return value < 0.0 ? -value : value;
}

inline bool pcg_invalid(const double value) {
#pragma HLS inline
    return value != value;
}

inline void pcg_stage_mark(tapa::ostream<PcgStageEvent> &Stage_Event_out,
                           const INDEX_TYPE stage,
                           const INDEX_TYPE op) {
#pragma HLS inline
    PcgStageEvent event;
    event.stage = stage;
    event.op = op;
    Stage_Event_out.write(event);
}
