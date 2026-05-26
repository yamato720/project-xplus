#pragma once

// Private implementation header for kernels/Cuper.cpp.
// It contains task definitions and should not be included by another translation unit.

#include <ap_int.h>
#include <tapa.h>

#include "pcg_common.hpp"

void Pcg_Stage_Timer(tapa::istream<PcgStageEvent> &Stage_Event_in,
                     tapa::ostream<ap_uint<64>> &Stage_Ticks_out) {
    ap_uint<64> now = 0;
    ap_uint<64> start[kPcgStageCount];
    ap_uint<64> elapsed[kPcgStageCount];
#pragma HLS array_partition variable=start complete
#pragma HLS array_partition variable=elapsed complete

init_stage_timer_arrays:
    for (INDEX_TYPE index = 0; index < kPcgStageCount; ++index) {
#pragma HLS unroll
        start[index] = 0;
        elapsed[index] = 0;
    }

stage_timer_loop:
    for (;;) {
#pragma HLS pipeline II=1
        ++now;
        if (!Stage_Event_in.empty()) {
            PcgStageEvent event;
            Stage_Event_in.try_read(event);
            if (event.op == kPcgStageStop) {
                break;
            }
            if (event.stage >= 0 && event.stage < kPcgStageCount) {
                if (event.op == kPcgStageBegin) {
                    start[event.stage] = now;
                } else if (event.op == kPcgStageEnd) {
                    elapsed[event.stage] += now - start[event.stage];
                }
            }
        }
    }

write_stage_timer_metrics:
    for (INDEX_TYPE index = 0; index < kPcgStageCount; ++index) {
#pragma HLS pipeline II=1
        Stage_Ticks_out.write(elapsed[index]);
    }
    Stage_Ticks_out.write(now);
}
