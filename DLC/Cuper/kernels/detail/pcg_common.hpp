#pragma once

// Private implementation header for kernels/Cuper.cpp.
// It contains task definitions and should not be included by another translation unit.

#include <ap_int.h>
#include <tapa.h>

#include "Cuper.h"

struct CuperSpmvCommand {
    // 传给 Cuper SpMV 服务流水的一次运行命令。
    // 在 CuperPcg 中每次 PCG 需要 A*x0 或 A*p 时，controller 都发送一次。
    // 一条 command 固定只触发一次 SpMV；PCG 的多轮迭代由 controller
    // 多次发送 command 表达，不再让 service 内部重复跑同一个 SpMV。
    // stop 非 0 表示退出常驻服务任务；各 loader/core/checker 收到后要有限返回。
    // vector_source 决定向量 loader 本轮从 X_spmv 还是 P_spmv 读 packed 输入。
    INDEX_TYPE stop;
    INDEX_TYPE vector_source;
};

struct PcgStageEvent {
    // stage/op 是 controller 发给 Pcg_Stage_Timer 的轻量事件。
    // stage 使用 kPcgStage* 编号；op 为 begin/end/stop。
    INDEX_TYPE stage;
    INDEX_TYPE op;
};

// Status[0] 的编码。host 只把 converged/max_iter 当作正常返回；
// breakdown 表示 p_ap、rz、tau 等数值异常或非法输入。
static constexpr INDEX_TYPE kPcgStatusConverged = 0;
static constexpr INDEX_TYPE kPcgStatusMaxIter = 1;
static constexpr INDEX_TYPE kPcgStatusBreakdown = 2;
// 常驻 task 链的停止令牌。它走 PE_Param/Vector_Y_Param 这类普通 stream，
// 避免另开一套复杂控制口。
static constexpr INDEX_TYPE kPcgStopToken = -1;
static constexpr INDEX_TYPE kPcgStageBegin = 0;
static constexpr INDEX_TYPE kPcgStageEnd = 1;
static constexpr INDEX_TYPE kPcgStageStop = 2;
// Metrics[16..24] 的 cycle 统计对应这些 stage。work-tick 统计在
// Metrics[5..14]，两者都用于定位 full-PCG 版到底慢在 SpMV 还是 PCG 周边。
static constexpr INDEX_TYPE kPcgStageInitSpmv = 0;
static constexpr INDEX_TYPE kPcgStageInitZp = 1;
static constexpr INDEX_TYPE kPcgStageIterSpmv = 2;
static constexpr INDEX_TYPE kPcgStageDotPAp = 3;
static constexpr INDEX_TYPE kPcgStageUpdateXr = 4;
static constexpr INDEX_TYPE kPcgStageUpdateZ = 5;
static constexpr INDEX_TYPE kPcgStageUpdateP = 6;
static constexpr INDEX_TYPE kPcgStageControllerTotal = 7;
static constexpr INDEX_TYPE kPcgStageCount = 8;
// packed 向量输入来源。X 只用于初始化 A*x0，P 用于每轮 A*p。
static constexpr INDEX_TYPE kPcgVectorSourceX = 0;
static constexpr INDEX_TYPE kPcgVectorSourceP = 1;
// PCG 分母过小直接判 breakdown，避免 alpha/beta 生成 inf/NaN 后污染全向量。
static constexpr double kPcgBreakdownEps = 1.0e-30;

inline double pcg_abs(const double value) {
#pragma HLS inline
    return value < 0.0 ? -value : value;
}

inline bool pcg_invalid(const double value) {
#pragma HLS inline
    // HLS 里避免依赖 std::isnan；NaN 唯一不等于自身。
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
