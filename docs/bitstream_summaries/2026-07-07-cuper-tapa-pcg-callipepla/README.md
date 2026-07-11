# 2026-07-07 Cuper TAPA Callipepla-Style full-PCG 候选

## 版本信息

- 主线：`cuper-tapa-pcg`
- 新源码目录：`DLC/Cuper-callipepla-pcg/`
- 顶层 kernel：`CuperPcgCallipepla`
- 状态：`loader_drain level=1` timing-clean debug artifact 已完成构建并同步。UUID
  `fdbc2e10-20ea-8e78-6b3c-72a01803cde1`，恢复真实 strip ptr HBM 读取、16 路
  matrix-length fanout 和 `PE_Param` drain；Matrix_data/vector/SpMV datapath 仍关闭并
  使用 fake-ack。最终 DATA/KERNEL/HBM 为 `100/500/450 MHz`，routed WNS
  `0.002 ns`、TNS `0`、WHS `0.009 ns`、THS `0`。服务器侧反馈已按约定验收口径通过
- 本轮 XO/硬件目录：
  `cuper-tapa-pcg-callipepla-loader-monitor-level1-xo-build/`
- 本轮硬件日志：
  `logs/cuper_tapa_pcg_callipepla_loader_monitor_level1_hw_20260711_004344.log`
- 当前同步文件：
  `395bitstream/cuper-tapa-pcg-fpga-u55c-20260711-demo.xclbin`
- 下一构建：`loader_drain level=2` 已完成源码、软件回归和 XO/RTL 审查；独立目录
  `cuper-tapa-pcg-callipepla-loader-monitor-level2-xo-build/`
- 构建 tmux 会话：
  `project-xplus-cuper-tapa-pcg-callipepla-loader-monitor-level1-hw`
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

当前同步文件为 `395bitstream/cuper-tapa-pcg-fpga-u55c-20260711-demo.xclbin`，UUID
`fdbc2e10-20ea-8e78-6b3c-72a01803cde1`，SHA256
`0e0e7e832b24afff008665f45ea0020c9cb121cfcf3c2c0f1cf20ea13ee76fe0`，`.xclbin.info`
SHA256 `5527fd3c42add486a2a193bdc7d08e209efb0d91c89e9ec75aaae62fd83cd2ea`。
DATA/KERNEL/HBM clock 为 `100/500/450 MHz`。Vitis link `impl Complete`，
VPL/POST-VPL 0 errors，总耗时 `2h38m33s`；routed timing 已收敛：WNS
`0.002 ns`、TNS `0.000 ns`、setup failing endpoints `0`、WHS `0.009 ns`、THS
`0.000 ns`。

该版使用 `CUPER_CALLIPEPLA_PROBE_MODE=loader_drain`、
`CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL=1`。controller/fake-ack 沿用 mode-2 事件握手，
真实 strip ptr loader 读取 16 路 matrix length 和每轮 boundary table，16 路 matrix
drain 消费 command/length 但生成 RTL 中不发 `Matrix_data` request，真实
`PE_Param` drain 完成每轮 boundary 消费。mode-3 monitor 是唯一 Status writer；
vector loader、SpMV core、accumulator、checker 和 sort 仍未恢复，因此不执行完整
PCG/SpMV datapath。

软件/TAPA simulation 已通过 `thermal2_n16 MAX_ITERS=0/1/10`。对应 vector
command/result 为 `3/2`、`8/7`、`53/52`，ptr commands 为 `2/3/12`，PE rounds
为 `1/2/11`，ptr HBM words 为 `48/80/368`；均为 `event=99`、full=0、
flags=`0x3e`、三类 event drop=0。mode 2 `cmd_drain` 与无 probe full graph 回归也保持
既有数值。2026-07-11 用户反馈该 artifact 已在服务器侧按约定验收口径通过：
`event=99`、预期计数闭合、full/drop 为 0、controller/ack/ptr/PE done flag 全部置位，
XRT error 为空且 CU 回到 IDLE。本地没有服务器 raw log，本记录按用户反馈登记。它仍是
fake-ack debug artifact，不做 PCG correctness 或性能结论，不更新正式
`source.diff`。下一定位边界进入 `loader_drain level=2`，恢复真实 X/P vector loader
和向量 stream drain，Matrix_data 与 SpMV core 仍不恢复。

level 2 源码已经接入真实 `PcgCallipepla_Vector_Loader`：按 SpMV command 从 X/P 当前
bank 读取 `double_v8`，转换为 `float_v16` 后由 tail drain 消费。mode-3 monitor 新增
vector command/round/HBM-word 计数、vector stop flag 和 event-drop 汇总；level 1 的
`Status[50..63]` 保持不变，level 2 使用 `Status[47..49]` 扩展字段。软件回归已通过
`thermal2_n16 MAX_ITERS=0/1/10`，vector commands/rounds/HBM words 分别为
`2/1/2`、`3/2/4`、`12/11/22`，所有点 `flags=0x7e`、full/drop=0。Matrix_data request
仍关闭，SpMV core 仍未接入。

2026-07-11 服务器侧反馈确认上一握手 artifact 已通过以下 demo-only 覆盖：

- `thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、
  `thermal2_n262144` 和完整 `thermal2` 的 `MAX_ITERS=0/1`；
- `thermal2_n16` 的 `MAX_ITERS=10/100` 压力测试。

`MAX_ITERS=I` 的 vector command/result 计数稳定为 `5*I+3` / `5*I+2`：
`I=0/1/10/100` 分别为 `3/2`、`8/7`、`53/52`、`503/502`。所有点均为
`command_full=0`、controller/ack event drop=`0/0`，controller done 和 ack stop flag
置位，XRT error 为空且 CU 回到 IDLE。该结果排除 controller、command FIFO、fake-ack、
result FIFO 和 stop/finalization 的握手死锁；由于 vector phase 仍是 fake-ack，结果不进入
PCG correctness、分段时间或性能图表。本地没有服务器 raw log，本记录按用户提供的
服务器侧反馈登记。

2026-07-08 低频 full graph demo UUID `9faa45b3-b6cb-1851-21c6-02fdd9a904bc`
已被当前 probe demo 槽替换；其最小上板 timeout 结论只作为历史失败边界保留。
2026-07-09 entry-probe 旧同步版 UUID `7ab50484-4649-ffd5-dd5c-0925c61a9504` 已在
服务器侧通过 `thermal2_n65536`、`thermal2_n131072`、`thermal2_n262144` 和完整
`thermal2`，证明入口、AXI-Lite 参数、BO 分配/同步和 mmap 写回链路在大规模下可用。
上一版顺序修复 cmd-drain UUID `d46c3285-6cc2-1b02-9350-1ad3dadb5c56` 在服务器侧
`thermal2_n16 MAX_ITERS=0` timeout，最后可见 `Status[52]=60`；该 checkpoint 在生成
RTL 中存在调度合并歧义，已由当前独立 monitor 版覆盖。更早的 thin-status cmd-drain
UUID `ad7b2a61-23d4-5c05-360d-acb2ee604830` 在服务器侧
最小 smoke 停在 `Status[52]=14`，现已由 command/result 顺序修复版覆盖。更早的多槽
checkpoint cmd-drain UUID `ea2f5c5a-f0f9-c536-8caf-7faa82aa4107` 停在
`Status[52]=11`。握手链路通过后，当前同步版已进入
`CUPER_CALLIPEPLA_PROBE_MODE=loader_drain`、
`CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL=1`，并在 `100/500/450 MHz` 下通过 routed
timing。上一 `20260710-demo` 文件已从同步目录删除，但其 Git 历史和服务器侧测试记录
继续保留。当前 level-1 artifact 不晋级标准版，也不更新正式 `source.diff`。

2026-07-10 对 thin-status 同步版的后续上板反馈显示 timeout 稳定停在
`Status[52]=14`。同一源码生成的旧 `cmd_drain` 与正式 full-graph RTL 都在 valid 分支
入口依赖 `Vector_Result_in_s_empty_n`，而 vector phase 必须先收到 command 才能产生
result，因此形成真实调度死锁。本轮修复后，两份 controller 报告都显示 7 个
`vector_command_transaction` 和外层 `pcg_iteration_loop` 为 `Pipelined=no`；生成 RTL
中 send-state 只检查 `Vector_Command_out_s_full_n`，wait-state 才读取
`Vector_Result_in_s_empty_n`。当前独立 monitor 版已覆盖同主线 demo 槽并完成上述
服务器侧握手验证；HTML 只新增 debug demo-only 边界，不把 fake-ack 数据混入 PCG
correctness/性能图表，也不更新正式 `source.diff`。
