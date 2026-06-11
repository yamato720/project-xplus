#pragma once

// Jacobi 向量更新 service。
// 它消费 Cuper SpMV service 输出的 -R*x_old，并计算 (b + (-R*x_old)) * D^{-1}。

#include <tapa.h>

#include "jacobi_common.hpp"

void Jacobi_Update_Service(tapa::istream<JacobiFrame> &Frame_in,
                           tapa::istream<float_v16> &Spmv_in,
                           tapa::ostream<JacobiUpdateResult> &Result_out,
                           tapa::async_mmap<float_v16> &B,
                           tapa::async_mmap<float_v16> &Diag_inv,
                           tapa::async_mmap<float_v16> &X0,
                           tapa::async_mmap<float_v16> &X1) {
    for (;;) {
#pragma HLS loop_flatten off
        // 每轮先等 controller 发 frame。frame.stop 是本 task 唯一退出条件，
        // 正常帧中的 packet_count 表示本轮要消费多少个 -Rx 包。
        const JacobiFrame frame = Frame_in.read();
        if (frame.stop != 0) {
            return;
        }

        float diff_max = 0.0f;
        INDEX_TYPE breakdown = 0;

    update_packets:
        for (INDEX_TYPE packet = 0, write_request = 0, write_response = 0;
             write_response < frame.packet_count;) {
#pragma HLS loop_tripcount min=1 max=500000
#pragma HLS pipeline II=1
            // 尽量提前发起 B/Diag_inv/x_old 的 mmap 读请求。
            // SpMV 输出 -Rx 和这些 mmap read_data 到齐后，才会实际计算并写回 x_next。
            if (packet < frame.packet_count &&
                !Spmv_in.empty() &&
                !B.read_addr.full() &&
                !Diag_inv.read_addr.full() &&
                !X0.read_addr.full() &&
                !X1.read_addr.full()) {
                B.read_addr.try_write(packet);
                Diag_inv.read_addr.try_write(packet);
                if (frame.read_from_x1 != 0) {
                    X1.read_addr.try_write(packet);
                } else {
                    X0.read_addr.try_write(packet);
                }
                ++packet;
            }

            // common_ready 覆盖 -Rx、b、diag_inv；x_ready 单独按本轮旧解
            // buffer 选择 X0 或 X1；write_ready 单独按新解 buffer 选择写端口。
            const bool common_ready =
                !Spmv_in.empty() &&
                !B.read_data.empty() &&
                !Diag_inv.read_data.empty();
            const bool x_ready = (frame.read_from_x1 != 0) ? !X1.read_data.empty()
                                                           : !X0.read_data.empty();
            const bool write_ready = (frame.write_to_x1 != 0)
                                         ? (!X1.write_addr.full() && !X1.write_data.full())
                                         : (!X0.write_addr.full() && !X0.write_data.full());

            if (write_request < frame.packet_count && common_ready && x_ready && write_ready) {
                float_v16 neg_rx;
                float_v16 b;
                float_v16 diag_inv;
                float_v16 x_old;
                float_v16 x_next;

                Spmv_in.try_read(neg_rx);
                B.read_data.try_read(b);
                Diag_inv.read_data.try_read(diag_inv);
                if (frame.read_from_x1 != 0) {
                    X1.read_data.try_read(x_old);
                } else {
                    X0.read_data.try_read(x_old);
                }

            lanes:
                for (INDEX_TYPE lane = 0; lane < 16; ++lane) {
#pragma HLS unroll
                    // Cuper service 的 Matrix_data 已在 host 侧拆成 R=A-D，
                    // Jacobi_Vector_Loader 又把输入 x_old 取负，因此这里收到的是 -Rx：
                    // x_next = (b + (-Rx)) * diag_inv。
                    // 最后一包中越界 lane 写 0，避免 padding 污染最终 buffer。
                    const INDEX_TYPE row = (write_request << 4) + lane;
                    const bool valid = row < frame.row_num;
                    const float next =
                        (b[lane] + neg_rx[lane]) * diag_inv[lane];
                    x_next[lane] = valid ? next : 0.0f;
                    if (valid) {
                        const float diff = Jacobi_AbsFloat(next - x_old[lane]);
                        if (diff > diff_max) {
                            diff_max = diff;
                        }
                        if (Jacobi_InvalidFloat(next) || Jacobi_InvalidFloat(diag_inv[lane])) {
                            breakdown = 1;
                        }
                    }
                }

                // 新解写到和旧解相反的 buffer。写地址按 float_v16 包编号，
                // 因此 host 侧也按 16 个 float 一组对齐分配 X0/X1。
                if (frame.write_to_x1 != 0) {
                    X1.write_addr.try_write(write_request);
                    X1.write_data.try_write(x_next);
                } else {
                    X0.write_addr.try_write(write_request);
                    X0.write_data.try_write(x_next);
                }
                ++write_request;
            }

            // async_mmap 写响应可能合并返回；num_responses 表示额外完成数量，
            // 所以实际完成数是 num_responses + 1。
            uint8_t num_responses = 0;
            if (frame.write_to_x1 != 0) {
                if (X1.write_resp.try_read(num_responses)) {
                    write_response += int(num_responses) + 1;
                }
            } else {
                if (X0.write_resp.try_read(num_responses)) {
                    write_response += int(num_responses) + 1;
                }
            }
        }

        // 一轮所有包写回完成后，把 diff_max/breakdown/final buffer 回传给 controller。
        JacobiUpdateResult result;
        result.diff_max = diff_max;
        result.breakdown = breakdown;
        result.wrote_x1 = frame.write_to_x1;
        result.iter = frame.iter;
        Result_out.write(result);
    }
}
