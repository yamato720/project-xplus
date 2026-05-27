#pragma once

// Private implementation header for kernels/Cuper.cpp.
// It contains task definitions and should not be included by another translation unit.

#include <algorithm>

#include <ap_int.h>
#include <tapa.h>

#include "cuper_spmv_tasks.hpp"
#include "pcg_common.hpp"

// 本文件把原始一次性 Cuper SpMV task 改造成 CuperPcg 可反复调用的
// “常驻服务”。共同约定：
//
//   1. controller 每需要一次 A*x0 或 A*p，就向 ptr/vector/matrix loader
//      广播一条 CuperSpmvCommand。
//   2. loader/core/accumulator/checker/sort tree 保持原 Cuper 的数据粒度：
//      矩阵 512-bit beat、向量/结果 float_v16、内部部分和 float_v2。
//   3. controller 结束时广播 stop token，所有常驻 task 有限退出，host
//      才能看到 AP_CTRL_HS done。

// PCG 版的 SpElement ptr loader。
//
// 原始 Cuper 顶层只启动一次，所以 loader 读固定参数后顺序跑完。
// CuperPcg 里 SpMV 会被 PCG controller 多次触发，因此 loader 作为
// 常驻服务任务，收到一条 CuperSpmvCommand 就向 PE_Param 重新广播
// Batch/Row/Iteration/Column 和每个 batch 的 SpElement 边界。
void Pcg_SpElement_list_ptr_Loader(const INDEX_TYPE Batch_num,
                                   const INDEX_TYPE Row_num,
                                   const INDEX_TYPE Column_num,
                                   tapa::async_mmap<INDEX_TYPE> &SpElement_list_ptr,
                                   tapa::istream<CuperSpmvCommand> &Command_in,
                                   tapa::ostream<INDEX_TYPE> &PE_Param) {
    for (;;) {
#pragma HLS loop_flatten off
        // read() 是阻塞的：没有 controller 命令时，ptr loader 处于等待状态，
        // 不会提前读取 SpElement_list_ptr。
        const CuperSpmvCommand command = Command_in.read();
        if (command.stop != 0) {
            PE_Param.write(kPcgStopToken);
            return;
        }
        const INDEX_TYPE iteration_time =
            (command.iteration_num == 0) ? 1 : command.iteration_num;

        PE_Param.write(Batch_num);
        PE_Param.write(Row_num);
        PE_Param.write(command.iteration_num);
        PE_Param.write(Column_num);

        const INDEX_TYPE batch_num_plus_1 = Batch_num + 1;
    iter:
        for (INDEX_TYPE iter = 0; iter < iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=1
        read_ptr:
            for (INDEX_TYPE i_request = 0, i_response = 0; i_response < batch_num_plus_1;) {
#pragma HLS loop_tripcount min=1 max=800
#pragma HLS pipeline II=1
                // Async_Read 用 request/response 两个计数器隐藏 HBM 读延迟；
                // 输出顺序仍保持 SpElement_list_ptr[0..Batch_num]。
                Async_Read(SpElement_list_ptr,
                           PE_Param,
                           batch_num_plus_1,
                           i_request,
                           i_response);
            }
        }
    }
}

// PCG 版向量 loader。
//
// standalone TAPA Cuper 的向量输入是 float_v16 packed HBM；早期 CuperPcg
// 让 controller 从 double X/P 逐元素读 16 次再打包，等于把原本的向量
// loader 退化成单 controller 标量读。这里重新让 SpMV 服务从 packed
// X_spmv/P_spmv 读入，目标是让内嵌 SpMV 的 feed 路径接近 single SpMV。
void Pcg_Vector_Loader(const INDEX_TYPE Column_num,
                       tapa::async_mmap<float_v16> &X_spmv,
                       tapa::async_mmap<float_v16> &P_spmv,
                       tapa::istream<CuperSpmvCommand> &Command_in,
                       tapa::ostream<float_v16> &Vector_X_Stream) {
    // 向量以 16 个 float 一包，和 standalone Cuper 的 X HBM 布局一致。
    // Column_num 通常等于 Row_num；这里按 Column_num 是因为 SpMV 语义上
    // 输入向量长度由矩阵列数决定。
    const INDEX_TYPE batch_num_x = ((Column_num + 15) >> 4);

    for (;;) {
#pragma HLS loop_flatten off
        const CuperSpmvCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }
        const INDEX_TYPE iteration_time =
            (command.iteration_num == 0) ? 1 : command.iteration_num;

    iter:
        for (INDEX_TYPE iter = 0; iter < iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=1
        loader_x:
            for (INDEX_TYPE i_request = 0, i_response = 0; i_response < batch_num_x;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
                // 初始化 A*x0 读 X_spmv；迭代 A*p 读 P_spmv。这样 SpMV 输入
                // 不再经过 controller 的逐元素 double->float_v16 打包路径。
                if (command.vector_source == kPcgVectorSourceP) {
                    Async_Read(P_spmv,
                               Vector_X_Stream,
                               batch_num_x,
                               i_request,
                               i_response);
                } else {
                    Async_Read(X_spmv,
                               Vector_X_Stream,
                               batch_num_x,
                               i_request,
                               i_response);
                }
            }
        }
    }
}

// 16 路矩阵 HBM loader 的 PCG 服务版。
//
// 每个 HBM channel 一个实例。收到 controller 发来的命令后，从对应
// Matrix_data[channel] 顺序读 Matrix_len 个 512-bit word，保持原 Cuper
// 的 16 通道矩阵吞吐。
void Pcg_Matrix_Loader(const INDEX_TYPE Matrix_len,
                       tapa::async_mmap<ap_uint<512>> &Matrix_data,
                       tapa::istream<CuperSpmvCommand> &Command_in,
                       tapa::ostream<ap_uint<512>> &Matrix_A_Stream) {
    for (;;) {
#pragma HLS loop_flatten off
        const CuperSpmvCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }
        const INDEX_TYPE iteration_time =
            (command.iteration_num == 0) ? 1 : command.iteration_num;

    iter:
        for (INDEX_TYPE iter = 0; iter < iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=1
        load_a:
            for (INDEX_TYPE i_request = 0, i_response = 0; i_response < Matrix_len;) {
#pragma HLS loop_tripcount min=1 max=10000
#pragma HLS pipeline II=1
                // 每个 Matrix_Loader 只读自己的 Matrix_data[channel]。
                // 16 个实例并行读 16 个 HBM bank，Matrix_len 是每个 channel
                // 的 512-bit word 数。
                Async_Read(Matrix_data,
                           Matrix_A_Stream,
                           Matrix_len,
                           i_request,
                           i_response);
            }
        }
    }
}

// Cuper Core 的常驻服务版。
//
// 计算逻辑保持原 Cuper Core 的结构：每个 channel 载入当前 slice 的
// x 本地缓存，解码 512-bit SpElement 包，完成 8 lane 乘法并输出
// Matrix_Mult_X 给 accumulator。区别是外层 for(;;) 允许 PCG controller
// 多次触发 SpMV。
void Pcg_Core(tapa::istream<INDEX_TYPE>    &PE_Param_in,
              tapa::istream<ap_uint<512> >  &Matrix_A_Stream,
              tapa::istream<float_v16>     &Vector_X_Stream_in,
              tapa::ostream<INDEX_TYPE>    &PE_Param_out,
              tapa::ostream<float_v16>     &Vector_X_Stream_out,
              tapa::ostream<INDEX_TYPE>    &Vector_Y_Param,
              tapa::ostream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream) {
    for (;;) {
#pragma HLS loop_flatten off
        // PE_Param_in 的第一项是 Batch_num 或停止令牌。停止令牌沿 core 链
        // 继续传到 PE_Param_out，让链尾 Destroy_int 也能退出。
        const INDEX_TYPE Batch_num = PE_Param_in.read();
        if (Batch_num == kPcgStopToken) {
            PE_Param_out.write(kPcgStopToken);
            Vector_Y_Param.write(kPcgStopToken);
            return;
        }
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
#pragma HLS loop_tripcount min=1 max=1
            VALUE_TYPE local_X[X_BRAM_DEPTH][Slice_WIDTH];

#pragma HLS bind_storage variable=local_X latency=2
#pragma HLS array_partition variable=local_X complete dim=1
#pragma HLS array_partition variable=local_X cyclic factor=X_PARTITION_FACTOR dim=2

            INDEX_TYPE start_32 = PE_Param_in.read();
            PE_Param_out.write(start_32);
            Vector_Y_Param.write(start_32);

        main:
            for (INDEX_TYPE i = 0; i < Batch_num; ++i) {
#pragma HLS loop_tripcount min=1 max=49
                // 每个 batch 对应一个 slice window。core 先把本 batch 需要的
                // x[col] 范围载入本地 BRAM，再解码本 channel 的矩阵元素。
                const INDEX_TYPE total_vector_packets = (Column_num + 15) >> 4;
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
                        // 向量包在 core 链上逐级转发；每个 core 都看到同一份
                        // packed X/P，但只消费自己矩阵分片需要的列值。
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

            decode:
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
                            // 一个 512-bit beat 打包 8 个 SpElement，每个元素：
                            //   col[13:0], row[17:0], value_bits[31:0]
                            // row 是 Cuper 内部重排编码，不是原始全局行号。
                            ap_uint<64> a = spelement(63 + p * 64, p * 64);
                            ap_uint<14> a_col = a(63, 50);
                            ap_uint<18> a_row = a(49, 32);
                            ap_uint<32> a_val = a(31, 0);

                            matmultx.row[p] = a_row;
                            if (a_row[17] == 0) {
#ifdef FLEX_REUSE
                                VALUE_TYPE val;
                                // FLEX_REUSE 是原 Cuper 的小优化：连续元素可能复用
                                // 上一个 value，减少 bit_cast/寄存器切换。
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
    }
}

// Cuper accumulator 的常驻服务版。
//
// 多个 Core 输出的是按物理 PE 分散的部分和；这里用 URAM 累加成每行
// y 值，再按 Cuper 原输出顺序吐出 float_v2。Pcg_Vector_Checker 和
// Mult_Sort_Tree 后续会重新拼成 float_v16。
void Pcg_Accumulator(tapa::istream<INDEX_TYPE>    &Vector_Y_Param,
                     tapa::istream<Matrix_Mult_X> &Matrix_Mult_Vector_Stream,
                     tapa::ostream<float_v2>      &Vector_Y_Stream) {
#ifdef PINGPONG
    // ping/pong 分别保存偶数行和奇数行的部分和。row 编码来自 host 侧
    // Reordering：bit0 表示奇偶，bit[17:1] 是局部累加地址，bit17=1
    // 表示空元素。这里不是按原始全局 row 直接索引。
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

    for (;;) {
#pragma HLS loop_flatten off
        const INDEX_TYPE Batch_num = Vector_Y_Param.read();
        if (Batch_num == kPcgStopToken) {
            return;
        }
        const INDEX_TYPE Row_num = Vector_Y_Param.read();
        const INDEX_TYPE Iteration_num = Vector_Y_Param.read();
        const INDEX_TYPE Iteration_time = (Iteration_num == 0) ? 1 : Iteration_num;
        const INDEX_TYPE num_v_init =
            (Row_num + HBM_CHANNEL_NUM_MULT_16 - 1) / HBM_CHANNEL_NUM_MULT_16;
        const INDEX_TYPE num_v_out =
            (Row_num + HBM_CHANNEL_NUM_MULT_2 - 1) / HBM_CHANNEL_NUM_MULT_2;

    iter:
        for (INDEX_TYPE iter = 0; iter < Iteration_time; ++iter) {
#pragma HLS loop_flatten off
#pragma HLS loop_tripcount min=1 max=1
        local_part_Y:
            for (int i = 0; i < num_v_init; ++i) {
#pragma HLS loop_tripcount min=1 max=800
#pragma HLS pipeline II=1
                // 每次 SpMV 前清空本 accumulator 的局部部分和。num_v_init
                // 按 16 HBM * 16 float 对齐，而不是简单 Row_num/16。
                for (int p = 0; p < 8; ++p) {
                    local_part_Y_ping[p][i] = 0;
#ifdef PINGPONG
                    local_part_Y_pong[p][i] = 0;
#endif
                }
            }

            INDEX_TYPE start_32 = Vector_Y_Param.read();

        main:
            for (int i = 0; i < Batch_num; ++i) {
#pragma HLS loop_tripcount min=1 max=49
                const INDEX_TYPE end_32 = Vector_Y_Param.read();

            accumulate:
                for (INDEX_TYPE j = start_32; j < end_32;) {
#pragma HLS loop_tripcount min=1 max=200
#pragma HLS pipeline II=1
#pragma HLS dependence true variable=local_part_Y_ping distance=WINDOWS
#ifdef PINGPONG
#pragma HLS dependence true variable=local_part_Y_pong distance=WINDOWS
#endif
                    if (!Matrix_Mult_Vector_Stream.empty()) {
                        Matrix_Mult_X matmultx;
                        Matrix_Mult_Vector_Stream.try_read(matmultx);

                        for (int p = 0; p < 8; ++p) {
                            ap_uint<18> a_row = matmultx.row[p];
#ifdef PINGPONG
                            // a_row[17] 为 1 是 padding/空元素；有效元素按
                            // a_row[0] 分流到 ping/pong，地址使用 a_row(17,1)。
                            // 所以这里的 18-bit row 不是 0..Row_num-1 的全局行号。
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
                // c_idx 在 8 个 lane/PE accumulator bank 间轮转，配合后面的
                // checker/sort tree 重新组装成 float_v16。
                ++c_idx;
                if (c_idx == 8) {
                    c_idx = 0;
                }
            }
        }
    }
}

// 过滤 Cuper accumulator 的补齐输出。
//
// Cuper 内部按 HBM/PE 对齐输出，真实 Row_num 末尾可能不足一个完整包。
// checker 只保留有效范围内的 float_v2，避免 controller 消费到 padding。
void Pcg_Vector_Checker(const INDEX_TYPE Row_num,
                        tapa::istreams<float_v2, HBM_CHANNEL_NUM_DIV_8> &Vector_Y_Stream,
                        tapa::ostream<float_v2> &Vector_Y_Stream_Aftck,
                        tapa::istream<INDEX_TYPE> &Stop_in) {
    const INDEX_TYPE num_pe_output =
        ((Row_num + HBM_CHANNEL_NUM_MULT_2 - 1) / HBM_CHANNEL_NUM_MULT_2) *
        HBM_CHANNEL_NUM_DIV_8;
    const INDEX_TYPE num_out = (Row_num + 15) >> 4;

    for (;;) {
#pragma HLS loop_flatten off
        if (!Stop_in.empty()) {
            INDEX_TYPE stop;
            Stop_in.try_read(stop);
            return;
        }
    out:
        for (INDEX_TYPE i = 0, c_idx = 0, o_idx = 0; i < num_pe_output;) {
#pragma HLS loop_tripcount min=1 max=1800
#pragma HLS pipeline II=1
            if (!Stop_in.empty()) {
                INDEX_TYPE stop;
                Stop_in.try_read(stop);
                return;
            }
            if (!Vector_Y_Stream[c_idx].empty() && !Vector_Y_Stream_Aftck.full()) {
                float_v2 tmp;
                Vector_Y_Stream[c_idx].try_read(tmp);
                // 只保留真实 Row_num 对应的输出包。padding 仍要从上游读掉，
                // 否则 accumulator 会被反压。
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
            }
        }
    }
}

// 把 8 路 float_v2 合并成 1 路 float_v16，恢复 controller 期望的
// “连续 16 行一包”的 SpMV 输出格式。
void Pcg_Mult_Sort_Tree(tapa::istreams<float_v2, 8> &Vector_Y_Stream_Aftck,
                        tapa::ostream<float_v16> &Vector_Y_Stream_Ans,
                        tapa::istream<INDEX_TYPE> &Stop_in) {
    for (;;) {
#pragma HLS pipeline II=1
        if (!Stop_in.empty()) {
            INDEX_TYPE stop;
            Stop_in.try_read(stop);
            return;
        }

        bool all_ready = true;
        for (int i = 0; i < 8; ++i) {
            if (Vector_Y_Stream_Aftck[i].empty()) {
                all_ready = false;
                break;
            }
        }

        if (all_ready && !Vector_Y_Stream_Ans.full()) {
            float_v16 tmpv16;
            for (int i = 0; i < 8; ++i) {
                float_v2 val;
                Vector_Y_Stream_Aftck[i].try_read(val);

                // lane 0/1 来自第 0 路 float_v2，lane 2/3 来自第 1 路，
                // 依次拼成 controller 读取的 A*x0/A*p packet。
                tmpv16[(i << 1)]     = val[0];
                tmpv16[(i << 1) + 1] = val[1];
            }
            Vector_Y_Stream_Ans.try_write(tmpv16);
        }
    }
}
