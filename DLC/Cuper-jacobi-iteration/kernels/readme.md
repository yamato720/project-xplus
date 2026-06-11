# Cuper Jacobi Iteration Kernels

这里放 `Cuper-jacobi-iteration` 独立实验目录自己的 TAPA kernel 代码。

建议后续放置：

- HLS 顶层 kernel
- kernel 内部 helper
- kernel 私有数据通路模块
- 为迁移验证准备的最小 kernel demo

当前拆分：

- `Cuper.cpp`：TAPA 编译入口，include 当前实验顶层。
- `detail/cuper_jacobi_top_graphs.hpp`：`CuperJacobiIteration` 顶层 task graph。
- `detail/cuper_spmv_tasks.hpp`：Cuper SpMV loader/core/accumulator/checker helper。
- `detail/spmv_service_common.hpp`：service SpMV command/stop 和小 helper。
- `detail/spmv_service_tasks.hpp`：中性命名的常驻 Cuper SpMV service task。
- `detail/spmv_service_drains.hpp`：service 链尾 drain/stop 消费任务。

当前原则：

- 先保证 `CuperJacobiIteration` 可独立编译和软件仿真
- Jacobi 控制逻辑集中在 `Jacobi_Controller` 和 `Jacobi_Update_Service`
- 不把完整 PCG controller/vector update 文件带回本目录
