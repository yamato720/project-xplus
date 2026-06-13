#pragma once

// Cuper SpMV service 的链尾 drain task。
// 16 级 Core 会继续转发 PE_Param 和 Vector_X_Stream，链尾必须有人消费，否则会反压。

#include <tapa.h>

#include "spmv_service_common.hpp"

void SpmvService_DestroyInt(tapa::istream<INDEX_TYPE> &PE_Param) {
    for (;;) {
#pragma HLS pipeline II=1
        // PE_Param 链尾只需要丢弃普通参数；读到停止令牌后退出。
        const INDEX_TYPE tmp = PE_Param.read();
        if (tmp == kSpmvServiceStopToken) {
            return;
        }
    }
}

void SpmvService_DestroyFloatV16(const INDEX_TYPE Batch_num,
                                 const INDEX_TYPE Column_num,
                                 const INDEX_TYPE Max_iters,
                                 tapa::istream<float_v16> &Vector_X_Stream,
                                 tapa::istream<INDEX_TYPE> &Stop_in) {
    const INDEX_TYPE packet_count = spmv_service_num_float_v16_packets(Column_num);
    const unsigned long long expected_packets =
        (Batch_num > 0 && Max_iters > 0)
            ? static_cast<unsigned long long>(packet_count) *
                  static_cast<unsigned long long>(Max_iters)
            : 0ULL;
    unsigned long long drained_packets = 0;
    bool stop_seen = false;

    for (;;) {
#pragma HLS pipeline II=1
        // Vector_X_Stream 没有内嵌 stop token，因此不能在看到 stream 暂时为空时
        // 直接吃 stop 退出；Core15 可能稍后还会转发本轮残余 X 包。这里按轮次数量
        // 精确 drain 完所有应到达链尾的 X 包，再允许 stop 结束，避免链尾提前退出后
        // Core15 写尾流无人消费，导致 host 卡在 Finish。
        // Batch_num=0 表示 R NNZ/Slice Num 为空，Core 不消费也不转发 X，期望包数必须为 0。
        if (drained_packets < expected_packets && !Vector_X_Stream.empty()) {
            float_v16 tmp;
            Vector_X_Stream.try_read(tmp);
            ++drained_packets;
        } else if (!stop_seen && !Stop_in.empty()) {
            INDEX_TYPE stop;
            Stop_in.try_read(stop);
            stop_seen = true;
        } else if (stop_seen && drained_packets >= expected_packets) {
            return;
        }
    }
}
