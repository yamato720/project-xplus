#pragma once

// CuperJacobiIteration 的 Jacobi 专用公共定义。
// 这个头只被 kernels/Cuper.cpp 间接包含，保存固定轮次迭代状态、帧元数据和小工具函数。

#include <ap_int.h>
#include <tapa.h>

#include "spmv_service_common.hpp"

// 当前 kernel 固定跑满 Max_iters；Status[0] 保持为 1，兼容已有 host/回归输出。
static constexpr INDEX_TYPE kJacobiStatusMaxIter = 1;

struct JacobiUpdateCommand {
    // stop 非 0 表示 Cuper 输出更新 task 退出；正常迭代命令中为 0。
    INDEX_TYPE stop;
    // 有效行数，用来屏蔽最后一个 float_v16 包里的 padding lane。
    INDEX_TYPE row_num;
    // 一轮 -Rx/x_next 需要处理的 float_v16 包数。
    INDEX_TYPE packet_count;
    // 迭代序号，只用于调试和结果回传，不参与数学计算。
    INDEX_TYPE iter;
};

struct JacobiUpdateDone {
    // X HBM writer 收齐本轮 write response 后发回 controller 的完成确认。
    INDEX_TYPE iter;
    INDEX_TYPE packet_count;
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

inline JacobiUpdateCommand Jacobi_MakeUpdateCommand(const INDEX_TYPE row_num,
                                                    const INDEX_TYPE iter) {
#pragma HLS inline
    JacobiUpdateCommand command;
    command.stop = 0;
    command.row_num = row_num;
    command.packet_count = spmv_service_num_float_v16_packets(row_num);
    command.iter = iter;
    return command;
}

inline JacobiUpdateCommand Jacobi_MakeUpdateStopCommand() {
#pragma HLS inline
    JacobiUpdateCommand command;
    command.stop = 1;
    command.row_num = 0;
    command.packet_count = 0;
    command.iter = 0;
    return command;
}

inline JacobiUpdateDone Jacobi_MakeUpdateDone(const INDEX_TYPE iter,
                                              const INDEX_TYPE packet_count) {
#pragma HLS inline
    JacobiUpdateDone done;
    done.iter = iter;
    done.packet_count = packet_count;
    return done;
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
