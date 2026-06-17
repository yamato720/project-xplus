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
    // 8 路 checker/sort tree 仍按 Cuper 原始输出规整逻辑工作。
    // 对 16/24/32 路而言，每个 checker 分别消费 2/3/4 路 accumulator。
    tapa::streams<float_v2, 8, FIFO_DEPTH>                 Vector_Y_Stream_Aftck("Vector_Y_Stream_Aftck");
    tapa::stream<float_v16, FIFO_DEPTH>                    Vector_Y_Stream_Ans("Vector_Y_Stream_Ans");

    tapa::task()
        // 读取 batch 边界表，把 Batch/Row/Iter/Column 和 start/end 发到 Core 链首。
        .invoke(SpElement_list_ptr_Loader,
                Batch_num,
                Row_num,
                Iteration_num,
                Column_num,
                SpElement_list_ptr,
                PE_Param[0])
        // 读取输入向量 X；本实验不取负，直接计算 Y=A*X。
        .invoke(Vector_Loader,
                Iteration_num,
                Column_num,
                X,
                Vector_X_Stream[0])
        // 每个 Matrix_data HBM channel 各自启动一个 loader。
        .invoke<tapa::join, HBM_CHANNEL_NUM>(Matrix_Loader,
                                             Iteration_num,
                                             Matrix_len,
                                             Matrix_data,
                                             Matrix_A_Stream)
        // Core 链按当前编译通道数展开：默认 16 路，实验可编 24/32 路。
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
