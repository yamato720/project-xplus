#pragma once

// Private implementation header for kernels/Cuper.cpp.
// It contains task definitions and should not be included by another translation unit.

#include <tapa.h>

#include "pcg_common.hpp"

void Pcg_Destroy_int(tapa::istream<INDEX_TYPE> &PE_Param) {
    for (;;) {
#pragma HLS pipeline II=1
        const INDEX_TYPE tmp = PE_Param.read();
        if (tmp == kPcgStopToken) {
            return;
        }
    }
}

void Pcg_Destroy_float_v16(tapa::istream<float_v16> &Vector_X_Stream,
                           tapa::istream<INDEX_TYPE> &Stop_in) {
    for (;;) {
#pragma HLS pipeline II=1
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
