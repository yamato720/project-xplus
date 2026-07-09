#pragma once

#include <ap_int.h>
#include <tapa.h>

#include "pcg_callipepla_common.hpp"
#include "pcg_callipepla_trace.hpp"

inline void PcgCallipepla_WriteLiveStatus(tapa::mmap<INDEX_TYPE> &Status,
                                          const INDEX_TYPE phase,
                                          const INDEX_TYPE iter,
                                          const INDEX_TYPE x_bank,
                                          const INDEX_TYPE r_bank,
                                          const INDEX_TYPE p_bank,
                                          const INDEX_TYPE spmv_rounds,
                                          const INDEX_TYPE packet_count,
                                          const INDEX_TYPE matrix_len) {
#pragma HLS inline
    Status[8] = phase;
    Status[9] = iter;
    Status[10] = x_bank;
    Status[11] = r_bank;
    Status[12] = p_bank;
    Status[13] = spmv_rounds;
    Status[14] = packet_count;
    Status[15] = matrix_len;
}

#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
inline void PcgCallipepla_WriteStatusSlot(
    tapa::ostream<PcgCallipeplaStatusWrite> &Status_Write_out,
    const INDEX_TYPE addr,
    const INDEX_TYPE value) {
#pragma HLS inline
    PcgCallipeplaStatusWrite write;
    write.addr = addr;
    write.value = value;
    Status_Write_out.write(write);
}

inline void PcgCallipepla_WriteLiveStatus(
    tapa::ostream<PcgCallipeplaStatusWrite> &Status_Write_out,
    const INDEX_TYPE phase,
    const INDEX_TYPE iter,
    const INDEX_TYPE x_bank,
    const INDEX_TYPE r_bank,
    const INDEX_TYPE p_bank,
    const INDEX_TYPE spmv_rounds,
    const INDEX_TYPE packet_count,
    const INDEX_TYPE matrix_len) {
#pragma HLS inline
    PcgCallipepla_WriteStatusSlot(Status_Write_out, 8, phase);
    PcgCallipepla_WriteStatusSlot(Status_Write_out, 9, iter);
    PcgCallipepla_WriteStatusSlot(Status_Write_out, 10, x_bank);
    PcgCallipepla_WriteStatusSlot(Status_Write_out, 11, r_bank);
    PcgCallipepla_WriteStatusSlot(Status_Write_out, 12, p_bank);
    PcgCallipepla_WriteStatusSlot(Status_Write_out, 13, spmv_rounds);
    PcgCallipepla_WriteStatusSlot(Status_Write_out, 14, packet_count);
    PcgCallipepla_WriteStatusSlot(Status_Write_out, 15, matrix_len);
}
#else
inline void PcgCallipepla_WriteStatusSlot(tapa::mmap<INDEX_TYPE> &Status,
                                          const INDEX_TYPE addr,
                                          const INDEX_TYPE value) {
#pragma HLS inline
    Status[addr] = value;
}
#endif

inline void PcgCallipepla_WriteVectorCommand(
    tapa::ostream<PcgCallipeplaVectorCommand> &Vector_Command_out,
    const PcgCallipeplaVectorCommand &command,
    INDEX_TYPE &vector_command_count) {
#pragma HLS inline
    Vector_Command_out.write(command);
    ++vector_command_count;
}

inline PcgCallipeplaVectorResult PcgCallipepla_ReadVectorResult(
    tapa::istream<PcgCallipeplaVectorResult> &Vector_Result_in,
    INDEX_TYPE &vector_result_count) {
#pragma HLS inline
    const PcgCallipeplaVectorResult result = Vector_Result_in.read();
    ++vector_result_count;
    return result;
}

#ifdef CUPER_CALLIPEPLA_PROBE_ENABLED
inline void PcgCallipepla_WriteProbeControllerStatus(
    tapa::mmap<INDEX_TYPE> &Status,
    const INDEX_TYPE stage,
    const INDEX_TYPE spmv_rounds,
    const INDEX_TYPE vector_command_count,
    const INDEX_TYPE vector_result_count,
    const INDEX_TYPE float_packet_count,
    const INDEX_TYPE matrix_len,
    const INDEX_TYPE row_num,
    const INDEX_TYPE batch_num,
    const INDEX_TYPE max_iters,
    const INDEX_TYPE iterations) {
#pragma HLS inline
    Status[50] = kPcgCallipeplaProbeMagic;
    Status[51] = CUPER_CALLIPEPLA_PROBE_MODE_ID;
    Status[52] = stage;
    Status[53] = spmv_rounds;
    Status[54] = spmv_rounds + 1;
    Status[55] = (spmv_rounds + 1) * HBM_CHANNEL_NUM;
    Status[56] = vector_command_count;
    Status[57] = vector_result_count;
    Status[58] = 8;
    Status[59] = 1;
    Status[60] = float_packet_count;
    Status[61] = matrix_len;
    Status[62] = row_num;
    Status[63] = (batch_num << 16) ^ ((max_iters & 0xff) << 8) ^ (iterations & 0xff);
}
#endif

void PcgCallipepla_Controller(
    tapa::ostream<CuperSpmvServiceCommand> &Ptr_Command_out,
    tapa::ostreams<CuperSpmvServiceCommand, HBM_CHANNEL_NUM> &Matrix_Command_out,
    tapa::ostream<PcgCallipeplaSpmvVectorCommand> &Spmv_Vector_Command_out,
    tapa::ostreams<INDEX_TYPE, 8> &Checker_Stop_out,
    tapa::ostream<INDEX_TYPE> &Sort_Stop_out,
    tapa::ostream<INDEX_TYPE> &Vector_Destroy_Expected_Rounds_out,
    tapa::ostream<PcgCallipeplaStageEvent> &Stage_Event_out,
    tapa::istream<ap_uint<64>> &Stage_Ticks_in,
    tapa::ostream<PcgCallipeplaVectorCommand> &Vector_Command_out,
    tapa::istream<PcgCallipeplaVectorResult> &Vector_Result_in,
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
    tapa::ostream<PcgCallipeplaStatusWrite> &Status_Write_out,
    tapa::ostream<PcgCallipeplaDebugEvent> &Debug_Event_out,
    tapa::ostream<INDEX_TYPE> &Debug_Stop_out,
#else
    tapa::mmap<INDEX_TYPE> Status,
#endif
    tapa::mmap<double> Residuals,
    tapa::mmap<double> Metrics,
    const INDEX_TYPE Batch_num,
    const INDEX_TYPE Matrix_len,
    const INDEX_TYPE Row_num,
    const INDEX_TYPE Column_num,
    const INDEX_TYPE Max_iters,
    const double Tau) {
    const INDEX_TYPE float_packet_count =
        Row_num > 0 ? pcg_callipepla_num_float_v16_packets(Row_num) : 0;
    const INDEX_TYPE double_packet_count =
        Row_num > 0 ? pcg_callipepla_num_double_v8_packets(Row_num) : 0;

    INDEX_TYPE status_code = kPcgCallipeplaStatusMaxIter;
    INDEX_TYPE iterations = 0;
    INDEX_TYPE x_bank = 0;
    INDEX_TYPE r_bank = 0;
    INDEX_TYPE p_bank = 0;
    INDEX_TYPE spmv_rounds = 0;
    double rz = 0.0;
    double rr = 0.0;
    double p_ap = 0.0;
    double alpha = 0.0;
    double beta = 0.0;
    INDEX_TYPE vector_command_count = 0;
    INDEX_TYPE vector_result_count = 0;

    unsigned long long init_spmv_work = 0;
    unsigned long long init_zp_work = 0;
    unsigned long long iter_spmv_work = 0;
    unsigned long long update_x_work = 0;
    unsigned long long update_r_work = 0;
    unsigned long long apply_m_inv_work = 0;
    unsigned long long update_p_work = 0;
    unsigned long long residual_writes = 0;

#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
    PcgCallipepla_WriteLiveStatus(Status_Write_out,
                                  1,
                                  0,
                                  x_bank,
                                  r_bank,
                                  p_bank,
                                  spmv_rounds,
                                  float_packet_count,
                                  Matrix_len);
    PcgCallipepla_DebugTryWrite(Debug_Event_out,
                                kPcgCallipeplaTraceSourceController,
                                kPcgCallipeplaTracePhaseEntry,
                                0,
                                Row_num);
#else
    PcgCallipepla_WriteLiveStatus(Status,
                                  1,
                                  0,
                                  x_bank,
                                  r_bank,
                                  p_bank,
                                  spmv_rounds,
                                  float_packet_count,
                                  Matrix_len);
#endif
#ifdef CUPER_CALLIPEPLA_PROBE_ENABLED
    PcgCallipepla_WriteProbeControllerStatus(Status,
                                             1,
                                             spmv_rounds,
                                             vector_command_count,
                                             vector_result_count,
                                             float_packet_count,
                                             Matrix_len,
                                             Row_num,
                                             Batch_num,
                                             Max_iters,
                                             iterations);
#endif
    pcg_callipepla_stage_mark(Stage_Event_out,
                              kPcgCallipeplaStageTotal,
                              kPcgCallipeplaStageBegin);

    if (Row_num <= 0 || Column_num <= 0 || Max_iters < 0 ||
        Tau <= 0.0 || pcg_callipepla_invalid(Tau)) {
        status_code = kPcgCallipeplaStatusBreakdown;
    } else {
        pcg_callipepla_stage_mark(Stage_Event_out,
                                  kPcgCallipeplaStageInitSpmv,
                                  kPcgCallipeplaStageBegin);
        pcg_callipepla_send_spmv_command(Ptr_Command_out,
                                         Matrix_Command_out,
                                         Spmv_Vector_Command_out,
                                         kPcgCallipeplaVectorSourceX,
                                         x_bank);
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
        PcgCallipepla_DebugTryWrite(Debug_Event_out,
                                    kPcgCallipeplaTraceSourceController,
                                    kPcgCallipeplaTracePhaseSend,
                                    kPcgCallipeplaPhaseInitSpmv,
                                    spmv_rounds);
#endif
        PcgCallipepla_WriteVectorCommand(
            Vector_Command_out,
            pcg_callipepla_make_vector_command(kPcgCallipeplaPhaseInitSpmv,
                                               -1,
                                               x_bank,
                                               x_bank,
                                               r_bank,
                                               r_bank,
                                               p_bank,
                                               p_bank,
                                               0.0,
                                               0.0),
            vector_command_count);
        (void)PcgCallipepla_ReadVectorResult(Vector_Result_in,
                                             vector_result_count);
        ++spmv_rounds;
        init_spmv_work += static_cast<unsigned long long>(float_packet_count) +
                          static_cast<unsigned long long>(double_packet_count);
        pcg_callipepla_stage_mark(Stage_Event_out,
                                  kPcgCallipeplaStageInitSpmv,
                                  kPcgCallipeplaStageEnd);
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
        PcgCallipepla_WriteLiveStatus(Status_Write_out,
                                      2,
                                      0,
                                      x_bank,
                                      r_bank,
                                      p_bank,
                                      spmv_rounds,
                                      float_packet_count,
                                      Matrix_len);
        PcgCallipepla_DebugTryWrite(Debug_Event_out,
                                    kPcgCallipeplaTraceSourceController,
                                    kPcgCallipeplaTracePhaseDone,
                                    kPcgCallipeplaPhaseInitSpmv,
                                    spmv_rounds);
#else
        PcgCallipepla_WriteLiveStatus(Status,
                                      2,
                                      0,
                                      x_bank,
                                      r_bank,
                                      p_bank,
                                      spmv_rounds,
                                      float_packet_count,
                                      Matrix_len);
#endif

        pcg_callipepla_stage_mark(Stage_Event_out,
                                  kPcgCallipeplaStageInitZp,
                                  kPcgCallipeplaStageBegin);
        PcgCallipepla_WriteVectorCommand(
            Vector_Command_out,
            pcg_callipepla_make_vector_command(kPcgCallipeplaPhaseInitZp,
                                               -1,
                                               x_bank,
                                               x_bank,
                                               r_bank,
                                               r_bank,
                                               p_bank,
                                               p_bank,
                                               0.0,
                                               0.0),
            vector_command_count);
        const PcgCallipeplaVectorResult init_result =
            PcgCallipepla_ReadVectorResult(Vector_Result_in,
                                           vector_result_count);
        rz = init_result.rz;
        rr = init_result.rr;
        Residuals[0] = rr;
        ++residual_writes;
        init_zp_work += 3ULL * static_cast<unsigned long long>(double_packet_count);
        pcg_callipepla_stage_mark(Stage_Event_out,
                                  kPcgCallipeplaStageInitZp,
                                  kPcgCallipeplaStageEnd);

    pcg_iteration_loop:
        for (INDEX_TYPE iter = 0; iter < Max_iters && rr > Tau; ++iter) {
#pragma HLS loop_tripcount min=1 max=1000
            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageRound,
                                      kPcgCallipeplaStageBegin);
            const INDEX_TYPE x_next_bank = 1 - x_bank;
            const INDEX_TYPE r_next_bank = 1 - r_bank;
            const INDEX_TYPE p_next_bank = 1 - p_bank;

#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
            PcgCallipepla_WriteLiveStatus(Status_Write_out,
                                          10,
                                          iter,
                                          x_bank,
                                          r_bank,
                                          p_bank,
                                          spmv_rounds,
                                          float_packet_count,
                                          Matrix_len);
            PcgCallipepla_DebugTryWrite(Debug_Event_out,
                                        kPcgCallipeplaTraceSourceController,
                                        kPcgCallipeplaTracePhaseProgress,
                                        kPcgCallipeplaPhaseIterDot,
                                        iter);
#else
            PcgCallipepla_WriteLiveStatus(Status,
                                          10,
                                          iter,
                                          x_bank,
                                          r_bank,
                                          p_bank,
                                          spmv_rounds,
                                          float_packet_count,
                                          Matrix_len);
#endif

            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageIterSpmv,
                                      kPcgCallipeplaStageBegin);
            pcg_callipepla_send_spmv_command(Ptr_Command_out,
                                             Matrix_Command_out,
                                             Spmv_Vector_Command_out,
                                             kPcgCallipeplaVectorSourceP,
                                             p_bank);
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
            PcgCallipepla_DebugTryWrite(Debug_Event_out,
                                        kPcgCallipeplaTraceSourceController,
                                        kPcgCallipeplaTracePhaseSend,
                                        kPcgCallipeplaPhaseIterDot,
                                        iter);
#endif
            PcgCallipepla_WriteVectorCommand(
                Vector_Command_out,
                pcg_callipepla_make_vector_command(kPcgCallipeplaPhaseIterDot,
                                                   iter,
                                                   x_bank,
                                                   x_next_bank,
                                                   r_bank,
                                                   r_next_bank,
                                                   p_bank,
                                                   p_next_bank,
                                                   0.0,
                                                   0.0),
                vector_command_count);
            const PcgCallipeplaVectorResult iter_dot =
                PcgCallipepla_ReadVectorResult(Vector_Result_in,
                                               vector_result_count);
            p_ap = iter_dot.p_ap;
            ++spmv_rounds;
            iter_spmv_work +=
                2ULL * static_cast<unsigned long long>(float_packet_count) +
                static_cast<unsigned long long>(double_packet_count);
            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageIterSpmv,
                                      kPcgCallipeplaStageEnd);

            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageDotAlpha,
                                      kPcgCallipeplaStageBegin);
            if (pcg_callipepla_invalid(p_ap) ||
                pcg_callipepla_abs(p_ap) <= kPcgCallipeplaBreakdownEps ||
                pcg_callipepla_invalid(rz) ||
                pcg_callipepla_abs(rz) <= kPcgCallipeplaBreakdownEps) {
                status_code = kPcgCallipeplaStatusBreakdown;
                pcg_callipepla_stage_mark(Stage_Event_out,
                                          kPcgCallipeplaStageDotAlpha,
                                          kPcgCallipeplaStageEnd);
                pcg_callipepla_stage_mark(Stage_Event_out,
                                          kPcgCallipeplaStageRound,
                                          kPcgCallipeplaStageEnd);
                break;
            }
            alpha = rz / p_ap;
            if (pcg_callipepla_invalid(alpha)) {
                status_code = kPcgCallipeplaStatusBreakdown;
                pcg_callipepla_stage_mark(Stage_Event_out,
                                          kPcgCallipeplaStageDotAlpha,
                                          kPcgCallipeplaStageEnd);
                pcg_callipepla_stage_mark(Stage_Event_out,
                                          kPcgCallipeplaStageRound,
                                          kPcgCallipeplaStageEnd);
                break;
            }
            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageDotAlpha,
                                      kPcgCallipeplaStageEnd);

            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageUpdateX,
                                      kPcgCallipeplaStageBegin);
            PcgCallipepla_WriteVectorCommand(
                Vector_Command_out,
                pcg_callipepla_make_vector_command(kPcgCallipeplaPhaseUpdateX,
                                                   iter,
                                                   x_bank,
                                                   x_next_bank,
                                                   r_bank,
                                                   r_next_bank,
                                                   p_bank,
                                                   p_next_bank,
                                                   alpha,
                                                   0.0),
                vector_command_count);
            (void)PcgCallipepla_ReadVectorResult(Vector_Result_in,
                                                 vector_result_count);
            x_bank = x_next_bank;
            update_x_work += 3ULL * static_cast<unsigned long long>(double_packet_count);
            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageUpdateX,
                                      kPcgCallipeplaStageEnd);

            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageUpdateR,
                                      kPcgCallipeplaStageBegin);
            PcgCallipepla_WriteVectorCommand(
                Vector_Command_out,
                pcg_callipepla_make_vector_command(kPcgCallipeplaPhaseUpdateR,
                                                   iter,
                                                   x_bank,
                                                   x_bank,
                                                   r_bank,
                                                   r_next_bank,
                                                   p_bank,
                                                   p_next_bank,
                                                   alpha,
                                                   0.0),
                vector_command_count);
            (void)PcgCallipepla_ReadVectorResult(Vector_Result_in,
                                                 vector_result_count);
            r_bank = r_next_bank;
            update_r_work += static_cast<unsigned long long>(float_packet_count) +
                             2ULL * static_cast<unsigned long long>(double_packet_count);
            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageUpdateR,
                                      kPcgCallipeplaStageEnd);

            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageApplyMInv,
                                      kPcgCallipeplaStageBegin);
            PcgCallipepla_WriteVectorCommand(
                Vector_Command_out,
                pcg_callipepla_make_vector_command(kPcgCallipeplaPhaseApplyMInvDot,
                                                   iter,
                                                   x_bank,
                                                   x_bank,
                                                   r_bank,
                                                   r_bank,
                                                   p_bank,
                                                   p_next_bank,
                                                   alpha,
                                                   0.0),
                vector_command_count);
            const PcgCallipeplaVectorResult rz_result =
                PcgCallipepla_ReadVectorResult(Vector_Result_in,
                                               vector_result_count);
            const double rz_new = rz_result.rz;
            const double rr_new = rz_result.rr;
            apply_m_inv_work += 3ULL * static_cast<unsigned long long>(double_packet_count);
            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageApplyMInv,
                                      kPcgCallipeplaStageEnd);

            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageDotRz,
                                      kPcgCallipeplaStageBegin);
            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageDotRz,
                                      kPcgCallipeplaStageEnd);
            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageDotResidual,
                                      kPcgCallipeplaStageBegin);
            Residuals[iter + 1] = rr_new;
            ++residual_writes;
            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageDotResidual,
                                      kPcgCallipeplaStageEnd);

            if (pcg_callipepla_invalid(rz_new) || pcg_callipepla_invalid(rr_new) ||
                pcg_callipepla_abs(rz) <= kPcgCallipeplaBreakdownEps) {
                status_code = kPcgCallipeplaStatusBreakdown;
                iterations = iter + 1;
                rz = rz_new;
                rr = rr_new;
                pcg_callipepla_stage_mark(Stage_Event_out,
                                          kPcgCallipeplaStageRound,
                                          kPcgCallipeplaStageEnd);
                break;
            }

            beta = rz_new / rz;
            if (pcg_callipepla_invalid(beta)) {
                status_code = kPcgCallipeplaStatusBreakdown;
                iterations = iter + 1;
                rz = rz_new;
                rr = rr_new;
                pcg_callipepla_stage_mark(Stage_Event_out,
                                          kPcgCallipeplaStageRound,
                                          kPcgCallipeplaStageEnd);
                break;
            }

            rz = rz_new;
            rr = rr_new;
            iterations = iter + 1;

#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
            PcgCallipepla_WriteLiveStatus(Status_Write_out,
                                          20,
                                          iterations,
                                          x_bank,
                                          r_bank,
                                          p_bank,
                                          spmv_rounds,
                                          float_packet_count,
                                          Matrix_len);
#else
            PcgCallipepla_WriteLiveStatus(Status,
                                          20,
                                          iterations,
                                          x_bank,
                                          r_bank,
                                          p_bank,
                                          spmv_rounds,
                                          float_packet_count,
                                          Matrix_len);
#endif

            if (rr <= Tau) {
                status_code = kPcgCallipeplaStatusConverged;
                pcg_callipepla_stage_mark(Stage_Event_out,
                                          kPcgCallipeplaStageRound,
                                          kPcgCallipeplaStageEnd);
                break;
            }

            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageUpdateP,
                                      kPcgCallipeplaStageBegin);
            PcgCallipepla_WriteVectorCommand(
                Vector_Command_out,
                pcg_callipepla_make_vector_command(kPcgCallipeplaPhaseUpdateP,
                                                   iter,
                                                   x_bank,
                                                   x_bank,
                                                   r_bank,
                                                   r_bank,
                                                   p_bank,
                                                   p_next_bank,
                                                   alpha,
                                                   beta),
                vector_command_count);
            (void)PcgCallipepla_ReadVectorResult(Vector_Result_in,
                                                 vector_result_count);
            p_bank = p_next_bank;
            update_p_work += 3ULL * static_cast<unsigned long long>(double_packet_count);
            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageUpdateP,
                                      kPcgCallipeplaStageEnd);

            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageRound,
                                      kPcgCallipeplaStageEnd);
        }

        if (status_code != kPcgCallipeplaStatusBreakdown &&
            status_code != kPcgCallipeplaStatusConverged) {
            status_code =
                rr <= Tau ? kPcgCallipeplaStatusConverged : kPcgCallipeplaStatusMaxIter;
        }
    }

    pcg_callipepla_send_spmv_stop(Ptr_Command_out,
                                  Matrix_Command_out,
                                  Spmv_Vector_Command_out);
#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
    PcgCallipepla_DebugTryWrite(Debug_Event_out,
                                kPcgCallipeplaTraceSourceController,
                                kPcgCallipeplaTracePhaseStop,
                                0,
                                iterations);
#endif
    PcgCallipepla_WriteVectorCommand(Vector_Command_out,
                                     pcg_callipepla_make_vector_stop(),
                                     vector_command_count);
stop_checkers:
    for (INDEX_TYPE index = 0; index < 8; ++index) {
#pragma HLS unroll
        Checker_Stop_out[index].write(1);
    }
    Sort_Stop_out.write(1);
    Vector_Destroy_Expected_Rounds_out.write(spmv_rounds);

    pcg_callipepla_stage_mark(Stage_Event_out,
                              kPcgCallipeplaStageTotal,
                              kPcgCallipeplaStageEnd);
    pcg_callipepla_stage_mark(Stage_Event_out, 0, kPcgCallipeplaStageStop);

    ap_uint<64> stage_cycles[kPcgCallipeplaStageCount + 1];
#pragma HLS array_partition variable=stage_cycles complete
read_stage_cycles:
    for (INDEX_TYPE index = 0; index < kPcgCallipeplaStageCount + 1; ++index) {
#pragma HLS pipeline II=1
        stage_cycles[index] = Stage_Ticks_in.read();
    }

    Metrics[0] = rz;
    Metrics[1] = rr;
    Metrics[2] = p_ap;
    Metrics[3] = alpha;
    Metrics[4] = beta;
    Metrics[5] = static_cast<double>(float_packet_count);
    Metrics[6] = static_cast<double>(double_packet_count);
    Metrics[7] = static_cast<double>(spmv_rounds);
    Metrics[8] = static_cast<double>(init_spmv_work);
    Metrics[9] = static_cast<double>(init_zp_work);
    Metrics[10] = static_cast<double>(iter_spmv_work);
    Metrics[11] = static_cast<double>(update_x_work + update_r_work +
                                      apply_m_inv_work + update_p_work);
    Metrics[12] = static_cast<double>(residual_writes);
    Metrics[13] = static_cast<double>(Row_num);
    Metrics[14] = static_cast<double>(Max_iters);
    Metrics[15] = static_cast<double>(init_spmv_work + init_zp_work +
                                      iter_spmv_work + update_x_work +
                                      update_r_work + apply_m_inv_work +
                                      update_p_work + residual_writes);
write_stage_metrics:
    for (INDEX_TYPE index = 0; index < kPcgCallipeplaStageCount; ++index) {
#pragma HLS pipeline II=1
        Metrics[16 + index] = static_cast<double>(stage_cycles[index].to_uint64());
    }
    Metrics[28] = static_cast<double>(stage_cycles[kPcgCallipeplaStageCount].to_uint64());
    Metrics[29] = static_cast<double>(update_r_work);
    Metrics[30] = static_cast<double>(apply_m_inv_work);
    Metrics[31] = static_cast<double>(update_p_work);

#ifdef CUPER_CALLIPEPLA_TRACE_ENABLED
    PcgCallipepla_WriteStatusSlot(Status_Write_out, 0, status_code);
    PcgCallipepla_WriteStatusSlot(Status_Write_out, 1, iterations);
    PcgCallipepla_WriteStatusSlot(Status_Write_out, 2, x_bank);
    PcgCallipepla_WriteStatusSlot(Status_Write_out, 3, r_bank);
    PcgCallipepla_WriteStatusSlot(Status_Write_out, 4, p_bank);
    PcgCallipepla_WriteStatusSlot(Status_Write_out, 5, HBM_CHANNEL_NUM);
    PcgCallipepla_WriteStatusSlot(Status_Write_out, 6, float_packet_count);
    PcgCallipepla_WriteStatusSlot(Status_Write_out, 7, Matrix_len);
    PcgCallipepla_WriteLiveStatus(Status_Write_out,
                                  99,
                                  iterations,
                                  x_bank,
                                  r_bank,
                                  p_bank,
                                  spmv_rounds,
                                  float_packet_count,
                                  Matrix_len);
    Debug_Stop_out.write(1);
#else
    PcgCallipepla_WriteStatusSlot(Status, 0, status_code);
    PcgCallipepla_WriteStatusSlot(Status, 1, iterations);
    PcgCallipepla_WriteStatusSlot(Status, 2, x_bank);
    PcgCallipepla_WriteStatusSlot(Status, 3, r_bank);
    PcgCallipepla_WriteStatusSlot(Status, 4, p_bank);
    PcgCallipepla_WriteStatusSlot(Status, 5, HBM_CHANNEL_NUM);
    PcgCallipepla_WriteStatusSlot(Status, 6, float_packet_count);
    PcgCallipepla_WriteStatusSlot(Status, 7, Matrix_len);
    PcgCallipepla_WriteLiveStatus(Status,
                                  99,
                                  iterations,
                                  x_bank,
                                  r_bank,
                                  p_bank,
                                  spmv_rounds,
                                  float_packet_count,
                                  Matrix_len);
#ifdef CUPER_CALLIPEPLA_PROBE_ENABLED
    PcgCallipepla_WriteProbeControllerStatus(Status,
                                             99,
                                             spmv_rounds,
                                             vector_command_count,
                                             vector_result_count,
                                             float_packet_count,
                                             Matrix_len,
                                             Row_num,
                                             Batch_num,
                                             Max_iters,
                                             iterations);
#endif
#endif
}
