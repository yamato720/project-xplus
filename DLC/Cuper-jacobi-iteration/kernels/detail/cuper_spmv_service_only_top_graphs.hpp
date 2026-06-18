#pragma once

// Cuper SpMV service-only 顶层。
//
// 这个文件用于 24/32 路 Cuper SpMV 隔离实验：只保留 Cuper 的 one-shot
// SpMV 数据通路，不接 Jacobi 的 b/diag/update/controller，也不接 PCG 控制。

#include <ap_int.h>
#include <tapa.h>

#include "cuper_spmv_tasks.hpp"

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

#ifdef JACOBI_SPMV_STRIP_PADDING
void CuperSpmvOnly_StripPtrLoader(
    const INDEX_TYPE Batch_num,
    const INDEX_TYPE Row_num,
    const INDEX_TYPE Iteration_num,
    const INDEX_TYPE Column_num,
    tapa::async_mmap<INDEX_TYPE> &SpElement_list_ptr,
    tapa::ostream<INDEX_TYPE> &PE_Param,
    tapa::ostreams<INDEX_TYPE, HBM_CHANNEL_NUM> &Matrix_Len_Stream) {
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

void CuperSpmvOnly_VectorWriter(const INDEX_TYPE Iteration_num,
                                const INDEX_TYPE Row_num,
                                const INDEX_TYPE Batch_num,
                                const INDEX_TYPE Matrix_len,
                                const INDEX_TYPE Column_num,
                                tapa::istream<float_v16> &Vector_Y_Stream_Ans,
                                tapa::async_mmap<float_v16> &Y_out,
                                tapa::async_mmap<INDEX_TYPE> &Status,
                                tapa::async_mmap<double> &Metrics) {
    // 和普通 Vector_Writer 一样按地址顺序写 Y_out；不同点是最终等待所有
    // write response 后再写 Status/Metrics，方便上板确认完整 SpMV 是否自然结束。
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
    const INDEX_TYPE num_ite_Y = Cuper_NumFloatV16Packets(Row_num);

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
                ++i_request;
            }

            uint8_t num_responses = 0;
            if (Y_out.write_resp.try_read(num_responses)) {
                i_response += int(num_responses) + 1;
            }
        }
    }

    CuperSpmvOnly_WriteStatus(Status, Iteration_time, Row_num);
    CuperSpmvOnly_WriteMetrics(Metrics,
                               Batch_num,
                               Matrix_len,
                               Row_num,
                               Column_num,
                               Iteration_time);
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

void CuperSpmvServiceOnly(tapa::mmap<INDEX_TYPE> SpElement_list_ptr,
                          tapa::mmaps<ap_uint<512>, HBM_CHANNEL_NUM> Matrix_data,
                          tapa::mmap<float_v16> X,
                          tapa::mmap<float_v16> Y_out,
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
    tapa::streams<float_v2, HBM_CHANNEL_NUM, 1024>         Vector_Y_Stream("Vector_Y_Stream");
#ifdef JACOBI_SPMV_STRIP_PADDING
    tapa::streams<INDEX_TYPE, HBM_CHANNEL_NUM, 2>           Matrix_Len_Stream("Matrix_Len_Stream");
#endif
    // 8 路 checker/sort tree 仍按 Cuper 原始输出规整逻辑工作。
    // 对 16/24/32 路而言，每个 checker 分别消费 2/3/4 路 accumulator。
    tapa::streams<float_v2, 8, FIFO_DEPTH>                 Vector_Y_Stream_Aftck("Vector_Y_Stream_Aftck");
    tapa::stream<float_v16, FIFO_DEPTH>                    Vector_Y_Stream_Ans("Vector_Y_Stream_Ans");

    tapa::task()
#ifdef JACOBI_SPMV_STRIP_PADDING
        // 去 padding 版本：ptr 表先给每个 Matrix loader 一路独立总长度，
        // 再按 HBM channel 分发每个 batch 的本地 start/end。
        .invoke(CuperSpmvOnly_StripPtrLoader,
                Batch_num,
                Row_num,
                Iteration_num,
                Column_num,
                SpElement_list_ptr,
                PE_Param[0],
                Matrix_Len_Stream)
#else
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
#ifdef JACOBI_SPMV_STRIP_PADDING
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
#ifdef JACOBI_SPMV_STRIP_PADDING
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
        // 每一路 accumulator 完成该 HBM/Core 局部 SpMV 累加。
        .invoke<tapa::join, HBM_CHANNEL_NUM>(Accumulator,
                                             Vector_Y_Param,
                                             Matrix_Mult_Vector_Stream,
                                             Vector_Y_Stream)
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
                Status,
                Metrics)
    ;
}

#undef CUPER_SPMV_ONLY_INVOKE_CORE
#undef CUPER_SPMV_ONLY_INVOKE_CORE_STRIP
