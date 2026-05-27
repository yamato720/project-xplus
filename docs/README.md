# Project-XPlus 文档目录

这份目录用来快速区分当前工程里的多套实现。现在代码里同时保留了普通多 kernel、单 control-kernel、Cuper 软件适配、Cuper TAPA、Cuper control-kernel 等版本；看报告或跑菜单前，先确认自己看的是什么版本。

## 推荐阅读顺序

0. [Codex 工作纪律](codex/coding.md)
   - 新对话里的 Codex 先读这个。它约定四条 Cuper 主线、bitstream 归档、build 目录、测试和推送纪律。
1. [Codex 测试纪律](codex/testing.md)
   - 服务器侧核验 bitstream 前读这个。它按 `395bitstream/cuper_spmv_u55c_compare_20260524.html` 给出数据集、命令和预期结果。
2. [Bitstream 版本总结](bitstream_summaries/README.md)
   - 一个 bitstream/demo 版本一个子目录，存放 Markdown 详细总结和“这一版改了什么”。
3. [实现版本索引](design/implementation_versions_zh.md)
   - 先看这个。它说明每个版本的 PCG 控制在哪里、SpMV 在哪里、用哪个 host/kernel、怎么构建和运行。
4. [Jacobi-PCG 数学原理与 XRT 执行流程](design/jacobi_pcg_algorithm_flow_zh.md)
   - 说明 PCG 的数学递推、Jacobi 预条件、默认单 control-kernel 路径的 host/kernel 映射。
5. [Project-XPlus HLS 路径源码解析](design/hls_source_walkthrough_zh.md)
   - 从 Makefile、host、kernel、connectivity、报告脚本角度串起默认 XRT 路径。
6. [Project-XPlus Jacobi-PCG HLS Design](design/hls.md)
   - HLS 设计入口和相关文档链接。

## Cuper / SpMV 相关

- [TAPA full-PCG SpMV 接近原生 Cuper 性能目标记录](bitstream_summaries/2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/README.md)
  - 记录当前 demo bitstream、历史 receive-path demo、测试边界、性能差值和是否建议晋级。
- [Cuper 大矩阵拆分运行方案](design/cuper_large_matrix_split_zh.md)
  - 说明 `DLC/Cuper` 的 slice/window 限制，以及大矩阵按 tile 拆给 Cuper 的方案。
- [Cuper / CuperPcg 比特流构建尝试记录](design/cuper_tapa_pcg_bitstream_attempts_zh.md)
  - 记录当前几版 Cuper-PCG xclbin、TAPA CuperPcg 全 FPGA 版的失败日志和后续优化方向。
- [滑动窗口 SpMV 与 dataflow 实验记录](design/spmv_windowed_dataflow_zh.md)
  - 说明默认 control-kernel 路径里的滑动窗口 SpMV。
- [SpMV block/window 原理图](design/spmv_block_window_principle_zh.html)
  - HTML 可视化说明。

## 参考资料

- [CSR5 对 Project-XPlus / FPGA PCG SpMV 的可行性评估备忘](refer/csr5_xplus_review.md)
