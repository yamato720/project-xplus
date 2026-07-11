#pragma once

#include <ap_int.h>
#include <tapa.h>

#include "Cuper.h"
#include "spmv_service_common.hpp"

static constexpr INDEX_TYPE kPcgCallipeplaStatusConverged = 0;
static constexpr INDEX_TYPE kPcgCallipeplaStatusMaxIter = 1;
static constexpr INDEX_TYPE kPcgCallipeplaStatusBreakdown = 2;
static constexpr INDEX_TYPE kPcgCallipeplaProbeMagic = 0x43505242;

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

static constexpr INDEX_TYPE kPcgCallipeplaProbePhaseStop = 7;

static constexpr INDEX_TYPE kPcgCallipeplaProbeEventControllerStart = 10;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventValidationPassed = 11;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventInitFanoutDone = 12;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventValidationFailed = 13;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventTransactionEnter = 20;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventCommandBlocked = 21;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventCommandAccepted = 22;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventWaitResult = 23;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventResultReceived = 24;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventStopEnter = 25;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventStopAccepted = 26;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventControllerFinalStatus = 28;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventControllerDone = 29;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventAckStart = 30;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventAckCommandReceived = 31;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventAckResultSent = 32;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventAckStop = 33;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventPtrStart = 40;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventPtrLengthsRead = 41;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventPtrCommandReceived = 42;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventPtrBoundaryProgress = 43;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventPtrRoundDone = 44;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventPtrStop = 45;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventPeStart = 50;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventPeRoundDone = 51;
static constexpr INDEX_TYPE kPcgCallipeplaProbeEventPeStop = 52;

static constexpr INDEX_TYPE kPcgCallipeplaProbeTxSend = 0;
static constexpr INDEX_TYPE kPcgCallipeplaProbeTxWaitResult = 1;
static constexpr INDEX_TYPE kPcgCallipeplaProbeTxDone = 2;
static constexpr INDEX_TYPE kPcgCallipeplaProbeTxStop = 3;
static constexpr INDEX_TYPE kPcgCallipeplaProbeTxFinal = 4;

static constexpr INDEX_TYPE kPcgCallipeplaProbeFlagLastFull = 1 << 0;
static constexpr INDEX_TYPE kPcgCallipeplaProbeFlagLastWriteSuccess = 1 << 1;
static constexpr INDEX_TYPE kPcgCallipeplaProbeFlagControllerDone = 1 << 2;
static constexpr INDEX_TYPE kPcgCallipeplaProbeFlagAckStop = 1 << 3;
static constexpr INDEX_TYPE kPcgCallipeplaProbeFlagControllerDrop = 1 << 4;
static constexpr INDEX_TYPE kPcgCallipeplaProbeFlagAckDrop = 1 << 5;

static constexpr INDEX_TYPE kPcgCallipeplaLoaderProbeFlagPtrDone = 1 << 4;
static constexpr INDEX_TYPE kPcgCallipeplaLoaderProbeFlagPeDone = 1 << 5;

#if defined(CUPER_CALLIPEPLA_PROBE_ENABLED) && \
    (CUPER_CALLIPEPLA_PROBE_MODE_ID == 2 || \
     CUPER_CALLIPEPLA_PROBE_MODE_ID == 3)
#define CUPER_CALLIPEPLA_PROBE_EVENT_MONITOR 1
#endif

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

struct PcgCallipeplaProbeEvent {
    INDEX_TYPE event;
    INDEX_TYPE phase;
    INDEX_TYPE value0;
    INDEX_TYPE value1;
};

inline PcgCallipeplaProbeEvent pcg_callipepla_make_probe_event(
    const INDEX_TYPE event,
    const INDEX_TYPE phase,
    const INDEX_TYPE value0,
    const INDEX_TYPE value1) {
#pragma HLS inline
    PcgCallipeplaProbeEvent probe_event;
    probe_event.event = event;
    probe_event.phase = phase;
    probe_event.value0 = value0;
    probe_event.value1 = value1;
    return probe_event;
}

inline INDEX_TYPE pcg_callipepla_probe_saturating_increment(
    const INDEX_TYPE value,
    const INDEX_TYPE limit) {
#pragma HLS inline
    return value < limit ? value + 1 : limit;
}

inline void pcg_callipepla_probe_try_write_event(
    tapa::ostream<PcgCallipeplaProbeEvent> &Probe_Event_out,
    INDEX_TYPE &drop_count,
    const INDEX_TYPE event,
    const INDEX_TYPE phase,
    const INDEX_TYPE value0,
    const INDEX_TYPE value1) {
#pragma HLS inline
    if (!Probe_Event_out.try_write(
            pcg_callipepla_make_probe_event(event, phase, value0, value1))) {
        drop_count = pcg_callipepla_probe_saturating_increment(
            drop_count, 0xff);
    }
}

inline INDEX_TYPE pcg_callipepla_pack_controller_probe_value(
    const INDEX_TYPE full_count,
    const bool full_before,
    const INDEX_TYPE drop_count) {
#pragma HLS inline
    const INDEX_TYPE saturated_full_count =
        full_count < 0x01000000 ? full_count : 0x00ffffff;
    const INDEX_TYPE saturated_drop_count =
        drop_count < 0x80 ? drop_count : 0x7f;
    ap_uint<32> packed = static_cast<unsigned int>(saturated_full_count);
    packed[24] = full_before;
    packed.range(31, 25) = saturated_drop_count;
    return static_cast<INDEX_TYPE>(packed.to_uint());
}

inline INDEX_TYPE pcg_callipepla_pack_controller_done_value(
    const INDEX_TYPE x_bank,
    const INDEX_TYPE r_bank,
    const INDEX_TYPE p_bank,
    const INDEX_TYPE spmv_rounds,
    const INDEX_TYPE drop_count) {
#pragma HLS inline
    const INDEX_TYPE saturated_drop_count =
        drop_count < 0x100 ? drop_count : 0xff;
    ap_uint<32> packed = 0;
    packed[0] = x_bank & 1;
    packed[1] = r_bank & 1;
    packed[2] = p_bank & 1;
    packed.range(23, 8) = spmv_rounds & 0xffff;
    packed.range(31, 24) = saturated_drop_count;
    return static_cast<INDEX_TYPE>(packed.to_uint());
}

inline INDEX_TYPE pcg_callipepla_pack_ack_stop_value(
    const INDEX_TYPE result_count,
    const INDEX_TYPE drop_count) {
#pragma HLS inline
    const INDEX_TYPE saturated_drop_count =
        drop_count < 0x100 ? drop_count : 0xff;
    ap_uint<32> packed = 0;
    packed.range(15, 0) = result_count & 0xffff;
    packed.range(31, 24) = saturated_drop_count;
    return static_cast<INDEX_TYPE>(packed.to_uint());
}

inline INDEX_TYPE pcg_callipepla_pack_controller_counts_value(
    const INDEX_TYPE result_count,
    const INDEX_TYPE drop_count) {
#pragma HLS inline
    return pcg_callipepla_pack_ack_stop_value(result_count, drop_count);
}

inline INDEX_TYPE pcg_callipepla_pack_loader_count_value(
    const INDEX_TYPE count,
    const INDEX_TYPE drop_count) {
#pragma HLS inline
    const INDEX_TYPE saturated_count =
        count < 0x01000000 ? count : 0x00ffffff;
    const INDEX_TYPE saturated_drop_count =
        drop_count < 0x100 ? drop_count : 0xff;
    ap_uint<32> packed = static_cast<unsigned int>(saturated_count);
    packed.range(31, 24) = saturated_drop_count;
    return static_cast<INDEX_TYPE>(packed.to_uint());
}

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
