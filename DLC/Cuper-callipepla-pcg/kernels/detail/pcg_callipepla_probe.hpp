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

void PcgCallipepla_Probe_VectorPhaseAck(
    tapa::istream<PcgCallipeplaVectorCommand> &Command_in,
    tapa::ostream<PcgCallipeplaVectorResult> &Result_out) {
probe_vector_ack_loop:
    for (;;) {
#pragma HLS loop_flatten off
        const PcgCallipeplaVectorCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }

        PcgCallipeplaVectorResult result =
            pcg_callipepla_make_vector_result(command.phase);
        if (command.phase == kPcgCallipeplaPhaseInitZp) {
            result.rz = 1.0;
            result.rr = 1.0;
        } else if (command.phase == kPcgCallipeplaPhaseIterDot) {
            result.p_ap = 1.0;
        } else if (command.phase == kPcgCallipeplaPhaseApplyMInvDot) {
            const INDEX_TYPE denom = command.iter < 0 ? 2 : command.iter + 2;
            const double value = 1.0 / static_cast<double>(denom);
            result.rz = value;
            result.rr = value;
        }
        Result_out.write(result);
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
