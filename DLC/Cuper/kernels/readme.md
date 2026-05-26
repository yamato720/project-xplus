# Cuper Kernels

这里放 `Cuper` 独立子项目自己的 HLS kernel 代码。

建议后续放置：

- HLS 顶层 kernel
- kernel 内部 helper
- kernel 私有数据通路模块
- 为迁移验证准备的最小 kernel demo

当前拆分：

- `Cuper.cpp`：TAPA 编译入口，只包含公共接口和私有实现入口。
- `detail/cuper_spmv_tasks.hpp`：普通 Cuper SpMV loader/core/accumulator/checker/writer。
- `detail/cuper_pcg_tasks.hpp`：FPGA-PCG 聚合入口。
- `detail/pcg_common.hpp`：FPGA-PCG 命令、状态、stage 事件和小 helper。
- `detail/pcg_spmv_service.hpp`：FPGA-PCG 常驻 Cuper SpMV 服务任务。
- `detail/pcg_stage_timer.hpp`：FPGA-PCG stage 计时任务。
- `detail/pcg_controller.hpp`：FPGA-PCG 主控和向量更新逻辑。
- `detail/pcg_drains.hpp`：FPGA-PCG 链尾 drain/stop 消费任务。
- `detail/cuper_top_graphs.hpp`：`Cuper` / `CuperPcg` 顶层 TAPA task graph。

当前原则：

- 先保证 `Cuper` 的 kernel 入口和上层工程彻底解耦
- 尽量保留原项目接口语义，方便逐个对齐
- 等形态稳定后，再判断是否需要回并到上层主线
