#pragma once

// Jacobi 专用输入向量 loader。
// 它把 Cuper SpMV 的单输入 X 改成 X0/X1 双缓冲选择，并在读入时取负。
// 后级 Core 数据通路仍只看到一条普通的 Vector_X_Stream。

#include <tapa.h>

#include "cuper_spmv_tasks.hpp"
#include "jacobi_common.hpp"

inline void Jacobi_ReadNegFloatV16Packets(const INDEX_TYPE packet_count,
                                          tapa::async_mmap<float_v16> &Vector_in,
                                          tapa::ostream<float_v16> &Vector_X_Stream) {
#pragma HLS inline
    // Jacobi 当前让 host 侧矩阵只保留 R=A-D。
    // 这里把 x_old 取负后送进 Cuper Core，后级 SpMV 自然得到 -R*x_old。
jacobi_read_neg_float_v16_packets:
    for (INDEX_TYPE i_request = 0, i_response = 0; i_response < packet_count;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
        if ((i_request < packet_count) && !Vector_in.read_addr.full()) {
            Vector_in.read_addr.try_write(i_request);
            ++i_request;
        }

        if (!Vector_X_Stream.full() && !Vector_in.read_data.empty()) {
            float_v16 x_old;
            float_v16 neg_x;
            Vector_in.read_data.try_read(x_old);
        lanes:
            for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
#pragma HLS unroll
                neg_x[lane] = -x_old[lane];
            }
            Vector_X_Stream.try_write(neg_x);
            ++i_response;
        }
    }
}

// 双缓冲 vector loader：SpMV service 仍由 command 触发和 stop 退出。
// command.vector_source 只表示本轮 x_old 来自哪个 buffer，不表示写回位置；
// 写回位置由 Jacobi_Update_Service 根据 JacobiFrame 决定。
void Jacobi_Vector_Loader(const INDEX_TYPE Column_num,
                          tapa::async_mmap<float_v16> &X0,
                          tapa::async_mmap<float_v16> &X1,
                          tapa::istream<CuperSpmvServiceCommand> &Command_in,
                          tapa::ostream<float_v16> &Vector_X_Stream) {
    // Cuper Core 链按 float_v16 消费输入向量，每包 16 个连续列元素。
    const INDEX_TYPE packet_count = spmv_service_num_float_v16_packets(Column_num);

    for (;;) {
#pragma HLS loop_flatten off
        const CuperSpmvServiceCommand command = Command_in.read();
        if (command.stop != 0) {
            return;
        }

        // 这里只切换 HBM 读源并取负，输出 stream 格式保持 Cuper 原来的 Vector_X_Stream。
        if (command.vector_source == kJacobiBufferX1) {
            Jacobi_ReadNegFloatV16Packets(packet_count,
                                          X1,
                                          Vector_X_Stream);
        } else {
            Jacobi_ReadNegFloatV16Packets(packet_count,
                                          X0,
                                          Vector_X_Stream);
        }
    }
}
