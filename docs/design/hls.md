# Project-XPlus Jacobi-PCG HLS Design

当前 `Project-XPlus` 作为新的工程根，将在此目录下继续沉淀：

1. 针对 `Project-XPlus` 的目录/构建约束
2. 单顶层 `pcg_control_kernel` HLS/XRT 实现细节
3. 与 `Project-XPlus` 自有数据集、golden 和报告链路的对接约定

配套文档见：

- [hls_source_walkthrough_zh.md](hls_source_walkthrough_zh.md)
- [jacobi_pcg_algorithm_flow_zh.md](jacobi_pcg_algorithm_flow_zh.md)
- [jacobi_pcg_xrt_flowchart.html](jacobi_pcg_xrt_flowchart.html)
- [spmv_block_window_principle_zh.html](spmv_block_window_principle_zh.html)
- [spmv_windowed_dataflow_zh.md](spmv_windowed_dataflow_zh.md)

当前阶段这份文档作为设计入口。算法数学原理、PCG 递推和 host/kernel 执行映射落在 `jacobi_pcg_algorithm_flow_zh.md`；源码链路细节落在 `hls_source_walkthrough_zh.md`。当前 XRT 默认实现把主循环控制放进 `pcg_control_kernel.cpp`，host 不再逐轮计算 `alpha / beta`。
