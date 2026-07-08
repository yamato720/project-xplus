#pragma once

#include <ap_int.h>
#include <tapa.h>

#include "pcg_callipepla_common.hpp"

void PcgCallipepla_Stage_Timer(
    tapa::istream<PcgCallipeplaStageEvent> &Stage_Event_in,
    tapa::ostream<ap_uint<64>> &Stage_Ticks_out) {
    ap_uint<64> now = 0;
    ap_uint<64> start[kPcgCallipeplaStageCount];
    ap_uint<64> elapsed[kPcgCallipeplaStageCount];
#pragma HLS array_partition variable=start complete
#pragma HLS array_partition variable=elapsed complete

init_stage_timer:
    for (INDEX_TYPE index = 0; index < kPcgCallipeplaStageCount; ++index) {
#pragma HLS unroll
        start[index] = 0;
        elapsed[index] = 0;
    }

stage_timer_loop:
    for (;;) {
#pragma HLS pipeline II=1
        ++now;
        if (!Stage_Event_in.empty()) {
            PcgCallipeplaStageEvent event = {0, 0};
            Stage_Event_in.try_read(event);
            if (event.op == kPcgCallipeplaStageStop) {
                break;
            }
            if (event.stage >= 0 && event.stage < kPcgCallipeplaStageCount) {
                if (event.op == kPcgCallipeplaStageBegin) {
                    start[event.stage] = now;
                } else if (event.op == kPcgCallipeplaStageEnd) {
                    elapsed[event.stage] += now - start[event.stage];
                }
            }
        }
    }

write_stage_ticks:
    for (INDEX_TYPE index = 0; index < kPcgCallipeplaStageCount; ++index) {
#pragma HLS pipeline II=1
        Stage_Ticks_out.write(elapsed[index]);
    }
    Stage_Ticks_out.write(now);
}
