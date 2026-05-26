#include "Cuper.h"

// TAPA Cuper 共享 kernel 入口。
//
// Project-XPlus 当前只把这里的两个顶层作为四条 Cuper 主线的一部分：
//   1. Cuper    ：TAPA Cuper / single SpMV，host 可选择 spmv-only 或兼容 host-PCG。
//   2. CuperPcg ：TAPA Cuper / FPGA-PCG，PCG 控制进入 TAPA task graph。
//
// 具体 task 实现拆在 detail/ 下：
//   - cuper_spmv_tasks.hpp：普通 Cuper SpMV 流水。
//   - cuper_pcg_tasks.hpp ：FPGA-PCG 常驻服务和 controller。
//   - cuper_top_graphs.hpp：Cuper / CuperPcg 顶层 task graph。
#include "detail/cuper_top_graphs.hpp"
