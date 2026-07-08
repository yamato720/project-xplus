#pragma once

// 可复用 Cuper SpMV service 的中性公共定义。
// 这里不写 PCG/Jacobi 语义，只描述“跑一轮 SpMV”和“停止常驻 task”的协议。

#include <ap_int.h>
#include <tapa.h>

#include "Cuper.h"

struct CuperSpmvServiceCommand {
    // 传给 Cuper SpMV 服务流水的一次运行命令。
    // stop 非 0 表示退出常驻服务任务；stop 为 0 表示启动一轮 SpMV。
    INDEX_TYPE stop;
};

// 常驻 SpMV service 的停止令牌。它走 PE_Param/Vector_Y_Param 普通 stream，
// 避免另开一套复杂控制口。
static constexpr INDEX_TYPE kSpmvServiceStopToken = -1;

// 下面几个 helper 包装 Cuper 原有的数量计算，避免 service 层直接散落
// “16 个 float 一包”“accumulator 输出按 HBM 对齐”这些细节。
inline INDEX_TYPE spmv_service_num_float_v16_packets(const INDEX_TYPE element_count) {
#pragma HLS inline
    return Cuper_NumFloatV16Packets(element_count);
}

inline INDEX_TYPE spmv_service_num_accumulator_outputs(const INDEX_TYPE row_num) {
#pragma HLS inline
    return Cuper_NumAccumulatorOutputs(row_num);
}

inline INDEX_TYPE spmv_service_num_checker_pe_outputs(const INDEX_TYPE row_num) {
#pragma HLS inline
    return Cuper_NumCheckerPeOutputs(row_num);
}

inline CuperSpmvServiceCommand spmv_service_make_command() {
#pragma HLS inline
    CuperSpmvServiceCommand command;
    command.stop = 0;
    return command;
}

// 停止命令只给 loader 类 task；Core/Accumulator 链上的停止通过
// kSpmvServiceStopToken 随 PE_Param/Vector_Y_Param 继续传播。
inline CuperSpmvServiceCommand spmv_service_make_stop_command() {
#pragma HLS inline
    CuperSpmvServiceCommand command;
    command.stop = 1;
    return command;
}

inline void spmv_service_broadcast_command(
    tapa::ostreams<CuperSpmvServiceCommand, 2> &Command_out,
    tapa::ostreams<CuperSpmvServiceCommand, HBM_CHANNEL_NUM> &Matrix_Command_out,
    const CuperSpmvServiceCommand command) {
#pragma HLS inline
    // 一轮 SpMV 要求 ptr loader、vector loader、16 路 matrix loader 同时收到
    // 同一条 command。controller 只调用这个 helper，不直接逐个 task 写 stream。
send_common_command:
    for (INDEX_TYPE index = 0; index < 2; ++index) {
#pragma HLS unroll
        // Command_out[0] 给 ptr loader，Command_out[1] 给 vector loader。
        Command_out[index].write(command);
    }
send_matrix_command:
    for (INDEX_TYPE index = 0; index < HBM_CHANNEL_NUM; ++index) {
#pragma HLS unroll
        // 每个 matrix loader 独立一条命令流，保证 16 路 HBM loader 同步启动/停止。
        Matrix_Command_out[index].write(command);
    }
}

inline void spmv_service_send_compute_command(
    tapa::ostreams<CuperSpmvServiceCommand, 2> &Command_out) {
#pragma HLS inline
send_compute_command:
    for (INDEX_TYPE index = 0; index < 2; ++index) {
#pragma HLS unroll
        // Command_out[0] 给 ptr loader，Command_out[1] 给 vector loader。
        Command_out[index].write(spmv_service_make_command());
    }
}

inline void spmv_service_send_matrix_command(
    tapa::ostreams<CuperSpmvServiceCommand, HBM_CHANNEL_NUM> &Matrix_Command_out) {
#pragma HLS inline
send_matrix_command_only:
    for (INDEX_TYPE index = 0; index < HBM_CHANNEL_NUM; ++index) {
#pragma HLS unroll
        Matrix_Command_out[index].write(spmv_service_make_command());
    }
}

inline void spmv_service_send_command(
    tapa::ostreams<CuperSpmvServiceCommand, 2> &Command_out,
    tapa::ostreams<CuperSpmvServiceCommand, HBM_CHANNEL_NUM> &Matrix_Command_out) {
#pragma HLS inline
    spmv_service_broadcast_command(Command_out,
                                   Matrix_Command_out,
                                   spmv_service_make_command());
}

inline void spmv_service_send_compute_stop(
    tapa::ostreams<CuperSpmvServiceCommand, 2> &Command_out) {
#pragma HLS inline
send_compute_stop:
    for (INDEX_TYPE index = 0; index < 2; ++index) {
#pragma HLS unroll
        Command_out[index].write(spmv_service_make_stop_command());
    }
}

inline void spmv_service_send_matrix_stop(
    tapa::ostreams<CuperSpmvServiceCommand, HBM_CHANNEL_NUM> &Matrix_Command_out) {
#pragma HLS inline
send_matrix_stop:
    for (INDEX_TYPE index = 0; index < HBM_CHANNEL_NUM; ++index) {
#pragma HLS unroll
        Matrix_Command_out[index].write(spmv_service_make_stop_command());
    }
}

inline void spmv_service_send_stop(
    tapa::ostreams<CuperSpmvServiceCommand, 2> &Command_out,
    tapa::ostreams<CuperSpmvServiceCommand, HBM_CHANNEL_NUM> &Matrix_Command_out) {
#pragma HLS inline
    spmv_service_broadcast_command(Command_out,
                                   Matrix_Command_out,
                                   spmv_service_make_stop_command());
}
