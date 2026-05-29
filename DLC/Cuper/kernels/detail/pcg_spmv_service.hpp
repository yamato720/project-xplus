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
//
// 当前边界：
//   - 本文件只服务 full CuperPcg(...) 的常驻 SpMV service。
//   - single SpMV demo CuperPcgSpmv(...) 已回到 Cuper(...) 风格的一次性 task graph，
//     不再复用这里的 Pcg_Single* controller/command/stop 包装层。
//   - 因此 PCG 控制优化只在本文件、pcg_controller.hpp 和相关 drain/timer
//     路径处理；single SpMV demo 只用于纯 SpMV 口径测试。

// PCG 版的 SpElement ptr loader。
//
// 原始 Cuper 顶层只启动一次，所以 loader 读固定参数后顺序跑完。
// CuperPcg 里 SpMV 会被 PCG controller 多次触发，因此 loader 作为
// 常驻服务任务，收到一条 CuperSpmvCommand 就向 PE_Param 重新广播
// Batch/Row/Column 和每个 batch 的 SpElement 边界。
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
        PE_Param.write(Batch_num);
        PE_Param.write(Row_num);
        PE_Param.write(Column_num);

        const INDEX_TYPE batch_num_plus_1 = Batch_num + 1;
        // 复用 standalone Cuper 的 HBM 读循环；以后优化 ptr loader 的
        // request/response 流控时，single SpMV 和 PCG service 同步生效。
        Cuper_ReadSpElementPtrPackets(batch_num_plus_1,
                                      SpElement_list_ptr,
                                      PE_Param);
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
    const INDEX_TYPE batch_num_x = pcg_num_float_v16_packets(Column_num);

    for (;;) {
#pragma HLS loop_flatten off
        const CuperSpmvCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }

        // 初始化 A*x0 读 X_spmv；迭代 A*p 读 P_spmv。这样 SpMV 输入
        // 不再经过 controller 的逐元素 double->float_v16 打包路径。
        if (command.vector_source == kPcgVectorSourceP) {
            Cuper_ReadFloatV16Packets(batch_num_x, P_spmv, Vector_X_Stream);
        } else {
            Cuper_ReadFloatV16Packets(batch_num_x, X_spmv, Vector_X_Stream);
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

        // 每个 Matrix_Loader 只读自己的 Matrix_data[channel]。
        // 16 个实例并行读 16 个 HBM bank，Matrix_len 是每个 channel
        // 的 512-bit word 数。读循环和 standalone Cuper 共用。
        Cuper_ReadMatrixPackets(Matrix_len,
                                Matrix_data,
                                Matrix_A_Stream);
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
        const INDEX_TYPE Column_num = PE_Param_in.read();

        PE_Param_out.write(Batch_num);
        PE_Param_out.write(Row_num);
        PE_Param_out.write(Column_num);

        Vector_Y_Param.write(Batch_num);
        Vector_Y_Param.write(Row_num);

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

// 过滤 Cuper accumulator 的补齐输出。
//
// Cuper 内部按 HBM/PE 对齐输出，真实 Row_num 末尾可能不足一个完整包。
// checker 只保留有效范围内的 float_v2，避免 controller 消费到 padding。
void Pcg_Vector_Checker(const INDEX_TYPE Row_num,
                        tapa::istreams<float_v2, HBM_CHANNEL_NUM_DIV_8> &Vector_Y_Stream,
                        tapa::ostream<float_v2> &Vector_Y_Stream_Aftck,
                        tapa::istream<INDEX_TYPE> &Stop_in) {
    const INDEX_TYPE num_pe_output = pcg_num_checker_pe_outputs(Row_num);
    const INDEX_TYPE num_out = pcg_num_float_v16_packets(Row_num);

    for (;;) {
#pragma HLS loop_flatten off
    wait_round:
        for (;;) {
#pragma HLS pipeline II=1
            if (!Vector_Y_Stream[0].empty()) {
                break;
            }
            if (!Stop_in.empty()) {
                INDEX_TYPE stop;
                Stop_in.try_read(stop);
                return;
            }
        }
    out:
        for (INDEX_TYPE i = 0, c_idx = 0, o_idx = 0; i < num_pe_output;) {
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

        (void)Cuper_TryPackFloatV16(Vector_Y_Stream_Aftck, Vector_Y_Stream_Ans);
    }
}
