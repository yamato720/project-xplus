# 2026-07-07 Cuper TAPA Callipepla-Style full-PCG 候选

## 版本信息

- 主线：`cuper-tapa-pcg`
- 新源码目录：`DLC/Cuper-callipepla-pcg/`
- 顶层 kernel：`CuperPcgCallipepla`
- 状态：软件级验证通过；第一轮 Vitis link 因全局拥塞失败；低频重试已生成并同步
  demo bitstream，尚未上板，routed timing 未收敛
- 构建目录：`cuper-tapa-pcg-callipepla-u55c-20260708-lowfreq-build/`
- 构建日志：`logs/cuper_tapa_pcg_callipepla_lowfreq_hw_20260708_105413.log`
- 同步文件：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260708-demo.xclbin`
- tmux 会话：`project-xplus-cuper-tapa-pcg-callipepla-hw`
- 默认配置：`CUPER_CALLIPEPLA_HBM_CHANNELS=16`，
  `CUPER_CALLIPEPLA_SPMV_STRIP_PADDING=1`，
  `CUPER_CALLIPEPLA_SPMV_ACC_WINDOW=10`
- 版本记录策略：这是隔离子项目，不覆盖 `DLC/Cuper` 当前 full-PCG 路线；
  未完成 demo-only 上板前不更新正式 `source.diff`，不替换标准 bitstream

## 目标

这一版按 `docs/refer/callipepla_pcg_reference/` 的流式 PCG/vector task graph
思路重写 full-PCG 实验路径，但不引入 Callipepla 稀疏矩阵格式。矩阵仍采用 Cuper
的 `SpElement_list_ptr + Matrix_data[0..15]`，SpMV service 复用
`DLC/Cuper-jacobi-iteration` 已验证的 strip16/去 HBM padding 方向，PCG 向量状态
保持 FP64。

## 当前结论

软件级验证已经覆盖计划中的四个 smoke 点，均通过，并且 host 输出确认
`strip_padding=1`。第一轮默认频率硬件构建通过此前失败的 `tapacc` front-end
边界，但 Vitis routing 报 `Design is not routable as its global congestion level is 7`，
最终停在 `impl ERROR`。低频重试使用 `CLOCK_PERIOD=5.0` 和
`CUPER_CALLIPEPLA_KERNEL_FREQUENCY=150`，Vitis link 已 `impl Complete` 并生成 demo
xclbin。

当前同步文件为 `395bitstream/cuper-tapa-pcg-fpga-u55c-20260708-demo.xclbin`，UUID
`9faa45b3-b6cb-1851-21c6-02fdd9a904bc`，DATA/KERNEL/HBM clock 为
`135/500/450 MHz`。routed timing 仍未收敛，WNS `-0.721 ns`、TNS `-3677.357 ns`；
尚未上板，因此不晋级标准版，也不更新正式 `source.diff`。
