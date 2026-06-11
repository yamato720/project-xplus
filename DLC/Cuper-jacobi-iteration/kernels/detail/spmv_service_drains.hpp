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

void SpmvService_DestroyFloatV16(tapa::istream<float_v16> &Vector_X_Stream,
                                 tapa::istream<INDEX_TYPE> &Stop_in) {
    for (;;) {
#pragma HLS pipeline II=1
        // Vector_X_Stream 没有内嵌 stop token，所以单独用 Stop_in 退出。
        // 优先消费残留 X 包，避免最后一级 Core 因链尾堵塞而卡住。
        if (!Vector_X_Stream.empty()) {
            float_v16 tmp;
            Vector_X_Stream.try_read(tmp);
        } else if (!Stop_in.empty()) {
            INDEX_TYPE stop;
            Stop_in.try_read(stop);
            return;
        }
    }
}
