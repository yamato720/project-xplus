# 2026-07-07 Cuper TAPA Callipepla-Style full-PCG 候选

## 版本信息

- 主线：`cuper-tapa-pcg`
- 新源码目录：`DLC/Cuper-callipepla-pcg/`
- 顶层 kernel：`CuperPcgCallipepla`
- 状态：`loader_drain level=4` 源码、TAPA software simulation、XO 静态审查和 Vitis
  硬件构建均通过。它恢复全部 16 路真实 `Matrix_data -> Matrix_A_Stream` loader/drain，
  仍不接 SpMV core、accumulator、checker、sort 或真实 vector phases；尚未上板。
- 当前同步文件是 level-4 timing-clean debug artifact：
  `395bitstream/cuper-tapa-pcg-fpga-u55c-20260712-demo.xclbin`，UUID
  `3625c6a3-d725-db95-e0f5-27dc644edd61`，DATA/KERNEL/HBM 为 `100/500/450 MHz`，
  routed WNS/TNS/WHS/THS 为 `0.003/0/0.008/0`。
- 当前同步文件：
  `395bitstream/cuper-tapa-pcg-fpga-u55c-20260712-demo.xclbin`
- 下一步：服务器侧从 `thermal2_n16 MAX_ITERS=0/1` 验证 16 路 Matrix HBM word count、
  16 channel done、event/done/drop 和 CU IDLE；当前无运行中的 level-4 构建 tmux
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

此前同步的 level-3 文件为 `395bitstream/cuper-tapa-pcg-fpga-u55c-20260711-demo.xclbin`，UUID
`02db72cc-c208-7772-4df4-3757a606929f`，SHA256
`bdf6552a7fb0ef1f87bbf7f1cea82ec796398dce16d9b5bbce140e96cacbdc16`，`.xclbin.info`
SHA256 `7aedf3a7f89c520bb8d01efdb7d800155e320d8cad93513b4413bc24e25cec8d`。
DATA/KERNEL/HBM clock 为 `100/500/450 MHz`。Vitis link `impl Complete`，VPL/POST-VPL
0 errors，总耗时 `2h16m59s`；routed timing clean：WNS `0.003 ns`、TNS `0.000 ns`、
setup failing endpoints `0`、WHS `0.009 ns`、THS `0.000 ns`。该 artifact 仍仅用于
服务器 debug，不写成真实 PCG 或性能通过版。

上一同步的 timing-clean level-1 artifact 使用
`CUPER_CALLIPEPLA_PROBE_MODE=loader_drain`、
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
`source.diff`。该 level-1 结果随后作为进入 `loader_drain level=2` 的历史基线；
level 2 恢复真实 X/P vector loader 和向量 stream drain，Matrix_data 与 SpMV core
仍不恢复。

level 2 已接入真实 `PcgCallipepla_Vector_Loader`：按 SpMV command 从 X/P 当前 bank
读取 `double_v8`，转换为 `float_v16` 后由 tail drain 消费。当前 level 3 在此基础上
保留 16 路 matrix command/length fanout，只让 `PcgCallipepla_Probe_MatrixLoaderStripDrain`
的 ch0/ch15 进入 `probe_read_matrix` HBM request/response loop；其余 14 路只 drain。
mode-3 monitor 的 level-2 `Status[47..49]` vector 计数与 `Status[63].bit6` vector done
语义不变，但没有新增 matrix word counter。软件回归已通过 `thermal2_n16 MAX_ITERS=0/1`，
两点均为 `event=99`、`flags=0x7e`、full/drop=0。SpMV core、accumulator、checker、sort
和真实 vector phases 仍未接入；完整 level-3 xclbin 已按上述 UUID/SHA 覆盖同主线 demo
槽，等待服务器侧上板，因此不做 PCG correctness 或性能结论。

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
继续保留。当前 level-3 artifact 不晋级标准版，也不更新正式 `source.diff`。

2026-07-10 对 thin-status 同步版的后续上板反馈显示 timeout 稳定停在
`Status[52]=14`。同一源码生成的旧 `cmd_drain` 与正式 full-graph RTL 都在 valid 分支
入口依赖 `Vector_Result_in_s_empty_n`，而 vector phase 必须先收到 command 才能产生
result，因此形成真实调度死锁。本轮修复后，两份 controller 报告都显示 7 个
`vector_command_transaction` 和外层 `pcg_iteration_loop` 为 `Pipelined=no`；生成 RTL
中 send-state 只检查 `Vector_Command_out_s_full_n`，wait-state 才读取
`Vector_Result_in_s_empty_n`。当前独立 monitor 版已覆盖同主线 demo 槽并完成上述
服务器侧握手验证；HTML 只新增 debug demo-only 边界，不把 fake-ack 数据混入 PCG
correctness/性能图表，也不更新正式 `source.diff`。

## 2026-07-11 loader-drain level 4 全 16 路 Matrix loader/drain

level 4 保留 level 3 的 controller fake-ack、真实 strip ptr/PE drain 与 X/P vector
loader，但将 16 个 `PcgCallipepla_Probe_MatrixLoaderStripDrain` 全部替换成真实
`SpmvService_MatrixLoaderStrip`。每路写入独立 `Matrix_A_Stream`，由配对 drain 消费。
loader 接收 stop 后写专用 stop token；drain 只有在已看到该 token 且自己的
`Matrix_A_Stream` 为空时才报告完成，因此不会提前截断 Matrix_data read response 或
FIFO 中残留的 beat。

mode-3 monitor 等待 16 个 drain 的可靠最终事件，再写 `event=99`。level 4 使用此前未占用的
`Status[44..46]`：Matrix HBM word 总数、完成 channel 数、matrix event drop 数；
`Status[63].bit7` 为 matrix done，`Status[63][31:24]` 继续汇总 ptr/PE/vector/matrix
loader drop。host 对 level 4 额外验证 `event=99`、16 channel done、
`matrix_words=stripped_matrix_len_total*(MAX_ITERS+1)`、full/drop=0，以及所有
controller/ack/ptr/PE/vector/matrix done flag。

本地 `thermal2_n16` TAPA simulation 的 Matrix HBM words 为：`MAX_ITERS=0/1/10` 分别
`88/176/968`，均为 16 channels、`event=99`、`flags=0xfe`、full/drop=0。level 3
服务器侧已按用户反馈成功，但本地没有 raw log，因此不补造其 Matrix count。level 4
仍是 fake-ack debug artifact，不构成完整 SpMV/PCG correctness、性能或 bitstream 晋级结论；
正式 `source.diff` 保持不更新。

2026-07-12 的 level-4 Vitis build 已生成
`395bitstream/cuper-tapa-pcg-fpga-u55c-20260712-demo.xclbin`。UUID 为
`3625c6a3-d725-db95-e0f5-27dc644edd61`，xclbin SHA256 为
`4f1b70e779e94a2a6e005e6ac03373a99a9651709da8ef54d9e909cb5652c123`，info SHA256 为
`a3a8796c91b438dc13cea1bdf4f5a4f822918fc874a3563c62e8465e6a98df6c`。构建日志
`logs/cuper_tapa_pcg_callipepla_hw_20260711_223213.log` 显示 `impl Complete`、VPL/POST-VPL
均 0 errors、总耗时 `2h48m55s`。routed timing summary 为 WNS/TNS/WHS/THS
`0.003/0/0.008/0 ns`，setup/hold failing endpoints 均为 0。生成 RTL 静态审查确认 16 个
`SpmvService_MatrixLoaderStrip` 和 16 个配对 matrix drain，全部 Matrix HBM 读端口存在，
且未接入 core/accumulator/checker/sort。该 demo 已同步但尚未上板，不能据此声称完整
SpMV/PCG correctness 或性能。
