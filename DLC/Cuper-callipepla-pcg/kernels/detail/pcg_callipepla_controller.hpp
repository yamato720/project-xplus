#pragma once

#include <ap_int.h>
#include <tapa.h>

#include "pcg_callipepla_common.hpp"

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
    tapa::mmap<double> Residuals,
    tapa::mmap<INDEX_TYPE> Status,
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

    unsigned long long init_spmv_work = 0;
    unsigned long long init_zp_work = 0;
    unsigned long long iter_spmv_work = 0;
    unsigned long long update_x_work = 0;
    unsigned long long update_r_work = 0;
    unsigned long long apply_m_inv_work = 0;
    unsigned long long update_p_work = 0;
    unsigned long long residual_writes = 0;

    pcg_callipepla_stage_mark(Stage_Event_out,
                              kPcgCallipeplaStageTotal,
                              kPcgCallipeplaStageBegin);
    PcgCallipepla_WriteLiveStatus(Status,
                                  1,
                                  0,
                                  x_bank,
                                  r_bank,
                                  p_bank,
                                  spmv_rounds,
                                  float_packet_count,
                                  Matrix_len);

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
        Vector_Command_out.write(
            pcg_callipepla_make_vector_command(kPcgCallipeplaPhaseInitSpmv,
                                               -1,
                                               x_bank,
                                               x_bank,
                                               r_bank,
                                               r_bank,
                                               p_bank,
                                               p_bank,
                                               0.0,
                                               0.0));
        (void)Vector_Result_in.read();
        ++spmv_rounds;
        init_spmv_work += static_cast<unsigned long long>(float_packet_count) +
                          static_cast<unsigned long long>(double_packet_count);
        pcg_callipepla_stage_mark(Stage_Event_out,
                                  kPcgCallipeplaStageInitSpmv,
                                  kPcgCallipeplaStageEnd);
        PcgCallipepla_WriteLiveStatus(Status,
                                      2,
                                      0,
                                      x_bank,
                                      r_bank,
                                      p_bank,
                                      spmv_rounds,
                                      float_packet_count,
                                      Matrix_len);

        pcg_callipepla_stage_mark(Stage_Event_out,
                                  kPcgCallipeplaStageInitZp,
                                  kPcgCallipeplaStageBegin);
        Vector_Command_out.write(
            pcg_callipepla_make_vector_command(kPcgCallipeplaPhaseInitZp,
                                               -1,
                                               x_bank,
                                               x_bank,
                                               r_bank,
                                               r_bank,
                                               p_bank,
                                               p_bank,
                                               0.0,
                                               0.0));
        const PcgCallipeplaVectorResult init_result = Vector_Result_in.read();
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

            PcgCallipepla_WriteLiveStatus(Status,
                                          10,
                                          iter,
                                          x_bank,
                                          r_bank,
                                          p_bank,
                                          spmv_rounds,
                                          float_packet_count,
                                          Matrix_len);

            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageIterSpmv,
                                      kPcgCallipeplaStageBegin);
            pcg_callipepla_send_spmv_command(Ptr_Command_out,
                                             Matrix_Command_out,
                                             Spmv_Vector_Command_out,
                                             kPcgCallipeplaVectorSourceP,
                                             p_bank);
            Vector_Command_out.write(
                pcg_callipepla_make_vector_command(kPcgCallipeplaPhaseIterDot,
                                                   iter,
                                                   x_bank,
                                                   x_next_bank,
                                                   r_bank,
                                                   r_next_bank,
                                                   p_bank,
                                                   p_next_bank,
                                                   0.0,
                                                   0.0));
            const PcgCallipeplaVectorResult iter_dot = Vector_Result_in.read();
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
            Vector_Command_out.write(
                pcg_callipepla_make_vector_command(kPcgCallipeplaPhaseUpdateX,
                                                   iter,
                                                   x_bank,
                                                   x_next_bank,
                                                   r_bank,
                                                   r_next_bank,
                                                   p_bank,
                                                   p_next_bank,
                                                   alpha,
                                                   0.0));
            (void)Vector_Result_in.read();
            x_bank = x_next_bank;
            update_x_work += 3ULL * static_cast<unsigned long long>(double_packet_count);
            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageUpdateX,
                                      kPcgCallipeplaStageEnd);

            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageUpdateR,
                                      kPcgCallipeplaStageBegin);
            Vector_Command_out.write(
                pcg_callipepla_make_vector_command(kPcgCallipeplaPhaseUpdateR,
                                                   iter,
                                                   x_bank,
                                                   x_bank,
                                                   r_bank,
                                                   r_next_bank,
                                                   p_bank,
                                                   p_next_bank,
                                                   alpha,
                                                   0.0));
            (void)Vector_Result_in.read();
            r_bank = r_next_bank;
            update_r_work += static_cast<unsigned long long>(float_packet_count) +
                             2ULL * static_cast<unsigned long long>(double_packet_count);
            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageUpdateR,
                                      kPcgCallipeplaStageEnd);

            pcg_callipepla_stage_mark(Stage_Event_out,
                                      kPcgCallipeplaStageApplyMInv,
                                      kPcgCallipeplaStageBegin);
            Vector_Command_out.write(
                pcg_callipepla_make_vector_command(kPcgCallipeplaPhaseApplyMInvDot,
                                                   iter,
                                                   x_bank,
                                                   x_bank,
                                                   r_bank,
                                                   r_bank,
                                                   p_bank,
                                                   p_next_bank,
                                                   alpha,
                                                   0.0));
            const PcgCallipeplaVectorResult rz_result = Vector_Result_in.read();
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

            PcgCallipepla_WriteLiveStatus(Status,
                                          20,
                                          iterations,
                                          x_bank,
                                          r_bank,
                                          p_bank,
                                          spmv_rounds,
                                          float_packet_count,
                                          Matrix_len);

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
            Vector_Command_out.write(
                pcg_callipepla_make_vector_command(kPcgCallipeplaPhaseUpdateP,
                                                   iter,
                                                   x_bank,
                                                   x_bank,
                                                   r_bank,
                                                   r_bank,
                                                   p_bank,
                                                   p_next_bank,
                                                   alpha,
                                                   beta));
            (void)Vector_Result_in.read();
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
    Vector_Command_out.write(pcg_callipepla_make_vector_stop());
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

    Status[0] = status_code;
    Status[1] = iterations;
    Status[2] = x_bank;
    Status[3] = r_bank;
    Status[4] = p_bank;
    Status[5] = HBM_CHANNEL_NUM;
    Status[6] = float_packet_count;
    Status[7] = Matrix_len;
    PcgCallipepla_WriteLiveStatus(Status,
                                  99,
                                  iterations,
                                  x_bank,
                                  r_bank,
                                  p_bank,
                                  spmv_rounds,
                                  float_packet_count,
                                  Matrix_len);
}
