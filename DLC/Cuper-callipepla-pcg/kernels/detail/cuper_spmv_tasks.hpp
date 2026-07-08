#pragma once

// Cuper SpMV 底层 helper/task 定义。
// 这个头由 kernels/Cuper.cpp 间接包含，保持单 translation unit，方便 TAPA 编译。
//
// 本文件是纯 Cuper one-shot SpMV 数据通路：
//
//   SpElement_list_ptr + Matrix_data[0..15] + X
//       -> loader
//       -> 16 级 Core 链
//       -> Accumulator
//       -> Vector_Checker
//       -> Mult_Sort_Tree
//       -> Y_out
//
// 它不包含 Jacobi/PCG 的 r/z/p/m_inv/alpha/beta/status。当前实验目录的
// `spmv_service_tasks.hpp` 会复用这里的底层 helper，并在 SpmvService_* task 中增加
// command/stop service 协议。

#include <algorithm>
#include <cstdint>

#include <ap_int.h>
#include <tapa.h>

#include "Cuper.h"

struct Matrix_Mult_X {
    // Core 输出给 Accumulator 的一拍局部乘积包。
    //
    // 一个 512-bit Matrix_data beat 解出 8 个 64-bit SpElement slot；每个 slot
    // 产生一个 row 编码和一个 value * x[col] 的局部乘积。row[17]=1 表示 padding，
    // Accumulator 会跳过。
    ap_uint<18> row[8];
    float_v8 val;
};

template <typename T1, typename T2>
inline void Async_Read(tapa::async_mmap<T1> &A,
                       tapa::ostream<T1> &fifo_A,
                       const T2 A_len,
                       T2 &i_request,
                       T2 &i_response
                      ) {

#pragma HLS inline
    // 通用 async_mmap 读壳：尽量发读地址，并在 read_data 可用时转发到 stream。
    // i_request 记录已发起请求数，i_response 记录已收到并写入 FIFO 的数据数。
    if((i_request < A_len) && !A.read_addr.full()) {
        A.read_addr.try_write(i_request);
        ++i_request;
    }
    if(!fifo_A.full() && !A.read_data.empty()) {
        T1 temp;
        A.read_data.try_read(temp);
        fifo_A.try_write(temp);
        ++i_response;
    }
}

inline void Cuper_ReadSpElementPtrPackets(const INDEX_TYPE packet_count,
                                          tapa::async_mmap<INDEX_TYPE> &SpElement_list_ptr,
                                          tapa::ostream<INDEX_TYPE> &PE_Param) {
#pragma HLS inline
    // 读取 batch 边界表 SpElement_list_ptr[0..Batch_num]，写入 PE_Param 链首。
    // 这是一份全局共享索引表，不属于任何 Matrix_data channel。
cuper_read_sp_element_ptr_packets:
    for (INDEX_TYPE i_request = 0, i_response = 0; i_response < packet_count;) {
#pragma HLS loop_tripcount min=1 max=800
#pragma HLS pipeline II=1
        Async_Read(SpElement_list_ptr,
                   PE_Param,
                   packet_count,
                   i_request,
                   i_response);
    }
}

inline void Cuper_ReadFloatV16Packets(const INDEX_TYPE packet_count,
                                      tapa::async_mmap<float_v16> &Vector_in,
                                      tapa::ostream<float_v16> &Vector_X_Stream) {
#pragma HLS inline
    // 读取 packed 输入向量。每个 float_v16 对应 16 个连续 x 元素。
cuper_read_float_v16_packets:
    for (INDEX_TYPE i_request = 0, i_response = 0; i_response < packet_count;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
        Async_Read(Vector_in,
                   Vector_X_Stream,
                   packet_count,
                   i_request,
                   i_response);
    }
}

inline void Cuper_ReadMatrixPackets(const INDEX_TYPE packet_count,
                                    tapa::async_mmap<ap_uint<512>> &Matrix_data,
                                    tapa::ostream<ap_uint<512>> &Matrix_A_Stream) {
#pragma HLS inline
    // 读取单个 HBM channel 的 packed 矩阵流。每个 ap_uint<512> beat
    // 包含 8 个 64-bit SpElement slot。
cuper_read_matrix_packets:
    for (INDEX_TYPE i_request = 0, i_response = 0; i_response < packet_count;) {
#pragma HLS loop_tripcount min=1 max=10000
#pragma HLS pipeline II=1
        Async_Read(Matrix_data,
                   Matrix_A_Stream,
                   packet_count,
                   i_request,
                   i_response);
    }
}

void SpElement_list_ptr_Loader(const INDEX_TYPE Batch_num,
                               const INDEX_TYPE Row_num,
                               const INDEX_TYPE Iteration_num,
                               const INDEX_TYPE Column_num,
                               tapa::async_mmap<INDEX_TYPE> &SpElement_list_ptr,
                               tapa::ostream<INDEX_TYPE> &PE_Param
                              ) {

    // standalone Cuper 的参数/边界 loader。
    //
    // 先向 Core 链广播一次全局参数：
    //   Batch_num, Row_num, Iteration_num, Column_num
    // 随后每次 SpMV iteration 都读取 Batch_num + 1 个 batch 边界：
    //   SpElement_list_ptr[0..Batch_num]
    //
    // Core 和 Accumulator 后续用相邻边界 [ptr[b], ptr[b+1]) 判断第 b 个
    // column-batch 要消费哪些 Matrix_data beat。
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;

    PE_Param.write(Batch_num);
    PE_Param.write(Row_num);
    PE_Param.write(Iteration_num);
    PE_Param.write(Column_num);

    const INDEX_TYPE Batch_num_plus_1 = Batch_num + 1;
iter:
    for(INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
        Cuper_ReadSpElementPtrPackets(Batch_num_plus_1,
                                      SpElement_list_ptr,
                                      PE_Param);
    }
}

void Vector_Loader(const INDEX_TYPE Iteration_num,
                   const INDEX_TYPE Column_num,
                   tapa::async_mmap<float_v16> &X,
                   tapa::ostream<float_v16> &Vector_X_Stream
                  ) {

    // 输入向量 loader。每次 SpMV iteration 都从 X 读取完整 Column_num 长度
    // 的 packed float_v16 向量，并送入 Vector_X_Stream[0]。16 个 Core 会
    // 串接转发同一份 X，每级只缓存当前 batch 需要的列窗口。
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
    const INDEX_TYPE Batch_num_X    = Cuper_NumFloatV16Packets(Column_num);

iter:
    for(INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
        Cuper_ReadFloatV16Packets(Batch_num_X,
                                  X,
                                  Vector_X_Stream);
    }
}

void Matrix_Loader(const INDEX_TYPE Iteration_num,
                   const INDEX_TYPE Matrix_len,
                   tapa::async_mmap<ap_uint<512>> &Matrix_data,
                   tapa::ostream<ap_uint<512>> &Matrix_A_Stream
                  ) {

     // 单 HBM channel 的矩阵 loader。Cuper 顶层会 join 出 16 个实例，
     // 分别读取 Matrix_data_0..15；每个实例每轮读取 Matrix_len 个
     // 512-bit beat。
     const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;

iter:
    for(INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
        Cuper_ReadMatrixPackets(Matrix_len,
                                Matrix_data,
                                Matrix_A_Stream);
    }
}

#ifdef X_TABLE
inline bool Comparator(INDEX_TYPE Data_a,
                       INDEX_TYPE Data_b
                      ) {
#pragma HLS inline
    return Data_a >= Data_b ? true : false;
}
#endif

inline void Cuper_Core_Compute_Round(
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
    // 单个 Core 的一次 SpMV round。
    //
    // 数据组织：
    //   1. 从 PE_Param_in 读取本轮第 0 个 batch 的 start_32；
    //   2. 对每个 column batch：
    //      - 从 Vector_X_Stream_in 读取当前列窗口，缓存到 local_X，并转发给下级 Core；
    //      - 从 PE_Param_in 读取 end_32；
    //      - 消费 Matrix_A_Stream[start_32, end_32) 中的 512-bit beat；
    //      - 每个 beat 解出 8 个 SpElement，计算 val * local_X[col]；
    //      - 输出 Matrix_Mult_X(row[8], val[8]) 给对应 Accumulator。
    //
    // PE_Param 和 Vector_X_Stream 都会继续转发到下一 Core，Matrix_A_Stream
    // 只属于当前 Core/HBM channel。
    VALUE_TYPE local_X[X_BRAM_DEPTH][Slice_WIDTH];

#pragma HLS bind_storage variable=local_X latency=2
#pragma HLS array_partition variable=local_X complete dim=1
#pragma HLS array_partition variable=local_X cyclic factor=X_PARTITION_FACTOR dim=2

    INDEX_TYPE start_32 = PE_Param_in.read();
    PE_Param_out.write(start_32);
    Vector_Y_Param.write(start_32);

cuper_core_main:
    for (INDEX_TYPE i = 0; i < Batch_num; ++i) {
#pragma HLS loop_tripcount min=1 max=49
        const INDEX_TYPE total_vector_packets = Cuper_NumFloatV16Packets(Column_num);
        const INDEX_TYPE start_idx = i * Slice_WIDTH_DIV_16;
        const INDEX_TYPE end_idx = std::min(start_idx + Slice_WIDTH_DIV_16,
                                       total_vector_packets);

    cuper_core_load_vector:
        for (INDEX_TYPE j = start_idx; j < end_idx;) {
#pragma HLS loop_tripcount min=1 max=512
#pragma HLS pipeline II = 1
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

        const INDEX_TYPE end_32 = PE_Param_in.read();
        PE_Param_out.write(end_32);
        Vector_Y_Param.write(end_32);

    cuper_core_decode:
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
                    ap_uint<64> a     = spelement(63 + p * 64, p * 64);
                    ap_uint<14> a_col = a(63, 50);
                    ap_uint<18> a_row = a(49, 32);
                    ap_uint<32> a_val = a(31,  0);

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

void Core(tapa::istream<INDEX_TYPE>    &PE_Param_in,
          tapa::istream<ap_uint<512> >  &Matrix_A_Stream,
          tapa::istream<float_v16>     &Vector_X_Stream_in,
          tapa::ostream<INDEX_TYPE>    &PE_Param_out,
          tapa::ostream<float_v16>     &Vector_X_Stream_out,
          tapa::ostream<INDEX_TYPE>    &Vector_Y_Param,
          tapa::ostream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream
         ) {

    // Core task 壳。读取并转发全局参数，然后按 Iteration_num 重复调用
    // Cuper_Core_Compute_Round。默认 X_TABLE 未开启时，这是纯 one-shot/repeat
    // SpMV Core；没有 PCG command 或 stop token。
    const INDEX_TYPE Batch_num     = PE_Param_in.read();
    const INDEX_TYPE Row_num       = PE_Param_in.read();
    const INDEX_TYPE Iteration_num = PE_Param_in.read();
    const INDEX_TYPE Column_num    = PE_Param_in.read();

    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;

    PE_Param_out.write(Batch_num);
    PE_Param_out.write(Row_num);
    PE_Param_out.write(Iteration_num);
    PE_Param_out.write(Column_num);

    Vector_Y_Param.write(Batch_num);
    Vector_Y_Param.write(Row_num);
    Vector_Y_Param.write(Iteration_num);

#ifdef X_TABLE
    INDEX_TYPE x_table_min[X_TABLE_DEPTH];
    INDEX_TYPE x_table_max[X_TABLE_DEPTH];

#pragma HLS bind_storage variable=x_table_min latency=1
#pragma HLS bind_storage variable=x_table_max latency=1

    for(INDEX_TYPE iter = 0; iter < X_TABLE_ITERATION_NUM; ++iter) {

        INDEX_TYPE start_32 = PE_Param_in.read();
        PE_Param_out.write(start_32);
        Vector_Y_Param.write(start_32);

        for(INDEX_TYPE i = 0; i < Batch_num; ++i) {
#pragma HLS loop_tripcount min=1 max=49

            const INDEX_TYPE end_32 = PE_Param_in.read();
            PE_Param_out.write(end_32);
            Vector_Y_Param.write(end_32);

            for(INDEX_TYPE j = 0; (j < Slice_WIDTH_DIV_16) && (j < Cuper_NumFloatV16Packets(Column_num) - i * Slice_WIDTH_DIV_16); ) {
#pragma HLS loop_tripcount min=1 max=512
#pragma HLS pipeline II = 1
                if(!Vector_X_Stream_in.empty() && !Vector_X_Stream_out.full()) {
                    float_v16 x;
                    Vector_X_Stream_in.try_read(x);
                    Vector_X_Stream_out.try_write(x);
                    ++j;
                }
            }

            INDEX_TYPE x_table_min_a = 8193;
            INDEX_TYPE x_table_max_a = -1;

            for(INDEX_TYPE j = start_32; j < end_32; ) {
                if(!Matrix_A_Stream.empty()) {

                    ap_uint<512> spelement;
                    Matrix_A_Stream.try_read(spelement);
                    Matrix_Mult_X matmultx;

                    for(INDEX_TYPE p = 0; p < 8; ++p) {
                        ap_uint<64> a     = spelement(63 + p * 64, p * 64);
                        ap_uint<14> a_col = a(63, 50);
                        ap_uint<18> a_row = a(49, 32);

                        matmultx.row[p] = a_row;

                        if(a_row[17] == 0) {
                            if(iter == 0) {
                                if(Comparator(x_table_min_a, a_col))
                                    x_table_min_a = a_col;
                                if(Comparator(a_col, x_table_max_a))
                                    x_table_max_a = a_col;
                            }
                        }
                    }
                    Matrix_Mult_Vector_Stream.write(matmultx);
                    ++j;
                }
            }
            start_32 = end_32;
            if(iter == 0) {
                x_table_min[i] = x_table_min_a;
                x_table_max[i] = x_table_max_a;
            }
        }
    }

iter:
    for(INDEX_TYPE iter = 0; iter < Iteration_time - X_TABLE_ITERATION_NUM; ++iter) {
#endif

#ifndef X_TABLE
iter:
    for(INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#endif

#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
        Cuper_Core_Compute_Round(Batch_num,
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

inline void Adder(ap_uint<18> addr,
                  VALUE_TYPE  val_new,
                  ap_uint<64> local_part_Y[URAM_DEPTH]
                 ) {

#pragma HLS inline
    // 非 PINGPONG 路径：一个 64-bit URAM word 同时保存偶/奇两个 FP32 部分和。
    // addr[0] 选择低/高 32 bit，addr[17:1] 是局部累加地址。

    ap_uint<64> part_val_u64     = local_part_Y[addr(17, 1)];
    ap_uint<32> part_val_d0_u32  = part_val_u64(31,  0);
    ap_uint<32> part_val_d1_u32  = part_val_u64(63, 32);
    ap_uint<32> part_val_u32     = (addr[0]) ? part_val_d1_u32 : part_val_d0_u32;

    VALUE_TYPE part_val_plus_new = tapa::bit_cast<VALUE_TYPE>(part_val_u32) + val_new;

    part_val_u32 = tapa::bit_cast<ap_uint<32>>(part_val_plus_new);

    if(addr[0]) {
        part_val_d1_u32 = part_val_u32;
    }
    else {
        part_val_d0_u32 = part_val_u32;
    }

    part_val_u64(63, 32)        = part_val_d1_u32;
    part_val_u64(31,  0)        = part_val_d0_u32;
    local_part_Y[addr(17, 1)]   = part_val_u64;
}

inline void Adder_p(ap_uint<17> addr,
                    VALUE_TYPE  val_new,
                    ap_uint<32> local_part_Y[URAM_DEPTH]
                   ) {

#pragma HLS inline
    // PINGPONG 路径：偶/奇行已经拆到 ping/pong 两个数组中，所以 addr
    // 只作为局部 URAM 索引。
    ap_uint<32> part_val_u32     = local_part_Y[addr];

    VALUE_TYPE part_val_plus_new = tapa::bit_cast<VALUE_TYPE>(part_val_u32) + val_new;
    part_val_u32 = tapa::bit_cast<ap_uint<32> >(part_val_plus_new);

    local_part_Y[addr]   = part_val_u32;
}

#ifdef PINGPONG
inline void Cuper_Accumulator_Compute_Round(
    const INDEX_TYPE Batch_num,
    const INDEX_TYPE Row_num,
    tapa::istream<INDEX_TYPE> &Vector_Y_Param,
    tapa::istream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream,
    tapa::ostream<float_v2> &Vector_Y_Stream,
    ap_uint<32> local_part_Y_ping[8][URAM_DEPTH],
    ap_uint<32> local_part_Y_pong[8][URAM_DEPTH]) {
#else
inline void Cuper_Accumulator_Compute_Round(
    const INDEX_TYPE Batch_num,
    const INDEX_TYPE Row_num,
    tapa::istream<INDEX_TYPE> &Vector_Y_Param,
    tapa::istream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream,
    tapa::ostream<float_v2> &Vector_Y_Stream,
    ap_uint<64> local_part_Y_ping[8][URAM_DEPTH]) {
#endif
#pragma HLS inline
    // 单个 Accumulator 的一次 SpMV round。
    //
    // 它先清零本地部分和数组，再按 batch 边界消费 Matrix_Mult_X：
    //   row[17] == 0: 有效局部乘积，累加到 row 编码对应的槽位；
    //   row[17] == 1: padding，跳过。
    // 最后按 Cuper 输出顺序吐出 float_v2，后续 checker/sort-tree 会过滤和拼包。
    const INDEX_TYPE num_v_init = Cuper_NumAccumulatorInitGroups(Row_num);
    const INDEX_TYPE num_v_out = Cuper_NumAccumulatorOutputs(Row_num);

cuper_acc_local_part_y:
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

cuper_acc_main:
    for (int i = 0; i < Batch_num; ++i) {
#pragma HLS loop_tripcount min=1 max=49
        const INDEX_TYPE end_32 = Vector_Y_Param.read();

    cuper_acc_accumulate:
        for (INDEX_TYPE j = start_32; j < end_32;) {
#pragma HLS loop_tripcount min=1 max=200
#pragma HLS pipeline II=1
#ifndef JACOBI_SPMV_LANE_STATIC_REAL
#pragma HLS dependence true variable=local_part_Y_ping distance=WINDOWS
#ifdef PINGPONG
#pragma HLS dependence true variable=local_part_Y_pong distance=WINDOWS
#endif
#endif
            if (!Matrix_Mult_Vector_Stream.empty()) {
                Matrix_Mult_X matmultx;
                Matrix_Mult_Vector_Stream.try_read(matmultx);

                for (int p = 0; p < 8; ++p) {
                    ap_uint<18> a_row = matmultx.row[p];
#ifdef PINGPONG
                    if (a_row[17] == 0 && a_row[0] == 0)
                        Adder_p(a_row(17, 1), matmultx.val[p], local_part_Y_ping[p]);
                    if (a_row[17] == 0 && a_row[0] == 1)
                        Adder_p(a_row(17, 1), matmultx.val[p], local_part_Y_pong[p]);
#else
                    if (a_row[17] == 0)
                        Adder(a_row, matmultx.val[p], local_part_Y_ping[p]);
#endif
                }
                ++j;
            }
        }
        start_32 = end_32;
    }

cuper_acc_writer:
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

void Accumulator(tapa::istream<INDEX_TYPE>    &Vector_Y_Param,
                 tapa::istream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream,
                 tapa::ostream<float_v2>      &Vector_Y_Stream
                ) {

    // Accumulator task 壳。每个 HBM channel/Core 对应一个 Accumulator 实例。
    // Vector_Y_Param 提供 Batch_num/Row_num/Iteration_num 以及每个 batch 的
    // start/end；Matrix_Mult_Vector_Stream 提供局部乘积。
    const INDEX_TYPE Batch_num      = Vector_Y_Param.read();
    const INDEX_TYPE Row_num        = Vector_Y_Param.read();
    const INDEX_TYPE Iteration_num  = Vector_Y_Param.read();
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
    for(INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
#ifdef PINGPONG
        Cuper_Accumulator_Compute_Round(Batch_num,
                                        Row_num,
                                        Vector_Y_Param,
                                        Matrix_Mult_Vector_Stream,
                                        Vector_Y_Stream,
                                        local_part_Y_ping,
                                        local_part_Y_pong);
#else
        Cuper_Accumulator_Compute_Round(Batch_num,
                                        Row_num,
                                        Vector_Y_Param,
                                        Matrix_Mult_Vector_Stream,
                                        Vector_Y_Stream,
                                        local_part_Y_ping);
#endif
    }
}

inline bool Cuper_TryForwardCheckerValue(
    const INDEX_TYPE num_pe_output,
    const INDEX_TYPE num_out,
    INDEX_TYPE &i,
    INDEX_TYPE &c_idx,
    INDEX_TYPE &o_idx,
    tapa::istreams<float_v2, HBM_CHANNEL_NUM_DIV_8> &Vector_Y_Stream,
    tapa::ostream<float_v2> &Vector_Y_Stream_Aftck) {
#pragma HLS inline
    // Checker 的一个非阻塞转发步骤。它按 16 路 Accumulator 输出的固定顺序
    // 轮询 float_v2，丢弃超出真实 Row_num 的 padding 输出。
    if (!Vector_Y_Stream[c_idx].empty() && !Vector_Y_Stream_Aftck.full()) {
        float_v2 tmp;
        Vector_Y_Stream[c_idx].try_read(tmp);
        if (o_idx < num_out) {
            Vector_Y_Stream_Aftck.try_write(tmp);
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
        return true;
    }
    return false;
}

inline bool Cuper_TryPackFloatV16(tapa::istreams<float_v2, 8> &Vector_Y_Stream_Aftck,
                                  tapa::ostream<float_v16> &Vector_Y_Stream_Ans) {
#pragma HLS inline
    // Sort tree 的一个非阻塞拼包步骤：8 路 float_v2 -> 1 路 float_v16，
    // 恢复成连续 16 个 y 元素的输出包。
    bool all_ready = true;
cuper_pack_ready_check:
    for (int i = 0; i < 8; ++i) {
#pragma HLS unroll
        if (Vector_Y_Stream_Aftck[i].empty()) {
            all_ready = false;
        }
    }

    if (all_ready && !Vector_Y_Stream_Ans.full()) {
        float_v16 tmpv16;
cuper_pack_lanes:
        for (int i = 0; i < 8; ++i) {
#pragma HLS unroll
            float_v2 val;
            Vector_Y_Stream_Aftck[i].try_read(val);
            tmpv16[(i << 1)]     = val[0];
            tmpv16[(i << 1) + 1] = val[1];
        }
        Vector_Y_Stream_Ans.try_write(tmpv16);
        return true;
    }
    return false;
}

void Vector_Checker(const INDEX_TYPE Iteration_num,
                    const INDEX_TYPE Row_num,
                    tapa::istreams<float_v2, HBM_CHANNEL_NUM_DIV_8> &Vector_Y_Stream,
                    tapa::ostream<float_v2> &Vector_Y_Stream_Aftck
                   ) {

    // 过滤 accumulator 为对齐产生的多余 float_v2。num_pe_output 是硬件内部
    // 对齐后的输出数，num_out 是真实 Row_num 需要的 float_v16 包数。
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
    const INDEX_TYPE num_pe_output = Cuper_NumCheckerPeOutputs(Row_num);
    const INDEX_TYPE num_out = Cuper_NumFloatV16Packets(Row_num);
    const INDEX_TYPE num_ite_Y = num_pe_output * Iteration_time;
out:
    for (INDEX_TYPE i = 0, c_idx = 0, o_idx = 0; i < num_ite_Y;) {
#pragma HLS loop_tripcount min=1 max=1800
#pragma HLS pipeline II=1
        (void)Cuper_TryForwardCheckerValue(num_pe_output,
                                           num_out,
                                           i,
                                           c_idx,
                                           o_idx,
                                           Vector_Y_Stream,
                                           Vector_Y_Stream_Aftck);
    }
}
void Mult_Sort_Tree(tapa::istreams<float_v2, 8> &Vector_Y_Stream_Aftck,
                    tapa::ostream<float_v16>    &Vector_Y_Stream_Ans
                   ) {
    // 常驻拼包 task。Cuper 顶层用 detach 启动它；Vector_Writer 写够 Row_num
    // 对应的输出包后，kernel 的 join task 自然完成。
    for(;;) {
#pragma HLS pipeline II=1
        (void)Cuper_TryPackFloatV16(Vector_Y_Stream_Aftck, Vector_Y_Stream_Ans);
    }
}
void Vector_Writer(const INDEX_TYPE Iteration_num,
                   const INDEX_TYPE Row_num,
                   tapa::istream<float_v16> &Vector_Y_Stream_Ans,
                   tapa::async_mmap<float_v16> &Y_out
                  ) {
    // 最终写回 task。每轮 SpMV 写 Cuper_NumFloatV16Packets(Row_num) 个
    // float_v16 到 Y_out；最后一个包的尾部 lane 可能是 padding。
    const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
    const INDEX_TYPE num_ite_Y = Cuper_NumFloatV16Packets(Row_num);

iter:
    for(INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=16
    write_Y:
        for(INDEX_TYPE i_request = 0, i_response = 0; i_response < num_ite_Y;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
            if((i_request < num_ite_Y) && !Vector_Y_Stream_Ans.empty() && !Y_out.write_addr.full() && !Y_out.write_data.full() ) {
                Y_out.write_addr.try_write(i_request);
                float_v16 tmpv16;
                Vector_Y_Stream_Ans.try_read(tmpv16);
                Y_out.write_data.try_write(tmpv16);
                ++i_request;
            }
            uint8_t n_resp;
            if(Y_out.write_resp.try_read(n_resp)) {
                i_response += int(n_resp) + 1;
            }
        }
    }

}

void Destroy_int(tapa::istream<INDEX_TYPE> &PE_Param) {
    // 消费 16 级 Core 链尾的 PE_Param，避免最后一级 Core 因尾流无人读而反压。
    for(;;) {
#pragma HLS pipeline II=1
        INDEX_TYPE tmp;
        PE_Param.try_read(tmp);
    }
}

void Destroy_float_v16(tapa::istream<float_v16> &Vector_X_Stream) {
    // 消费 16 级 Core 链尾的 X 向量流，避免最后一级 Core 因尾流无人读而反压。
    for(;;) {
#pragma HLS pipeline II=1
        float_v16 tmp;
        Vector_X_Stream.try_read(tmp);
    }
}
