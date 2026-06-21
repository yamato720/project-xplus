#pragma once

// Cuper SpMV service-only 顶层。
//
// 这个文件用于 24/32 路 Cuper SpMV 隔离实验：只保留 Cuper 的 one-shot
// SpMV 数据通路，不接 Jacobi 的 b/diag/update/controller，也不接 PCG 控制。

#include <ap_int.h>
#include <tapa.h>

#include "cuper_spmv_tasks.hpp"

static constexpr INDEX_TYPE kCuperSpmvOnlyProgressMagic = 0x53504d56;  // "SPMV"
static constexpr INDEX_TYPE kCuperSpmvOnlyProgressEntry = 1;
static constexpr INDEX_TYPE kCuperSpmvOnlyProgressPtrLengths = 2;
static constexpr INDEX_TYPE kCuperSpmvOnlyProgressPtrDone = 3;
static constexpr INDEX_TYPE kCuperSpmvOnlyProgressScatterStart = 10;
static constexpr INDEX_TYPE kCuperSpmvOnlyProgressScatterFirstTag = 11;
static constexpr INDEX_TYPE kCuperSpmvOnlyProgressScatterFirstWrite = 12;
static constexpr INDEX_TYPE kCuperSpmvOnlyProgressScatterFirstResp = 13;
static constexpr INDEX_TYPE kCuperSpmvOnlyProgressScatterDone = 14;
static constexpr INDEX_TYPE kCuperSpmvOnlyProgressFinal = 15;

struct CuperSpmvOnlyProgressEvent {
    INDEX_TYPE stage;
    INDEX_TYPE value0;
    INDEX_TYPE value1;
    INDEX_TYPE value2;
};

inline void CuperSpmvOnly_WriteStatus(tapa::async_mmap<INDEX_TYPE> &Status,
                                      const INDEX_TYPE iterations_done,
                                      const INDEX_TYPE row_num);

inline void CuperSpmvOnly_WriteMetrics(tapa::async_mmap<double> &Metrics,
                                       const INDEX_TYPE batch_num,
                                       const INDEX_TYPE matrix_len,
                                       const INDEX_TYPE row_num,
                                       const INDEX_TYPE column_num,
                                       const INDEX_TYPE iterations_done);

inline CuperSpmvOnlyProgressEvent CuperSpmvOnly_MakeProgressEvent(
    const INDEX_TYPE stage,
    const INDEX_TYPE value0,
    const INDEX_TYPE value1,
    const INDEX_TYPE value2) {
#pragma HLS inline
    CuperSpmvOnlyProgressEvent event;
    event.stage = stage;
    event.value0 = value0;
    event.value1 = value1;
    event.value2 = value2;
    return event;
}

inline void CuperSpmvOnly_WriteProgressSnapshot(
    tapa::async_mmap<INDEX_TYPE> &Status,
    tapa::async_mmap<double> &Metrics,
    const CuperSpmvOnlyProgressEvent event,
    const INDEX_TYPE event_count) {
#pragma HLS inline
    Status.write_addr.write(8);
    Status.write_data.write(kCuperSpmvOnlyProgressMagic);
    Status.write_addr.write(9);
    Status.write_data.write(event.stage);
    Status.write_addr.write(10);
    Status.write_data.write(event.value0);
    Status.write_addr.write(11);
    Status.write_data.write(event.value1);
    Status.write_addr.write(12);
    Status.write_data.write(event.value2);
    Status.write_addr.write(13);
    Status.write_data.write(event_count);
    Status.write_addr.write(14);
    Status.write_data.write(HBM_CHANNEL_NUM);
    Status.write_addr.write(15);
    Status.write_data.write(Slice_WIDTH);

write_spmv_progress_status_resp:
    for (INDEX_TYPE response_count = 0; response_count < 8;) {
#pragma HLS pipeline II=1
        uint8_t num_responses = 0;
        if (Status.write_resp.try_read(num_responses)) {
            response_count += int(num_responses) + 1;
        }
    }

    Metrics.write_addr.write(8);
    Metrics.write_data.write(static_cast<double>(kCuperSpmvOnlyProgressMagic));
    Metrics.write_addr.write(9);
    Metrics.write_data.write(static_cast<double>(event.stage));
    Metrics.write_addr.write(10);
    Metrics.write_data.write(static_cast<double>(event.value0));
    Metrics.write_addr.write(11);
    Metrics.write_data.write(static_cast<double>(event.value1));
    Metrics.write_addr.write(12);
    Metrics.write_data.write(static_cast<double>(event.value2));
    Metrics.write_addr.write(13);
    Metrics.write_data.write(static_cast<double>(event_count));
    Metrics.write_addr.write(14);
    Metrics.write_data.write(static_cast<double>(HBM_CHANNEL_NUM));
    Metrics.write_addr.write(15);
    Metrics.write_data.write(static_cast<double>(Slice_WIDTH));

write_spmv_progress_metrics_resp:
    for (INDEX_TYPE response_count = 0; response_count < 8;) {
#pragma HLS pipeline II=1
        uint8_t num_responses = 0;
        if (Metrics.write_resp.try_read(num_responses)) {
            response_count += int(num_responses) + 1;
        }
    }
}

void CuperSpmvOnly_ProgressWriter(
    const INDEX_TYPE Batch_num,
    const INDEX_TYPE Matrix_len,
    const INDEX_TYPE Row_num,
    const INDEX_TYPE Column_num,
    const INDEX_TYPE Iteration_num,
    tapa::istream<CuperSpmvOnlyProgressEvent> &Ptr_Progress_in,
    tapa::istream<CuperSpmvOnlyProgressEvent> &Writer_Progress_in,
    tapa::async_mmap<INDEX_TYPE> &Status,
    tapa::async_mmap<double> &Metrics) {
    // Status/Metrics 的唯一 writer。
    //
    // 其他 task 只发 progress event，避免多个 task 同时写同一个 mmap 端口。
    // 这些槽位会被 host 在 Finish() 前主动 sync 读取，用来判断板上卡住时
    // kernel 到达了哪一段。
    INDEX_TYPE event_count = 1;
    CuperSpmvOnly_WriteProgressSnapshot(
        Status,
        Metrics,
        CuperSpmvOnly_MakeProgressEvent(kCuperSpmvOnlyProgressEntry,
                                        Row_num,
                                        Batch_num,
                                        Matrix_len),
        event_count);

progress_loop:
    for (bool done = false; !done;) {
        CuperSpmvOnlyProgressEvent event;
        bool got_event = false;
        if (!Writer_Progress_in.empty()) {
            Writer_Progress_in.try_read(event);
            got_event = true;
        } else if (!Ptr_Progress_in.empty()) {
            Ptr_Progress_in.try_read(event);
            got_event = true;
        }

        if (got_event) {
            ++event_count;
            CuperSpmvOnly_WriteProgressSnapshot(Status, Metrics, event, event_count);
            done = (event.stage == kCuperSpmvOnlyProgressFinal);
        }
    }

    CuperSpmvOnly_WriteStatus(Status, Iteration_num == 0 ? 1 : Iteration_num, Row_num);
    CuperSpmvOnly_WriteMetrics(Metrics,
                               Batch_num,
                               Matrix_len,
                               Row_num,
                               Column_num,
                               Iteration_num == 0 ? 1 : Iteration_num);
}

void CuperSpmvOnly_NullProgressSource(
    tapa::ostream<CuperSpmvOnlyProgressEvent> &Progress_out) {
    (void)Progress_out;
}

inline void CuperSpmvOnly_WriteStatus(tapa::async_mmap<INDEX_TYPE> &Status,
                                      const INDEX_TYPE iterations_done,
                                      const INDEX_TYPE row_num) {
#pragma HLS inline
    // Status[0] = 1 表示 writer 已经等到 Y_out write response 并正常收尾。
    // Status[1] 记录当前编译出来的 HBM 通道数，便于 host/日志核对 ABI。
    // Status[2] 记录完成的 SpMV repeat 数；Status[3] 记录 Row_num。
    Status.write_addr.write(0);
    Status.write_data.write(1);
    Status.write_addr.write(1);
    Status.write_data.write(HBM_CHANNEL_NUM);
    Status.write_addr.write(2);
    Status.write_data.write(iterations_done);
    Status.write_addr.write(3);
    Status.write_data.write(row_num);

write_spmv_status_resp:
    for (INDEX_TYPE response_count = 0; response_count < 4;) {
#pragma HLS pipeline II=1
        uint8_t num_responses = 0;
        if (Status.write_resp.try_read(num_responses)) {
            response_count += int(num_responses) + 1;
        }
    }
}

inline void CuperSpmvOnly_WriteMetrics(tapa::async_mmap<double> &Metrics,
                                       const INDEX_TYPE batch_num,
                                       const INDEX_TYPE matrix_len,
                                       const INDEX_TYPE row_num,
                                       const INDEX_TYPE column_num,
                                       const INDEX_TYPE iterations_done) {
#pragma HLS inline
    // 这里只写固定工作量指标，不引入额外 debug dataflow。
    Metrics.write_addr.write(0);
    Metrics.write_data.write(static_cast<double>(iterations_done));
    Metrics.write_addr.write(1);
    Metrics.write_data.write(static_cast<double>(row_num));
    Metrics.write_addr.write(2);
    Metrics.write_data.write(static_cast<double>(column_num));
    Metrics.write_addr.write(3);
    Metrics.write_data.write(static_cast<double>(Cuper_NumFloatV16Packets(row_num)));
    Metrics.write_addr.write(4);
    Metrics.write_data.write(static_cast<double>(batch_num));
    Metrics.write_addr.write(5);
    Metrics.write_data.write(static_cast<double>(matrix_len));
    Metrics.write_addr.write(6);
    Metrics.write_data.write(static_cast<double>(HBM_CHANNEL_NUM));
    Metrics.write_addr.write(7);
    Metrics.write_data.write(static_cast<double>(Slice_WIDTH));

write_spmv_metrics_resp:
    for (INDEX_TYPE response_count = 0; response_count < 8;) {
#pragma HLS pipeline II=1
        uint8_t num_responses = 0;
        if (Metrics.write_resp.try_read(num_responses)) {
            response_count += int(num_responses) + 1;
        }
    }
}

#if defined(JACOBI_SPMV_STRIP_PADDING) || \
    defined(JACOBI_SPMV_COMPACT_PE) || \
    defined(JACOBI_SPMV_LANE_STATIC_REAL)
void CuperSpmvOnly_StripPtrLoader(
    const INDEX_TYPE Batch_num,
    const INDEX_TYPE Row_num,
    const INDEX_TYPE Iteration_num,
    const INDEX_TYPE Column_num,
    tapa::async_mmap<INDEX_TYPE> &SpElement_list_ptr,
    tapa::ostream<INDEX_TYPE> &PE_Param,
    tapa::ostreams<INDEX_TYPE, HBM_CHANNEL_NUM> &Matrix_Len_Stream,
    tapa::ostream<CuperSpmvOnlyProgressEvent> &Progress_out) {
    // 去 padding 版本的 ptr 表格式：
    //   [0, HBM_CHANNEL_NUM)                         : 每路 Matrix_data 总 beat 数
    //   [HBM_CHANNEL_NUM, ...] boundary-major layout : 每个 batch boundary 的每路 HBM 边界
    INDEX_TYPE matrix_len[HBM_CHANNEL_NUM];
#pragma HLS array_partition variable=matrix_len complete

read_lengths:
    for (INDEX_TYPE i_request = 0, i_response = 0; i_response < HBM_CHANNEL_NUM;) {
#pragma HLS loop_tripcount min=16 max=32
#pragma HLS pipeline II=1
        if (i_request < HBM_CHANNEL_NUM && !SpElement_list_ptr.read_addr.full()) {
            SpElement_list_ptr.read_addr.try_write(i_request);
            ++i_request;
        }
        if (!SpElement_list_ptr.read_data.empty()) {
            INDEX_TYPE value = 0;
            SpElement_list_ptr.read_data.try_read(value);
            matrix_len[i_response] = value;
            ++i_response;
        }
    }

    Progress_out.write(CuperSpmvOnly_MakeProgressEvent(
        kCuperSpmvOnlyProgressPtrLengths,
        Batch_num,
        matrix_len[0],
        matrix_len[HBM_CHANNEL_NUM - 1]));

    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;

    PE_Param.write(Batch_num);
    PE_Param.write(Row_num);
    PE_Param.write(Iteration_num);
    PE_Param.write(Column_num);

iter:
    for (INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
    send_lengths:
        for (INDEX_TYPE channel = 0; channel < HBM_CHANNEL_NUM; ++channel) {
#pragma HLS unroll
            Matrix_Len_Stream[channel].write(matrix_len[channel]);
        }

        const INDEX_TYPE packet_count = (Batch_num + 1) * HBM_CHANNEL_NUM;
        const INDEX_TYPE base_addr = HBM_CHANNEL_NUM;
    read_ptrs:
        for (INDEX_TYPE i_request = 0, i_response = 0; i_response < packet_count;) {
#pragma HLS loop_tripcount min=17 max=2600
#pragma HLS pipeline II=1
            if (i_request < packet_count && !SpElement_list_ptr.read_addr.full()) {
                SpElement_list_ptr.read_addr.try_write(base_addr + i_request);
                ++i_request;
            }
            if (!PE_Param.full() && !SpElement_list_ptr.read_data.empty()) {
                INDEX_TYPE value = 0;
                SpElement_list_ptr.read_data.try_read(value);
                PE_Param.try_write(value);
                ++i_response;
            }
        }
    }

    Progress_out.write(CuperSpmvOnly_MakeProgressEvent(
        kCuperSpmvOnlyProgressPtrDone,
        Row_num,
        Batch_num,
        Column_num));
}

void CuperSpmvOnly_MatrixLoaderStrip(
    const INDEX_TYPE Iteration_num,
    tapa::async_mmap<ap_uint<512>> &Matrix_data,
    tapa::istream<INDEX_TYPE> &Matrix_Len_Stream,
    tapa::ostream<ap_uint<512>> &Matrix_A_Stream) {
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;

iter:
    for (INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
        const INDEX_TYPE matrix_len = Matrix_Len_Stream.read();
        Cuper_ReadMatrixPackets(matrix_len,
                                Matrix_data,
                                Matrix_A_Stream);
    }
}

inline INDEX_TYPE CuperSpmvOnly_ReadStripBoundary(
    const INDEX_TYPE core_id,
    tapa::istream<INDEX_TYPE> &PE_Param_in,
    tapa::ostream<INDEX_TYPE> &PE_Param_out) {
#pragma HLS inline
    INDEX_TYPE local_boundary = 0;

read_boundary_group:
    for (INDEX_TYPE channel = core_id; channel < HBM_CHANNEL_NUM; ++channel) {
#pragma HLS loop_tripcount min=1 max=32
        const INDEX_TYPE boundary = PE_Param_in.read();
        if (channel == core_id) {
            local_boundary = boundary;
        } else {
            PE_Param_out.write(boundary);
        }
    }
    return local_boundary;
}

inline void CuperSpmvOnly_CoreComputeRoundStrip(
    const INDEX_TYPE Core_id,
    const INDEX_TYPE Batch_num,
    const INDEX_TYPE Column_num,
    tapa::istream<INDEX_TYPE> &PE_Param_in,
    tapa::istream<ap_uint<512>> &Matrix_A_Stream,
    tapa::istream<float_v16> &Vector_X_Stream_in,
    tapa::ostream<INDEX_TYPE> &PE_Param_out,
    tapa::ostream<float_v16> &Vector_X_Stream_out,
    tapa::ostream<INDEX_TYPE> &Vector_Y_Param,
    tapa::ostream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream) {
#pragma HLS inline
    VALUE_TYPE local_X[X_BRAM_DEPTH][Slice_WIDTH];

#pragma HLS bind_storage variable=local_X latency=2
#pragma HLS array_partition variable=local_X complete dim=1
#pragma HLS array_partition variable=local_X cyclic factor=X_PARTITION_FACTOR dim=2

    INDEX_TYPE start_32 =
        CuperSpmvOnly_ReadStripBoundary(Core_id, PE_Param_in, PE_Param_out);
    Vector_Y_Param.write(start_32);

cuper_spmv_only_strip_core_main:
    for (INDEX_TYPE i = 0; i < Batch_num; ++i) {
#pragma HLS loop_tripcount min=1 max=200
        const INDEX_TYPE total_vector_packets = Cuper_NumFloatV16Packets(Column_num);
        const INDEX_TYPE start_idx = i * Slice_WIDTH_DIV_16;
        const INDEX_TYPE end_idx = std::min(start_idx + Slice_WIDTH_DIV_16,
                                            total_vector_packets);

    load_vector:
        for (INDEX_TYPE j = start_idx; j < end_idx;) {
#pragma HLS loop_tripcount min=1 max=512
#pragma HLS pipeline II=1
            if (!Vector_X_Stream_in.empty() && !Vector_X_Stream_out.full()) {
                float_v16 x;
                Vector_X_Stream_in.try_read(x);
                Vector_X_Stream_out.try_write(x);

                for (INDEX_TYPE k = 0; k < 16; ++k) {
                    for (INDEX_TYPE l = 0; l < X_BRAM_DEPTH; ++l) {
                        local_X[l][((j - start_idx) << 4) + k] = x[k];
                    }
                }
                ++j;
            }
        }

        const INDEX_TYPE end_32 =
            CuperSpmvOnly_ReadStripBoundary(Core_id, PE_Param_in, PE_Param_out);
        Vector_Y_Param.write(end_32);

    decode_matrix:
        for (INDEX_TYPE j = start_32; j < end_32;) {
#pragma HLS loop_tripcount min=1 max=200
#pragma HLS pipeline II=1
            if (!Matrix_A_Stream.empty()) {
                ap_uint<512> spelement;
                Matrix_A_Stream.try_read(spelement);
                Matrix_Mult_X matmultx;

#ifdef FLEX_REUSE
                ap_uint<14> col_old = 0x3FFF;
                VALUE_TYPE val_old = 0.0;
#endif
                for (INDEX_TYPE p = 0; p < 8; ++p) {
                    ap_uint<64> a = spelement(63 + p * 64, p * 64);
                    ap_uint<14> a_col = a(63, 50);
                    ap_uint<18> a_row = a(49, 32);
                    ap_uint<32> a_val = a(31, 0);

                    matmultx.row[p] = a_row;
                    if (a_row[17] == 0) {
#ifdef FLEX_REUSE
                        VALUE_TYPE val;
                        if ((col_old & a_col) == 0x3FFF) {
                            val = val_old;
                        } else {
                            val = tapa::bit_cast<VALUE_TYPE>(a_val);
                        }
#else
                        VALUE_TYPE val = tapa::bit_cast<VALUE_TYPE>(a_val);
#endif
                        matmultx.val[p] =
                            val * local_X[p / (8 / X_BRAM_DEPTH)][a_col];
#ifdef FLEX_REUSE
                        col_old = a_col;
                        val_old = val;
#endif
                    }
                }
                Matrix_Mult_Vector_Stream.write(matmultx);
                ++j;
            }
        }
        start_32 = end_32;
    }
}

void CuperSpmvOnly_CoreStrip(
    tapa::istream<INDEX_TYPE> &PE_Param_in,
    tapa::istream<ap_uint<512>> &Matrix_A_Stream,
    tapa::istream<float_v16> &Vector_X_Stream_in,
    tapa::ostream<INDEX_TYPE> &PE_Param_out,
    tapa::ostream<float_v16> &Vector_X_Stream_out,
    tapa::ostream<INDEX_TYPE> &Vector_Y_Param,
    tapa::ostream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream,
    const INDEX_TYPE Core_id) {
    const INDEX_TYPE Batch_num = PE_Param_in.read();
    const INDEX_TYPE Row_num = PE_Param_in.read();
    const INDEX_TYPE Iteration_num = PE_Param_in.read();
    const INDEX_TYPE Column_num = PE_Param_in.read();

    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;

    PE_Param_out.write(Batch_num);
    PE_Param_out.write(Row_num);
    PE_Param_out.write(Iteration_num);
    PE_Param_out.write(Column_num);

    Vector_Y_Param.write(Batch_num);
    Vector_Y_Param.write(Row_num);
    Vector_Y_Param.write(Iteration_num);

iter:
    for (INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
        CuperSpmvOnly_CoreComputeRoundStrip(Core_id,
                                            Batch_num,
                                            Column_num,
                                            PE_Param_in,
                                            Matrix_A_Stream,
                                            Vector_X_Stream_in,
                                            PE_Param_out,
                                            Vector_X_Stream_out,
                                            Vector_Y_Param,
                                            Matrix_Mult_Vector_Stream);
    }
}
#endif

#ifdef JACOBI_SPMV_COMPACT_PE
inline ap_uint<3> CuperSpmvOnly_CompactLane(ap_uint<18> tagged_row) {
#pragma HLS inline
    return tagged_row(16, 14);
}

inline ap_uint<18> CuperSpmvOnly_CompactCleanRow(ap_uint<18> tagged_row) {
#pragma HLS inline
    ap_uint<18> clean = 0;
    clean(13, 0) = tagged_row(13, 0);
    return clean;
}

inline void CuperSpmvOnly_AddCompactTagged(
    ap_uint<18> tagged_row,
    VALUE_TYPE val_new,
#ifdef PINGPONG
    ap_uint<32> local_part_Y_ping[8][URAM_DEPTH],
    ap_uint<32> local_part_Y_pong[8][URAM_DEPTH]
#else
    ap_uint<64> local_part_Y_ping[8][URAM_DEPTH]
#endif
) {
#pragma HLS inline
    if (tagged_row[17] != 0) {
        return;
    }

    const ap_uint<3> lane = CuperSpmvOnly_CompactLane(tagged_row);
    const ap_uint<18> clean_row = CuperSpmvOnly_CompactCleanRow(tagged_row);

    // compact-PE beat 内可能出现多个 slot 指向同一个 lane。这里用 switch
    // 显式选择 lane，避免把旧 accumulator 的“slot p == lane p”假设带进来。
    switch (lane) {
    case 0:
#ifdef PINGPONG
        if (clean_row[0] == 0) Adder_p(clean_row(17, 1), val_new, local_part_Y_ping[0]);
        else Adder_p(clean_row(17, 1), val_new, local_part_Y_pong[0]);
#else
        Adder(clean_row, val_new, local_part_Y_ping[0]);
#endif
        break;
    case 1:
#ifdef PINGPONG
        if (clean_row[0] == 0) Adder_p(clean_row(17, 1), val_new, local_part_Y_ping[1]);
        else Adder_p(clean_row(17, 1), val_new, local_part_Y_pong[1]);
#else
        Adder(clean_row, val_new, local_part_Y_ping[1]);
#endif
        break;
    case 2:
#ifdef PINGPONG
        if (clean_row[0] == 0) Adder_p(clean_row(17, 1), val_new, local_part_Y_ping[2]);
        else Adder_p(clean_row(17, 1), val_new, local_part_Y_pong[2]);
#else
        Adder(clean_row, val_new, local_part_Y_ping[2]);
#endif
        break;
    case 3:
#ifdef PINGPONG
        if (clean_row[0] == 0) Adder_p(clean_row(17, 1), val_new, local_part_Y_ping[3]);
        else Adder_p(clean_row(17, 1), val_new, local_part_Y_pong[3]);
#else
        Adder(clean_row, val_new, local_part_Y_ping[3]);
#endif
        break;
    case 4:
#ifdef PINGPONG
        if (clean_row[0] == 0) Adder_p(clean_row(17, 1), val_new, local_part_Y_ping[4]);
        else Adder_p(clean_row(17, 1), val_new, local_part_Y_pong[4]);
#else
        Adder(clean_row, val_new, local_part_Y_ping[4]);
#endif
        break;
    case 5:
#ifdef PINGPONG
        if (clean_row[0] == 0) Adder_p(clean_row(17, 1), val_new, local_part_Y_ping[5]);
        else Adder_p(clean_row(17, 1), val_new, local_part_Y_pong[5]);
#else
        Adder(clean_row, val_new, local_part_Y_ping[5]);
#endif
        break;
    case 6:
#ifdef PINGPONG
        if (clean_row[0] == 0) Adder_p(clean_row(17, 1), val_new, local_part_Y_ping[6]);
        else Adder_p(clean_row(17, 1), val_new, local_part_Y_pong[6]);
#else
        Adder(clean_row, val_new, local_part_Y_ping[6]);
#endif
        break;
    default:
#ifdef PINGPONG
        if (clean_row[0] == 0) Adder_p(clean_row(17, 1), val_new, local_part_Y_ping[7]);
        else Adder_p(clean_row(17, 1), val_new, local_part_Y_pong[7]);
#else
        Adder(clean_row, val_new, local_part_Y_ping[7]);
#endif
        break;
    }
}

inline void CuperSpmvOnly_CoreComputeRoundCompactPe(
    const INDEX_TYPE Core_id,
    const INDEX_TYPE Batch_num,
    const INDEX_TYPE Column_num,
    tapa::istream<INDEX_TYPE> &PE_Param_in,
    tapa::istream<ap_uint<512>> &Matrix_A_Stream,
    tapa::istream<float_v16> &Vector_X_Stream_in,
    tapa::ostream<INDEX_TYPE> &PE_Param_out,
    tapa::ostream<float_v16> &Vector_X_Stream_out,
    tapa::ostream<INDEX_TYPE> &Vector_Y_Param,
    tapa::ostream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream) {
#pragma HLS inline
    VALUE_TYPE local_X[X_BRAM_DEPTH][Slice_WIDTH];

#pragma HLS bind_storage variable=local_X latency=2
#pragma HLS array_partition variable=local_X complete dim=1
#pragma HLS array_partition variable=local_X cyclic factor=X_PARTITION_FACTOR dim=2

    INDEX_TYPE start_32 =
        CuperSpmvOnly_ReadStripBoundary(Core_id, PE_Param_in, PE_Param_out);
    Vector_Y_Param.write(start_32);

cuper_spmv_only_compact_core_main:
    for (INDEX_TYPE i = 0; i < Batch_num; ++i) {
#pragma HLS loop_tripcount min=1 max=200
        const INDEX_TYPE total_vector_packets = Cuper_NumFloatV16Packets(Column_num);
        const INDEX_TYPE start_idx = i * Slice_WIDTH_DIV_16;
        const INDEX_TYPE end_idx = std::min(start_idx + Slice_WIDTH_DIV_16,
                                            total_vector_packets);

    load_vector:
        for (INDEX_TYPE j = start_idx; j < end_idx;) {
#pragma HLS loop_tripcount min=1 max=512
#pragma HLS pipeline II=1
            if (!Vector_X_Stream_in.empty() && !Vector_X_Stream_out.full()) {
                float_v16 x;
                Vector_X_Stream_in.try_read(x);
                Vector_X_Stream_out.try_write(x);

                for (INDEX_TYPE k = 0; k < 16; ++k) {
                    for (INDEX_TYPE l = 0; l < X_BRAM_DEPTH; ++l) {
                        local_X[l][((j - start_idx) << 4) + k] = x[k];
                    }
                }
                ++j;
            }
        }

        const INDEX_TYPE end_32 =
            CuperSpmvOnly_ReadStripBoundary(Core_id, PE_Param_in, PE_Param_out);
        Vector_Y_Param.write(end_32);

    decode_matrix:
        for (INDEX_TYPE j = start_32; j < end_32;) {
#pragma HLS loop_tripcount min=1 max=200
#pragma HLS pipeline II=1
            if (!Matrix_A_Stream.empty()) {
                ap_uint<512> spelement;
                Matrix_A_Stream.try_read(spelement);
                Matrix_Mult_X matmultx;

                for (INDEX_TYPE p = 0; p < 8; ++p) {
                    matmultx.row[p] = 0x3FFFF;
                    matmultx.val[p] = 0.0;
                }

#ifdef FLEX_REUSE
                ap_uint<14> col_old = 0x3FFF;
                VALUE_TYPE val_old = 0.0;
#endif
                for (INDEX_TYPE p = 0; p < 8; ++p) {
                    ap_uint<64> a = spelement(63 + p * 64, p * 64);
                    ap_uint<14> a_col = a(63, 50);
                    ap_uint<18> a_row = a(49, 32);
                    ap_uint<32> a_val = a(31, 0);

                    matmultx.row[p] = a_row;
                    if (a_row[17] == 0) {
                        const ap_uint<3> lane = CuperSpmvOnly_CompactLane(a_row);
#ifdef FLEX_REUSE
                        VALUE_TYPE val;
                        if ((col_old & a_col) == 0x3FFF) {
                            val = val_old;
                        } else {
                            val = tapa::bit_cast<VALUE_TYPE>(a_val);
                        }
#else
                        VALUE_TYPE val = tapa::bit_cast<VALUE_TYPE>(a_val);
#endif
                        matmultx.val[p] =
                            val * local_X[lane / (8 / X_BRAM_DEPTH)][a_col];
#ifdef FLEX_REUSE
                        col_old = a_col;
                        val_old = val;
#endif
                    }
                }
                Matrix_Mult_Vector_Stream.write(matmultx);
                ++j;
            }
        }
        start_32 = end_32;
    }
}

void CuperSpmvOnly_CoreCompactPe(
    tapa::istream<INDEX_TYPE> &PE_Param_in,
    tapa::istream<ap_uint<512>> &Matrix_A_Stream,
    tapa::istream<float_v16> &Vector_X_Stream_in,
    tapa::ostream<INDEX_TYPE> &PE_Param_out,
    tapa::ostream<float_v16> &Vector_X_Stream_out,
    tapa::ostream<INDEX_TYPE> &Vector_Y_Param,
    tapa::ostream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream,
    const INDEX_TYPE Core_id) {
    const INDEX_TYPE Batch_num = PE_Param_in.read();
    const INDEX_TYPE Row_num = PE_Param_in.read();
    const INDEX_TYPE Iteration_num = PE_Param_in.read();
    const INDEX_TYPE Column_num = PE_Param_in.read();

    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;

    PE_Param_out.write(Batch_num);
    PE_Param_out.write(Row_num);
    PE_Param_out.write(Iteration_num);
    PE_Param_out.write(Column_num);

    Vector_Y_Param.write(Batch_num);
    Vector_Y_Param.write(Row_num);
    Vector_Y_Param.write(Iteration_num);

iter:
    for (INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
        CuperSpmvOnly_CoreComputeRoundCompactPe(Core_id,
                                                Batch_num,
                                                Column_num,
                                                PE_Param_in,
                                                Matrix_A_Stream,
                                                Vector_X_Stream_in,
                                                PE_Param_out,
                                                Vector_X_Stream_out,
                                                Vector_Y_Param,
                                                Matrix_Mult_Vector_Stream);
    }
}

void CuperSpmvOnly_AccumulatorCompactPe(
    tapa::istream<INDEX_TYPE> &Vector_Y_Param,
    tapa::istream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream,
    tapa::ostream<float_v2> &Vector_Y_Stream) {
    const INDEX_TYPE Batch_num = Vector_Y_Param.read();
    const INDEX_TYPE Row_num = Vector_Y_Param.read();
    const INDEX_TYPE Iteration_num = Vector_Y_Param.read();
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;

#ifdef PINGPONG
    ap_uint<32> local_part_Y_ping[8][URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_ping type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_ping dim=1

    ap_uint<32> local_part_Y_pong[8][URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_pong type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_pong dim=1
#else
    ap_uint<64> local_part_Y_ping[8][URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_ping type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_ping dim=1
#endif

iter:
    for (INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
        const INDEX_TYPE num_v_init = Cuper_NumAccumulatorInitGroups(Row_num);
        const INDEX_TYPE num_v_out = Cuper_NumAccumulatorOutputs(Row_num);

    init:
        for (int i = 0; i < num_v_init; ++i) {
#pragma HLS loop_tripcount min=1 max=800
#pragma HLS pipeline II=1
            for (int p = 0; p < 8; ++p) {
                local_part_Y_ping[p][i] = 0;
#ifdef PINGPONG
                local_part_Y_pong[p][i] = 0;
#endif
            }
        }

        INDEX_TYPE start_32 = Vector_Y_Param.read();
    batches:
        for (int i = 0; i < Batch_num; ++i) {
#pragma HLS loop_tripcount min=1 max=200
            const INDEX_TYPE end_32 = Vector_Y_Param.read();
        accumulate:
            for (INDEX_TYPE j = start_32; j < end_32; ++j) {
#pragma HLS loop_tripcount min=1 max=200
                Matrix_Mult_X matmultx = Matrix_Mult_Vector_Stream.read();
            slots:
                for (int p = 0; p < 8; ++p) {
#pragma HLS loop_tripcount min=8 max=8
                    CuperSpmvOnly_AddCompactTagged(
                        matmultx.row[p],
                        matmultx.val[p],
#ifdef PINGPONG
                        local_part_Y_ping,
                        local_part_Y_pong
#else
                        local_part_Y_ping
#endif
                    );
                }
            }
            start_32 = end_32;
        }

    writer:
        for (INDEX_TYPE i = 0, c_idx = 0; i < num_v_out; ++i) {
#pragma HLS loop_tripcount min=1 max=1800
#pragma HLS pipeline II=1
            float_v2 out_v;
#ifdef PINGPONG
            ap_uint<32> u_32_0 = local_part_Y_ping[c_idx][i >> 3];
            ap_uint<32> u_32_1 = local_part_Y_pong[c_idx][i >> 3];
            out_v[0] = tapa::bit_cast<VALUE_TYPE>(u_32_0);
            out_v[1] = tapa::bit_cast<VALUE_TYPE>(u_32_1);
#else
            ap_uint<64> u_64 = local_part_Y_ping[c_idx][i >> 3];
            for (INDEX_TYPE d = 0; d < 2; ++d) {
                ap_uint<32> u_32_d = u_64(31 + 32 * d, 32 * d);
                out_v[d] = tapa::bit_cast<VALUE_TYPE>(u_32_d);
            }
#endif
            Vector_Y_Stream.write(out_v);
            ++c_idx;
            if (c_idx == 8) {
                c_idx = 0;
            }
        }
    }
}
#endif

#ifdef JACOBI_SPMV_LANE_STATIC_REAL
#ifndef PINGPONG
#error "CuperSpmvOnly tagged accumulator currently requires PINGPONG."
#endif

struct CuperSpmvOnly_TaggedFloatV2 {
    INDEX_TYPE packet_idx;
    INDEX_TYPE pair_lane;
    float_v2 value;
};

struct CuperSpmvOnly_TaggedScalar {
    ap_uint<1> done;
    INDEX_TYPE packet_idx;
    INDEX_TYPE pair_lane;
    INDEX_TYPE scalar_lane;
    VALUE_TYPE value;
};

constexpr int CUPER_SPMV_ROW_CACHE_SIZE = 4;

inline INDEX_TYPE CuperSpmvOnly_TaggedPacketIndexFromSlot(
    const INDEX_TYPE source_core,
    const INDEX_TYPE local_row_group,
    const INDEX_TYPE scalar_lane) {
#pragma HLS inline
    // 和 CuperHostMapRowToPe / CuperSpmvOnly_TaggedPacketIndex 是同一套映射。
    // source_core 是 Router 从第几路 Core stream 读到的数据，不需要 Core 包显式带 id。
    const INDEX_TYPE group_size = HBM_CHANNEL_NUM_DIV_8;
    const INDEX_TYPE acc_offset = source_core % group_size;
    return local_row_group * HBM_CHANNEL_NUM + scalar_lane * group_size + acc_offset;
}

inline INDEX_TYPE CuperSpmvOnly_OwnerFromPacket(const INDEX_TYPE packet_idx) {
#pragma HLS inline
    // 这一版把 partial sum 所有权绑定到最终输出 packet，而不是来源 Core。
    // HBM_CHANNEL_NUM 为 16/24/32，均可用低位取模实现均匀分片。
    return packet_idx % HBM_CHANNEL_NUM;
}

inline VALUE_TYPE CuperSpmvOnly_ReadCachedSum(
    const ap_uint<17> addr,
    ap_uint<32> local_part_Y[URAM_DEPTH]) {
#pragma HLS inline
    return tapa::bit_cast<VALUE_TYPE>(local_part_Y[addr]);
}

inline void CuperSpmvOnly_WriteCachedSum(
    const ap_uint<17> addr,
    const VALUE_TYPE value,
    ap_uint<32> local_part_Y[URAM_DEPTH]) {
#pragma HLS inline
    local_part_Y[addr] = tapa::bit_cast<ap_uint<32>>(value);
}

inline void CuperSpmvOnly_FlushRowCacheEntry(
    bool &valid,
    ap_uint<17> &addr,
    VALUE_TYPE &value,
    ap_uint<32> local_part_Y[URAM_DEPTH]) {
#pragma HLS inline
    if (valid) {
        CuperSpmvOnly_WriteCachedSum(addr, value, local_part_Y);
        valid = false;
        value = 0.0f;
    }
}

inline void CuperSpmvOnly_PushRowCacheUpdate(
    const ap_uint<17> addr,
    const VALUE_TYPE value,
    bool valid[CUPER_SPMV_ROW_CACHE_SIZE],
    ap_uint<17> cached_addr[CUPER_SPMV_ROW_CACHE_SIZE],
    VALUE_TYPE cached_value[CUPER_SPMV_ROW_CACHE_SIZE],
    INDEX_TYPE &victim,
    ap_uint<32> local_part_Y[URAM_DEPTH]) {
#pragma HLS inline
    // 小型 write-back row cache。
    //
    // 命中时直接在寄存器里按原输入顺序继续 FP32 累加；缺失时先从 URAM 读出
    // 当前部分和再加新值；cache 满时只写回一个 victim。这样不改变每个 row
    // 的加法顺序，但能把短时间内反复出现的 row 从 URAM RAW 路径上拿出来。
    bool hit = false;
    INDEX_TYPE hit_idx = 0;
find_hit:
    for (INDEX_TYPE entry = 0; entry < CUPER_SPMV_ROW_CACHE_SIZE; ++entry) {
#pragma HLS unroll
        if (valid[entry] && cached_addr[entry] == addr) {
            hit = true;
            hit_idx = entry;
        }
    }

    if (hit) {
        cached_value[hit_idx] += value;
    } else {
        bool found_empty = false;
        INDEX_TYPE target = 0;
    find_empty:
        for (INDEX_TYPE entry = 0; entry < CUPER_SPMV_ROW_CACHE_SIZE; ++entry) {
#pragma HLS unroll
            if (!valid[entry] && !found_empty) {
                found_empty = true;
                target = entry;
            }
        }

        if (!found_empty) {
            target = victim;
            CuperSpmvOnly_FlushRowCacheEntry(valid[target],
                                             cached_addr[target],
                                             cached_value[target],
                                             local_part_Y);
            ++victim;
            if (victim == CUPER_SPMV_ROW_CACHE_SIZE) {
                victim = 0;
            }
        }

        cached_addr[target] = addr;
        cached_value[target] = CuperSpmvOnly_ReadCachedSum(addr, local_part_Y) + value;
        valid[target] = true;
    }
}

inline INDEX_TYPE CuperSpmvOnly_TaggedPacketIndex(const INDEX_TYPE channel_id,
                                                  const INDEX_TYPE local_output_idx) {
#pragma HLS inline
    // local_output_idx 的低 3 bit 是 channel 内 lane，剩余高位是 accumulator
    // 局部 row group。由 channel_id 反推出 acc_offset 后，得到最终 float_v16
    // packet 地址。pair lane 由 channel_id / group_size 单独随数据带出。
    const INDEX_TYPE group_size = HBM_CHANNEL_NUM_DIV_8;
    const INDEX_TYPE acc_offset = channel_id % group_size;
    const INDEX_TYPE lane = local_output_idx & 0x7;
    const INDEX_TYPE row_group = local_output_idx >> 3;
    return row_group * HBM_CHANNEL_NUM + lane * group_size + acc_offset;
}

void CuperSpmvOnly_AccumulatorTagged(
    tapa::istream<INDEX_TYPE> &Vector_Y_Param,
    tapa::istream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream,
    tapa::ostream<CuperSpmvOnly_TaggedFloatV2> &Vector_Y_Tagged_Stream,
    const INDEX_TYPE Channel_id) {
    // 解绑版 accumulator。
    //
    // 它仍只保留一份 partial-sum URAM，避免 full-bank 复制把 URAM 用量翻倍。
    // 输入端仍保持 lane-static real 的安全打包顺序；本 task 在 URAM 前加
    // 每 lane 的小型 write-back row cache，命中时在寄存器里继续累加，
    // 逐出/收尾时再写回 URAM。输出端显式携带最终 Y 地址 tag，后级先到先
    // scalar scatter。
    const INDEX_TYPE Batch_num = Vector_Y_Param.read();
    const INDEX_TYPE Row_num = Vector_Y_Param.read();
    const INDEX_TYPE Iteration_num = Vector_Y_Param.read();
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
    const INDEX_TYPE num_out_packets = Cuper_NumFloatV16Packets(Row_num);

    ap_uint<32> local_part_Y_ping[8][URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_ping type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_ping dim=1

    ap_uint<32> local_part_Y_pong[8][URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_pong type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_pong dim=1

iter:
    for (INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
        const INDEX_TYPE num_v_init = Cuper_NumAccumulatorInitGroups(Row_num);
        const INDEX_TYPE num_v_out = Cuper_NumAccumulatorOutputs(Row_num);

        bool cache_ping_valid[8][CUPER_SPMV_ROW_CACHE_SIZE];
#pragma HLS array_partition complete variable=cache_ping_valid dim=0
        bool cache_pong_valid[8][CUPER_SPMV_ROW_CACHE_SIZE];
#pragma HLS array_partition complete variable=cache_pong_valid dim=0
        ap_uint<17> cache_ping_addr[8][CUPER_SPMV_ROW_CACHE_SIZE];
#pragma HLS array_partition complete variable=cache_ping_addr dim=0
        ap_uint<17> cache_pong_addr[8][CUPER_SPMV_ROW_CACHE_SIZE];
#pragma HLS array_partition complete variable=cache_pong_addr dim=0
        VALUE_TYPE cache_ping_value[8][CUPER_SPMV_ROW_CACHE_SIZE];
#pragma HLS array_partition complete variable=cache_ping_value dim=0
        VALUE_TYPE cache_pong_value[8][CUPER_SPMV_ROW_CACHE_SIZE];
#pragma HLS array_partition complete variable=cache_pong_value dim=0
        INDEX_TYPE cache_ping_victim[8];
#pragma HLS array_partition complete variable=cache_ping_victim dim=1
        INDEX_TYPE cache_pong_victim[8];
#pragma HLS array_partition complete variable=cache_pong_victim dim=1

    init:
        for (INDEX_TYPE i = 0; i < num_v_init; ++i) {
#pragma HLS loop_tripcount min=1 max=800
#pragma HLS pipeline II=1
            for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                local_part_Y_ping[lane][i] = 0;
                local_part_Y_pong[lane][i] = 0;
            }
        }

    init_pending:
        for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
            cache_ping_victim[lane] = 0;
            cache_pong_victim[lane] = 0;
            for (INDEX_TYPE entry = 0; entry < CUPER_SPMV_ROW_CACHE_SIZE; ++entry) {
#pragma HLS unroll
                cache_ping_valid[lane][entry] = false;
                cache_pong_valid[lane][entry] = false;
                cache_ping_addr[lane][entry] = 0;
                cache_pong_addr[lane][entry] = 0;
                cache_ping_value[lane][entry] = 0.0f;
                cache_pong_value[lane][entry] = 0.0f;
            }
        }

        INDEX_TYPE start_32 = Vector_Y_Param.read();

    batches:
        for (INDEX_TYPE batch = 0; batch < Batch_num; ++batch) {
#pragma HLS loop_tripcount min=1 max=200
            const INDEX_TYPE end_32 = Vector_Y_Param.read();

        accumulate:
            for (INDEX_TYPE j = start_32; j < end_32;) {
#pragma HLS loop_tripcount min=1 max=200
#pragma HLS pipeline II=1
                if (!Matrix_Mult_Vector_Stream.empty()) {
                    Matrix_Mult_X matmultx;
                    Matrix_Mult_Vector_Stream.try_read(matmultx);

                slots:
                    for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
                        const ap_uint<18> a_row = matmultx.row[lane];
                        if (a_row[17] == 0 && a_row[0] == 0) {
                            CuperSpmvOnly_PushRowCacheUpdate(
                                a_row(17, 1),
                                matmultx.val[lane],
                                cache_ping_valid[lane],
                                cache_ping_addr[lane],
                                cache_ping_value[lane],
                                cache_ping_victim[lane],
                                local_part_Y_ping[lane]);
                        }
                        if (a_row[17] == 0 && a_row[0] == 1) {
                            CuperSpmvOnly_PushRowCacheUpdate(
                                a_row(17, 1),
                                matmultx.val[lane],
                                cache_pong_valid[lane],
                                cache_pong_addr[lane],
                                cache_pong_value[lane],
                                cache_pong_victim[lane],
                                local_part_Y_pong[lane]);
                        }
                    }

                    ++j;
                }
            }
            start_32 = end_32;
        }

    flush_pending:
        for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS unroll
            for (INDEX_TYPE entry = 0; entry < CUPER_SPMV_ROW_CACHE_SIZE; ++entry) {
#pragma HLS unroll
                CuperSpmvOnly_FlushRowCacheEntry(cache_ping_valid[lane][entry],
                                                 cache_ping_addr[lane][entry],
                                                 cache_ping_value[lane][entry],
                                                 local_part_Y_ping[lane]);
                CuperSpmvOnly_FlushRowCacheEntry(cache_pong_valid[lane][entry],
                                                 cache_pong_addr[lane][entry],
                                                 cache_pong_value[lane][entry],
                                                 local_part_Y_pong[lane]);
            }
        }

    writer:
        for (INDEX_TYPE i = 0, lane = 0; i < num_v_out; ++i) {
#pragma HLS loop_tripcount min=1 max=1800
#pragma HLS pipeline II=1
            const INDEX_TYPE addr = i >> 3;
            const INDEX_TYPE packet_idx =
                CuperSpmvOnly_TaggedPacketIndex(Channel_id, i);
            if (packet_idx < num_out_packets) {
                CuperSpmvOnly_TaggedFloatV2 tagged;
                tagged.packet_idx = packet_idx;
                tagged.pair_lane = Channel_id / HBM_CHANNEL_NUM_DIV_8;

                const ap_uint<32> ping_u32 = local_part_Y_ping[lane][addr];
                const ap_uint<32> pong_u32 = local_part_Y_pong[lane][addr];
                tagged.value[0] = tapa::bit_cast<VALUE_TYPE>(ping_u32);
                tagged.value[1] = tapa::bit_cast<VALUE_TYPE>(pong_u32);

                Vector_Y_Tagged_Stream.write(tagged);
            }

            ++lane;
            if (lane == 8) {
                lane = 0;
            }
        }
    }
}

#ifdef JACOBI_SPMV_OOO_ACCUMULATE
void CuperSpmvOnly_RowRouterOoo(
    tapa::istreams<INDEX_TYPE, HBM_CHANNEL_NUM> &Vector_Y_Param,
    tapa::istreams<Matrix_Mult_X, HBM_CHANNEL_NUM> &Matrix_Mult_Vector_Stream,
    tapa::ostreams<CuperSpmvOnly_TaggedScalar, HBM_CHANNEL_NUM> &Owner_Scalar_Stream) {
    // Core 后乱序路由层。
    //
    // 这里不修改 Matrix_Mult_X，不让 Core 包显式携带 core_id。Router 从第 source
    // 路 Matrix_Mult_Vector_Stream 读数据时，source 就是该 beat 的 Core/HBM 来源。
    // 然后根据 source + slot lane + Cuper 内部 row 编码恢复最终 Y 坐标，并按
    // packet_idx 的 owner 分片发给后级 accumulator。
    INDEX_TYPE batch_num_by_source[HBM_CHANNEL_NUM];
#pragma HLS array_partition complete variable=batch_num_by_source dim=1
    INDEX_TYPE row_num_by_source[HBM_CHANNEL_NUM];
#pragma HLS array_partition complete variable=row_num_by_source dim=1
    INDEX_TYPE iteration_num_by_source[HBM_CHANNEL_NUM];
#pragma HLS array_partition complete variable=iteration_num_by_source dim=1
    INDEX_TYPE start_32[HBM_CHANNEL_NUM];
#pragma HLS array_partition complete variable=start_32 dim=1
    INDEX_TYPE remaining[HBM_CHANNEL_NUM];
#pragma HLS array_partition complete variable=remaining dim=1

read_header:
    for (INDEX_TYPE source = 0; source < HBM_CHANNEL_NUM; ++source) {
#pragma HLS unroll
        batch_num_by_source[source] = Vector_Y_Param[source].read();
        row_num_by_source[source] = Vector_Y_Param[source].read();
        iteration_num_by_source[source] = Vector_Y_Param[source].read();
    }

    const INDEX_TYPE Batch_num = batch_num_by_source[0];
    const INDEX_TYPE Iteration_num = iteration_num_by_source[0];
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;

iter:
    for (INDEX_TYPE iter_idx = 0; iter_idx < Iteration_time; ++iter_idx) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
    read_start:
        for (INDEX_TYPE source = 0; source < HBM_CHANNEL_NUM; ++source) {
#pragma HLS unroll
            start_32[source] = Vector_Y_Param[source].read();
        }

    batches:
        for (INDEX_TYPE batch = 0; batch < Batch_num; ++batch) {
#pragma HLS loop_tripcount min=1 max=200
            INDEX_TYPE batch_total = 0;

        read_end:
            for (INDEX_TYPE source = 0; source < HBM_CHANNEL_NUM; ++source) {
#pragma HLS unroll
                const INDEX_TYPE end_32 = Vector_Y_Param[source].read();
                remaining[source] = end_32 - start_32[source];
                start_32[source] = end_32;
                batch_total += remaining[source];
            }

            INDEX_TYPE source_cursor = 0;
        route_batch:
            for (INDEX_TYPE consumed = 0; consumed < batch_total;) {
#pragma HLS loop_tripcount min=1 max=4096
#pragma HLS pipeline II=1
                if (remaining[source_cursor] != 0 &&
                    !Matrix_Mult_Vector_Stream[source_cursor].empty()) {
                    Matrix_Mult_X matmultx;
                    Matrix_Mult_Vector_Stream[source_cursor].try_read(matmultx);

                route_slots:
                    for (INDEX_TYPE lane = 0; lane < 8; ++lane) {
#pragma HLS loop_tripcount min=8 max=8
                        const ap_uint<18> a_row = matmultx.row[lane];
                        if (a_row[17] == 0) {
                            CuperSpmvOnly_TaggedScalar tagged;
                            tagged.done = 0;
                            tagged.packet_idx =
                                CuperSpmvOnly_TaggedPacketIndexFromSlot(
                                    source_cursor,
                                    a_row(17, 1),
                                    lane);
                            tagged.pair_lane =
                                source_cursor / HBM_CHANNEL_NUM_DIV_8;
                            tagged.scalar_lane = a_row[0];
                            tagged.value = matmultx.val[lane];
                            Owner_Scalar_Stream[
                                CuperSpmvOnly_OwnerFromPacket(
                                    tagged.packet_idx)].write(tagged);
                        }
                    }

                    --remaining[source_cursor];
                    ++consumed;
                }

                ++source_cursor;
                if (source_cursor == HBM_CHANNEL_NUM) {
                    source_cursor = 0;
                }
            }
        }

    send_done:
        for (INDEX_TYPE owner = 0; owner < HBM_CHANNEL_NUM; ++owner) {
#pragma HLS unroll
            CuperSpmvOnly_TaggedScalar done;
            done.done = 1;
            done.packet_idx = 0;
            done.pair_lane = 0;
            done.scalar_lane = 0;
            done.value = 0.0f;
            Owner_Scalar_Stream[owner].write(done);
        }
    }
}

void CuperSpmvOnly_OwnerAccumulatorOoo(
    const INDEX_TYPE Iteration_num,
    const INDEX_TYPE Row_num,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Scalar_Stream,
    tapa::ostream<CuperSpmvOnly_TaggedFloatV2> &Vector_Y_Tagged_Stream,
    const INDEX_TYPE Owner_id) {
    // 真正乱序归属的 accumulator bank。
    //
    // 旧 accumulator 的 partial sum 归来源 Core/HBM channel 所有；这里改成按最终
    // packet_idx 分片，Owner_id == packet_idx % HBM_CHANNEL_NUM。这样任意 Core
    // 的贡献都可以被路由到同一个 row-owner bank，再由该 bank 独立输出 tagged Y。
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
    const INDEX_TYPE num_out_packets = Cuper_NumFloatV16Packets(Row_num);
    const INDEX_TYPE num_owner_groups =
        (num_out_packets + HBM_CHANNEL_NUM - 1) / HBM_CHANNEL_NUM;

    ap_uint<32> local_part_Y[16][URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y dim=1

iter:
    for (INDEX_TYPE iter_idx = 0; iter_idx < Iteration_time; ++iter_idx) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
        bool cache_valid[16][CUPER_SPMV_ROW_CACHE_SIZE];
#pragma HLS array_partition complete variable=cache_valid dim=0
        ap_uint<17> cache_addr[16][CUPER_SPMV_ROW_CACHE_SIZE];
#pragma HLS array_partition complete variable=cache_addr dim=0
        VALUE_TYPE cache_value[16][CUPER_SPMV_ROW_CACHE_SIZE];
#pragma HLS array_partition complete variable=cache_value dim=0
        INDEX_TYPE cache_victim[16];
#pragma HLS array_partition complete variable=cache_victim dim=1

    init:
        for (INDEX_TYPE i = 0; i < num_owner_groups; ++i) {
#pragma HLS loop_tripcount min=1 max=8000
#pragma HLS pipeline II=1
            for (INDEX_TYPE scalar_slot = 0; scalar_slot < 16; ++scalar_slot) {
#pragma HLS unroll
                local_part_Y[scalar_slot][i] = 0;
            }
        }

    init_cache:
        for (INDEX_TYPE scalar_slot = 0; scalar_slot < 16; ++scalar_slot) {
#pragma HLS unroll
            cache_victim[scalar_slot] = 0;
            for (INDEX_TYPE entry = 0; entry < CUPER_SPMV_ROW_CACHE_SIZE; ++entry) {
#pragma HLS unroll
                cache_valid[scalar_slot][entry] = false;
                cache_addr[scalar_slot][entry] = 0;
                cache_value[scalar_slot][entry] = 0.0f;
            }
        }

    consume:
        for (bool done = false; !done;) {
#pragma HLS loop_tripcount min=1 max=4000000
#pragma HLS pipeline II=1
            CuperSpmvOnly_TaggedScalar tagged = Owner_Scalar_Stream.read();
            if (tagged.done != 0) {
                done = true;
            } else {
                const INDEX_TYPE scalar_slot =
                    (tagged.pair_lane << 1) + tagged.scalar_lane;
                const ap_uint<17> addr = tagged.packet_idx / HBM_CHANNEL_NUM;
                CuperSpmvOnly_PushRowCacheUpdate(addr,
                                                 tagged.value,
                                                 cache_valid[scalar_slot],
                                                 cache_addr[scalar_slot],
                                                 cache_value[scalar_slot],
                                                 cache_victim[scalar_slot],
                                                 local_part_Y[scalar_slot]);
            }
        }

    flush_cache:
        for (INDEX_TYPE scalar_slot = 0; scalar_slot < 16; ++scalar_slot) {
#pragma HLS unroll
            for (INDEX_TYPE entry = 0; entry < CUPER_SPMV_ROW_CACHE_SIZE; ++entry) {
#pragma HLS unroll
                CuperSpmvOnly_FlushRowCacheEntry(cache_valid[scalar_slot][entry],
                                                 cache_addr[scalar_slot][entry],
                                                 cache_value[scalar_slot][entry],
                                                 local_part_Y[scalar_slot]);
            }
        }

    writer:
        for (INDEX_TYPE owner_group = 0; owner_group < num_owner_groups; ++owner_group) {
#pragma HLS loop_tripcount min=1 max=8000
            const INDEX_TYPE packet_idx =
                owner_group * HBM_CHANNEL_NUM + Owner_id;
            if (packet_idx < num_out_packets) {
            write_pairs:
                for (INDEX_TYPE pair_lane = 0; pair_lane < 8; ++pair_lane) {
#pragma HLS loop_tripcount min=8 max=8
#pragma HLS pipeline II=1
                    CuperSpmvOnly_TaggedFloatV2 tagged;
                    tagged.packet_idx = packet_idx;
                    tagged.pair_lane = pair_lane;

                    const INDEX_TYPE scalar_base = pair_lane << 1;
                    tagged.value[0] = tapa::bit_cast<VALUE_TYPE>(
                        local_part_Y[scalar_base][owner_group]);
                    tagged.value[1] = tapa::bit_cast<VALUE_TYPE>(
                        local_part_Y[scalar_base + 1][owner_group]);
                    Vector_Y_Tagged_Stream.write(tagged);
                }
            }
        }
    }
}

inline void CuperSpmvOnly_WriteSplitLaneOoo(
    const INDEX_TYPE Source_id,
    const INDEX_TYPE lane,
    const Matrix_Mult_X &matmultx,
    tapa::ostream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream) {
#pragma HLS inline
    const ap_uint<18> a_row = matmultx.row[lane];
    if (a_row[17] == 0) {
        CuperSpmvOnly_TaggedScalar tagged;
        tagged.done = 0;
        tagged.packet_idx =
            CuperSpmvOnly_TaggedPacketIndexFromSlot(Source_id,
                                                    a_row(17, 1),
                                                    lane);
        tagged.pair_lane = Source_id / HBM_CHANNEL_NUM_DIV_8;
        tagged.scalar_lane = a_row[0];
        tagged.value = matmultx.val[lane];
        Owner_Lane_Stream.write(tagged);
    }
}

inline void CuperSpmvOnly_WriteSplitDoneOoo(
    const INDEX_TYPE Source_id,
    const INDEX_TYPE lane,
    tapa::ostream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream) {
#pragma HLS inline
    CuperSpmvOnly_TaggedScalar done;
    done.done = 1;
    done.packet_idx =
        CuperSpmvOnly_TaggedPacketIndexFromSlot(Source_id, 0, lane);
    done.pair_lane = Source_id / HBM_CHANNEL_NUM_DIV_8;
    done.scalar_lane = 0;
    done.value = 0.0f;
    Owner_Lane_Stream.write(done);
}

void CuperSpmvOnly_SourceLaneSplitterOoo(
    tapa::istream<INDEX_TYPE> &Vector_Y_Param,
    tapa::istream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream,
    tapa::ostream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_0,
    tapa::ostream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_1,
    tapa::ostream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_2,
    tapa::ostream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_3,
    tapa::ostream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_4,
    tapa::ostream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_5,
    tapa::ostream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_6,
    tapa::ostream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_7,
    const INDEX_TYPE Source_id) {
    // 静态 8-lane transpose。
    //
    // 对 16 路而言，一个 Core beat 的 8 个 slot 会发到 8 个固定 owner：
    // 偶数 source -> owner 0/2/.../14，奇数 source -> owner 1/3/.../15。
    // 24/32 路同理按 source offset 分到每个 lane 的 group_size 个 owner。
    // 这样保留“按最终 packet owner 乱序累加”的设计，同时把运行时动态路由
    // 变成编译期固定连线，避免中心 Router 的 FIFO 写依赖。
    const INDEX_TYPE Batch_num = Vector_Y_Param.read();
    Vector_Y_Param.read();  // Row_num 由 owner accumulator 直接从 top 参数获得。
    const INDEX_TYPE Iteration_num = Vector_Y_Param.read();
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;

iter:
    for (INDEX_TYPE iter_idx = 0; iter_idx < Iteration_time; ++iter_idx) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
        INDEX_TYPE start_32 = Vector_Y_Param.read();

    batches:
        for (INDEX_TYPE batch = 0; batch < Batch_num; ++batch) {
#pragma HLS loop_tripcount min=1 max=200
            const INDEX_TYPE end_32 = Vector_Y_Param.read();

        split_packets:
            for (INDEX_TYPE j = start_32; j < end_32; ++j) {
#pragma HLS loop_tripcount min=1 max=200
#pragma HLS pipeline II=1
                Matrix_Mult_X matmultx = Matrix_Mult_Vector_Stream.read();
                CuperSpmvOnly_WriteSplitLaneOoo(Source_id, 0, matmultx,
                                                Owner_Lane_Stream_0);
                CuperSpmvOnly_WriteSplitLaneOoo(Source_id, 1, matmultx,
                                                Owner_Lane_Stream_1);
                CuperSpmvOnly_WriteSplitLaneOoo(Source_id, 2, matmultx,
                                                Owner_Lane_Stream_2);
                CuperSpmvOnly_WriteSplitLaneOoo(Source_id, 3, matmultx,
                                                Owner_Lane_Stream_3);
                CuperSpmvOnly_WriteSplitLaneOoo(Source_id, 4, matmultx,
                                                Owner_Lane_Stream_4);
                CuperSpmvOnly_WriteSplitLaneOoo(Source_id, 5, matmultx,
                                                Owner_Lane_Stream_5);
                CuperSpmvOnly_WriteSplitLaneOoo(Source_id, 6, matmultx,
                                                Owner_Lane_Stream_6);
                CuperSpmvOnly_WriteSplitLaneOoo(Source_id, 7, matmultx,
                                                Owner_Lane_Stream_7);
            }
            start_32 = end_32;
        }

        CuperSpmvOnly_WriteSplitDoneOoo(Source_id, 0, Owner_Lane_Stream_0);
        CuperSpmvOnly_WriteSplitDoneOoo(Source_id, 1, Owner_Lane_Stream_1);
        CuperSpmvOnly_WriteSplitDoneOoo(Source_id, 2, Owner_Lane_Stream_2);
        CuperSpmvOnly_WriteSplitDoneOoo(Source_id, 3, Owner_Lane_Stream_3);
        CuperSpmvOnly_WriteSplitDoneOoo(Source_id, 4, Owner_Lane_Stream_4);
        CuperSpmvOnly_WriteSplitDoneOoo(Source_id, 5, Owner_Lane_Stream_5);
        CuperSpmvOnly_WriteSplitDoneOoo(Source_id, 6, Owner_Lane_Stream_6);
        CuperSpmvOnly_WriteSplitDoneOoo(Source_id, 7, Owner_Lane_Stream_7);
    }
}

void CuperSpmvOnly_RtlOwnerLanePassThrough(
    const INDEX_TYPE Iteration_num,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Rtl_In_Stream,
    tapa::ostream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Rtl_Out_Stream) {
    // TAPA custom RTL 接入烟测点。
    //
    // 这一层当前只做 TaggedScalar 原样转发，目的是先验证 TAPA 的
    // non_synthesizable/custom-rtl task 边界、stream 握手和 ap_ctrl_hs 生命周期。
    // 数值累加仍交给后面的 HLS OwnerLaneAccumulatorOoo，避免在 Verilog FP32
    // 累加器完成前破坏 SpMV 正确性。后续可以把这一层替换成真正 RTL accumulator。
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;

iter:
    for (INDEX_TYPE iter_idx = 0; iter_idx < Iteration_time; ++iter_idx) {
#pragma HLS loop_tripcount min=1 max=16
    pass_one_round:
        for (bool done = false; !done;) {
#pragma HLS pipeline II=1
            CuperSpmvOnly_TaggedScalar tagged = Owner_Lane_Rtl_In_Stream.read();
            Owner_Lane_Rtl_Out_Stream.write(tagged);
            done = (tagged.done != 0);
        }
    }
}

void CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo(
    const INDEX_TYPE Iteration_num,
    const INDEX_TYPE Row_num,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream,
    tapa::ostream<CuperSpmvOnly_TaggedFloatV2> &Vector_Y_Tagged_Stream) {
    // TAPA custom RTL accumulator 的 C++ 占位实现。
    //
    // 软件仿真和 TAPA HLS synth 阶段使用这份等价逻辑；硬件 pack 前会把同名
    // wrapper 替换成 verilog/tapa/CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo.v。
    // 这里保留真实 FP32 加法，目的是让 TAPA/Vitis 生成可复用的 FP32 adder IP。
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
    const INDEX_TYPE num_out_packets = Cuper_NumFloatV16Packets(Row_num);
    const INDEX_TYPE num_owner_groups =
        (num_out_packets + HBM_CHANNEL_NUM - 1) / HBM_CHANNEL_NUM;

    ap_uint<32> local_part_Y_ping[URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_ping type=RAM_2P impl=URAM latency=1
    ap_uint<32> local_part_Y_pong[URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_pong type=RAM_2P impl=URAM latency=1

iter:
    for (INDEX_TYPE iter_idx = 0; iter_idx < Iteration_time; ++iter_idx) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
    init:
        for (INDEX_TYPE i = 0; i < num_owner_groups; ++i) {
#pragma HLS loop_tripcount min=1 max=8000
#pragma HLS pipeline II=1
            local_part_Y_ping[i] = 0;
            local_part_Y_pong[i] = 0;
        }

        INDEX_TYPE owner_id = 0;
        INDEX_TYPE pair_lane = 0;
        bool meta_valid = false;

    consume:
        for (bool done = false; !done;) {
#pragma HLS loop_tripcount min=1 max=4000000
#pragma HLS pipeline II=1
            CuperSpmvOnly_TaggedScalar tagged = Owner_Lane_Stream.read();
            if (!meta_valid) {
                owner_id = CuperSpmvOnly_OwnerFromPacket(tagged.packet_idx);
                pair_lane = tagged.pair_lane;
                meta_valid = true;
            }

            if (tagged.done != 0) {
                done = true;
            } else {
                const ap_uint<17> addr = tagged.packet_idx / HBM_CHANNEL_NUM;
                if (tagged.scalar_lane == 0) {
                    Adder_p(addr, tagged.value, local_part_Y_ping);
                } else {
                    Adder_p(addr, tagged.value, local_part_Y_pong);
                }
            }
        }

    writer:
        for (INDEX_TYPE owner_group = 0; owner_group < num_owner_groups; ++owner_group) {
#pragma HLS loop_tripcount min=1 max=8000
#pragma HLS pipeline II=1
            const INDEX_TYPE packet_idx =
                owner_group * HBM_CHANNEL_NUM + owner_id;
            if (packet_idx < num_out_packets) {
                CuperSpmvOnly_TaggedFloatV2 tagged;
                tagged.packet_idx = packet_idx;
                tagged.pair_lane = pair_lane;
                tagged.value[0] = tapa::bit_cast<VALUE_TYPE>(
                    local_part_Y_ping[owner_group]);
                tagged.value[1] = tapa::bit_cast<VALUE_TYPE>(
                    local_part_Y_pong[owner_group]);
                Vector_Y_Tagged_Stream.write(tagged);
            }
        }
    }
}

inline void CuperSpmvOnly_ConsumeOwnerLaneAdderOoo(
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream,
    bool &done,
    ap_uint<32> local_part_Y_ping[URAM_DEPTH],
    ap_uint<32> local_part_Y_pong[URAM_DEPTH]) {
#pragma HLS inline
    if (!done && !Owner_Lane_Stream.empty()) {
        CuperSpmvOnly_TaggedScalar tagged;
        Owner_Lane_Stream.try_read(tagged);
        if (tagged.done != 0) {
            done = true;
        } else {
            const ap_uint<17> addr = tagged.packet_idx / HBM_CHANNEL_NUM;
            if (tagged.scalar_lane == 0) {
                Adder_p(addr, tagged.value, local_part_Y_ping);
            } else {
                Adder_p(addr, tagged.value, local_part_Y_pong);
            }
        }
    }
}

#ifdef JACOBI_SPMV_OOO_ACCUMULATE_RTL
[[tapa::target("non_synthesizable")]]
#endif
void CuperSpmvOnly_RtlOwnerBankAccumulatorOoo(
    const INDEX_TYPE Iteration_num,
    const INDEX_TYPE Row_num,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_0,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_1,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_2,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_3,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_4,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_5,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_6,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_7,
    tapa::ostream<CuperSpmvOnly_TaggedFloatV2> &Vector_Y_Tagged_Stream,
    const INDEX_TYPE Owner_id) {
    // RTL owner-bank accumulator 的 C++ 占位实现。
    //
    // 旧 RTL 版把 owner 的 8 条 pair-lane 拆成 8 个独立 TAPA task，16 路时就是
    // 128 个 wrapper/stream/FIFO，route 阶段已经证明太碎。这里改成每个 owner
    // 只有一个 bank task；bank 内部仍有 8 条 lane 的独立 partial sum，但对外
    // 只暴露 8 个输入 FIFO 和 1 个输出 FIFO，减少顶层跨 SLR 布线。
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
    const INDEX_TYPE num_out_packets = Cuper_NumFloatV16Packets(Row_num);
    const INDEX_TYPE num_owner_groups =
        (num_out_packets + HBM_CHANNEL_NUM - 1) / HBM_CHANNEL_NUM;

    ap_uint<32> local_part_Y_ping[8][URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_ping type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_ping dim=1
    ap_uint<32> local_part_Y_pong[8][URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_pong type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_pong dim=1

iter:
    for (INDEX_TYPE iter_idx = 0; iter_idx < Iteration_time; ++iter_idx) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
    init:
        for (INDEX_TYPE i = 0; i < num_owner_groups; ++i) {
#pragma HLS loop_tripcount min=1 max=8000
#pragma HLS pipeline II=1
            for (INDEX_TYPE pair_lane = 0; pair_lane < 8; ++pair_lane) {
#pragma HLS unroll
                local_part_Y_ping[pair_lane][i] = 0;
                local_part_Y_pong[pair_lane][i] = 0;
            }
        }

        bool done[8];
#pragma HLS array_partition complete variable=done dim=1
        for (INDEX_TYPE i = 0; i < 8; ++i) {
#pragma HLS unroll
            done[i] = false;
        }

        INDEX_TYPE lane_cursor = 0;
    consume:
        for (; !(done[0] && done[1] && done[2] && done[3] &&
                 done[4] && done[5] && done[6] && done[7]);) {
#pragma HLS loop_tripcount min=1 max=4000000
#pragma HLS pipeline II=1
            switch (lane_cursor) {
            case 0:
                CuperSpmvOnly_ConsumeOwnerLaneAdderOoo(Owner_Lane_Stream_0,
                                                       done[0],
                                                       local_part_Y_ping[0],
                                                       local_part_Y_pong[0]);
                break;
            case 1:
                CuperSpmvOnly_ConsumeOwnerLaneAdderOoo(Owner_Lane_Stream_1,
                                                       done[1],
                                                       local_part_Y_ping[1],
                                                       local_part_Y_pong[1]);
                break;
            case 2:
                CuperSpmvOnly_ConsumeOwnerLaneAdderOoo(Owner_Lane_Stream_2,
                                                       done[2],
                                                       local_part_Y_ping[2],
                                                       local_part_Y_pong[2]);
                break;
            case 3:
                CuperSpmvOnly_ConsumeOwnerLaneAdderOoo(Owner_Lane_Stream_3,
                                                       done[3],
                                                       local_part_Y_ping[3],
                                                       local_part_Y_pong[3]);
                break;
            case 4:
                CuperSpmvOnly_ConsumeOwnerLaneAdderOoo(Owner_Lane_Stream_4,
                                                       done[4],
                                                       local_part_Y_ping[4],
                                                       local_part_Y_pong[4]);
                break;
            case 5:
                CuperSpmvOnly_ConsumeOwnerLaneAdderOoo(Owner_Lane_Stream_5,
                                                       done[5],
                                                       local_part_Y_ping[5],
                                                       local_part_Y_pong[5]);
                break;
            case 6:
                CuperSpmvOnly_ConsumeOwnerLaneAdderOoo(Owner_Lane_Stream_6,
                                                       done[6],
                                                       local_part_Y_ping[6],
                                                       local_part_Y_pong[6]);
                break;
            default:
                CuperSpmvOnly_ConsumeOwnerLaneAdderOoo(Owner_Lane_Stream_7,
                                                       done[7],
                                                       local_part_Y_ping[7],
                                                       local_part_Y_pong[7]);
                break;
            }

            ++lane_cursor;
            if (lane_cursor == 8) {
                lane_cursor = 0;
            }
        }

    writer:
        for (INDEX_TYPE owner_group = 0; owner_group < num_owner_groups; ++owner_group) {
#pragma HLS loop_tripcount min=1 max=8000
            const INDEX_TYPE packet_idx =
                owner_group * HBM_CHANNEL_NUM + Owner_id;
            if (packet_idx < num_out_packets) {
            write_pairs:
                for (INDEX_TYPE pair_lane = 0; pair_lane < 8; ++pair_lane) {
#pragma HLS loop_tripcount min=8 max=8
#pragma HLS pipeline II=1
                    CuperSpmvOnly_TaggedFloatV2 tagged;
                    tagged.packet_idx = packet_idx;
                    tagged.pair_lane = pair_lane;
                    tagged.value[0] = tapa::bit_cast<VALUE_TYPE>(
                        local_part_Y_ping[pair_lane][owner_group]);
                    tagged.value[1] = tapa::bit_cast<VALUE_TYPE>(
                        local_part_Y_pong[pair_lane][owner_group]);
                    Vector_Y_Tagged_Stream.write(tagged);
                }
            }
        }
    }
}

void CuperSpmvOnly_OwnerLaneAccumulatorOoo(
    const INDEX_TYPE Iteration_num,
    const INDEX_TYPE Row_num,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream,
    tapa::ostream<CuperSpmvOnly_TaggedFloatV2> &Vector_Y_Tagged_Stream) {
    // 细粒度 owner-lane accumulator。
    //
    // 前一版在一个 owner task 里同时维护 8 条 pair-lane 的 cache，HLS 会把
    // cache_* 数组识别成同一个 consume 循环内的距离 1 相关，最终 II=14。
    // 这里把每条 owner-lane 拆成单独 task：一个 task 只接一条 FIFO，只维护
    // 一份 ping/pong partial sum。乱序所有权仍按 output packet owner 分片，
    // 但 HLS 看到的是更接近原 Cuper accumulator 的单输入累加器。
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
    const INDEX_TYPE num_out_packets = Cuper_NumFloatV16Packets(Row_num);
    const INDEX_TYPE num_owner_groups =
        (num_out_packets + HBM_CHANNEL_NUM - 1) / HBM_CHANNEL_NUM;

    ap_uint<32> local_part_Y_ping[URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_ping type=RAM_2P impl=URAM latency=1
    ap_uint<32> local_part_Y_pong[URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_pong type=RAM_2P impl=URAM latency=1

iter:
    for (INDEX_TYPE iter_idx = 0; iter_idx < Iteration_time; ++iter_idx) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
    init:
        for (INDEX_TYPE i = 0; i < num_owner_groups; ++i) {
#pragma HLS loop_tripcount min=1 max=8000
#pragma HLS pipeline II=1
            local_part_Y_ping[i] = 0;
            local_part_Y_pong[i] = 0;
        }

        INDEX_TYPE owner_id = 0;
        INDEX_TYPE pair_lane = 0;
        bool meta_valid = false;

    consume:
        for (bool done = false; !done;) {
#pragma HLS loop_tripcount min=1 max=4000000
#pragma HLS pipeline II=1
            CuperSpmvOnly_TaggedScalar tagged = Owner_Lane_Stream.read();
            if (!meta_valid) {
                owner_id = CuperSpmvOnly_OwnerFromPacket(tagged.packet_idx);
                pair_lane = tagged.pair_lane;
                meta_valid = true;
            }

            if (tagged.done != 0) {
                done = true;
            } else {
                const ap_uint<17> addr = tagged.packet_idx / HBM_CHANNEL_NUM;
                if (tagged.scalar_lane == 0) {
                    Adder_p(addr, tagged.value, local_part_Y_ping);
                } else {
                    Adder_p(addr, tagged.value, local_part_Y_pong);
                }
            }
        }

    writer:
        for (INDEX_TYPE owner_group = 0; owner_group < num_owner_groups; ++owner_group) {
#pragma HLS loop_tripcount min=1 max=8000
#pragma HLS pipeline II=1
            const INDEX_TYPE packet_idx =
                owner_group * HBM_CHANNEL_NUM + owner_id;
            if (packet_idx < num_out_packets) {
                CuperSpmvOnly_TaggedFloatV2 tagged;
                tagged.packet_idx = packet_idx;
                tagged.pair_lane = pair_lane;
                tagged.value[0] = tapa::bit_cast<VALUE_TYPE>(
                    local_part_Y_ping[owner_group]);
                tagged.value[1] = tapa::bit_cast<VALUE_TYPE>(
                    local_part_Y_pong[owner_group]);
                Vector_Y_Tagged_Stream.write(tagged);
            }
        }
    }
}

inline void CuperSpmvOnly_ConsumeOwnerLaneOoo(
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream,
    bool &done,
    bool cache_ping_valid[CUPER_SPMV_ROW_CACHE_SIZE],
    bool cache_pong_valid[CUPER_SPMV_ROW_CACHE_SIZE],
    ap_uint<17> cache_ping_addr[CUPER_SPMV_ROW_CACHE_SIZE],
    ap_uint<17> cache_pong_addr[CUPER_SPMV_ROW_CACHE_SIZE],
    VALUE_TYPE cache_ping_value[CUPER_SPMV_ROW_CACHE_SIZE],
    VALUE_TYPE cache_pong_value[CUPER_SPMV_ROW_CACHE_SIZE],
    INDEX_TYPE &cache_ping_victim,
    INDEX_TYPE &cache_pong_victim,
    ap_uint<32> local_part_Y_ping[URAM_DEPTH],
    ap_uint<32> local_part_Y_pong[URAM_DEPTH]) {
#pragma HLS inline
    if (!done && !Owner_Lane_Stream.empty()) {
        CuperSpmvOnly_TaggedScalar tagged;
        Owner_Lane_Stream.try_read(tagged);
        if (tagged.done != 0) {
            done = true;
        } else if (tagged.scalar_lane == 0) {
            CuperSpmvOnly_PushRowCacheUpdate(tagged.packet_idx / HBM_CHANNEL_NUM,
                                             tagged.value,
                                             cache_ping_valid,
                                             cache_ping_addr,
                                             cache_ping_value,
                                             cache_ping_victim,
                                             local_part_Y_ping);
        } else {
            CuperSpmvOnly_PushRowCacheUpdate(tagged.packet_idx / HBM_CHANNEL_NUM,
                                             tagged.value,
                                             cache_pong_valid,
                                             cache_pong_addr,
                                             cache_pong_value,
                                             cache_pong_victim,
                                             local_part_Y_pong);
        }
    }
}

void CuperSpmvOnly_OwnerAccumulatorTransposeOoo(
    const INDEX_TYPE Iteration_num,
    const INDEX_TYPE Row_num,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_0,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_1,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_2,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_3,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_4,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_5,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_6,
    tapa::istream<CuperSpmvOnly_TaggedScalar> &Owner_Lane_Stream_7,
    tapa::ostream<CuperSpmvOnly_TaggedFloatV2> &Vector_Y_Tagged_Stream,
    const INDEX_TYPE Owner_id) {
    // 每个 owner 接 8 条固定来源流，每条流对应一个 pair_lane。
    // 这把旧中心 Router 的动态 owner 选择改成了静态 transpose 网络：
    // source/pair_lane 在连线上已经固定，owner 内部只处理 ping/pong 两个
    // scalar lane，并按最终 packet owner 写自己的 partial sum。
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
    const INDEX_TYPE num_out_packets = Cuper_NumFloatV16Packets(Row_num);
    const INDEX_TYPE num_owner_groups =
        (num_out_packets + HBM_CHANNEL_NUM - 1) / HBM_CHANNEL_NUM;

    ap_uint<32> local_part_Y_ping[8][URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_ping type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_ping dim=1
    ap_uint<32> local_part_Y_pong[8][URAM_DEPTH];
#pragma HLS bind_storage variable=local_part_Y_pong type=RAM_2P impl=URAM latency=1
#pragma HLS array_partition complete variable=local_part_Y_pong dim=1

iter:
    for (INDEX_TYPE iter_idx = 0; iter_idx < Iteration_time; ++iter_idx) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
        bool cache_ping_valid[8][CUPER_SPMV_ROW_CACHE_SIZE];
#pragma HLS array_partition complete variable=cache_ping_valid dim=0
        bool cache_pong_valid[8][CUPER_SPMV_ROW_CACHE_SIZE];
#pragma HLS array_partition complete variable=cache_pong_valid dim=0
        ap_uint<17> cache_ping_addr[8][CUPER_SPMV_ROW_CACHE_SIZE];
#pragma HLS array_partition complete variable=cache_ping_addr dim=0
        ap_uint<17> cache_pong_addr[8][CUPER_SPMV_ROW_CACHE_SIZE];
#pragma HLS array_partition complete variable=cache_pong_addr dim=0
        VALUE_TYPE cache_ping_value[8][CUPER_SPMV_ROW_CACHE_SIZE];
#pragma HLS array_partition complete variable=cache_ping_value dim=0
        VALUE_TYPE cache_pong_value[8][CUPER_SPMV_ROW_CACHE_SIZE];
#pragma HLS array_partition complete variable=cache_pong_value dim=0
        INDEX_TYPE cache_ping_victim[8];
#pragma HLS array_partition complete variable=cache_ping_victim dim=1
        INDEX_TYPE cache_pong_victim[8];
#pragma HLS array_partition complete variable=cache_pong_victim dim=1

    init:
        for (INDEX_TYPE i = 0; i < num_owner_groups; ++i) {
#pragma HLS loop_tripcount min=1 max=8000
#pragma HLS pipeline II=1
            for (INDEX_TYPE pair_lane = 0; pair_lane < 8; ++pair_lane) {
#pragma HLS unroll
                local_part_Y_ping[pair_lane][i] = 0;
                local_part_Y_pong[pair_lane][i] = 0;
            }
        }

    init_cache:
        for (INDEX_TYPE pair_lane = 0; pair_lane < 8; ++pair_lane) {
#pragma HLS unroll
            cache_ping_victim[pair_lane] = 0;
            cache_pong_victim[pair_lane] = 0;
            for (INDEX_TYPE entry = 0; entry < CUPER_SPMV_ROW_CACHE_SIZE; ++entry) {
#pragma HLS unroll
                cache_ping_valid[pair_lane][entry] = false;
                cache_pong_valid[pair_lane][entry] = false;
                cache_ping_addr[pair_lane][entry] = 0;
                cache_pong_addr[pair_lane][entry] = 0;
                cache_ping_value[pair_lane][entry] = 0.0f;
                cache_pong_value[pair_lane][entry] = 0.0f;
            }
        }

        bool done[8];
#pragma HLS array_partition complete variable=done dim=1
        for (INDEX_TYPE i = 0; i < 8; ++i) {
#pragma HLS unroll
            done[i] = false;
        }

        INDEX_TYPE lane_cursor = 0;
    consume:
        for (; !(done[0] && done[1] && done[2] && done[3] &&
                 done[4] && done[5] && done[6] && done[7]);) {
#pragma HLS loop_tripcount min=1 max=4000000
#pragma HLS pipeline II=1
            // 每个 owner 仍接 8 条 pair-lane 输入，但每拍只消费一条。
            // 这样把乱序范围保留在 8 路静态分组内，同时避免 HLS 在同一个
            // consume 循环里并行展开 8 套 cache/FP 累加路径导致 II 拉高。
            switch (lane_cursor) {
            case 0:
                CuperSpmvOnly_ConsumeOwnerLaneOoo(Owner_Lane_Stream_0,
                                                  done[0],
                                                  cache_ping_valid[0],
                                                  cache_pong_valid[0],
                                                  cache_ping_addr[0],
                                                  cache_pong_addr[0],
                                                  cache_ping_value[0],
                                                  cache_pong_value[0],
                                                  cache_ping_victim[0],
                                                  cache_pong_victim[0],
                                                  local_part_Y_ping[0],
                                                  local_part_Y_pong[0]);
                break;
            case 1:
                CuperSpmvOnly_ConsumeOwnerLaneOoo(Owner_Lane_Stream_1,
                                                  done[1],
                                                  cache_ping_valid[1],
                                                  cache_pong_valid[1],
                                                  cache_ping_addr[1],
                                                  cache_pong_addr[1],
                                                  cache_ping_value[1],
                                                  cache_pong_value[1],
                                                  cache_ping_victim[1],
                                                  cache_pong_victim[1],
                                                  local_part_Y_ping[1],
                                                  local_part_Y_pong[1]);
                break;
            case 2:
                CuperSpmvOnly_ConsumeOwnerLaneOoo(Owner_Lane_Stream_2,
                                                  done[2],
                                                  cache_ping_valid[2],
                                                  cache_pong_valid[2],
                                                  cache_ping_addr[2],
                                                  cache_pong_addr[2],
                                                  cache_ping_value[2],
                                                  cache_pong_value[2],
                                                  cache_ping_victim[2],
                                                  cache_pong_victim[2],
                                                  local_part_Y_ping[2],
                                                  local_part_Y_pong[2]);
                break;
            case 3:
                CuperSpmvOnly_ConsumeOwnerLaneOoo(Owner_Lane_Stream_3,
                                                  done[3],
                                                  cache_ping_valid[3],
                                                  cache_pong_valid[3],
                                                  cache_ping_addr[3],
                                                  cache_pong_addr[3],
                                                  cache_ping_value[3],
                                                  cache_pong_value[3],
                                                  cache_ping_victim[3],
                                                  cache_pong_victim[3],
                                                  local_part_Y_ping[3],
                                                  local_part_Y_pong[3]);
                break;
            case 4:
                CuperSpmvOnly_ConsumeOwnerLaneOoo(Owner_Lane_Stream_4,
                                                  done[4],
                                                  cache_ping_valid[4],
                                                  cache_pong_valid[4],
                                                  cache_ping_addr[4],
                                                  cache_pong_addr[4],
                                                  cache_ping_value[4],
                                                  cache_pong_value[4],
                                                  cache_ping_victim[4],
                                                  cache_pong_victim[4],
                                                  local_part_Y_ping[4],
                                                  local_part_Y_pong[4]);
                break;
            case 5:
                CuperSpmvOnly_ConsumeOwnerLaneOoo(Owner_Lane_Stream_5,
                                                  done[5],
                                                  cache_ping_valid[5],
                                                  cache_pong_valid[5],
                                                  cache_ping_addr[5],
                                                  cache_pong_addr[5],
                                                  cache_ping_value[5],
                                                  cache_pong_value[5],
                                                  cache_ping_victim[5],
                                                  cache_pong_victim[5],
                                                  local_part_Y_ping[5],
                                                  local_part_Y_pong[5]);
                break;
            case 6:
                CuperSpmvOnly_ConsumeOwnerLaneOoo(Owner_Lane_Stream_6,
                                                  done[6],
                                                  cache_ping_valid[6],
                                                  cache_pong_valid[6],
                                                  cache_ping_addr[6],
                                                  cache_pong_addr[6],
                                                  cache_ping_value[6],
                                                  cache_pong_value[6],
                                                  cache_ping_victim[6],
                                                  cache_pong_victim[6],
                                                  local_part_Y_ping[6],
                                                  local_part_Y_pong[6]);
                break;
            default:
                CuperSpmvOnly_ConsumeOwnerLaneOoo(Owner_Lane_Stream_7,
                                                  done[7],
                                                  cache_ping_valid[7],
                                                  cache_pong_valid[7],
                                                  cache_ping_addr[7],
                                                  cache_pong_addr[7],
                                                  cache_ping_value[7],
                                                  cache_pong_value[7],
                                                  cache_ping_victim[7],
                                                  cache_pong_victim[7],
                                                  local_part_Y_ping[7],
                                                  local_part_Y_pong[7]);
                break;
            }

            ++lane_cursor;
            if (lane_cursor == 8) {
                lane_cursor = 0;
            }
        }

    flush_cache:
        for (INDEX_TYPE pair_lane = 0; pair_lane < 8; ++pair_lane) {
#pragma HLS unroll
            for (INDEX_TYPE entry = 0; entry < CUPER_SPMV_ROW_CACHE_SIZE; ++entry) {
#pragma HLS unroll
                CuperSpmvOnly_FlushRowCacheEntry(cache_ping_valid[pair_lane][entry],
                                                 cache_ping_addr[pair_lane][entry],
                                                 cache_ping_value[pair_lane][entry],
                                                 local_part_Y_ping[pair_lane]);
                CuperSpmvOnly_FlushRowCacheEntry(cache_pong_valid[pair_lane][entry],
                                                 cache_pong_addr[pair_lane][entry],
                                                 cache_pong_value[pair_lane][entry],
                                                 local_part_Y_pong[pair_lane]);
            }
        }

    writer:
        for (INDEX_TYPE owner_group = 0; owner_group < num_owner_groups; ++owner_group) {
#pragma HLS loop_tripcount min=1 max=8000
            const INDEX_TYPE packet_idx =
                owner_group * HBM_CHANNEL_NUM + Owner_id;
            if (packet_idx < num_out_packets) {
            write_pairs:
                for (INDEX_TYPE pair_lane = 0; pair_lane < 8; ++pair_lane) {
#pragma HLS loop_tripcount min=8 max=8
#pragma HLS pipeline II=1
                    CuperSpmvOnly_TaggedFloatV2 tagged;
                    tagged.packet_idx = packet_idx;
                    tagged.pair_lane = pair_lane;
                    tagged.value[0] = tapa::bit_cast<VALUE_TYPE>(
                        local_part_Y_ping[pair_lane][owner_group]);
                    tagged.value[1] = tapa::bit_cast<VALUE_TYPE>(
                        local_part_Y_pong[pair_lane][owner_group]);
                    Vector_Y_Tagged_Stream.write(tagged);
                }
            }
        }
    }
}
#endif

void CuperSpmvOnly_TaggedScatterWriter(
    const INDEX_TYPE Iteration_num,
    const INDEX_TYPE Row_num,
    const INDEX_TYPE Batch_num,
    const INDEX_TYPE Matrix_len,
    const INDEX_TYPE Column_num,
    tapa::istreams<CuperSpmvOnly_TaggedFloatV2, HBM_CHANNEL_NUM> &Vector_Y_Tagged_Stream,
    tapa::async_mmap<VALUE_TYPE> &Y_out,
    tapa::ostream<CuperSpmvOnlyProgressEvent> &Progress_out) {
    // SpMV-only 乱序写回层。
    //
    // 旧后端必须等 8 个 pair lane 齐了才能拼一个 float_v16，并隐含依赖
    // accumulator 输出顺序。这里直接把 tagged float_v2 写成两个 scalar float：
    //   Y[packet_idx * 16 + pair_lane * 2 + 0/1]
    // 因此来自任意 HBM/Core/Accumulator 的 tag 都能先到先写，不再按 packet 顺序
    // 做全局重排。host 侧在 lanereal16 宏下也用 float mmap 传入同一个 Y buffer。
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
    const INDEX_TYPE num_out_packets = Cuper_NumFloatV16Packets(Row_num);
    const INDEX_TYPE tagged_pairs_per_iter = num_out_packets * 8;
    const INDEX_TYPE tagged_pairs_total = tagged_pairs_per_iter * Iteration_time;
    const INDEX_TYPE scalar_writes_total = tagged_pairs_total * 2;

    INDEX_TYPE channel_cursor = 0;
    bool pending_valid = false;
    INDEX_TYPE pending_addr = 0;
    VALUE_TYPE pending_data = 0.0f;
    bool next_valid = false;
    INDEX_TYPE next_addr = 0;
    VALUE_TYPE next_data = 0.0f;
    bool first_tag_reported = false;
    bool first_write_reported = false;
    bool first_resp_reported = false;

    Progress_out.write(CuperSpmvOnly_MakeProgressEvent(
        kCuperSpmvOnlyProgressScatterStart,
        Row_num,
        tagged_pairs_total,
        scalar_writes_total));
scatter:
    for (INDEX_TYPE pair_count = 0, response_count = 0;
         response_count < scalar_writes_total;) {
#pragma HLS loop_tripcount min=8 max=4000000
#pragma HLS pipeline II=1
        if (!pending_valid && next_valid) {
            pending_addr = next_addr;
            pending_data = next_data;
            pending_valid = true;
            next_valid = false;
        }

        if (!pending_valid && !next_valid &&
            pair_count < tagged_pairs_total &&
            !Vector_Y_Tagged_Stream[channel_cursor].empty()) {
            CuperSpmvOnly_TaggedFloatV2 tagged;
            Vector_Y_Tagged_Stream[channel_cursor].try_read(tagged);

            const INDEX_TYPE base_addr =
                (tagged.packet_idx << 4) + (tagged.pair_lane << 1);

            pending_addr = base_addr;
            pending_data = tagged.value[0];
            pending_valid = true;
            next_addr = base_addr + 1;
            next_data = tagged.value[1];
            next_valid = true;

            ++pair_count;
            if (!first_tag_reported) {
                Progress_out.write(CuperSpmvOnly_MakeProgressEvent(
                    kCuperSpmvOnlyProgressScatterFirstTag,
                    channel_cursor,
                    tagged.packet_idx,
                    tagged.pair_lane));
                first_tag_reported = true;
            }
        }

        if (pending_valid &&
            !Y_out.write_addr.full() &&
            !Y_out.write_data.full()) {
            Y_out.write_addr.try_write(pending_addr);
            Y_out.write_data.try_write(pending_data);
            if (!first_write_reported) {
                Progress_out.write(CuperSpmvOnly_MakeProgressEvent(
                    kCuperSpmvOnlyProgressScatterFirstWrite,
                    pending_addr,
                    pair_count,
                    response_count));
                first_write_reported = true;
            }
            pending_valid = false;
        }

        uint8_t num_responses = 0;
        if (Y_out.write_resp.try_read(num_responses)) {
            response_count += int(num_responses) + 1;
            if (!first_resp_reported) {
                Progress_out.write(CuperSpmvOnly_MakeProgressEvent(
                    kCuperSpmvOnlyProgressScatterFirstResp,
                    response_count,
                    int(num_responses) + 1,
                    pair_count));
                first_resp_reported = true;
            }
        }

        ++channel_cursor;
        if (channel_cursor == HBM_CHANNEL_NUM) {
            channel_cursor = 0;
        }
    }

    Progress_out.write(CuperSpmvOnly_MakeProgressEvent(
        kCuperSpmvOnlyProgressFinal,
        scalar_writes_total,
        tagged_pairs_total,
        Iteration_time));
}

void CuperSpmvOnly_TaggedScatterWriterOoo(
    const INDEX_TYPE Iteration_num,
    const INDEX_TYPE Row_num,
    const INDEX_TYPE Batch_num,
    const INDEX_TYPE Matrix_len,
    const INDEX_TYPE Column_num,
    tapa::istreams<CuperSpmvOnly_TaggedFloatV2, HBM_CHANNEL_NUM> &Vector_Y_Tagged_Stream,
    tapa::async_mmap<VALUE_TYPE> &Y_out,
    tapa::ostream<CuperSpmvOnlyProgressEvent> &Progress_out) {
    // OOO owner-bank 写回层。
    //
    // OOO 分支现在每个 output owner 只有一个 bank accumulator。bank 内部仍按
    // 8 条 pair-lane 乱序累加，但对外只输出一条 tagged stream，因此这里轮询
    // HBM_CHANNEL_NUM 条 owner-bank 输出。写回协议和普通 TaggedScatterWriter
    // 相同：tag 直接给出最终 scalar 地址。
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
    const INDEX_TYPE num_out_packets = Cuper_NumFloatV16Packets(Row_num);
    const INDEX_TYPE tagged_pairs_per_iter = num_out_packets * 8;
    const INDEX_TYPE tagged_pairs_total = tagged_pairs_per_iter * Iteration_time;
    const INDEX_TYPE scalar_writes_total = tagged_pairs_total * 2;
    const INDEX_TYPE tagged_stream_num = HBM_CHANNEL_NUM;

    INDEX_TYPE channel_cursor = 0;
    bool pending_valid = false;
    INDEX_TYPE pending_addr = 0;
    VALUE_TYPE pending_data = 0.0f;
    bool next_valid = false;
    INDEX_TYPE next_addr = 0;
    VALUE_TYPE next_data = 0.0f;
    bool first_tag_reported = false;
    bool first_write_reported = false;
    bool first_resp_reported = false;

    Progress_out.write(CuperSpmvOnly_MakeProgressEvent(
        kCuperSpmvOnlyProgressScatterStart,
        Row_num,
        tagged_pairs_total,
        scalar_writes_total));
scatter:
    for (INDEX_TYPE pair_count = 0, response_count = 0;
         response_count < scalar_writes_total;) {
#pragma HLS loop_tripcount min=8 max=4000000
#pragma HLS pipeline II=1
        if (!pending_valid && next_valid) {
            pending_addr = next_addr;
            pending_data = next_data;
            pending_valid = true;
            next_valid = false;
        }

        if (!pending_valid && !next_valid &&
            pair_count < tagged_pairs_total &&
            !Vector_Y_Tagged_Stream[channel_cursor].empty()) {
            CuperSpmvOnly_TaggedFloatV2 tagged;
            Vector_Y_Tagged_Stream[channel_cursor].try_read(tagged);

            const INDEX_TYPE base_addr =
                (tagged.packet_idx << 4) + (tagged.pair_lane << 1);

            pending_addr = base_addr;
            pending_data = tagged.value[0];
            pending_valid = true;
            next_addr = base_addr + 1;
            next_data = tagged.value[1];
            next_valid = true;

            ++pair_count;
            if (!first_tag_reported) {
                Progress_out.write(CuperSpmvOnly_MakeProgressEvent(
                    kCuperSpmvOnlyProgressScatterFirstTag,
                    channel_cursor,
                    tagged.packet_idx,
                    tagged.pair_lane));
                first_tag_reported = true;
            }
        }

        if (pending_valid &&
            !Y_out.write_addr.full() &&
            !Y_out.write_data.full()) {
            Y_out.write_addr.try_write(pending_addr);
            Y_out.write_data.try_write(pending_data);
            if (!first_write_reported) {
                Progress_out.write(CuperSpmvOnly_MakeProgressEvent(
                    kCuperSpmvOnlyProgressScatterFirstWrite,
                    pending_addr,
                    pair_count,
                    response_count));
                first_write_reported = true;
            }
            pending_valid = false;
        }

        uint8_t num_responses = 0;
        if (Y_out.write_resp.try_read(num_responses)) {
            response_count += int(num_responses) + 1;
            if (!first_resp_reported) {
                Progress_out.write(CuperSpmvOnly_MakeProgressEvent(
                    kCuperSpmvOnlyProgressScatterFirstResp,
                    response_count,
                    int(num_responses) + 1,
                    pair_count));
                first_resp_reported = true;
            }
        }

        ++channel_cursor;
        if (channel_cursor == tagged_stream_num) {
            channel_cursor = 0;
        }
    }

    Progress_out.write(CuperSpmvOnly_MakeProgressEvent(
        kCuperSpmvOnlyProgressFinal,
        scalar_writes_total,
        tagged_pairs_total,
        Iteration_time));
}
#endif

void CuperSpmvOnly_VectorWriter(const INDEX_TYPE Iteration_num,
                                const INDEX_TYPE Row_num,
                                const INDEX_TYPE Batch_num,
                                const INDEX_TYPE Matrix_len,
                                const INDEX_TYPE Column_num,
                                tapa::istream<float_v16> &Vector_Y_Stream_Ans,
                                tapa::async_mmap<float_v16> &Y_out,
                                tapa::ostream<CuperSpmvOnlyProgressEvent> &Progress_out) {
    // 和普通 Vector_Writer 一样按地址顺序写 Y_out；不同点是最终等待所有
    // write response 后再写 Status/Metrics，方便上板确认完整 SpMV 是否自然结束。
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
    const INDEX_TYPE num_ite_Y = Cuper_NumFloatV16Packets(Row_num);
    Progress_out.write(CuperSpmvOnly_MakeProgressEvent(
        kCuperSpmvOnlyProgressScatterStart,
        Row_num,
        num_ite_Y,
        Iteration_time));

iter:
    for (INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
    write_Y:
        for (INDEX_TYPE i_request = 0, i_response = 0; i_response < num_ite_Y;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
            if ((i_request < num_ite_Y) &&
                !Vector_Y_Stream_Ans.empty() &&
                !Y_out.write_addr.full() &&
                !Y_out.write_data.full()) {
                Y_out.write_addr.try_write(i_request);
                float_v16 tmpv16;
                Vector_Y_Stream_Ans.try_read(tmpv16);
                Y_out.write_data.try_write(tmpv16);
                if (iter == 0 && i_request == 0) {
                    Progress_out.write(CuperSpmvOnly_MakeProgressEvent(
                        kCuperSpmvOnlyProgressScatterFirstWrite,
                        0,
                        num_ite_Y,
                        Iteration_time));
                }
                ++i_request;
            }

            uint8_t num_responses = 0;
            if (Y_out.write_resp.try_read(num_responses)) {
                i_response += int(num_responses) + 1;
                if (iter == 0 && i_response == int(num_responses) + 1) {
                    Progress_out.write(CuperSpmvOnly_MakeProgressEvent(
                        kCuperSpmvOnlyProgressScatterFirstResp,
                        i_response,
                        int(num_responses) + 1,
                        num_ite_Y));
                }
            }
        }
    }

    Progress_out.write(CuperSpmvOnly_MakeProgressEvent(
        kCuperSpmvOnlyProgressFinal,
        num_ite_Y,
        Row_num,
        Iteration_time));
}

#define CUPER_SPMV_ONLY_INVOKE_CORE(CORE_ID) \
        .invoke(Core, \
                PE_Param[CORE_ID], \
                Matrix_A_Stream[CORE_ID], \
                Vector_X_Stream[CORE_ID], \
                PE_Param[(CORE_ID) + 1], \
                Vector_X_Stream[(CORE_ID) + 1], \
                Vector_Y_Param[CORE_ID], \
                Matrix_Mult_Vector_Stream[CORE_ID])

#define CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(CORE_ID) \
        .invoke(CuperSpmvOnly_CoreStrip, \
                PE_Param[CORE_ID], \
                Matrix_A_Stream[CORE_ID], \
                Vector_X_Stream[CORE_ID], \
                PE_Param[(CORE_ID) + 1], \
                Vector_X_Stream[(CORE_ID) + 1], \
                Vector_Y_Param[CORE_ID], \
                Matrix_Mult_Vector_Stream[CORE_ID], \
                CORE_ID)

#define CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(CORE_ID) \
        .invoke(CuperSpmvOnly_CoreCompactPe, \
                PE_Param[CORE_ID], \
                Matrix_A_Stream[CORE_ID], \
                Vector_X_Stream[CORE_ID], \
                PE_Param[(CORE_ID) + 1], \
                Vector_X_Stream[(CORE_ID) + 1], \
                Vector_Y_Param[CORE_ID], \
                Matrix_Mult_Vector_Stream[CORE_ID], \
                CORE_ID)

#define CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(CORE_ID) \
        .invoke(CuperSpmvOnly_AccumulatorTagged, \
                Vector_Y_Param[CORE_ID], \
                Matrix_Mult_Vector_Stream[CORE_ID], \
                Vector_Y_Tagged_Stream[CORE_ID], \
                CORE_ID)

#ifdef JACOBI_SPMV_OOO_ACCUMULATE_RTL
#define CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(OWNER_ID) \
        .invoke(CuperSpmvOnly_RtlOwnerBankAccumulatorOoo, \
                Iteration_num, \
                Row_num, \
                Owner_Lane_Stream[(OWNER_ID) * 8 + 0], \
                Owner_Lane_Stream[(OWNER_ID) * 8 + 1], \
                Owner_Lane_Stream[(OWNER_ID) * 8 + 2], \
                Owner_Lane_Stream[(OWNER_ID) * 8 + 3], \
                Owner_Lane_Stream[(OWNER_ID) * 8 + 4], \
                Owner_Lane_Stream[(OWNER_ID) * 8 + 5], \
                Owner_Lane_Stream[(OWNER_ID) * 8 + 6], \
                Owner_Lane_Stream[(OWNER_ID) * 8 + 7], \
                Vector_Y_Tagged_Stream[OWNER_ID], \
                OWNER_ID)
#else
#define CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(OWNER_ID) \
        .invoke(CuperSpmvOnly_OwnerAccumulatorTransposeOoo, \
                Iteration_num, \
                Row_num, \
                Owner_Lane_Stream[(OWNER_ID) * 8 + 0], \
                Owner_Lane_Stream[(OWNER_ID) * 8 + 1], \
                Owner_Lane_Stream[(OWNER_ID) * 8 + 2], \
                Owner_Lane_Stream[(OWNER_ID) * 8 + 3], \
                Owner_Lane_Stream[(OWNER_ID) * 8 + 4], \
                Owner_Lane_Stream[(OWNER_ID) * 8 + 5], \
                Owner_Lane_Stream[(OWNER_ID) * 8 + 6], \
                Owner_Lane_Stream[(OWNER_ID) * 8 + 7], \
                Vector_Y_Tagged_Stream[OWNER_ID], \
                OWNER_ID)
#endif

#define CUPER_SPMV_ONLY_OWNER_LANE_INDEX(SOURCE_ID, LANE_ID) \
        ((((LANE_ID) * HBM_CHANNEL_NUM_DIV_8) + ((SOURCE_ID) % HBM_CHANNEL_NUM_DIV_8)) * 8 + \
         ((SOURCE_ID) / HBM_CHANNEL_NUM_DIV_8))

#define CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(SOURCE_ID) \
        .invoke(CuperSpmvOnly_SourceLaneSplitterOoo, \
                Vector_Y_Param[SOURCE_ID], \
                Matrix_Mult_Vector_Stream[SOURCE_ID], \
                Owner_Lane_Stream[CUPER_SPMV_ONLY_OWNER_LANE_INDEX(SOURCE_ID, 0)], \
                Owner_Lane_Stream[CUPER_SPMV_ONLY_OWNER_LANE_INDEX(SOURCE_ID, 1)], \
                Owner_Lane_Stream[CUPER_SPMV_ONLY_OWNER_LANE_INDEX(SOURCE_ID, 2)], \
                Owner_Lane_Stream[CUPER_SPMV_ONLY_OWNER_LANE_INDEX(SOURCE_ID, 3)], \
                Owner_Lane_Stream[CUPER_SPMV_ONLY_OWNER_LANE_INDEX(SOURCE_ID, 4)], \
                Owner_Lane_Stream[CUPER_SPMV_ONLY_OWNER_LANE_INDEX(SOURCE_ID, 5)], \
                Owner_Lane_Stream[CUPER_SPMV_ONLY_OWNER_LANE_INDEX(SOURCE_ID, 6)], \
                Owner_Lane_Stream[CUPER_SPMV_ONLY_OWNER_LANE_INDEX(SOURCE_ID, 7)], \
                SOURCE_ID)

void CuperSpmvServiceOnly(tapa::mmap<INDEX_TYPE> SpElement_list_ptr,
                          tapa::mmaps<ap_uint<512>, HBM_CHANNEL_NUM> Matrix_data,
                          tapa::mmap<float_v16> X,
#ifdef JACOBI_SPMV_LANE_STATIC_REAL
                          tapa::mmap<VALUE_TYPE> Y_out,
#else
                          tapa::mmap<float_v16> Y_out,
#endif
                          tapa::mmap<INDEX_TYPE> Status,
                          tapa::mmap<double> Metrics,
                          const INDEX_TYPE Batch_num,
                          const INDEX_TYPE Matrix_len,
                          const INDEX_TYPE Row_num,
                          const INDEX_TYPE Column_num,
                          const INDEX_TYPE Iteration_num
                         ) {
    // 参数和 X 仍采用 Cuper 原始串接方式：每个 Core 只消费自己的 Matrix_data
    // HBM channel，同时把 PE 参数和 X 向量转发给下一级 Core。
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM + 1, 128>    PE_Param("PE_Param");
    tapa::streams<float_v16, HBM_CHANNEL_NUM + 1, 1024>    Vector_X_Stream("Vector_X_Stream");
    tapa::streams<ap_uint<512>, HBM_CHANNEL_NUM, 512>      Matrix_A_Stream("Matrix_A_Stream");
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM, 64>         Vector_Y_Param("Vector_Y_Param");
    tapa::streams<Matrix_Mult_X, HBM_CHANNEL_NUM, 1024>    Matrix_Mult_Vector_Stream("Matrix_Mult_Vector_Stream");
#ifdef JACOBI_SPMV_LANE_STATIC_REAL
#ifdef JACOBI_SPMV_OOO_ACCUMULATE
    tapa::streams<CuperSpmvOnly_TaggedFloatV2, HBM_CHANNEL_NUM, 1024>
                                                                 Vector_Y_Tagged_Stream("Vector_Y_Tagged_Stream");
    tapa::streams<CuperSpmvOnly_TaggedScalar, HBM_CHANNEL_NUM * 8, 64>
                                                                 Owner_Lane_Stream("Owner_Lane_Stream");
#else
    tapa::streams<CuperSpmvOnly_TaggedFloatV2, HBM_CHANNEL_NUM, 1024>
                                                                 Vector_Y_Tagged_Stream("Vector_Y_Tagged_Stream");
#endif
#else
    tapa::streams<float_v2, HBM_CHANNEL_NUM, 1024>         Vector_Y_Stream("Vector_Y_Stream");
    // 8 路 checker/sort tree 仍按 Cuper 原始输出规整逻辑工作。
    // 对 16/24/32 路而言，每个 checker 分别消费 2/3/4 路 accumulator。
    tapa::streams<float_v2, 8, FIFO_DEPTH>                 Vector_Y_Stream_Aftck("Vector_Y_Stream_Aftck");
    tapa::stream<float_v16, FIFO_DEPTH>                    Vector_Y_Stream_Ans("Vector_Y_Stream_Ans");
#endif
#if defined(JACOBI_SPMV_STRIP_PADDING) || \
    defined(JACOBI_SPMV_COMPACT_PE) || \
    defined(JACOBI_SPMV_LANE_STATIC_REAL)
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM, 2>           Matrix_Len_Stream("Matrix_Len_Stream");
#endif
    tapa::stream<CuperSpmvOnlyProgressEvent, 64>             Ptr_Progress_Stream("Ptr_Progress_Stream");
    tapa::stream<CuperSpmvOnlyProgressEvent, 64>             Writer_Progress_Stream("Writer_Progress_Stream");

    tapa::task()
        .invoke(CuperSpmvOnly_ProgressWriter,
                Batch_num,
                Matrix_len,
                Row_num,
                Column_num,
                Iteration_num,
                Ptr_Progress_Stream,
                Writer_Progress_Stream,
                Status,
                Metrics)
#if defined(JACOBI_SPMV_STRIP_PADDING) || \
    defined(JACOBI_SPMV_COMPACT_PE) || \
    defined(JACOBI_SPMV_LANE_STATIC_REAL)
        // 去 padding 版本：ptr 表先给每个 Matrix loader 一路独立总长度，
        // 再按 HBM channel 分发每个 batch 的本地 start/end。
        .invoke(CuperSpmvOnly_StripPtrLoader,
                Batch_num,
                Row_num,
                Iteration_num,
                Column_num,
                SpElement_list_ptr,
                PE_Param[0],
                Matrix_Len_Stream,
                Ptr_Progress_Stream)
#else
        .invoke(CuperSpmvOnly_NullProgressSource,
                Ptr_Progress_Stream)
        // 读取 batch 边界表，把 Batch/Row/Iter/Column 和 start/end 发到 Core 链首。
        .invoke(SpElement_list_ptr_Loader,
                Batch_num,
                Row_num,
                Iteration_num,
                Column_num,
                SpElement_list_ptr,
                PE_Param[0])
#endif
        // 读取输入向量 X；本实验不取负，直接计算 Y=A*X。
        .invoke(Vector_Loader,
                Iteration_num,
                Column_num,
                X,
                Vector_X_Stream[0])
#if defined(JACOBI_SPMV_STRIP_PADDING) || \
    defined(JACOBI_SPMV_COMPACT_PE) || \
    defined(JACOBI_SPMV_LANE_STATIC_REAL)
        // 每个 HBM channel 按自己的总长度读矩阵，剔除跨 channel 的尾部 padding。
        .invoke<tapa::join, HBM_CHANNEL_NUM>(CuperSpmvOnly_MatrixLoaderStrip,
                                             Iteration_num,
                                             Matrix_data,
                                             Matrix_Len_Stream,
                                             Matrix_A_Stream)
#else
        // 每个 Matrix_data HBM channel 各自启动一个 loader。
        .invoke<tapa::join, HBM_CHANNEL_NUM>(Matrix_Loader,
                                             Iteration_num,
                                             Matrix_len,
                                             Matrix_data,
                                             Matrix_A_Stream)
#endif
        // Core 链按当前编译通道数展开：默认 16 路，实验可编 24/32 路。
#ifdef JACOBI_SPMV_COMPACT_PE
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(0)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(1)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(2)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(3)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(4)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(5)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(6)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(7)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(8)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(9)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(10)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(11)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(12)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(13)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(14)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(15)
#ifdef JACOBI_HBM_CHANNELS_GE_24
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(16)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(17)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(18)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(19)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(20)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(21)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(22)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(23)
#endif
#ifdef JACOBI_HBM_CHANNELS_GE_32
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(24)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(25)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(26)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(27)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(28)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(29)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(30)
        CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE(31)
#endif
#elif defined(JACOBI_SPMV_STRIP_PADDING) || defined(JACOBI_SPMV_LANE_STATIC_REAL)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(0)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(1)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(2)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(3)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(4)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(5)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(6)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(7)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(8)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(9)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(10)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(11)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(12)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(13)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(14)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(15)
#ifdef JACOBI_HBM_CHANNELS_GE_24
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(16)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(17)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(18)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(19)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(20)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(21)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(22)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(23)
#endif
#ifdef JACOBI_HBM_CHANNELS_GE_32
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(24)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(25)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(26)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(27)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(28)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(29)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(30)
        CUPER_SPMV_ONLY_INVOKE_CORE_STRIP(31)
#endif
#else
        CUPER_SPMV_ONLY_INVOKE_CORE(0)
        CUPER_SPMV_ONLY_INVOKE_CORE(1)
        CUPER_SPMV_ONLY_INVOKE_CORE(2)
        CUPER_SPMV_ONLY_INVOKE_CORE(3)
        CUPER_SPMV_ONLY_INVOKE_CORE(4)
        CUPER_SPMV_ONLY_INVOKE_CORE(5)
        CUPER_SPMV_ONLY_INVOKE_CORE(6)
        CUPER_SPMV_ONLY_INVOKE_CORE(7)
        CUPER_SPMV_ONLY_INVOKE_CORE(8)
        CUPER_SPMV_ONLY_INVOKE_CORE(9)
        CUPER_SPMV_ONLY_INVOKE_CORE(10)
        CUPER_SPMV_ONLY_INVOKE_CORE(11)
        CUPER_SPMV_ONLY_INVOKE_CORE(12)
        CUPER_SPMV_ONLY_INVOKE_CORE(13)
        CUPER_SPMV_ONLY_INVOKE_CORE(14)
        CUPER_SPMV_ONLY_INVOKE_CORE(15)
#ifdef JACOBI_HBM_CHANNELS_GE_24
        CUPER_SPMV_ONLY_INVOKE_CORE(16)
        CUPER_SPMV_ONLY_INVOKE_CORE(17)
        CUPER_SPMV_ONLY_INVOKE_CORE(18)
        CUPER_SPMV_ONLY_INVOKE_CORE(19)
        CUPER_SPMV_ONLY_INVOKE_CORE(20)
        CUPER_SPMV_ONLY_INVOKE_CORE(21)
        CUPER_SPMV_ONLY_INVOKE_CORE(22)
        CUPER_SPMV_ONLY_INVOKE_CORE(23)
#endif
#ifdef JACOBI_HBM_CHANNELS_GE_32
        CUPER_SPMV_ONLY_INVOKE_CORE(24)
        CUPER_SPMV_ONLY_INVOKE_CORE(25)
        CUPER_SPMV_ONLY_INVOKE_CORE(26)
        CUPER_SPMV_ONLY_INVOKE_CORE(27)
        CUPER_SPMV_ONLY_INVOKE_CORE(28)
        CUPER_SPMV_ONLY_INVOKE_CORE(29)
        CUPER_SPMV_ONLY_INVOKE_CORE(30)
        CUPER_SPMV_ONLY_INVOKE_CORE(31)
#endif
#endif
        // 链尾 drain 和 sort tree 是 one-shot Cuper 的原始写法：它们常驻消费尾流。
        .invoke<tapa::detach>(Destroy_int, PE_Param[HBM_CHANNEL_NUM])
        .invoke<tapa::detach>(Destroy_float_v16, Vector_X_Stream[HBM_CHANNEL_NUM])
#ifdef JACOBI_SPMV_LANE_STATIC_REAL
#ifdef JACOBI_SPMV_OOO_ACCUMULATE
        // 乱序 accumulator 方案 2：静态 8-lane transpose。
        // 每个 Core 的 8 个 slot 固定写到 8 个 owner-lane FIFO；每个 owner
        // 再从 8 条 pair-lane FIFO 中乱序消费。这样保留 output-owner partial
        // sum 设计，同时避免中心 Router 的动态 FIFO 写依赖。
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(0)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(1)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(2)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(3)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(4)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(5)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(6)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(7)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(8)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(9)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(10)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(11)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(12)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(13)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(14)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(15)
#ifdef JACOBI_HBM_CHANNELS_GE_24
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(16)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(17)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(18)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(19)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(20)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(21)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(22)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(23)
#endif
#ifdef JACOBI_HBM_CHANNELS_GE_32
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(24)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(25)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(26)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(27)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(28)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(29)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(30)
        CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER(31)
#endif
#ifdef JACOBI_SPMV_OOO_ACCUMULATE_RTL
        // TAPA custom RTL owner-bank accumulator。每个 owner bank 接 8 条
        // pair-lane 输入流，在 bank 内部做局部乱序累加，对外只输出一条
        // tagged float_v2 stream，减少 128 个细粒度 task 带来的布线拥塞。
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(0)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(1)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(2)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(3)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(4)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(5)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(6)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(7)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(8)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(9)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(10)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(11)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(12)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(13)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(14)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(15)
#ifdef JACOBI_HBM_CHANNELS_GE_24
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(16)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(17)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(18)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(19)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(20)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(21)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(22)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(23)
#endif
#ifdef JACOBI_HBM_CHANNELS_GE_32
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(24)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(25)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(26)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(27)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(28)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(29)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(30)
        CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC(31)
#endif
#else
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(0)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(1)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(2)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(3)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(4)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(5)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(6)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(7)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(8)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(9)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(10)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(11)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(12)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(13)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(14)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(15)
#ifdef JACOBI_HBM_CHANNELS_GE_24
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(16)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(17)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(18)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(19)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(20)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(21)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(22)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(23)
#endif
#ifdef JACOBI_HBM_CHANNELS_GE_32
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(24)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(25)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(26)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(27)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(28)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(29)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(30)
        CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC(31)
#endif
#endif
#else
        // lane-static real 这一代继续向后端推进：accumulator 输出 packet tag，
        // 后级按 tag 直接 scalar scatter 写回 Y，不再依赖旧 checker/sort-tree
        // 的隐式输出顺序，也不再等待 8 个 pair lane 齐包。
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(0)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(1)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(2)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(3)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(4)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(5)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(6)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(7)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(8)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(9)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(10)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(11)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(12)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(13)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(14)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(15)
#ifdef JACOBI_HBM_CHANNELS_GE_24
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(16)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(17)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(18)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(19)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(20)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(21)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(22)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(23)
#endif
#ifdef JACOBI_HBM_CHANNELS_GE_32
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(24)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(25)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(26)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(27)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(28)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(29)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(30)
        CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC(31)
#endif
#endif
#ifdef JACOBI_SPMV_OOO_ACCUMULATE
        .invoke(CuperSpmvOnly_TaggedScatterWriterOoo,
                Iteration_num,
                Row_num,
                Batch_num,
                Matrix_len,
                Column_num,
                Vector_Y_Tagged_Stream,
                Y_out,
                Writer_Progress_Stream)
#else
        .invoke(CuperSpmvOnly_TaggedScatterWriter,
                Iteration_num,
                Row_num,
                Batch_num,
                Matrix_len,
                Column_num,
                Vector_Y_Tagged_Stream,
                Y_out,
                Writer_Progress_Stream)
#endif
#else
#ifdef JACOBI_SPMV_COMPACT_PE
        // compact-PE 版本的 slot 位置不再等于 lane，需要 accumulator 按 lane tag 累加。
        .invoke<tapa::join, HBM_CHANNEL_NUM>(CuperSpmvOnly_AccumulatorCompactPe,
                                             Vector_Y_Param,
                                             Matrix_Mult_Vector_Stream,
                                             Vector_Y_Stream)
#else
        // 每一路 accumulator 完成该 HBM/Core 局部 SpMV 累加。
        .invoke<tapa::join, HBM_CHANNEL_NUM>(Accumulator,
                                             Vector_Y_Param,
                                             Matrix_Mult_Vector_Stream,
                                             Vector_Y_Stream)
#endif
        // 8 路 checker 过滤 padding，随后 sort tree 重新拼成 float_v16。
        .invoke<tapa::join, 8>(Vector_Checker,
                               Iteration_num,
                               Row_num,
                               Vector_Y_Stream,
                               Vector_Y_Stream_Aftck)
        .invoke<tapa::detach>(Mult_Sort_Tree,
                              Vector_Y_Stream_Aftck,
                              Vector_Y_Stream_Ans)
        // 写回 Y，并在最后写 Status/Metrics 完成标记。
        .invoke(CuperSpmvOnly_VectorWriter,
                Iteration_num,
                Row_num,
                Batch_num,
                Matrix_len,
                Column_num,
                Vector_Y_Stream_Ans,
                Y_out,
                Writer_Progress_Stream)
#endif
    ;
}

#undef CUPER_SPMV_ONLY_INVOKE_CORE
#undef CUPER_SPMV_ONLY_INVOKE_CORE_STRIP
#undef CUPER_SPMV_ONLY_INVOKE_CORE_COMPACT_PE
#undef CUPER_SPMV_ONLY_INVOKE_TAGGED_ACC
#undef CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC
#undef CUPER_SPMV_ONLY_INVOKE_OOO_OWNER_ACC
#undef CUPER_SPMV_ONLY_OWNER_LANE_INDEX
#undef CUPER_SPMV_ONLY_INVOKE_OOO_SPLITTER
