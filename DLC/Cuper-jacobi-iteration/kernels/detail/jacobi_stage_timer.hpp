#pragma once

// Jacobi timing task。
// controller 只发送 begin/end/stop 事件；这个常驻 task 独立累加 cycle，
// 避免 controller 阻塞等待 update result 时漏计 SpMV/update 流水时间。

#include <ap_int.h>
#include <tapa.h>

#include "jacobi_common.hpp"

void Jacobi_Stage_Timer(tapa::istream<JacobiStageEvent> &Stage_Event_in,
                        tapa::ostream<ap_uint<64>> &Stage_Ticks_out) {
    ap_uint<64> now = 0;
    ap_uint<64> start[kJacobiStageCount];
    ap_uint<64> elapsed[kJacobiStageCount];
#pragma HLS array_partition variable=start complete
#pragma HLS array_partition variable=elapsed complete

init_jacobi_stage_timer:
    for (INDEX_TYPE index = 0; index < kJacobiStageCount; ++index) {
#pragma HLS unroll
        start[index] = 0;
        elapsed[index] = 0;
    }

jacobi_stage_timer_loop:
    for (;;) {
#pragma HLS pipeline II=1
        ++now;
        if (!Stage_Event_in.empty()) {
            JacobiStageEvent event = {0, 0};
            Stage_Event_in.try_read(event);
            if (event.op == kJacobiStageStop) {
                break;
            }
            if (event.stage >= 0 && event.stage < kJacobiStageCount) {
                if (event.op == kJacobiStageBegin) {
                    start[event.stage] = now;
                } else if (event.op == kJacobiStageEnd) {
                    elapsed[event.stage] += now - start[event.stage];
                }
            }
        }
    }

write_jacobi_stage_timer_metrics:
    for (INDEX_TYPE index = 0; index < kJacobiStageCount; ++index) {
#pragma HLS pipeline II=1
        Stage_Ticks_out.write(elapsed[index]);
    }
    Stage_Ticks_out.write(now);
}
