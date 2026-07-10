#pragma once

#include <ap_int.h>
#include <tapa.h>

#include "pcg_callipepla_common.hpp"
#include "spmv_service_common.hpp"

inline void PcgCallipepla_Probe_WriteHeader(
    tapa::mmap<INDEX_TYPE> &Status,
    const INDEX_TYPE mode,
    const INDEX_TYPE stage,
    const INDEX_TYPE value0,
    const INDEX_TYPE value1) {
#pragma HLS inline
    Status[50] = kPcgCallipeplaProbeMagic;
    Status[51] = mode;
    Status[52] = stage;
    Status[53] = value0;
    Status[54] = value1;
}

template <typename T>
inline void PcgCallipepla_Probe_AsyncWrite(tapa::async_mmap<T> &Output,
                                           const INDEX_TYPE addr,
                                           const T value) {
#pragma HLS inline
    bool issued = false;
    bool done = false;
probe_async_write:
    while (!done) {
#pragma HLS pipeline II=1
        if (!issued && !Output.write_addr.full() && !Output.write_data.full()) {
            Output.write_addr.try_write(addr);
            Output.write_data.try_write(value);
            issued = true;
        }
        uint8_t num_responses = 0;
        if (Output.write_resp.try_read(num_responses)) {
            done = true;
        }
    }
}

void PcgCallipepla_Probe_EntryTask(tapa::async_mmap<double> &Residuals,
                                   tapa::async_mmap<INDEX_TYPE> &Status,
                                   tapa::async_mmap<double> &Metrics,
                                   const INDEX_TYPE Batch_num,
                                   const INDEX_TYPE Matrix_len,
                                   const INDEX_TYPE Row_num,
                                   const INDEX_TYPE Column_num,
                                   const INDEX_TYPE Max_iters,
                                   const double Tau) {
    (void)Tau;
    PcgCallipepla_Probe_AsyncWrite(Status, 0, kPcgCallipeplaStatusConverged);
    PcgCallipepla_Probe_AsyncWrite(Status, 1, 0);
    PcgCallipepla_Probe_AsyncWrite(Status, 2, 0);
    PcgCallipepla_Probe_AsyncWrite(Status, 3, 0);
    PcgCallipepla_Probe_AsyncWrite(Status, 4, 0);
    PcgCallipepla_Probe_AsyncWrite(Status, 5, HBM_CHANNEL_NUM);
    PcgCallipepla_Probe_AsyncWrite(
        Status, 6, pcg_callipepla_num_float_v16_packets(Row_num));
    PcgCallipepla_Probe_AsyncWrite(Status, 7, Matrix_len);
    PcgCallipepla_Probe_AsyncWrite(Status, 8, 99);
    PcgCallipepla_Probe_AsyncWrite(Status, 9, 0);
    PcgCallipepla_Probe_AsyncWrite(Status, 10, 0);
    PcgCallipepla_Probe_AsyncWrite(Status, 11, 0);
    PcgCallipepla_Probe_AsyncWrite(Status, 12, 0);
    PcgCallipepla_Probe_AsyncWrite(Status, 13, 0);
    PcgCallipepla_Probe_AsyncWrite(
        Status, 14, pcg_callipepla_num_float_v16_packets(Column_num));
    PcgCallipepla_Probe_AsyncWrite(Status, 15, Matrix_len);
    PcgCallipepla_Probe_AsyncWrite(Status, 50, kPcgCallipeplaProbeMagic);
    PcgCallipepla_Probe_AsyncWrite(Status, 51, CUPER_CALLIPEPLA_PROBE_MODE_ID);
    PcgCallipepla_Probe_AsyncWrite(Status, 52, 99);
    PcgCallipepla_Probe_AsyncWrite(Status, 53, 0);
    PcgCallipepla_Probe_AsyncWrite(Status, 54, 0);
    PcgCallipepla_Probe_AsyncWrite(Status, 55, 0);
    PcgCallipepla_Probe_AsyncWrite(Status, 56, 0);
    PcgCallipepla_Probe_AsyncWrite(Status, 57, 0);
    PcgCallipepla_Probe_AsyncWrite(Status, 58, HBM_CHANNEL_NUM);
    PcgCallipepla_Probe_AsyncWrite(Status, 59, Max_iters);
    PcgCallipepla_Probe_AsyncWrite(Status, 60, Batch_num);
    PcgCallipepla_Probe_AsyncWrite(Status, 61, Matrix_len);
    PcgCallipepla_Probe_AsyncWrite(Status, 62, Row_num);
    PcgCallipepla_Probe_AsyncWrite(Status, 63, Column_num);

    PcgCallipepla_Probe_AsyncWrite(Residuals, 0, 0.0);
    PcgCallipepla_Probe_AsyncWrite(Metrics, 0, 0.0);
    PcgCallipepla_Probe_AsyncWrite(Metrics, 1, 0.0);
    PcgCallipepla_Probe_AsyncWrite(
        Metrics, 5,
        static_cast<double>(pcg_callipepla_num_float_v16_packets(Row_num)));
    PcgCallipepla_Probe_AsyncWrite(
        Metrics, 6,
        static_cast<double>(pcg_callipepla_num_double_v8_packets(Row_num)));
    PcgCallipepla_Probe_AsyncWrite(Metrics, 13, static_cast<double>(Row_num));
    PcgCallipepla_Probe_AsyncWrite(Metrics, 14, static_cast<double>(Max_iters));
}

void PcgCallipepla_Probe_TouchIndex(tapa::async_mmap<INDEX_TYPE> &Input) {
    const bool full = Input.read_addr.full();
    (void)full;
}

void PcgCallipepla_Probe_TouchMatrix(tapa::async_mmap<ap_uint<512>> &Input,
                                     const INDEX_TYPE Debug_channel) {
    (void)Debug_channel;
    const bool full = Input.read_addr.full();
    (void)full;
}

void PcgCallipepla_Probe_TouchDoubleV8(tapa::async_mmap<double_v8> &Input) {
    const bool full = Input.read_addr.full();
    (void)full;
}

void PcgCallipepla_Probe_TouchFloatV16(tapa::async_mmap<float_v16> &Input) {
    const bool full = Input.read_addr.full();
    (void)full;
}

void PcgCallipepla_Probe_CommandDrain(
    tapa::istream<CuperSpmvServiceCommand> &Command_in,
    const INDEX_TYPE Debug_channel) {
    (void)Debug_channel;
probe_command_drain_loop:
    for (;;) {
#pragma HLS pipeline II=1
        const CuperSpmvServiceCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }
    }
}

void PcgCallipepla_Probe_SpmvVectorCommandDrain(
    tapa::istream<PcgCallipeplaSpmvVectorCommand> &Command_in) {
probe_spmv_vector_command_drain_loop:
    for (;;) {
#pragma HLS pipeline II=1
        const PcgCallipeplaSpmvVectorCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }
    }
}

void PcgCallipepla_Probe_StopDrain(tapa::istream<INDEX_TYPE> &Stop_in,
                                   const INDEX_TYPE Debug_channel) {
    (void)Debug_channel;
    (void)Stop_in.read();
}

void PcgCallipepla_Probe_ScalarStopDrain(tapa::istream<INDEX_TYPE> &Stop_in) {
    (void)Stop_in.read();
}

#if defined(CUPER_CALLIPEPLA_PROBE_ENABLED) && CUPER_CALLIPEPLA_PROBE_MODE_ID == 2
inline void PcgCallipepla_ProbeAckTryWriteEvent(
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

inline void PcgCallipepla_ProbeMonitorWriteSnapshot(
    tapa::mmap<INDEX_TYPE> &Status,
    const INDEX_TYPE last_event,
    const INDEX_TYPE monitor_heartbeat,
    const INDEX_TYPE producer_attempts,
    const INDEX_TYPE command_full_count,
    const INDEX_TYPE command_accepted_count,
    const INDEX_TYPE ack_command_count,
    const INDEX_TYPE ack_result_count,
    const INDEX_TYPE controller_result_count,
    const INDEX_TYPE current_phase,
    const INDEX_TYPE controller_state,
    const INDEX_TYPE ack_heartbeat,
    const INDEX_TYPE flags) {
#pragma HLS inline
    Status[50] = kPcgCallipeplaProbeMagic;
    Status[51] = CUPER_CALLIPEPLA_PROBE_MODE_ID;
    Status[52] = last_event;
    Status[53] = monitor_heartbeat;
    Status[54] = producer_attempts;
    Status[55] = command_full_count;
    Status[56] = command_accepted_count;
    Status[57] = ack_command_count;
    Status[58] = ack_result_count;
    Status[59] = controller_result_count;
    Status[60] = current_phase;
    Status[61] = controller_state;
    Status[62] = ack_heartbeat;
    Status[63] = flags;
}

void PcgCallipepla_Probe_HandshakeMonitor(
    tapa::istream<PcgCallipeplaProbeEvent> &Controller_Event_in,
    tapa::istream<PcgCallipeplaProbeEvent> &Ack_Event_in,
    tapa::mmap<INDEX_TYPE> Status,
    const INDEX_TYPE Matrix_len,
    const INDEX_TYPE Row_num) {
    const INDEX_TYPE float_packet_count =
        Row_num > 0 ? pcg_callipepla_num_float_v16_packets(Row_num) : 0;

    INDEX_TYPE last_event = 0;
    INDEX_TYPE monitor_heartbeat = 0;
    INDEX_TYPE producer_attempts = 0;
    INDEX_TYPE command_full_count = 0;
    INDEX_TYPE command_accepted_count = 0;
    INDEX_TYPE ack_command_count = 0;
    INDEX_TYPE ack_result_count = 0;
    INDEX_TYPE controller_result_count = 0;
    INDEX_TYPE current_phase = -1;
    INDEX_TYPE controller_state = kPcgCallipeplaProbeTxFinal;
    INDEX_TYPE ack_heartbeat = 0;
    INDEX_TYPE controller_drop_count = 0;
    INDEX_TYPE ack_drop_count = 0;
    ap_uint<32> flags = 0;
    ap_uint<22> heartbeat_divider = 0;

    INDEX_TYPE final_status = kPcgCallipeplaStatusMaxIter;
    INDEX_TYPE final_iterations = 0;
    INDEX_TYPE final_x_bank = 0;
    INDEX_TYPE final_r_bank = 0;
    INDEX_TYPE final_p_bank = 0;
    INDEX_TYPE final_spmv_rounds = 0;
    bool controller_done = false;
    bool ack_stop_seen = false;

    Status[0] = kPcgCallipeplaStatusMaxIter;
    Status[1] = 0;
    Status[2] = 0;
    Status[3] = 0;
    Status[4] = 0;
    Status[5] = HBM_CHANNEL_NUM;
    Status[6] = float_packet_count;
    Status[7] = Matrix_len;
    Status[8] = 1;
    Status[9] = 0;
    Status[10] = 0;
    Status[11] = 0;
    Status[12] = 0;
    Status[13] = 0;
    Status[14] = float_packet_count;
    Status[15] = Matrix_len;
    PcgCallipepla_ProbeMonitorWriteSnapshot(Status,
                                            last_event,
                                            monitor_heartbeat,
                                            producer_attempts,
                                            command_full_count,
                                            command_accepted_count,
                                            ack_command_count,
                                            ack_result_count,
                                            controller_result_count,
                                            current_phase,
                                            controller_state,
                                            ack_heartbeat,
                                            flags.to_uint());

probe_handshake_monitor_loop:
    while (!controller_done || !ack_stop_seen) {
#pragma HLS loop_flatten off
#pragma HLS pipeline off
        bool snapshot_changed = false;
        PcgCallipeplaProbeEvent event;
        if (Controller_Event_in.try_read(event)) {
            last_event = event.event;
            snapshot_changed = true;

            if (event.event == kPcgCallipeplaProbeEventTransactionEnter) {
                current_phase = event.phase;
                controller_state = kPcgCallipeplaProbeTxSend;
            } else if (event.event == kPcgCallipeplaProbeEventCommandBlocked ||
                       event.event == kPcgCallipeplaProbeEventCommandAccepted ||
                       event.event == kPcgCallipeplaProbeEventWaitResult ||
                       event.event == kPcgCallipeplaProbeEventStopEnter ||
                       event.event == kPcgCallipeplaProbeEventStopAccepted) {
                const ap_uint<32> packed =
                    static_cast<unsigned int>(event.value1);
                producer_attempts = event.value0;
                command_full_count = packed.range(23, 0).to_uint();
                controller_drop_count = packed.range(31, 25).to_uint();
                current_phase = event.phase;

                flags[0] = packed[24];
                flags[1] = event.event == kPcgCallipeplaProbeEventCommandAccepted ||
                           event.event == kPcgCallipeplaProbeEventStopAccepted;
                flags[4] = controller_drop_count != 0;
                flags.range(15, 8) = controller_drop_count;

                if (event.event == kPcgCallipeplaProbeEventCommandBlocked) {
                    controller_state = event.phase == kPcgCallipeplaProbePhaseStop
                                           ? kPcgCallipeplaProbeTxStop
                                           : kPcgCallipeplaProbeTxSend;
                } else if (event.event == kPcgCallipeplaProbeEventCommandAccepted) {
                    ++command_accepted_count;
                    controller_state = kPcgCallipeplaProbeTxWaitResult;
                } else if (event.event == kPcgCallipeplaProbeEventWaitResult) {
                    controller_state = kPcgCallipeplaProbeTxWaitResult;
                } else if (event.event == kPcgCallipeplaProbeEventStopEnter) {
                    controller_state = kPcgCallipeplaProbeTxStop;
                } else if (event.event == kPcgCallipeplaProbeEventStopAccepted) {
                    ++command_accepted_count;
                    controller_state = kPcgCallipeplaProbeTxFinal;
                }
            } else if (event.event == kPcgCallipeplaProbeEventResultReceived) {
                current_phase = event.phase;
                controller_result_count = event.value0;
                controller_state = kPcgCallipeplaProbeTxDone;
            } else if (event.event ==
                       kPcgCallipeplaProbeEventControllerFinalStatus) {
                const ap_uint<32> packed =
                    static_cast<unsigned int>(event.value1);
                final_status = event.phase;
                final_iterations = event.value0;
                final_x_bank = packed[0];
                final_r_bank = packed[1];
                final_p_bank = packed[2];
                final_spmv_rounds = packed.range(23, 8).to_uint();
                controller_drop_count = packed.range(31, 24).to_uint();
                flags[4] = controller_drop_count != 0;
                flags.range(15, 8) = controller_drop_count;
            } else if (event.event == kPcgCallipeplaProbeEventControllerDone) {
                const ap_uint<32> packed =
                    static_cast<unsigned int>(event.value1);
                command_accepted_count = event.value0;
                controller_result_count = packed.range(15, 0).to_uint();
                controller_drop_count = packed.range(31, 24).to_uint();
                controller_done = true;
                controller_state = kPcgCallipeplaProbeTxFinal;
                flags[2] = 1;
                flags[4] = controller_drop_count != 0;
                flags.range(15, 8) = controller_drop_count;
            } else if (event.event == kPcgCallipeplaProbeEventValidationPassed ||
                       event.event == kPcgCallipeplaProbeEventInitFanoutDone) {
                current_phase = event.phase;
            }
        }

        if (Ack_Event_in.try_read(event)) {
            last_event = event.event;
            ++ack_heartbeat;
            snapshot_changed = true;

            if (event.event == kPcgCallipeplaProbeEventAckCommandReceived) {
                current_phase = event.phase;
                ack_command_count = event.value0;
                ack_drop_count = event.value1 & 0xff;
            } else if (event.event == kPcgCallipeplaProbeEventAckResultSent) {
                current_phase = event.phase;
                ack_result_count = event.value0;
                ack_drop_count = event.value1 & 0xff;
            } else if (event.event == kPcgCallipeplaProbeEventAckStop) {
                const ap_uint<32> packed =
                    static_cast<unsigned int>(event.value1);
                ack_command_count = event.value0;
                ack_result_count = packed.range(15, 0).to_uint();
                ack_drop_count = packed.range(31, 24).to_uint();
                ack_stop_seen = true;
                current_phase = kPcgCallipeplaProbePhaseStop;
                flags[3] = 1;
            }
            flags[5] = ack_drop_count != 0;
            flags.range(23, 16) = ack_drop_count;
        }

        ++heartbeat_divider;
        if (heartbeat_divider == 0) {
            ++monitor_heartbeat;
            Status[53] = monitor_heartbeat;
        }

        if (snapshot_changed) {
            PcgCallipepla_ProbeMonitorWriteSnapshot(Status,
                                                    last_event,
                                                    monitor_heartbeat,
                                                    producer_attempts,
                                                    command_full_count,
                                                    command_accepted_count,
                                                    ack_command_count,
                                                    ack_result_count,
                                                    controller_result_count,
                                                    current_phase,
                                                    controller_state,
                                                    ack_heartbeat,
                                                    flags.to_uint());
        }
    }

    Status[0] = final_status;
    Status[1] = final_iterations;
    Status[2] = final_x_bank;
    Status[3] = final_r_bank;
    Status[4] = final_p_bank;
    Status[8] = 99;
    Status[9] = final_iterations;
    Status[10] = final_x_bank;
    Status[11] = final_r_bank;
    Status[12] = final_p_bank;
    Status[13] = final_spmv_rounds;
    last_event = 99;
    current_phase = kPcgCallipeplaProbePhaseStop;
    controller_state = kPcgCallipeplaProbeTxFinal;
    PcgCallipepla_ProbeMonitorWriteSnapshot(Status,
                                            last_event,
                                            monitor_heartbeat,
                                            producer_attempts,
                                            command_full_count,
                                            command_accepted_count,
                                            ack_command_count,
                                            ack_result_count,
                                            controller_result_count,
                                            current_phase,
                                            controller_state,
                                            ack_heartbeat,
                                            flags.to_uint());
}
#endif

void PcgCallipepla_Probe_VectorPhaseAck(
    tapa::istream<PcgCallipeplaVectorCommand> &Command_in,
    tapa::ostream<PcgCallipeplaVectorResult> &Result_out
#if defined(CUPER_CALLIPEPLA_PROBE_ENABLED) && CUPER_CALLIPEPLA_PROBE_MODE_ID == 2
    ,
    tapa::ostream<PcgCallipeplaProbeEvent> &Probe_Event_out
#endif
    ) {
#if defined(CUPER_CALLIPEPLA_PROBE_ENABLED) && CUPER_CALLIPEPLA_PROBE_MODE_ID == 2
    INDEX_TYPE command_count = 0;
    INDEX_TYPE result_count = 0;
    INDEX_TYPE probe_event_drop_count = 0;
    PcgCallipepla_ProbeAckTryWriteEvent(Probe_Event_out,
                                        probe_event_drop_count,
                                        kPcgCallipeplaProbeEventAckStart,
                                        -1,
                                        0,
                                        0);
#endif
probe_vector_ack_loop:
    for (;;) {
#pragma HLS loop_flatten off
#pragma HLS pipeline off
        const PcgCallipeplaVectorCommand command = Command_in.read();
        if (command.stop != 0) {
#if defined(CUPER_CALLIPEPLA_PROBE_ENABLED) && CUPER_CALLIPEPLA_PROBE_MODE_ID == 2
            Probe_Event_out.write(pcg_callipepla_make_probe_event(
                kPcgCallipeplaProbeEventAckStop,
                kPcgCallipeplaProbePhaseStop,
                command_count,
                pcg_callipepla_pack_ack_stop_value(
                    result_count, probe_event_drop_count)));
#endif
            return;
        }

#if defined(CUPER_CALLIPEPLA_PROBE_ENABLED) && CUPER_CALLIPEPLA_PROBE_MODE_ID == 2
        ++command_count;
        PcgCallipepla_ProbeAckTryWriteEvent(
            Probe_Event_out,
            probe_event_drop_count,
            kPcgCallipeplaProbeEventAckCommandReceived,
            command.phase,
            command_count,
            probe_event_drop_count);
#endif

        PcgCallipeplaVectorResult result =
            pcg_callipepla_make_vector_result(command.phase);
        if (command.phase == kPcgCallipeplaPhaseInitZp) {
            result.rz = 1.0;
            result.rr = 1.0;
        } else if (command.phase == kPcgCallipeplaPhaseIterDot) {
            result.p_ap = 1.0;
        } else if (command.phase == kPcgCallipeplaPhaseApplyMInvDot) {
            result.rz = 0.5;
            result.rr = 0.5;
        }
        Result_out.write(result);
#if defined(CUPER_CALLIPEPLA_PROBE_ENABLED) && CUPER_CALLIPEPLA_PROBE_MODE_ID == 2
        ++result_count;
        PcgCallipepla_ProbeAckTryWriteEvent(Probe_Event_out,
                                            probe_event_drop_count,
                                            kPcgCallipeplaProbeEventAckResultSent,
                                            command.phase,
                                            result_count,
                                            probe_event_drop_count);
#endif
    }
}

void PcgCallipepla_Probe_PEParamDrain(tapa::istream<INDEX_TYPE> &PE_Param) {
probe_pe_param_rounds:
    for (;;) {
#pragma HLS loop_flatten off
        const INDEX_TYPE batch_num = PE_Param.read();
        if (batch_num == kSpmvServiceStopToken) {
            return;
        }
        (void)PE_Param.read();
        (void)PE_Param.read();
#ifdef JACOBI_SPMV_STRIP_PADDING
        const INDEX_TYPE boundary_count = (batch_num + 1) * HBM_CHANNEL_NUM;
#else
        const INDEX_TYPE boundary_count = batch_num + 1;
#endif
    probe_pe_param_boundaries:
        for (INDEX_TYPE index = 0; index < boundary_count; ++index) {
#pragma HLS loop_tripcount min=17 max=2600
#pragma HLS pipeline II=1
            (void)PE_Param.read();
        }
    }
}

#ifdef JACOBI_SPMV_STRIP_PADDING
void PcgCallipepla_Probe_MatrixLoaderStripDrain(
    tapa::async_mmap<ap_uint<512>> &Matrix_data,
    tapa::istream<CuperSpmvServiceCommand> &Command_in,
    tapa::istream<INDEX_TYPE> &Matrix_Len_Stream,
    const INDEX_TYPE Debug_channel) {
probe_matrix_strip_rounds:
    for (;;) {
#pragma HLS loop_flatten off
        const CuperSpmvServiceCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }
        const INDEX_TYPE matrix_len = Matrix_Len_Stream.read();
#if CUPER_CALLIPEPLA_PROBE_LOADER_LEVEL >= 3
        const bool read_matrix =
            Debug_channel == 0 || Debug_channel == HBM_CHANNEL_NUM - 1;
#else
        const bool read_matrix = false;
#endif
        if (read_matrix) {
        probe_read_matrix:
            for (INDEX_TYPE i_request = 0, i_response = 0;
                 i_response < matrix_len;) {
#pragma HLS loop_tripcount min=1 max=10000
#pragma HLS pipeline II=1
                if (i_request < matrix_len && !Matrix_data.read_addr.full()) {
                    Matrix_data.read_addr.try_write(i_request);
                    ++i_request;
                }
                if (!Matrix_data.read_data.empty()) {
                    ap_uint<512> tmp = 0;
                    Matrix_data.read_data.try_read(tmp);
                    ++i_response;
                }
            }
        }
    }
}
#else
void PcgCallipepla_Probe_MatrixLoaderDrain(
    const INDEX_TYPE Matrix_len,
    tapa::async_mmap<ap_uint<512>> &Matrix_data,
    tapa::istream<CuperSpmvServiceCommand> &Command_in,
    const INDEX_TYPE Debug_channel) {
probe_matrix_rounds:
    for (;;) {
#pragma HLS loop_flatten off
        const CuperSpmvServiceCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }
#if CUPER_CALLIPEPLA_PROBE_LOADER_LEVEL >= 3
        const bool read_matrix =
            Debug_channel == 0 || Debug_channel == HBM_CHANNEL_NUM - 1;
#else
        const bool read_matrix = false;
#endif
        if (read_matrix) {
        probe_read_matrix:
            for (INDEX_TYPE i_request = 0, i_response = 0;
                 i_response < Matrix_len;) {
#pragma HLS loop_tripcount min=1 max=10000
#pragma HLS pipeline II=1
                if (i_request < Matrix_len && !Matrix_data.read_addr.full()) {
                    Matrix_data.read_addr.try_write(i_request);
                    ++i_request;
                }
                if (!Matrix_data.read_data.empty()) {
                    ap_uint<512> tmp = 0;
                    Matrix_data.read_data.try_read(tmp);
                    ++i_response;
                }
            }
        }
    }
}
#endif
