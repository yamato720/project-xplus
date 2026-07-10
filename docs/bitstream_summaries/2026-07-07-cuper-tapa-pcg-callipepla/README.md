# 2026-07-07 Cuper TAPA Callipepla-Style full-PCG 候选

## 版本信息

- 主线：`cuper-tapa-pcg`
- 新源码目录：`DLC/Cuper-callipepla-pcg/`
- 顶层 kernel：`CuperPcgCallipepla`
- 状态：真实 controller 的七条 command/result 配对已改成显式非阻塞状态握手；旧
  顺序修复 artifact 上板后 `Status[52]=60` 无法区分 command、ack 和 result 边界，
  当前 `cmd_drain` 已加入独立握手 monitor 并完成 Vitis link、覆盖 demo 槽，等待服务器
  最小 smoke。实现完成但请求 500 MHz DATA 下 routed setup timing 未收敛，最终 xclbin
  自动选择 138 MHz DATA
- 本轮 XO/硬件目录：
  `cuper-tapa-pcg-callipepla-handshake-monitor-cmd-drain-xo-build/`
- 本轮硬件日志：
  `logs/cuper_tapa_pcg_callipepla_handshake_monitor_hw_20260710_174735.log`
- 同步文件：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260710-demo.xclbin`
- 构建 tmux 会话：
  `project-xplus-cuper-tapa-pcg-callipepla-handshake-monitor-hw`
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

当前同步文件为 `395bitstream/cuper-tapa-pcg-fpga-u55c-20260710-demo.xclbin`，UUID
`4a272f84-1e4d-fdb8-0cfa-1fa5e77f433c`，SHA256
`840018ee2c1cb30e7e8ed6f5f6c815e9abf830f9e060de850bfc6aeecb17198c`，`.xclbin.info`
SHA256 `9c62740088b12652bf9ee3249af67b21636118ad8de8b0532b4aa93f8bb411b8`。
DATA/KERNEL/HBM clock 为 `138/500/450 MHz`，DATA requested/achieved 为
`500/138.8 MHz`。Vitis link `impl Complete`，VPL/POST-VPL 0 errors，总耗时
`3h23m03s`；请求频率下 routed timing 未收敛：WNS `-5.200 ns`、TNS
`-26783.914 ns`、setup failing endpoints `24766`、WHS `0.009 ns`、THS
`0.000 ns`。

该版仍使用 `CUPER_CALLIPEPLA_PROBE_MODE=cmd_drain`，但 controller 不再直接写
Status。controller/fake-ack 分别向两条事件 stream 报告 command full/accepted、ack
receive/result send 和 controller result receive，独立 handshake monitor 是 mode 2
唯一 Status writer。后级 ptr/matrix/vector consumers 仍由 drain/fake ack 替代，
不执行完整 PCG/SpMV datapath。

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
`Status[52]=11`。当前版下一步只跑
`thermal2_n16 MAX_ITERS=0/1 KERNEL_TIMEOUT_SEC=20`，检查
`Status[50]=0x43505242` 和 `Status[51]=2`；若 timeout，使用 `Status[54..59]` 的
command attempts/full/accepted、ack receive/result send 和 controller result receive
逐级定位握手边界。
该版不晋级标准版，也不更新正式 `source.diff`。

2026-07-10 对 thin-status 同步版的后续上板反馈显示 timeout 稳定停在
`Status[52]=14`。同一源码生成的旧 `cmd_drain` 与正式 full-graph RTL 都在 valid 分支
入口依赖 `Vector_Result_in_s_empty_n`，而 vector phase 必须先收到 command 才能产生
result，因此形成真实调度死锁。本轮修复后，两份 controller 报告都显示 7 个
`vector_command_transaction` 和外层 `pcg_iteration_loop` 为 `Pipelined=no`；生成 RTL
中 send-state 只检查 `Vector_Command_out_s_full_n`，wait-state 才读取
`Vector_Result_in_s_empty_n`。当前独立 monitor 版已覆盖同主线 demo 槽；服务器上板前
不更新 HTML 或正式 `source.diff`。
