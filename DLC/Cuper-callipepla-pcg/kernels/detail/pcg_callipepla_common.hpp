#pragma once

#include <ap_int.h>
#include <tapa.h>

#include "Cuper.h"
#include "spmv_service_common.hpp"

static constexpr INDEX_TYPE kPcgCallipeplaStatusConverged = 0;
static constexpr INDEX_TYPE kPcgCallipeplaStatusMaxIter = 1;
static constexpr INDEX_TYPE kPcgCallipeplaStatusBreakdown = 2;

static constexpr INDEX_TYPE kPcgCallipeplaVectorSourceX = 0;
static constexpr INDEX_TYPE kPcgCallipeplaVectorSourceP = 1;
static constexpr double kPcgCallipeplaBreakdownEps = 1.0e-30;

static constexpr INDEX_TYPE kPcgCallipeplaStageBegin = 0;
static constexpr INDEX_TYPE kPcgCallipeplaStageEnd = 1;
static constexpr INDEX_TYPE kPcgCallipeplaStageStop = 2;

static constexpr INDEX_TYPE kPcgCallipeplaStageInitSpmv = 0;
static constexpr INDEX_TYPE kPcgCallipeplaStageInitZp = 1;
static constexpr INDEX_TYPE kPcgCallipeplaStageIterSpmv = 2;
static constexpr INDEX_TYPE kPcgCallipeplaStageDotAlpha = 3;
static constexpr INDEX_TYPE kPcgCallipeplaStageUpdateX = 4;
static constexpr INDEX_TYPE kPcgCallipeplaStageUpdateR = 5;
static constexpr INDEX_TYPE kPcgCallipeplaStageApplyMInv = 6;
static constexpr INDEX_TYPE kPcgCallipeplaStageDotRz = 7;
static constexpr INDEX_TYPE kPcgCallipeplaStageUpdateP = 8;
static constexpr INDEX_TYPE kPcgCallipeplaStageDotResidual = 9;
static constexpr INDEX_TYPE kPcgCallipeplaStageRound = 10;
static constexpr INDEX_TYPE kPcgCallipeplaStageTotal = 11;
static constexpr INDEX_TYPE kPcgCallipeplaStageCount = 12;

static constexpr INDEX_TYPE kPcgCallipeplaPhaseInitSpmv = 0;
static constexpr INDEX_TYPE kPcgCallipeplaPhaseInitZp = 1;
static constexpr INDEX_TYPE kPcgCallipeplaPhaseIterDot = 2;
static constexpr INDEX_TYPE kPcgCallipeplaPhaseUpdateX = 3;
static constexpr INDEX_TYPE kPcgCallipeplaPhaseUpdateR = 4;
static constexpr INDEX_TYPE kPcgCallipeplaPhaseApplyMInvDot = 5;
static constexpr INDEX_TYPE kPcgCallipeplaPhaseUpdateP = 6;

struct PcgCallipeplaSpmvVectorCommand {
    INDEX_TYPE stop;
    INDEX_TYPE vector_source;
    INDEX_TYPE bank;
};

struct PcgCallipeplaVectorCommand {
    INDEX_TYPE phase;
    INDEX_TYPE stop;
    INDEX_TYPE iter;
    INDEX_TYPE x_read_bank;
    INDEX_TYPE x_write_bank;
    INDEX_TYPE r_read_bank;
    INDEX_TYPE r_write_bank;
    INDEX_TYPE p_read_bank;
    INDEX_TYPE p_write_bank;
    double alpha;
    double beta;
};

struct PcgCallipeplaVectorResult {
    INDEX_TYPE phase;
    double rz;
    double rr;
    double p_ap;
};

struct PcgCallipeplaStageEvent {
    INDEX_TYPE stage;
    INDEX_TYPE op;
};

inline INDEX_TYPE pcg_callipepla_num_float_v16_packets(const INDEX_TYPE element_count) {
#pragma HLS inline
    return Cuper_NumFloatV16Packets(element_count);
}

inline INDEX_TYPE pcg_callipepla_num_double_v8_packets(const INDEX_TYPE element_count) {
#pragma HLS inline
    return Cuper_NumDoubleV8Packets(element_count);
}

inline INDEX_TYPE pcg_callipepla_num_checker_pe_outputs(const INDEX_TYPE row_num) {
#pragma HLS inline
    return Cuper_NumCheckerPeOutputs(row_num);
}

inline PcgCallipeplaSpmvVectorCommand pcg_callipepla_make_spmv_vector_command(
    const INDEX_TYPE vector_source,
    const INDEX_TYPE bank) {
#pragma HLS inline
    PcgCallipeplaSpmvVectorCommand command;
    command.stop = 0;
    command.vector_source = vector_source;
    command.bank = bank;
    return command;
}

inline PcgCallipeplaSpmvVectorCommand pcg_callipepla_make_spmv_vector_stop() {
#pragma HLS inline
    PcgCallipeplaSpmvVectorCommand command;
    command.stop = 1;
    command.vector_source = kPcgCallipeplaVectorSourceX;
    command.bank = 0;
    return command;
}

inline PcgCallipeplaVectorCommand pcg_callipepla_make_vector_command(
    const INDEX_TYPE phase,
    const INDEX_TYPE iter,
    const INDEX_TYPE x_read_bank,
    const INDEX_TYPE x_write_bank,
    const INDEX_TYPE r_read_bank,
    const INDEX_TYPE r_write_bank,
    const INDEX_TYPE p_read_bank,
    const INDEX_TYPE p_write_bank,
    const double alpha,
    const double beta) {
#pragma HLS inline
    PcgCallipeplaVectorCommand command;
    command.phase = phase;
    command.stop = 0;
    command.iter = iter;
    command.x_read_bank = x_read_bank;
    command.x_write_bank = x_write_bank;
    command.r_read_bank = r_read_bank;
    command.r_write_bank = r_write_bank;
    command.p_read_bank = p_read_bank;
    command.p_write_bank = p_write_bank;
    command.alpha = alpha;
    command.beta = beta;
    return command;
}

inline PcgCallipeplaVectorCommand pcg_callipepla_make_vector_stop() {
#pragma HLS inline
    PcgCallipeplaVectorCommand command = pcg_callipepla_make_vector_command(
        kPcgCallipeplaPhaseInitSpmv, 0, 0, 0, 0, 0, 0, 0, 0.0, 0.0);
    command.stop = 1;
    return command;
}

inline PcgCallipeplaVectorResult pcg_callipepla_make_vector_result(
    const INDEX_TYPE phase) {
#pragma HLS inline
    PcgCallipeplaVectorResult result;
    result.phase = phase;
    result.rz = 0.0;
    result.rr = 0.0;
    result.p_ap = 0.0;
    return result;
}

inline double pcg_callipepla_abs(const double value) {
#pragma HLS inline
    return value < 0.0 ? -value : value;
}

inline bool pcg_callipepla_invalid(const double value) {
#pragma HLS inline
    return value != value;
}

inline void pcg_callipepla_stage_mark(
    tapa::ostream<PcgCallipeplaStageEvent> &Stage_Event_out,
    const INDEX_TYPE stage,
    const INDEX_TYPE op) {
#pragma HLS inline
    PcgCallipeplaStageEvent event;
    event.stage = stage;
    event.op = op;
    Stage_Event_out.write(event);
}

inline void pcg_callipepla_send_spmv_command(
    tapa::ostream<CuperSpmvServiceCommand> &Ptr_Command_out,
    tapa::ostreams<CuperSpmvServiceCommand, HBM_CHANNEL_NUM> &Matrix_Command_out,
    tapa::ostream<PcgCallipeplaSpmvVectorCommand> &Vector_Command_out,
    const INDEX_TYPE vector_source,
    const INDEX_TYPE bank) {
#pragma HLS inline
    Ptr_Command_out.write(spmv_service_make_command());
send_matrix_command:
    for (INDEX_TYPE index = 0; index < HBM_CHANNEL_NUM; ++index) {
#pragma HLS unroll
        Matrix_Command_out[index].write(spmv_service_make_command());
    }
    Vector_Command_out.write(
        pcg_callipepla_make_spmv_vector_command(vector_source, bank));
}

inline void pcg_callipepla_send_spmv_stop(
    tapa::ostream<CuperSpmvServiceCommand> &Ptr_Command_out,
    tapa::ostreams<CuperSpmvServiceCommand, HBM_CHANNEL_NUM> &Matrix_Command_out,
    tapa::ostream<PcgCallipeplaSpmvVectorCommand> &Vector_Command_out) {
#pragma HLS inline
    Ptr_Command_out.write(spmv_service_make_stop_command());
send_matrix_stop:
    for (INDEX_TYPE index = 0; index < HBM_CHANNEL_NUM; ++index) {
#pragma HLS unroll
        Matrix_Command_out[index].write(spmv_service_make_stop_command());
    }
    Vector_Command_out.write(pcg_callipepla_make_spmv_vector_stop());
}
