#pragma once

// CuperJacobiIteration 的 Jacobi 专用公共定义。
// 这个头只被 kernels/Cuper.cpp 间接包含，保存固定轮次迭代状态、帧元数据和小工具函数。

#include <ap_int.h>
#include <tapa.h>

#include "spmv_service_common.hpp"

// 当前 kernel 固定跑满 Max_iters；Status[0] 保持为 1，兼容已有 host/回归输出。
static constexpr INDEX_TYPE kJacobiStatusMaxIter = 1;

struct JacobiFrame {
    // stop 非 0 表示 Cuper 输出更新 task 退出；正常迭代帧中为 0。
    INDEX_TYPE stop;
    // 固定轮次上限；输出更新 task 用它判断是否生成下一轮 token。
    INDEX_TYPE max_iters;
    // 有效行数，用来屏蔽最后一个 float_v16 包里的 padding lane。
    INDEX_TYPE row_num;
    // 一轮 -Rx/x_next 需要处理的 float_v16 包数。
    INDEX_TYPE packet_count;
    // 迭代序号，只用于调试和结果回传，不参与数学计算。
    INDEX_TYPE iter;
};

struct JacobiRoundToken {
    // stop 非 0 表示所有正常迭代轮次已经完成，dispatcher 应广播 stop 并写状态。
    INDEX_TYPE stop;
    // 已完成轮数；stop token 上用它写 Status[2]。
    INDEX_TYPE iterations_done;
    // 固定轮次上限。
    INDEX_TYPE max_iters;
    // 有效行数和包数，随 token 走，避免下游重复解释 scalar。
    INDEX_TYPE row_num;
    INDEX_TYPE packet_count;
    // 当前轮次编号。
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

inline JacobiRoundToken Jacobi_MakeRoundToken(const INDEX_TYPE row_num,
                                              const INDEX_TYPE max_iters,
                                              const INDEX_TYPE iter) {
#pragma HLS inline
    JacobiRoundToken token;
    token.stop = 0;
    token.iterations_done = iter;
    token.max_iters = max_iters;
    token.row_num = row_num;
    token.packet_count = spmv_service_num_float_v16_packets(row_num);
    token.iter = iter;
    return token;
}

inline JacobiRoundToken Jacobi_MakeStopToken(const INDEX_TYPE row_num,
                                             const INDEX_TYPE max_iters,
                                             const INDEX_TYPE iterations_done) {
#pragma HLS inline
    JacobiRoundToken token;
    token.stop = 1;
    token.iterations_done = iterations_done;
    token.max_iters = max_iters;
    token.row_num = row_num;
    token.packet_count = spmv_service_num_float_v16_packets(row_num);
    token.iter = iterations_done;
    return token;
}

// 构造一轮正常输出更新帧。dispatcher 按 round token 同步发 SpMV command 和 frame，
// 输出更新 task 用 frame 判断本轮行数、包数、写回 buffer 和是否生成下一轮 token。
inline JacobiFrame Jacobi_MakeFrame(const JacobiRoundToken token) {
#pragma HLS inline
    JacobiFrame frame;
    frame.stop = token.stop;
    frame.max_iters = token.max_iters;
    frame.row_num = token.row_num;
    frame.packet_count = token.packet_count;
    frame.iter = token.iter;
    return frame;
}

inline JacobiFrame Jacobi_MakeFrame(const INDEX_TYPE max_iters,
                                    const INDEX_TYPE row_num,
                                    const INDEX_TYPE iter) {
#pragma HLS inline
    JacobiFrame frame;
    frame.stop = 0;
    frame.max_iters = max_iters;
    frame.row_num = row_num;
    frame.packet_count = spmv_service_num_float_v16_packets(row_num);
    frame.iter = iter;
    return frame;
}

// 构造输出更新 task 的退出帧。SpMV service 自己用 command/stop token 退出，
// Cuper 输出更新 task 则通过这个 frame 退出。
inline JacobiFrame Jacobi_MakeStopFrame() {
#pragma HLS inline
    JacobiFrame frame;
    frame.stop = 1;
    frame.max_iters = 0;
    frame.row_num = 0;
    frame.packet_count = 0;
    frame.iter = 0;
    return frame;
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
