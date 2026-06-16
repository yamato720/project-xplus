#include "Cuper.h"

// Cuper Jacobi iteration 实验入口。
//
// 这个子项目从 DLC/Cuper 拆出，当前保留 service 化 Cuper SpMV，并在
// Cuper checker 输出端融合 Jacobi 更新。原 full-PCG controller 和
// standalone one-shot Cuper 顶层不在本目录维护。
//
// 具体 task 实现拆在 detail/ 下：
//   - cuper_spmv_tasks.hpp        ：Cuper SpMV 底层 helper。
//   - spmv_service_tasks.hpp        ：中性命名的常驻 SpMV service task。
//   - jacobi_cuper_output_update.hpp：Cuper 输出拼包、Jacobi 更新和写回。
//   - cuper_jacobi_top_graphs.hpp ：当前实验顶层 task graph。
#include "detail/cuper_jacobi_top_graphs.hpp"
