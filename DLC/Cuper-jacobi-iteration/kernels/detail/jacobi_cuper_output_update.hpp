#pragma once

// Jacobi 专用 Cuper 输出更新 stage。
// 这里不再保留独立 VectorChecker/SortTree。8 个 pair compute 实例各自直接消费
// 两路 accumulator 输出，按 Cuper 原 checker 顺序丢弃 padding，然后立即完成两 lane 的
// x_next = (b + (-R*x_old)) * diag_inv。写回拆成 pack FIFO 和 HBM writer 两段：
// pack 端只负责按 HBM float_v16 宽度收包，HBM writer 独立写 X 并等待 write response。
// 轮次开始/停止由 Jacobi_MasterController 直接广播 JacobiUpdateCommand，不再使用
// 后端 frame fork 和 writer 自反馈 token。

#include <tapa.h>

#include "jacobi_common.hpp"
#ifdef JACOBI_TRACE_ENABLED
#include "jacobi_deadlock_debug.hpp"
#endif

struct JacobiCoeffPair {
    float_v2 b;
    float_v2 diag_inv;
};

struct JacobiUpdatedPair {
    float_v2 value;
};

inline void Jacobi_SplitCoeffPair(const float_v16 &b,
                                  const float_v16 &diag_inv,
                                  const INDEX_TYPE lane_pair,
                                  JacobiCoeffPair &pair) {
#pragma HLS inline
    pair.b[0] = b[lane_pair << 1];
    pair.b[1] = b[(lane_pair << 1) + 1];
    pair.diag_inv[0] = diag_inv[lane_pair << 1];
    pair.diag_inv[1] = diag_inv[(lane_pair << 1) + 1];
}

void Jacobi_UpdateCoeffLoader(tapa::istream<JacobiUpdateCommand> &Command_in,
                              tapa::ostreams<JacobiCoeffPair, 8> &Coeff_out,
                              tapa::async_mmap<float_v16> &B,
                              tapa::async_mmap<float_v16> &Diag_inv
#ifdef JACOBI_TRACE_ENABLED
                              ,
                              tapa::ostream<JacobiDebugEvent> &Debug_Event_out
#endif
                              ) {
    for (;;) {
#pragma HLS loop_flatten off
        const JacobiUpdateCommand command = Command_in.read();
        if (command.stop != 0) {
#ifdef JACOBI_TRACE_ENABLED
            Jacobi_DebugTryWrite(Debug_Event_out,
                                 kJacobiDebugSourceCoeffLoader,
                                 kJacobiDebugPhaseStop,
                                 command.iter,
                                 0);
#endif
            return;
        }
#ifdef JACOBI_TRACE_ENABLED
        Jacobi_DebugTryWrite(Debug_Event_out,
                             kJacobiDebugSourceCoeffLoader,
                             kJacobiDebugPhaseRecv,
                             command.iter,
                             command.packet_count);
        INDEX_TYPE debug_wait_tick = 0;
#endif

    load_coeff_packets:
        for (INDEX_TYPE request = 0, response = 0; response < command.packet_count;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
            if (request < command.packet_count &&
                !B.read_addr.full() &&
                !Diag_inv.read_addr.full()) {
                B.read_addr.try_write(request);
                Diag_inv.read_addr.try_write(request);
                ++request;
#ifdef JACOBI_TRACE_ENABLED
                if ((request & 0x3ff) == 0 || request == command.packet_count) {
                    Jacobi_DebugTryWrite(Debug_Event_out,
                                         kJacobiDebugSourceCoeffLoader,
                                         kJacobiDebugPhaseReadIssue,
                                         command.iter,
                                         request);
                }
#endif
            }

            bool coeff_output_ready = true;
        check_coeff_output_ready:
            for (INDEX_TYPE lane_pair = 0; lane_pair < 8; ++lane_pair) {
#pragma HLS unroll
                if (Coeff_out[lane_pair].full()) {
                    coeff_output_ready = false;
                }
            }

            if (coeff_output_ready && !B.read_data.empty() && !Diag_inv.read_data.empty()) {
                float_v16 b;
                float_v16 diag_inv;
                B.read_data.try_read(b);
                Diag_inv.read_data.try_read(diag_inv);
#ifdef JACOBI_TRACE_ENABLED
                debug_wait_tick = 0;
#endif

            split_coeff_lanes:
                for (INDEX_TYPE lane_pair = 0; lane_pair < 8; ++lane_pair) {
#pragma HLS unroll
                    JacobiCoeffPair pair;
                    Jacobi_SplitCoeffPair(b, diag_inv, lane_pair, pair);
                    Coeff_out[lane_pair].try_write(pair);
                }
                ++response;
#ifdef JACOBI_TRACE_ENABLED
                if ((response & 0x3ff) == 0 || response == command.packet_count) {
                    Jacobi_DebugTryWrite(Debug_Event_out,
                                         kJacobiDebugSourceCoeffLoader,
                                         kJacobiDebugPhaseReadResp,
                                         command.iter,
                                         response);
                }
#endif
#ifdef JACOBI_TRACE_ENABLED
            } else {
                INDEX_TYPE wait_code = 0;
                if (B.read_addr.full()) {
                    wait_code = 1;
                } else if (Diag_inv.read_addr.full()) {
                    wait_code = 2;
                } else if (B.read_data.empty()) {
                    wait_code = 3;
                } else if (Diag_inv.read_data.empty()) {
                    wait_code = 4;
                } else if (!coeff_output_ready) {
                    wait_code = 5;
                }
                ++debug_wait_tick;
                if ((debug_wait_tick & 0x3ff) == 0) {
                    Jacobi_DebugTryWrite(Debug_Event_out,
                                         kJacobiDebugSourceCoeffLoader,
                                         kJacobiDebugPhaseWait,
                                         wait_code,
                                         response);
                }
#endif
            }
        }
#ifdef JACOBI_TRACE_ENABLED
        Jacobi_DebugTryWrite(Debug_Event_out,
                             kJacobiDebugSourceCoeffLoader,
                             kJacobiDebugPhaseDoneRound,
                             command.iter,
                             command.packet_count);
#endif
    }
}

void Jacobi_UpdatePairCompute(tapa::istream<JacobiUpdateCommand> &Command_in,
                              tapa::istream<float_v2> &Neg_Rx_in_0,
                              tapa::istream<float_v2> &Neg_Rx_in_1,
                              tapa::istream<JacobiCoeffPair> &Coeff_in,
                              tapa::ostream<JacobiUpdatedPair> &Updated_out
#ifdef JACOBI_TRACE_ENABLED
                              ,
                              tapa::ostream<JacobiDebugEvent> &Debug_Event_out,
                              const INDEX_TYPE Debug_source
#endif
                              ) {
    for (;;) {
#pragma HLS loop_flatten off
        const JacobiUpdateCommand command = Command_in.read();
        if (command.stop != 0) {
#ifdef JACOBI_TRACE_ENABLED
            Jacobi_DebugTryWrite(Debug_Event_out,
                                 Debug_source,
                                 kJacobiDebugPhaseStop,
                                 0,
                                 0);
#endif
            return;
        }

        const INDEX_TYPE num_pe_output = spmv_service_num_checker_pe_outputs(command.row_num);
        const INDEX_TYPE num_out = command.packet_count;
#ifdef JACOBI_TRACE_ENABLED
        Jacobi_DebugTryWrite(Debug_Event_out,
                             Debug_source,
                             kJacobiDebugPhaseEnterRound,
                             command.iter,
                             num_out);
        INDEX_TYPE debug_wait_tick = 0;
#endif
    filter_update_round:
        for (INDEX_TYPE i = 0, c_idx = 0, o_idx = 0; i < num_pe_output;) {
#pragma HLS pipeline II=1
            const bool is_valid_output = (o_idx < num_out);
            const bool can_emit_update = !Coeff_in.empty() && !Updated_out.full();
            const bool neg_rx_ready = (c_idx == 0) ? !Neg_Rx_in_0.empty() : !Neg_Rx_in_1.empty();
            if (neg_rx_ready && (!is_valid_output || can_emit_update)) {
#ifdef JACOBI_TRACE_ENABLED
                debug_wait_tick = 0;
#endif
                float_v2 neg_rx;
                if (c_idx == 0) {
                    Neg_Rx_in_0.try_read(neg_rx);
                } else {
                    Neg_Rx_in_1.try_read(neg_rx);
                }

                if (is_valid_output) {
                    JacobiCoeffPair coeff;
                    JacobiUpdatedPair updated;
                    Coeff_in.try_read(coeff);

        update_pair_lanes:
                    for (INDEX_TYPE lane = 0; lane < 2; ++lane) {
#pragma HLS unroll
                        updated.value[lane] = (coeff.b[lane] + neg_rx[lane]) * coeff.diag_inv[lane];
                    }
                    Updated_out.try_write(updated);
#ifdef JACOBI_TRACE_ENABLED
                    if ((o_idx & 0x3ff) == 0) {
                        Jacobi_DebugTryWrite(Debug_Event_out,
                                             Debug_source,
                                             kJacobiDebugPhaseProgress,
                                             c_idx,
                                             o_idx);
                    }
#endif
                }

                ++i;
                ++c_idx;
                ++o_idx;
                if (c_idx == HBM_CHANNEL_NUM_DIV_8) {
                    c_idx = 0;
                }
                if (o_idx == num_pe_output) {
                    o_idx = 0;
                }
#ifdef JACOBI_TRACE_ENABLED
            } else {
                INDEX_TYPE wait_code = 0;
                if (!neg_rx_ready) {
                    wait_code = 1 + c_idx;
                } else if (is_valid_output && Coeff_in.empty()) {
                    wait_code = 10;
                } else if (is_valid_output && Updated_out.full()) {
                    wait_code = 11;
                }
                ++debug_wait_tick;
                if (i != 0 && (debug_wait_tick & 0x3ff) == 0) {
                    Jacobi_DebugTryWrite(Debug_Event_out,
                                         Debug_source,
                                         kJacobiDebugPhaseWait,
                                         wait_code,
                                         i);
                }
#endif
            }
        }
#ifdef JACOBI_TRACE_ENABLED
        Jacobi_DebugTryWrite(Debug_Event_out,
                             Debug_source,
                             kJacobiDebugPhaseDoneRound,
                             0,
                             num_out);
#endif
    }
}

void Jacobi_UpdatePackWriter(tapa::istream<JacobiUpdateCommand> &Command_in,
                             tapa::istreams<JacobiUpdatedPair, 8> &Updated_in,
                             tapa::ostream<float_v16> &X_Write_out
#ifdef JACOBI_TRACE_ENABLED
                             ,
                             tapa::ostream<JacobiDebugEvent> &Debug_Event_out
#endif
                             ) {
    for (;;) {
#pragma HLS loop_flatten off
        const JacobiUpdateCommand command = Command_in.read();
        if (command.stop != 0) {
#ifdef JACOBI_TRACE_ENABLED
            Jacobi_DebugTryWrite(Debug_Event_out,
                                 kJacobiDebugSourcePackWriter,
                                 kJacobiDebugPhaseStop,
                                 command.iter,
                                 0);
#endif
            return;
        }
#ifdef JACOBI_TRACE_ENABLED
        Jacobi_DebugTryWrite(Debug_Event_out,
                             kJacobiDebugSourcePackWriter,
                             kJacobiDebugPhaseEnterRound,
                             command.iter,
                             command.packet_count);
        INDEX_TYPE debug_wait_tick = 0;
#endif

    pack_packets:
        for (INDEX_TYPE packet = 0; packet < command.packet_count;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
            bool all_updated_ready = true;
        check_updated_ready:
            for (INDEX_TYPE lane_pair = 0; lane_pair < 8; ++lane_pair) {
#pragma HLS unroll
                if (Updated_in[lane_pair].empty()) {
                    all_updated_ready = false;
                }
            }

            if (all_updated_ready && !X_Write_out.full()) {
#ifdef JACOBI_TRACE_ENABLED
                debug_wait_tick = 0;
#endif
                float_v16 x_next;

            pack_updated_lanes:
                for (INDEX_TYPE lane_pair = 0; lane_pair < 8; ++lane_pair) {
#pragma HLS unroll
                    JacobiUpdatedPair updated;
                    Updated_in[lane_pair].try_read(updated);
                    x_next[lane_pair << 1] = updated.value[0];
                    x_next[(lane_pair << 1) + 1] = updated.value[1];
                }

            mask_padding_lanes:
                for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
#pragma HLS unroll
                    const INDEX_TYPE row = (packet << 4) + lane;
                    if (row >= command.row_num) {
                        x_next[lane] = 0.0f;
                    }
                }

                X_Write_out.try_write(x_next);
                ++packet;
#ifdef JACOBI_TRACE_ENABLED
                if ((packet & 0x3ff) == 0 || packet == command.packet_count) {
                    Jacobi_DebugTryWrite(Debug_Event_out,
                                         kJacobiDebugSourcePackWriter,
                                         kJacobiDebugPhaseProgress,
                                         command.iter,
                                         packet);
                }
#endif
#ifdef JACOBI_TRACE_ENABLED
            } else {
                INDEX_TYPE wait_code = 0;
                if (!all_updated_ready) {
                find_empty_updated_lane:
                    for (INDEX_TYPE lane_pair = 0; lane_pair < 8; ++lane_pair) {
#pragma HLS unroll
                        if (wait_code == 0 && Updated_in[lane_pair].empty()) {
                            wait_code = 1 + lane_pair;
                        }
                    }
                } else if (X_Write_out.full()) {
                    wait_code = 20;
                }
                ++debug_wait_tick;
                if ((debug_wait_tick & 0x3ff) == 0) {
                    Jacobi_DebugTryWrite(Debug_Event_out,
                                         kJacobiDebugSourcePackWriter,
                                         kJacobiDebugPhaseWait,
                                         wait_code,
                                         packet);
                }
#endif
            }
        }
#ifdef JACOBI_TRACE_ENABLED
        Jacobi_DebugTryWrite(Debug_Event_out,
                             kJacobiDebugSourcePackWriter,
                             kJacobiDebugPhaseDoneRound,
                             command.iter,
                             command.packet_count);
#endif
    }
}

void Jacobi_XHbmWriter(tapa::istream<JacobiUpdateCommand> &Command_in,
                       tapa::istream<float_v16> &X_Write_in,
                       tapa::ostream<JacobiUpdateDone> &Done_out,
                       tapa::async_mmap<float_v16> &X
#ifdef JACOBI_TRACE_ENABLED
                       ,
                       tapa::ostream<JacobiDebugEvent> &Debug_Event_out
#endif
                       ) {
    for (;;) {
#pragma HLS loop_flatten off
        const JacobiUpdateCommand command = Command_in.read();
        if (command.stop != 0) {
#ifdef JACOBI_TRACE_ENABLED
            Jacobi_DebugTryWrite(Debug_Event_out,
                                 kJacobiDebugSourceHbmWriter,
                                 kJacobiDebugPhaseStop,
                                 command.iter,
                                 0);
#endif
            return;
        }

#ifdef JACOBI_TRACE_ENABLED
        Jacobi_DebugTryWrite(Debug_Event_out,
                             kJacobiDebugSourceHbmWriter,
                             kJacobiDebugPhaseEnterRound,
                             command.iter,
                             command.packet_count);
        INDEX_TYPE debug_wait_tick = 0;
#endif

    write_packets:
        for (INDEX_TYPE write_issued = 0, write_response = 0;
             write_response < command.packet_count;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
            const bool write_ready = !X_Write_in.empty() &&
                                     !X.write_addr.full() &&
                                     !X.write_data.full();

            if (write_issued < command.packet_count && write_ready) {
#ifdef JACOBI_TRACE_ENABLED
                debug_wait_tick = 0;
#endif
                float_v16 x_next;
                X_Write_in.try_read(x_next);
                X.write_addr.try_write(write_issued);
                X.write_data.try_write(x_next);
                ++write_issued;
#ifdef JACOBI_TRACE_ENABLED
                if ((write_issued & 0x3ff) == 0 || write_issued == command.packet_count) {
                    Jacobi_DebugTryWrite(Debug_Event_out,
                                         kJacobiDebugSourceHbmWriter,
                                         kJacobiDebugPhaseProgress,
                                         1,
                                         write_issued);
                }
#endif
#ifdef JACOBI_TRACE_ENABLED
            } else if (write_issued < command.packet_count) {
                INDEX_TYPE wait_code = 0;
                if (X_Write_in.empty()) {
                    wait_code = 1;
                } else if (X.write_addr.full()) {
                    wait_code = 2;
                } else if (X.write_data.full()) {
                    wait_code = 3;
                }
                ++debug_wait_tick;
                if ((debug_wait_tick & 0x3ff) == 0) {
                    Jacobi_DebugTryWrite(Debug_Event_out,
                                         kJacobiDebugSourceHbmWriter,
                                         kJacobiDebugPhaseWait,
                                         wait_code,
                                         write_issued);
                }
#endif
            }

            uint8_t num_responses = 0;
            if (X.write_resp.try_read(num_responses)) {
                write_response += int(num_responses) + 1;
#ifdef JACOBI_TRACE_ENABLED
                if ((write_response & 0x3ff) == 0 || write_response >= command.packet_count) {
                    Jacobi_DebugTryWrite(Debug_Event_out,
                                         kJacobiDebugSourceHbmWriter,
                                         kJacobiDebugPhaseProgress,
                                         2,
                                         write_response);
                }
#endif
            }
        }

#ifdef JACOBI_TRACE_ENABLED
        Jacobi_DebugTryWrite(Debug_Event_out,
                             kJacobiDebugSourceHbmWriter,
                             kJacobiDebugPhaseDoneRound,
                             command.iter,
                             command.packet_count);
#endif
        Done_out.write(Jacobi_MakeUpdateDone(command.iter, command.packet_count));
#ifdef JACOBI_TRACE_ENABLED
        Jacobi_DebugTryWrite(Debug_Event_out,
                             kJacobiDebugSourceHbmWriter,
                             kJacobiDebugPhaseFeedback,
                             command.iter,
                             command.packet_count);
#endif
    }
}
