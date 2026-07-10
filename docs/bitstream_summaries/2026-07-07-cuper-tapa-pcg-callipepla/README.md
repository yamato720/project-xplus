# 2026-07-07 Cuper TAPA Callipepla-Style full-PCG 候选

## 版本信息

- 主线：`cuper-tapa-pcg`
- 新源码目录：`DLC/Cuper-callipepla-pcg/`
- 顶层 kernel：`CuperPcgCallipepla`
- 状态：软件级验证通过；第一轮 Vitis link 因全局拥塞失败；低频 full graph demo
  已生成但 `thermal2_n16 MAX_ITERS=0/1` 最小上板 smoke 均 timeout；trace-light
  定位版 routing verification 失败；entry-probe 已通过完整 `thermal2` 上板入口/mmap
  验证；当前同步槽为 `cmd_drain` thin-status checkpoint probe xclbin，用于区分
  Status mmap 写回、条件判断、stage timer、command fanout、stop 和 fake ack 路径
- 构建目录：`cuper-tapa-pcg-callipepla-probe-cmd-drain-thinstatus-build/`
- 构建日志：`logs/cuper_tapa_pcg_callipepla_thinstatus_hw_20260709_234820.log`
- 同步文件：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260709-demo.xclbin`
- 构建 tmux 会话：`project-xplus-cuper-tapa-pcg-callipepla-thinstatus-hw`
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

当前同步文件为 `395bitstream/cuper-tapa-pcg-fpga-u55c-20260709-demo.xclbin`，UUID
`ad7b2a61-23d4-5c05-360d-acb2ee604830`，DATA/KERNEL/HBM clock 为
`100/500/450 MHz`。该版是 `CUPER_CALLIPEPLA_PROBE_MODE=cmd_drain` thin-status
debug artifact：入口写一次 probe header/规模信息，运行中 checkpoint 只更新
`Status[52]`，正常完成时再写完整最终 counters。它保留真实 controller 和 stage timer，
后级 ptr/matrix/vector consumers 用 drain/fake ack 替代，不执行完整 PCG/SpMV datapath。
Vitis link `impl Complete` 且 routed timing clean：WNS `0.003 ns`、TNS `0.000 ns`、
setup failing endpoints `0`、WHS `0.009 ns`、THS `0.000 ns`。

2026-07-08 低频 full graph demo UUID `9faa45b3-b6cb-1851-21c6-02fdd9a904bc`
已被当前 probe demo 槽替换；其最小上板 timeout 结论只作为历史失败边界保留。
2026-07-09 entry-probe 旧同步版 UUID `7ab50484-4649-ffd5-dd5c-0925c61a9504` 已在
服务器侧通过 `thermal2_n65536`、`thermal2_n131072`、`thermal2_n262144` 和完整
`thermal2`，证明入口、AXI-Lite 参数、BO 分配/同步和 mmap 写回链路在大规模下可用。
上一版多槽 checkpoint cmd-drain UUID `ea2f5c5a-f0f9-c536-8caf-7faa82aa4107`
在服务器侧最小 smoke 停在 `Status[52]=11`，现已由 thin-status 版覆盖。当前版下一步只跑
`thermal2_n16 MAX_ITERS=0/1 KERNEL_TIMEOUT_SEC=20`，检查
`Status[50]=0x43505242` 和 `Status[51]=2`；若 timeout，只以 `Status[52]` 判断卡在
Status mmap、条件判断、stage timer、command fanout 还是 fake ack/finalization，运行中
`detail0/detail1` 可能是 stale 值。
该版不晋级标准版，也不更新正式 `source.diff`。
