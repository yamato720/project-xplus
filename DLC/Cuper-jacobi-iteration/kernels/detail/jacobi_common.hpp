#pragma once

// CuperJacobiIteration 的 Jacobi 专用公共定义。
// 这个头只被 kernels/Cuper.cpp 间接包含，保存迭代状态码、帧元数据和小工具函数。

#include <ap_int.h>
#include <tapa.h>

#include "spmv_service_common.hpp"

// Status[0] 的返回值约定。host 侧用它区分正常收敛、达到最大迭代次数、
// 以及对角线逆/结果出现 NaN 等 breakdown 情况。
static constexpr INDEX_TYPE kJacobiStatusConverged = 0;
static constexpr INDEX_TYPE kJacobiStatusMaxIter = 1;
static constexpr INDEX_TYPE kJacobiStatusBreakdown = 2;

// X0/X1 双缓冲编号。每轮 Jacobi 读一个旧解 buffer，写另一个新解 buffer。
static constexpr INDEX_TYPE kJacobiBufferX0 = 0;
static constexpr INDEX_TYPE kJacobiBufferX1 = 1;

struct JacobiFrame {
    // stop 非 0 表示 update service 退出；正常迭代帧中为 0。
    INDEX_TYPE stop;
    // 本轮 SpMV 和 update 从哪个旧解 buffer 读取 x_old。
    INDEX_TYPE read_from_x1;
    // 本轮 update 把 x_next 写入哪个 buffer。
    INDEX_TYPE write_to_x1;
    // 有效行数，用来屏蔽最后一个 float_v16 包里的 padding lane。
    INDEX_TYPE row_num;
    // 一轮 -Rx/x_next 需要处理的 float_v16 包数。
    INDEX_TYPE packet_count;
    // 迭代序号，只用于调试和结果回传，不参与数学计算。
    INDEX_TYPE iter;
};

struct JacobiUpdateResult {
    // 本轮 max_i |x_next[i] - x_old[i]|，controller 用它和 Tau 比较。
    float diff_max;
    // 非 0 表示 update 发现 NaN/非法数值，controller 会按 breakdown 退出。
    INDEX_TYPE breakdown;
    // 本轮实际写入的最终 buffer 编号，host 通过 Status[1] 读取。
    INDEX_TYPE wrote_x1;
    // 对应的迭代序号，当前主要用于调试保留。
    INDEX_TYPE iter;
};

struct JacobiStageEvent {
    // stage/op 是 controller 发给 Jacobi_Stage_Timer 的轻量事件。
    // 当前只统计两类：每轮 SpMV+update 等待，以及 controller 主体总时间。
    INDEX_TYPE stage;
    INDEX_TYPE op;
};

static constexpr INDEX_TYPE kJacobiStageBegin = 0;
static constexpr INDEX_TYPE kJacobiStageEnd = 1;
static constexpr INDEX_TYPE kJacobiStageStop = 2;
static constexpr INDEX_TYPE kJacobiStageSpmvUpdate = 0;
static constexpr INDEX_TYPE kJacobiStageControllerTotal = 1;
static constexpr INDEX_TYPE kJacobiStageCount = 2;

// 构造一轮正常 update 帧。controller 同时把 SpMV command 和这个 frame 发出，
// 使后级 update service 知道本轮 -Rx 属于哪个旧解 buffer。
inline JacobiFrame Jacobi_MakeFrame(const INDEX_TYPE read_from_x1,
                                    const INDEX_TYPE write_to_x1,
                                    const INDEX_TYPE row_num,
                                    const INDEX_TYPE iter) {
#pragma HLS inline
    JacobiFrame frame;
    frame.stop = 0;
    frame.read_from_x1 = read_from_x1;
    frame.write_to_x1 = write_to_x1;
    frame.row_num = row_num;
    frame.packet_count = spmv_service_num_float_v16_packets(row_num);
    frame.iter = iter;
    return frame;
}

// 构造 update service 的退出帧。SpMV service 自己用 command/stop token 退出，
// update service 则通过这个 frame 退出。
inline JacobiFrame Jacobi_MakeStopFrame() {
#pragma HLS inline
    JacobiFrame frame;
    frame.stop = 1;
    frame.read_from_x1 = 0;
    frame.write_to_x1 = 0;
    frame.row_num = 0;
    frame.packet_count = 0;
    frame.iter = 0;
    return frame;
}

// HLS 内联版绝对值，避免在 kernel 侧引入不必要的库函数路径。
inline float Jacobi_AbsFloat(const float value) {
#pragma HLS inline
    return value < 0.0f ? -value : value;
}

// float 自身不相等即可判断 NaN；这里用于快速标记 Jacobi 数值 breakdown。
inline bool Jacobi_InvalidFloat(const float value) {
#pragma HLS inline
    return value != value;
}

inline void Jacobi_StageMark(tapa::ostream<JacobiStageEvent> &Stage_Event_out,
                             const INDEX_TYPE stage,
                             const INDEX_TYPE op) {
#pragma HLS inline
    JacobiStageEvent event;
    event.stage = stage;
    event.op = op;
    Stage_Event_out.write(event);
}
