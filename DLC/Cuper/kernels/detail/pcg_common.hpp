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

struct PcgVectorCommand {
    // controller 发给 PCG 向量 worker 的阶段命令。
    // stop 非 0 表示退出常驻 worker；row_num 是本轮向量长度；
    // scalar 用于 alpha/beta 这类阶段标量，未使用时保持 0。
    INDEX_TYPE stop;
    INDEX_TYPE row_num;
    double scalar;
};

struct PcgUpdateZResult {
    // update_z worker 完成后返回新的归约标量。
    // breakdown 非 0 表示 rz/rr 已经产生 NaN。
    double rz;
    double rr;
    INDEX_TYPE breakdown;
};

struct PcgUpdateZReadPacket {
    // update_z 的 HBM shell -> compute 数据包。
    // compute 只看 stream，不直接访问 R/M_inv HBM。
    double_v8 r;
    double_v8 minv;
};

struct PcgUpdateZWritePacket {
    // update_z compute -> HBM writer 数据包。
    double_v8 z;
};

struct PcgUpdatePReadPacket {
    // update_p 的 HBM shell -> compute 数据包。一个包对应一个
    // float_v16 P_spmv 输出，也就是最多两个 double_v8 Z/P 输入。
    double_v8 z_lo;
    double_v8 p_lo;
    double_v8 z_hi;
    double_v8 p_hi;
    INDEX_TYPE has_hi;
};

struct PcgUpdatePWritePacket {
    // update_p compute -> HBM writer 数据包。
    double_v8 p_lo;
    double_v8 p_hi;
    float_v16 p_spmv;
    INDEX_TYPE has_hi;
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
// Metrics[16..24] 的 cycle 统计对应这些 stage。当前 packed-double
// 架构里 p^T AP 已融合进 iter_spmv 接收路径，kPcgStageDotPAp 保留为
// 兼容占位，不再单独发 begin/end 事件。Metrics[5..15] 存的是各阶段
// 按当前数据通路估算的 packed memory packet work，不是实测 cycle。
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
// FP64 reduction banking. Each lane gets several independent accumulators so
// the outer packet loop is not limited by one scalar FP adder recurrence.
static constexpr INDEX_TYPE kPcgReductionBanks = 8;
// PCG 分母过小直接判 breakdown，避免 alpha/beta 生成 inf/NaN 后污染全向量。
static constexpr double kPcgBreakdownEps = 1.0e-30;

inline INDEX_TYPE pcg_num_float_v16_packets(const INDEX_TYPE element_count) {
#pragma HLS inline
    return Cuper_NumFloatV16Packets(element_count);
}

inline INDEX_TYPE pcg_num_double_v8_packets(const INDEX_TYPE element_count) {
#pragma HLS inline
    return Cuper_NumDoubleV8Packets(element_count);
}

inline INDEX_TYPE pcg_num_accumulator_init_groups(const INDEX_TYPE row_num) {
#pragma HLS inline
    return Cuper_NumAccumulatorInitGroups(row_num);
}

inline INDEX_TYPE pcg_num_accumulator_outputs(const INDEX_TYPE row_num) {
#pragma HLS inline
    return Cuper_NumAccumulatorOutputs(row_num);
}

inline INDEX_TYPE pcg_num_checker_pe_outputs(const INDEX_TYPE row_num) {
#pragma HLS inline
    return Cuper_NumCheckerPeOutputs(row_num);
}

inline CuperSpmvCommand pcg_make_spmv_command(const INDEX_TYPE vector_source) {
#pragma HLS inline
    CuperSpmvCommand command;
    command.stop = 0;
    command.vector_source = vector_source;
    return command;
}

inline CuperSpmvCommand pcg_make_spmv_stop_command() {
#pragma HLS inline
    CuperSpmvCommand command;
    command.stop = 1;
    // stop 命令不再触发向量读取；保留 X 作为无害默认值，方便调试波形。
    command.vector_source = kPcgVectorSourceX;
    return command;
}

inline void pcg_broadcast_spmv_command(
    tapa::ostreams<CuperSpmvCommand, 2> &Command_out,
    tapa::ostreams<CuperSpmvCommand, HBM_CHANNEL_NUM> &Matrix_Command_out,
    const CuperSpmvCommand command) {
#pragma HLS inline
send_common_command:
    for (INDEX_TYPE index = 0; index < 2; ++index) {
#pragma HLS unroll
        // Command_out[0] 给 ptr loader，Command_out[1] 给 vector loader。
        Command_out[index].write(command);
    }
send_common_matrix_command:
    for (INDEX_TYPE index = 0; index < HBM_CHANNEL_NUM; ++index) {
#pragma HLS unroll
        // 每个 matrix loader 独立一条命令流，保证 16 路 HBM loader 同步启动/停止。
        Matrix_Command_out[index].write(command);
    }
}

inline void pcg_send_spmv_command(
    tapa::ostreams<CuperSpmvCommand, 2> &Command_out,
    tapa::ostreams<CuperSpmvCommand, HBM_CHANNEL_NUM> &Matrix_Command_out,
    const INDEX_TYPE vector_source) {
#pragma HLS inline
    pcg_broadcast_spmv_command(Command_out,
                               Matrix_Command_out,
                               pcg_make_spmv_command(vector_source));
}

inline void pcg_send_spmv_stop(
    tapa::ostreams<CuperSpmvCommand, 2> &Command_out,
    tapa::ostreams<CuperSpmvCommand, HBM_CHANNEL_NUM> &Matrix_Command_out) {
#pragma HLS inline
    pcg_broadcast_spmv_command(Command_out,
                               Matrix_Command_out,
                               pcg_make_spmv_stop_command());
}

inline PcgVectorCommand pcg_make_vector_command(const INDEX_TYPE row_num,
                                                const double scalar) {
#pragma HLS inline
    PcgVectorCommand command;
    command.stop = 0;
    command.row_num = row_num;
    command.scalar = scalar;
    return command;
}

inline PcgVectorCommand pcg_make_vector_stop_command() {
#pragma HLS inline
    PcgVectorCommand command;
    command.stop = 1;
    command.row_num = 0;
    command.scalar = 0.0;
    return command;
}

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
