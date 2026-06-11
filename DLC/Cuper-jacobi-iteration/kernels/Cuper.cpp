#include "Cuper.h"

// Cuper Jacobi iteration 实验入口。
//
// 这个子项目从 DLC/Cuper 拆出，当前保留 service 化 Cuper SpMV，并在其后接
// Jacobi controller/update。原 full-PCG controller 和 standalone one-shot Cuper
// 顶层不在本目录维护。
//
// 具体 task 实现拆在 detail/ 下：
//   - cuper_spmv_tasks.hpp        ：Cuper SpMV 底层 helper。
//   - spmv_service_tasks.hpp        ：中性命名的常驻 SpMV service task。
//   - cuper_jacobi_top_graphs.hpp ：当前实验顶层 task graph。
#include "detail/cuper_jacobi_top_graphs.hpp"
