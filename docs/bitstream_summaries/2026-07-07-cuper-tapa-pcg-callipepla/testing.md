# testing

## 软件级验证

已完成同一 top/ABI 的 TAPA software simulation smoke，均启用
`CUPER_CALLIPEPLA_SPMV_STRIP_PADDING=1`。

```bash
make cuper-tapa-pcg-callipepla-build-host
```

结果：通过。

```bash
make run-cuper-tapa-pcg-callipepla DATASET=data/generated/cgsolver/n512 MAX_ITERS=0 DIFF_TOL=1e-3
```

关键输出：

```text
strip_padding=1
original_read_beats=1632 stripped_read_beats=1604 saved_beats=28
[done] iter=0 status=max_iter rr=3.427876635047e+01
max_abs_diff=0 max_rel_diff=0
```

```bash
make run-cuper-tapa-pcg-callipepla DATASET=data/generated/cgsolver/n512 MAX_ITERS=1 DIFF_TOL=1e-3
```

关键输出：

```text
strip_padding=1
[done] iter=1 status=max_iter final_x_bank=1 final_r_bank=1 final_p_bank=1
rr=9.407299557997e+01
max_abs_diff=1.845319074767e-07 max_rel_diff=3.783121170261e-05
```

```bash
make run-cuper-tapa-pcg-callipepla DATASET=data/suitesparse/Schmid/csr/thermal2_n16 MAX_ITERS=0 DIFF_TOL=1e-3
```

关键输出：

```text
strip_padding=1
original_read_beats=176 stripped_read_beats=88 saved_beats=88
[done] iter=0 status=max_iter rr=1.104105301696e+02
max_abs_diff=0 max_rel_diff=0
```

```bash
make run-cuper-tapa-pcg-callipepla DATASET=data/suitesparse/Schmid/csr/thermal2_n16 MAX_ITERS=1 DIFF_TOL=1e-3
```

关键输出：

```text
strip_padding=1
[done] iter=1 status=converged final_x_bank=1 final_r_bank=1 final_p_bank=0
rr=6.162139191957e-14
max_abs_diff=1.086781531434e-08 max_rel_diff=9.286405498855e-09
```

## 硬件构建

启动命令：

```bash
make cuper-tapa-pcg-callipepla-hw-tmux \
  CUPER_TAPA_PCG_CALLIPEPLA_BUILD_DIR=cuper-tapa-pcg-callipepla-u55c-20260707-build
```

当前日志：

```text
logs/cuper_tapa_pcg_callipepla_hw_20260707_203638.log
```

原安全点阶段：

```text
tapacc analysis passed
HLS synthesis completed
TAPA generated cuper-tapa-pcg-callipepla-u55c-20260707-build/CuperPcgCallipepla.xo
Vitis link started
VPL completed create_project/create_bd/update_bd
VPL generate_target running as of 20:51 CST
final xclbin not generated yet
```

第一轮硬件构建曾在 `tapacc` front-end 失败：`.invoke(...)` 不能接受 `X[0]`、
`P[0]`、`R[0]` 这样的 `mmaps` index 表达式。已修复为显式 `X_0/X_1`、
`P_0/P_1`、`R_0/R_1` 顶层端口后重新启动构建。

最终结果：Vitis link 在 VPL `impl` routing 阶段失败，没有生成 xclbin。

关键日志：

```text
[21:34:00] Run vpl: Step synth: Completed
[21:34:00] Run vpl: Step impl: Started
[01:44:18] Starting logic routing..
[02:53:14] Run vpl: Step impl: Failed
[02:53:15] Run vpl: FINISHED. Run Status: impl ERROR
ERROR: [VPL 35-3] Design is not routable as its global congestion level is 7.
ERROR: [VPL 18-1000] Routing results verification failed due to partially-conflicted nets
ERROR: [VPL 60-704] Integration error ... route_design ERROR
```

详细实现日志：

```text
cuper-tapa-pcg-callipepla-u55c-20260707-build/vpp_tmp/link/vivado/vpl/prj/prj.runs/impl_1/runme.log
```

路由日志中的拥塞信号：

```text
WARNING: [Route 35-3311] The design has high localized SLL routing demand.
SLR [1-2] max demand column: 1471 / 1440 = 102%
SLR [0-1] max demand columns: 2174 / 1440 = 151%, 1760 / 1440 = 122%
ERROR: [Route 35-3] Design is not routable as its global congestion level is 7.
```

被列出的 partially-conflicted nets 主要落在：

```text
SpmvService_CoreStrip_15 load_vector/read_boundary_group
SpmvService_CoreStrip_10 read_boundary_group/load_vector/decode_matrix
SpmvService_Accumulator_12 writer/local_part_y
PcgCallipepla_Controller_0 pcg_iteration_loop
```

placed utilization 摘要：

```text
Overall: LUT 39.60%, register 25.74%, BRAM tile 66.49%, URAM 53.33%, DSP 11.97%
SLR0: CLB 95.59%, LUT 69.52%, register 42.11%, BRAM tile 57.37%, URAM 30.00%
SLR1: CLB 53.68%, BRAM tile 82.22%, URAM 70.00%
SLR2: CLB 42.13%, BRAM tile 59.90%, URAM 60.00%
SLR0 <-> SLR1 SLL used: 13740 / 23040 = 59.64%
SLR1 <-> SLR2 SLL used: 11358 / 23040 = 49.30%
```

结论：这不是 host、软件仿真或 TAPA front-end 功能失败；它是实现阶段局部布线拥塞。
全片资源余量仍有，但 SLR0 CLB 接近打满，同时 `HBM[0..31]` 端口和 16 路 strip
service/PCG 向量图造成局部 SLL demand 超额。

## 当前待补

- 低频 demo UUID `9faa45b3-b6cb-1851-21c6-02fdd9a904bc` 已在最小上板 smoke
  timeout，不继续 sweep。
- 下一步只构建并同步 `CUPER_CALLIPEPLA_TRACE_LIGHT=1` 定位版，覆盖同一 full-PCG
  demo 槽后再跑 `thermal2_n16 MAX_ITERS=0/1`。
- trace-light 最小 smoke 未返回前，不跑更大规模、不晋级标准版、不更新正式
  `source.diff`。

## 2026-07-08 低频重试

用户要求直接启动 tmux 构建，不等待安全点。由于旧失败会话仍占用
`project-xplus-cuper-tapa-pcg-callipepla-hw`，本轮使用独立 tmux 会话和 build dir。

```bash
tmux attach -t project-xplus-cuper-tapa-pcg-callipepla-lowfreq-hw
tail -f logs/cuper_tapa_pcg_callipepla_lowfreq_hw_20260708_105413.log
```

配置：

```text
build dir: cuper-tapa-pcg-callipepla-u55c-20260708-lowfreq-build
log: logs/cuper_tapa_pcg_callipepla_lowfreq_hw_20260708_105413.log
CLOCK_PERIOD=5.0
CUPER_CALLIPEPLA_KERNEL_FREQUENCY=150
```

tmux 内先跑 `n512 MAX_ITERS=1 DIFF_TOL=1e-3` 软件 smoke；启动确认时该 smoke 已通过，
随后进入 `tapa compile` 生成 `CuperPcgCallipepla.xo`。

最终结果：Vitis link 完成并生成 xclbin，已同步为 full-PCG demo 候选。

```text
[19:28:00] Run vpl: Step impl: Completed
[19:28:01] Run vpl: FINISHED. Run Status: impl Complete!
INFO: [v++ 60-1230] ... hbm_aclk = 450, KERNEL = 500, DATA = 135
INFO: [v++ 60-586] Created cuper-tapa-pcg-callipepla-u55c-20260708-lowfreq-build/CuperPcgCallipepla.xclbin
INFO: [v++ 60-791] Total elapsed time: 8h 23m 21s
```

同步文件：

```text
395bitstream/cuper-tapa-pcg-fpga-u55c-20260708-demo.xclbin
395bitstream/cuper-tapa-pcg-fpga-u55c-20260708-demo.xclbin.info
```

bitstream 信息：

```text
UUID: 9faa45b3-b6cb-1851-21c6-02fdd9a904bc
SHA256: 019163fafd84d9c399260962a7555bc010a63a404ae9fcbd122589f7eb6370d7
INFO SHA256: 0505aa6378d8e1b05d097778fb8d7b73b5d250abddf726c11bfbf74c85ab621e
DATA/KERNEL/HBM clock: 135 / 500 / 450 MHz
Requested DATA clock: 150 MHz
Achieved DATA clock: 135.3 MHz
```

routed timing summary：

```text
WNS=-0.721 ns
TNS=-3677.357 ns
setup failing endpoints=10576
WHS=0.003 ns
THS=0.000 ns
```

结论：低频重试解决了 `global congestion level 7` 的不可布线问题，但 timing 仍未收敛。
该 xclbin 只作为 demo 候选同步，进入最小上板 smoke。

## 2026-07-08 demo-only 上板 smoke

测试对象为同步槽当前 UUID `9faa45b3-b6cb-1851-21c6-02fdd9a904bc`。

```bash
make cuper-tapa-pcg-callipepla-run-hw \
  BITFILE=395bitstream/cuper-tapa-pcg-fpga-u55c-20260708-demo.xclbin \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=0 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=1 DIFF_TOL=1e-3

make cuper-tapa-pcg-callipepla-run-hw \
  BITFILE=395bitstream/cuper-tapa-pcg-fpga-u55c-20260708-demo.xclbin \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=1 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=1 DIFF_TOL=1e-3
```

结果：

| Dataset | MAX_ITERS | Timeout | 最后 live phase | 结论 |
| --- | ---: | ---: | ---: | --- |
| `thermal2_n16` | 0 | 20s | 1 | timeout |
| `thermal2_n16` | 1 | 20s | 0 | timeout |

该 UUID 先定性为失败 demo：最小 init-only / 1iter 都没有返回，尚未跑更大规模，
不晋级标准版，不更新正式 `source.diff`。

## 2026-07-08 trace-light 软件 smoke

上板 timeout 后新增 `CUPER_CALLIPEPLA_TRACE_LIGHT=1` 定位版，保持 ABI/HBM mapping
不变，只用 `Status[16..63]` 输出 task/stream 进度。

```bash
make cuper-tapa-pcg-callipepla-build-host CUPER_CALLIPEPLA_TRACE_LIGHT=1

make run-cuper-tapa-pcg-callipepla \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=0 DIFF_TOL=1e-3 CUPER_CALLIPEPLA_TRACE_LIGHT=1

make run-cuper-tapa-pcg-callipepla \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=1 DIFF_TOL=1e-3 CUPER_CALLIPEPLA_TRACE_LIGHT=1
```

结果：三条命令均通过。关键输出：

```text
MAX_ITERS=0: [done] iter=0 status=max_iter rr=1.104105301696e+02
MAX_ITERS=0: max_abs_diff=0 max_rel_diff=0
MAX_ITERS=1: [done] iter=1 status=converged final_x_bank=1 final_r_bank=1 final_p_bank=0
MAX_ITERS=1: max_abs_diff=1.086781531434e-08 max_rel_diff=9.286405498855e-09
```

## 2026-07-09 trace-light 硬件构建失败

启动命令：

```bash
make cuper-tapa-pcg-callipepla-hw-tmux \
  CUPER_TAPA_PCG_CALLIPEPLA_BUILD_DIR=cuper-tapa-pcg-callipepla-u55c-20260708-trace-build \
  CUPER_CALLIPEPLA_TRACE_LIGHT=1 \
  CLOCK_PERIOD=8.0 \
  CUPER_CALLIPEPLA_KERNEL_FREQUENCY=100 \
  CUPER_CALLIPEPLA_HBM_CHANNELS=16 \
  CUPER_CALLIPEPLA_SPMV_STRIP_PADDING=1 \
  CUPER_CALLIPEPLA_SPMV_ACC_WINDOW=10
```

构建记录：

```text
tmux: project-xplus-cuper-tapa-pcg-callipepla-hw
log: logs/cuper_tapa_pcg_callipepla_hw_20260708_232242.log
build dir: cuper-tapa-pcg-callipepla-u55c-20260708-trace-build
```

TAPA/XO 阶段已通过，且 Vitis link 已进入并通过 synthesis：

```text
I0708 23:35:41.445 ... generated the v++ xo file at .../CuperPcgCallipepla.xo
[23:39:37] Run vpl: Step synth: Started
[00:23:13] Run vpl: Step synth: Completed
[00:23:13] Run vpl: Step impl: Started
```

最终失败在 Vivado routing verification，没有生成 `CuperPcgCallipepla.xclbin`：

```text
[05:28:43] Phase 9 Verifying routed nets
[05:35:09] Run vpl: Step impl: Failed
[05:35:10] Run vpl: FINISHED. Run Status: impl ERROR
ERROR: [VPL 18-1000] Routing results verification failed due to partially-conflicted nets
ERROR: [VPL 60-704] Integration error ... route_design ERROR
ERROR: [v++ 60-703] Failed to finish linking
make: *** [Makefile:489: cuper-tapa-pcg-callipepla-link-xclbin] Error 1
```

Vivado `impl_1/runme.log` 中的 routing 失败信号：

```text
CRITICAL WARNING: [Route 35-162] 13926 signals failed to route due to routing congestion.
CRITICAL WARNING: [Route 35-2] Design is not legally routed. There are 9945 node overlaps.
ERROR: [Constraints 18-1000] Routing results verification failed due to partially-conflicted nets
```

拥塞报告显示主要仍是 routing congestion，而不是 C++/TAPA front-end 错误：

```text
South Dir 64x64 Area, Max Cong = 92.6611%
Direction: South
Congested clusters found at Level 6
Effective congestion level: 7
```

前 10 个 partially-conflicted nets 同时包含 HBM shell/interconnect 路径和 kernel 内
trace/command FIFO：

```text
level0_i/ulp/hmss_0/inst/path_28/interconnect27_28/...
level0_i/ulp/hmss_0/inst/path_16/interconnect1_16/...
level0_i/ulp/CuperPcgCallipepla_1/inst/Matrix_Command_Stream_14/...
level0_i/ulp/CuperPcgCallipepla_1/inst/Matrix_Command_Stream_8/...
level0_i/ulp/hmss_0/inst/path_12/...
level0_i/ulp/hmss_0/inst/path_23/...
```

结论：

- trace-light 源码和软件 smoke 通过，但当前 trace-light xclbin 未生成。
- 当时 `395bitstream/cuper-tapa-pcg-fpga-u55c-20260708-demo.xclbin` 未被覆盖，仍是旧
  UUID `9faa45b3-b6cb-1851-21c6-02fdd9a904bc` 的失败 demo；2026-07-09 后续已由
  entry-probe demo 槽替换。
- 本轮没有新 UUID、没有新 SHA256、没有上板测试结果。
- 不继续 sweep，不晋级标准版，不更新正式 `source.diff`。
- 下一步若继续做硬件定位，需要先减少 trace-light 的 routing 压力，例如降低
  trace stream 数量/深度、只保留 controller+ptr/vector loader+少量 ch0 观测点，
  或改成更小的 mmap-only/entry-probe 定位 kernel。

## 2026-07-09 hollow-probe 软件 smoke

trace-light 路由失败后，新增互斥的 hollow-probe 编译模式，先验证 entry、Status
mmap、controller command fanout 和 loader 层边界。

Host/TAPA 编译检查：

```bash
make cuper-tapa-pcg-callipepla-build-host CUPER_CALLIPEPLA_PROBE_MODE=entry
make cuper-tapa-pcg-callipepla-build-host CUPER_CALLIPEPLA_PROBE_MODE=cmd_drain
make cuper-tapa-pcg-callipepla-build-host \
  CUPER_CALLIPEPLA_PROBE_MODE=loader_drain CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL=1
```

结果：三条命令均通过。

TAPA software smoke：

```bash
CUPER_CALLIPEPLA_PROBE_MODE=entry \
make run-cuper-tapa-pcg-callipepla \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=0 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=0

CUPER_CALLIPEPLA_PROBE_MODE=cmd_drain \
make run-cuper-tapa-pcg-callipepla \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=0 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=0

CUPER_CALLIPEPLA_PROBE_MODE=cmd_drain \
make run-cuper-tapa-pcg-callipepla \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=1 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=0

CUPER_CALLIPEPLA_PROBE_MODE=loader_drain CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL=1 \
make run-cuper-tapa-pcg-callipepla \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=0 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=0

CUPER_CALLIPEPLA_PROBE_MODE=loader_drain CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL=2 \
make run-cuper-tapa-pcg-callipepla \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=0 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=0

CUPER_CALLIPEPLA_PROBE_MODE=loader_drain CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL=3 \
make run-cuper-tapa-pcg-callipepla \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=0 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=0
```

结果：六条 smoke 均返回。关键 probe 输出：

```text
entry MAX_ITERS=0:
[probe] magic=0x43505242 mode_id=1 stage=99 spmv_rounds=0
slot58=16 slot59=0 slot60=1 slot61=11 slot62=16 slot63=16

cmd_drain MAX_ITERS=0:
[probe] magic=0x43505242 mode_id=2 stage=99 spmv_rounds=1
spmv_cmds_with_stop=2 matrix_cmds_with_stop=32 vector_cmds=3 vector_acks=2

cmd_drain MAX_ITERS=1:
[probe] magic=0x43505242 mode_id=2 stage=99 spmv_rounds=2
spmv_cmds_with_stop=3 matrix_cmds_with_stop=48 vector_cmds=8 vector_acks=7

loader_drain level=1/2/3 MAX_ITERS=0:
[probe] magic=0x43505242 mode_id=3 stage=99 spmv_rounds=1
spmv_cmds_with_stop=2 matrix_cmds_with_stop=32 vector_cmds=3 vector_acks=2
```

这些 smoke 只验证 probe graph 可退出，不代表完整 PCG 数值正确性。

## 2026-07-09 entry-probe 硬件构建与同步

构建命令：

```bash
CUPER_TAPA_PCG_CALLIPEPLA_BUILD_DIR=cuper-tapa-pcg-callipepla-probe-entry-xo-build \
CUPER_CALLIPEPLA_PROBE_MODE=entry \
CUPER_CALLIPEPLA_TRACE_LIGHT=0 \
make cuper-tapa-pcg-callipepla-build-xo

make CUPER_TAPA_PCG_CALLIPEPLA_BUILD_DIR=cuper-tapa-pcg-callipepla-probe-entry-xo-build \
  CUPER_CALLIPEPLA_PROBE_MODE=entry \
  CUPER_CALLIPEPLA_TRACE_LIGHT=0 \
  CUPER_CALLIPEPLA_KERNEL_FREQUENCY=100 \
  cuper-tapa-pcg-callipepla-link-xclbin
```

tmux/log：

```text
tmux: project-xplus-cuper-tapa-pcg-callipepla-probe-entry-hw
log: logs/cuper_tapa_pcg_callipepla_probe_entry_hw_20260709_150430.log
build dir: cuper-tapa-pcg-callipepla-probe-entry-xo-build/
```

结果：Vitis link 完成并生成 xclbin。

```text
[16:44:45] Run vpl: FINISHED. Run Status: impl Complete!
INFO: [v++ 60-1230] ... hbm_aclk = 450, KERNEL = 500, DATA = 100
INFO: [v++ 60-586] Created .../CuperPcgCallipepla.xclbin
INFO: [v++ 60-791] Total elapsed time: 1h 40m 21s
```

同步文件：

```text
395bitstream/cuper-tapa-pcg-fpga-u55c-20260709-demo.xclbin
395bitstream/cuper-tapa-pcg-fpga-u55c-20260709-demo.xclbin.info
```

bitstream 信息：

```text
UUID: 7ab50484-4649-ffd5-dd5c-0925c61a9504
SHA256: 88a6750835c9e2b6c3c94e468d6468716730407e21ad9eabbf3df876fca48fcd
INFO SHA256: 0e3a8a283a417e0e0cdbcd8ebc410ce23d3cab605a01110a511fc2e7855d0a16
DATA/KERNEL/HBM clock: 100 / 500 / 450 MHz
```

routed timing summary：

```text
WNS=0.003 ns
TNS=0.000 ns
setup failing endpoints=0
WHS=0.009 ns
THS=0.000 ns
```

当前同步槽已由 2026-07-08 低频 full graph 失败 demo 替换为 2026-07-09 entry-probe
debug artifact。该 artifact 不执行完整 PCG/SpMV datapath，不做性能或数值结论。
服务器侧先跑最小上板 smoke：

```bash
make cuper-tapa-pcg-callipepla-run-hw \
  BITFILE=395bitstream/cuper-tapa-pcg-fpga-u55c-20260709-demo.xclbin \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=0 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=1 DIFF_TOL=1e-3
```

预期检查：

```text
Status[50] = 0x43505242
Status[51] = 1
Status[52] = 99
```

本轮不更新正式 `source.diff`，因为这是 debug/probe artifact，不是 full-PCG 性能提升
候选。

## 2026-07-09 entry-probe 服务器侧大规模上板

用户反馈 `20260709 entry-probe` 在更大规模上板测试全部通过：

```text
Dataset             batch    matrix_len    row/col    result
thermal2_n65536         8          4279      65536    pass
thermal2_n131072       16          8572     131072    pass
thermal2_n262144       32         17553     262144    pass
thermal2              150         85839    1228045    pass
```

完整 `thermal2` 关键输出：

```text
[done] iter=0 status=converged
[probe] magic=0x43505242 mode_id=1 stage=99
slot58=16 slot59=0 slot60=150 slot61=85839 slot62=1228045 slot63=1228045
```

结论：entry-probe 在完整 `thermal2` 规模下也能正常加载 xclbin、传入 AXI-Lite
参数、分配/同步 BO，并把 Status/Metrics/Residuals 写回；板卡 error report 未报错。
这仍然只证明入口和 mmap/参数链路在大数据量下是通的，不代表 full graph 能跑。

## 2026-07-09 cmd-drain 细粒度 checkpoint 构建与同步

entry-probe 全规模通过后，下一档切到 `cmd_drain`，保留真实 controller 和 stage
timer，后级 ptr/matrix/vector consumers 用 drain/fake ack 替代。本轮进一步把
controller 里的第一轮 init SpMV command 从整体 helper 拆成细粒度 checkpoint：
`10/11` total stage begin 前后，`20/21` init_spmv stage begin 前后，`30/31` ptr
command 写入前后，`40/41` 16 路 matrix command fanout 前后，`50/51` SpMV vector
command 前后，`60/61` init-spmv vector fake command 前后，`70/71` fake ack read
前后，`80/81` init_spmv stage end 前后，`90/91` init_zp command 前后，`100/101`
init_zp result read 前后，`110+` 覆盖 stop/finalization。

本地软件 smoke：

```bash
CUPER_CALLIPEPLA_PROBE_MODE=cmd_drain \
make run-cuper-tapa-pcg-callipepla \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=0 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=0

CUPER_CALLIPEPLA_PROBE_MODE=cmd_drain \
make run-cuper-tapa-pcg-callipepla \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=1 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=0
```

两条均返回。关键计数：

```text
MAX_ITERS=0:
[probe] magic=0x43505242 mode_id=2 stage=99 spmv_rounds=1
spmv_cmds_with_stop=2 matrix_cmds_with_stop=32 vector_cmds=3 vector_acks=2
detail0=1 detail1=0 slot60=1 slot61=11 slot62=16 slot63=65536

MAX_ITERS=1:
[probe] magic=0x43505242 mode_id=2 stage=99 spmv_rounds=2
spmv_cmds_with_stop=3 matrix_cmds_with_stop=48 vector_cmds=8 vector_acks=7
detail0=1 detail1=1 slot60=1 slot61=11 slot62=16 slot63=65793
```

硬件构建命令：

```bash
CUPER_TAPA_PCG_CALLIPEPLA_BUILD_DIR=cuper-tapa-pcg-callipepla-probe-cmd-drain-trace-xo-build \
CUPER_CALLIPEPLA_PROBE_MODE=cmd_drain \
CUPER_CALLIPEPLA_TRACE_LIGHT=0 \
CUPER_CALLIPEPLA_KERNEL_FREQUENCY=100 \
make cuper-tapa-pcg-callipepla-hw-tmux
```

tmux/log：

```text
tmux: project-xplus-cuper-tapa-pcg-callipepla-hw
log: logs/cuper_tapa_pcg_callipepla_hw_20260709_211951.log
build dir: cuper-tapa-pcg-callipepla-probe-cmd-drain-trace-xo-build/
```

结果：Vitis link 完成并生成 xclbin。

```text
[23:13:39] Run vpl: FINISHED. Run Status: impl Complete!
INFO: [v++ 60-1230] ... hbm_aclk = 440, KERNEL = 500, DATA = 100
INFO: [v++ 60-586] Created cuper-tapa-pcg-callipepla-probe-cmd-drain-trace-xo-build/CuperPcgCallipepla.xclbin
INFO: [v++ 60-791] Total elapsed time: 1h 49m 34s
```

同步文件仍覆盖同一个 full-PCG demo 槽：

```text
395bitstream/cuper-tapa-pcg-fpga-u55c-20260709-demo.xclbin
395bitstream/cuper-tapa-pcg-fpga-u55c-20260709-demo.xclbin.info
```

bitstream 信息：

```text
UUID: ea2f5c5a-f0f9-c536-8caf-7faa82aa4107
SHA256: 1346b57afcaa1167294a048f00533398be5995b4ebf20c727d6267fdae2a23d3
INFO SHA256: 7862fed40dfa4dc8d5ee70582ac067a723e59de677818ffebcb04a39025e4399
DATA/KERNEL/HBM clock: 100 / 500 / 440 MHz
```

routed timing summary：

```text
WNS=-0.048 ns
TNS=-0.470 ns
setup failing endpoints=34
WHS=0.010 ns
THS=0.000 ns
```

下一步服务器侧先跑 `thermal2_n16 MAX_ITERS=0/1 KERNEL_TIMEOUT_SEC=20
LIVE_STATUS_POLL_SEC=1`。若 `cmd_drain` timeout，问题收敛到 controller
command fanout、stop、stage timer 或 fake ack 路径，并可直接用 `Status[52]`
checkpoint 判断卡点；若返回，再进入 `loader_drain level=1`。

## 2026-07-09/10 cmd-drain thin-status checkpoint 更新

服务器侧上一版 timeout 停在 `Status[52]=11`，说明 controller 已完成入口 live/probe
写和 `total stage begin` 事件写入后的 checkpoint，但尚未进入 init SpMV command。
下一版不继续扩大业务图，而是把 `cmd_drain` 的运行中 probe checkpoint 减薄：入口只写
一次 `Status[50]=0x43505242`、`Status[51]=2` 和规模信息，运行中 checkpoint 只写
`Status[52]`，正常完成时再写完整最终 `Status[50..63]` counters。timeout 解释只看
`Status[52]`；运行中 `detail0/detail1` 可能是 stale 值。

新增 checkpoint：

```text
12: 进入条件判断前
13: 完成 Row_num/Column_num/Max_iters 判断后
14: 完成 Tau <= 0 / Tau != Tau 判断后
15: 进入 breakdown 分支
20: 进入 valid 分支、准备 init_spmv stage begin
```

保留既有业务 checkpoint：`20/21` init_spmv stage begin 前后，`30/31` ptr command
写入前后，`40/41` matrix command fanout 前后，`50/51` SpMV vector command 前后，
`60/61` init-spmv fake vector command 前后，`70/71` fake ack read 前后，`80/81`
init_spmv stage end 前后，`90/91` init_zp command 前后，`100/101` init_zp result
read 前后，`110+` stop/finalization。

本地软件 smoke：

```bash
CUPER_CALLIPEPLA_PROBE_MODE=cmd_drain \
make run-cuper-tapa-pcg-callipepla \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=0 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=0

CUPER_CALLIPEPLA_PROBE_MODE=cmd_drain \
make run-cuper-tapa-pcg-callipepla \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=1 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=0
```

两条均返回 `stage=99`，最终 counters 与上一版一致：

```text
MAX_ITERS=0:
[probe] magic=0x43505242 mode_id=2 stage=99 spmv_rounds=1
spmv_cmds_with_stop=2 matrix_cmds_with_stop=32 vector_cmds=3 vector_acks=2
detail0=1 detail1=0 slot60=1 slot61=11 slot62=16 slot63=65536

MAX_ITERS=1:
[probe] magic=0x43505242 mode_id=2 stage=99 spmv_rounds=2
spmv_cmds_with_stop=3 matrix_cmds_with_stop=48 vector_cmds=8 vector_acks=7
detail0=1 detail1=1 slot60=1 slot61=11 slot62=16 slot63=65793
```

硬件构建命令：

```bash
CUPER_TAPA_PCG_CALLIPEPLA_BUILD_DIR=cuper-tapa-pcg-callipepla-probe-cmd-drain-thinstatus-build \
CUPER_CALLIPEPLA_PROBE_MODE=cmd_drain \
CUPER_CALLIPEPLA_TRACE_LIGHT=0 \
CUPER_CALLIPEPLA_KERNEL_FREQUENCY=100 \
make cuper-tapa-pcg-callipepla-hw-tmux
```

实际使用独立 tmux 会话
`project-xplus-cuper-tapa-pcg-callipepla-thinstatus-hw` 启动同一组
host/XO/xclbin targets，构建日志为
`logs/cuper_tapa_pcg_callipepla_thinstatus_hw_20260709_234820.log`。

构建结果：

```text
[01:29:29] Run vpl: FINISHED. Run Status: impl Complete!
INFO: [v++ 60-1230] ... hbm_aclk = 450, KERNEL = 500, DATA = 100
INFO: [v++ 60-586] Created cuper-tapa-pcg-callipepla-probe-cmd-drain-thinstatus-build/CuperPcgCallipepla.xclbin
INFO: [v++ 60-791] Total elapsed time: 1h 36m 58s
```

bitstream 信息：

```text
UUID: ad7b2a61-23d4-5c05-360d-acb2ee604830
SHA256: 7a83e480304dc16225e83cdc52ba38a9d759051a7085a6494161ca4d274cf6b5
INFO SHA256: f6a8062acd475c123978ed0ff66f5aab42d0ff682fe0b44cc1408fd2a01d73e0
DATA/KERNEL/HBM clock: 100 / 500 / 450 MHz
```

routed timing clean：

```text
WNS=0.003 ns
TNS=0.000 ns
setup failing endpoints=0
WHS=0.009 ns
THS=0.000 ns
hold failing endpoints=0
```

已覆盖同步到同一个 full-PCG demo 槽：

```text
395bitstream/cuper-tapa-pcg-fpga-u55c-20260709-demo.xclbin
395bitstream/cuper-tapa-pcg-fpga-u55c-20260709-demo.xclbin.info
```

该版本仍是 debug artifact，尚未完成服务器侧 demo-only smoke，不晋级标准版，也不
更新正式 `source.diff`。服务器侧先跑 `thermal2_n16 MAX_ITERS=0`；若返回再跑
`MAX_ITERS=1`，均使用 `KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=1`。

## 2026-07-10 controller command/result 顺序修复

thin-status 同步版后续服务器侧最小 smoke timeout 停在 `Status[52]=14`。旧
`cmd_drain` 与正式 full-graph XO 的 controller RTL 都把
`Vector_Result_in_s_empty_n` 纳入 valid 分支入口条件，导致 vector command 尚未发出
时先等待 result；vector phase 又必须先收到 command 才能产生 result，构成调度死锁。

本轮直接修复真实 `PcgCallipepla_Controller`，没有增加 Tau bypass 或新 probe mode。
配对的 vector command/result 改为 nonblocking 状态事务，覆盖初始化和迭代的全部七条
路径。

软件 smoke 使用独立 build dir：

```bash
make run-cuper-tapa-pcg-callipepla \
  CUPER_TAPA_PCG_CALLIPEPLA_BUILD_DIR=cuper-tapa-pcg-callipepla-orderfix-cmd-drain-build \
  CUPER_CALLIPEPLA_PROBE_MODE=cmd_drain \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=0 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=0 DIFF_TOL=1e-3

make run-cuper-tapa-pcg-callipepla \
  CUPER_TAPA_PCG_CALLIPEPLA_BUILD_DIR=cuper-tapa-pcg-callipepla-orderfix-cmd-drain-build \
  CUPER_CALLIPEPLA_PROBE_MODE=cmd_drain \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=1 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=0 DIFF_TOL=1e-3

make run-cuper-tapa-pcg-callipepla \
  CUPER_TAPA_PCG_CALLIPEPLA_BUILD_DIR=cuper-tapa-pcg-callipepla-orderfix-full-build \
  CUPER_CALLIPEPLA_PROBE_MODE= \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=0 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=0 DIFF_TOL=1e-3

make run-cuper-tapa-pcg-callipepla \
  CUPER_TAPA_PCG_CALLIPEPLA_BUILD_DIR=cuper-tapa-pcg-callipepla-orderfix-full-build \
  CUPER_CALLIPEPLA_PROBE_MODE= \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=1 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=0 DIFF_TOL=1e-3
```

结果：四条均返回。`cmd_drain` counters 保持既有语义：

```text
MAX_ITERS=0: stage=99, spmv_rounds=1, vector_cmds=3, vector_acks=2
MAX_ITERS=1: stage=99, spmv_rounds=2, vector_cmds=8, vector_acks=7
full MAX_ITERS=0: max_abs_diff=0, rr=110.4105301696
full MAX_ITERS=1: converged, max_abs_diff=1.086781531434e-08
```

分别生成 `cmd_drain` 与无 probe 正式图 XO：

```text
cuper-tapa-pcg-callipepla-orderfix-cmd-drain-xo-build/CuperPcgCallipepla.xo
cuper-tapa-pcg-callipepla-orderfix-full-xo-build/CuperPcgCallipepla.xo
```

两份 `PcgCallipepla_Controller_csynth.rpt` 均显示：两个初始化事务、外层
`pcg_iteration_loop` 和其中五个迭代事务全部 `Pipelined=no`。两份生成 RTL 均满足：

- vector command write 条件不依赖 `Vector_Result_in_s_empty_n`；
- state `0` 由 `Vector_Command_out_s_full_n` 成功握手切到 state `1`；
- result read 只在 state 非 `0` 且非 `2` 时使能，成功后切到 state `2`；
- 正式图 valid 分支入口 block 条件不再包含 result-empty。

通过 XO 审查后，使用已审查的 `cmd_drain` XO 启动 100 MHz Vitis link：

```text
tmux: project-xplus-cuper-tapa-pcg-callipepla-orderfix-cmd-drain-hw
build dir: cuper-tapa-pcg-callipepla-orderfix-cmd-drain-xo-build/
log: logs/cuper_tapa_pcg_callipepla_orderfix_cmd_drain_hw_20260710_125452.log
requested DATA clock: 100 MHz
final clocks: DATA/KERNEL/HBM = 100/500/450 MHz
```

Vitis link 在 2026-07-10 14:43 完成，`Run Status: impl Complete`，POST-VPL `0 errors`，
总耗时 `1h45m07s`。routed timing summary：

```text
WNS=0.000 ns
TNS=0.000 ns
setup failing endpoints=0
WHS=0.009 ns
THS=0.000 ns
hold failing endpoints=0
```

同步 artifact：

```text
file: 395bitstream/cuper-tapa-pcg-fpga-u55c-20260710-demo.xclbin
UUID: d46c3285-6cc2-1b02-9350-1ad3dadb5c56
SHA256: e24b1bf9e8e5b2c5d262fbb0fed90154940867d95020f43618cb4d4191478cdd
INFO SHA256: 091f9db8072425596743b10255e6227928bf1d4a5e0c0898ad2e4f46cb95ed6c
```

该文件已覆盖同一 `cuper-tapa-pcg` demo 槽，旧 `20260709-demo` 动态结论只作为历史
定位记录。服务器侧仍需先跑：

```bash
make cuper-tapa-pcg-callipepla-run-hw \
  BITFILE=395bitstream/cuper-tapa-pcg-fpga-u55c-20260710-demo.xclbin \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=0 KERNEL_TIMEOUT_SEC=20 LIVE_STATUS_POLL_SEC=1 DIFF_TOL=1e-3
```

返回后再补 `MAX_ITERS=1`。上板前不更新 HTML，也不更新正式 `source.diff`；该 artifact
不晋级标准 bitstream。

## 2026-07-10 独立握手 monitor 定位版

顺序修复版 UUID `d46c3285-6cc2-1b02-9350-1ad3dadb5c56` 在服务器侧
`thermal2_n16 MAX_ITERS=0` timeout，最后可见 `Status[52]=60`。同一 host、板卡和
数据运行旧 entry-probe 正常返回，当前源码 software simulation 也正常，因此问题仍在
综合后硬件边界。

重新检查顺序修复版 XO 后发现：controller schedule 虽然保留 state 0 command
`nbwrite` 和 state 1 result `nbread`，但事务内部的 `61/70/71` Status 写已被 HLS
合并/消除，controller RTL 只保留常量 `60`。所以 `60` 不能继续解释成 command 未被
接受；它也可能表示 command 已接受但 fake-ack/result/controller 后续任一点未推进。

本轮在现有 `cmd_drain` mode 中加入独立事件 monitor，不新增 probe mode。软件 smoke：

```text
cmd_drain MAX_ITERS=0:
  event=99 command_attempts=3 command_full=0 command_accepted=3
  ack_command_received=2 ack_result_sent=2 controller_result_received=2
  flags=0xe controller_done=1 ack_stop=1 drops=0/0

cmd_drain MAX_ITERS=1:
  event=99 command_attempts=8 command_full=0 command_accepted=8
  ack_command_received=7 ack_result_sent=7 controller_result_received=7
  flags=0xe controller_done=1 ack_stop=1 drops=0/0

loader_drain level=1 MAX_ITERS=0:
  stage=99 vector_cmds=3 vector_acks=2

full graph MAX_ITERS=0:
  max_abs_diff=0, rr=110.4105301696

full graph MAX_ITERS=1:
  converged, max_abs_diff=1.086781531434e-08
```

XO 目录：

```text
cuper-tapa-pcg-callipepla-handshake-monitor-cmd-drain-xo-build/
```

XO 静态审查通过：

- `PcgCallipepla_Controller.v` 没有 Status/m_axi_Status 端口；
  `PcgCallipepla_Probe_HandshakeMonitor` 是顶层唯一 Status master。
- controller schedule 同时保留 `NbWriteReq` 对 `Vector_Command_out.full()` 的直接采样
  和 `NbWrite` command 尝试；result `NbRead` predicate 仍是 state 非 0/2。
- controller、monitor 和 fake-ack RTL 中保留 handshake event 常量，正常完成常量
  `99` 由 monitor 写回。
- fake-ack `probe_vector_ack_loop` 为 `Pipelined=no`、iteration latency 2、DSP 0，
  RTL/report 中没有 divider。

使用已审查 XO 启动硬件 link：

```text
tmux: project-xplus-cuper-tapa-pcg-callipepla-handshake-monitor-hw
log: logs/cuper_tapa_pcg_callipepla_handshake_monitor_hw_20260710_174735.log
build dir: cuper-tapa-pcg-callipepla-handshake-monitor-cmd-drain-xo-build/
requested DATA/KERNEL clock: 500/500 MHz
```

Vitis link 已完成：

```text
[21:10:31] Run vpl: FINISHED. Run Status: impl Complete!
Check VPL, containing 1 checks, has run: 0 errors
Check POST-VPL, containing 1 checks, has run: 0 errors
Total elapsed time: 3h 23m 3s
```

最终 xclbin 时钟与 routed timing：

```text
DATA/KERNEL/HBM clock: 138 / 500 / 450 MHz
DATA requested/achieved: 500 / 138.8 MHz
WNS=-5.200 ns
TNS=-26783.914 ns
setup failing endpoints=24766
WHS=0.009 ns
THS=0.000 ns
hold failing endpoints=0
```

实现和封装成功，但请求 500 MHz DATA 下 setup timing 未收敛；该 artifact 只用于
握手定位，不作为 timing-clean 或性能候选。已覆盖同步到同一个 full-PCG demo 槽：

```text
file: 395bitstream/cuper-tapa-pcg-fpga-u55c-20260710-demo.xclbin
UUID: 4a272f84-1e4d-fdb8-0cfa-1fa5e77f433c
SHA256: 840018ee2c1cb30e7e8ed6f5f6c815e9abf830f9e060de850bfc6aeecb17198c
INFO SHA256: 9c62740088b12652bf9ee3249af67b21636118ad8de8b0532b4aa93f8bb411b8
```

旧顺序修复 UUID `d46c3285-6cc2-1b02-9350-1ad3dadb5c56` 的 `Status[52]=60`
timeout 结论只作为历史记录，不再对应当前同步文件。

板上判定：attempts 增长且 full 增长但 accepted 为 0，说明 command FIFO `full_n`
未开放；full 为 0 但 accepted 为 0，说明 `nbwrite` 实现异常；accepted 大于 0 但
ack receive 为 0，问题在 command FIFO/consumer；ack receive 大于 result sent，问题在
fake-ack；result sent 大于 controller received，问题在 result FIFO/controller read；
controller received 已增长但未完成，则继续查 stage timer、stop 或 finalization。

## 2026-07-11 独立握手 monitor 服务器侧板测记录

测试对象固定为：

```text
file: 395bitstream/cuper-tapa-pcg-fpga-u55c-20260710-demo.xclbin
UUID: 4a272f84-1e4d-fdb8-0cfa-1fa5e77f433c
git baseline: bbdb0d9
probe mode: cmd_drain / mode_id=2
```

本地没有同步服务器 raw log；下表按用户提供的服务器侧反馈登记。五组数据集均先跑
`MAX_ITERS=0`，再跑 `MAX_ITERS=1`；最小数据集另补 `10/100` 轮压力测试。

| Dataset | MAX_ITERS | command accepted / result | ack command / result | full | event/drop | XRT/CU |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| `thermal2_n16` | 0 | `3 / 2` | `2 / 2` | 0 | `99`, `0 / 0` | error 空，IDLE |
| `thermal2_n16` | 1 | `8 / 7` | `7 / 7` | 0 | `99`, `0 / 0` | error 空，IDLE |
| `thermal2_n65536` | 0 / 1 | `3 / 2`; `8 / 7` | `2 / 2`; `7 / 7` | 0 | `99`, `0 / 0` | error 空，IDLE |
| `thermal2_n131072` | 0 / 1 | `3 / 2`; `8 / 7` | `2 / 2`; `7 / 7` | 0 | `99`, `0 / 0` | error 空，IDLE |
| `thermal2_n262144` | 0 / 1 | `3 / 2`; `8 / 7` | `2 / 2`; `7 / 7` | 0 | `99`, `0 / 0` | error 空，IDLE |
| `thermal2` | 0 / 1 | `3 / 2`; `8 / 7` | `2 / 2`; `7 / 7` | 0 | `99`, `0 / 0` | error 空，IDLE |
| `thermal2_n16` | 10 | `53 / 52` | `52 / 52` | 0 | `99`, `0 / 0` | error 空，IDLE |
| `thermal2_n16` | 100 | `503 / 502` | `502 / 502` | 0 | `99`, `0 / 0` | error 空，IDLE |

所有运行中 `command_attempts=command_accepted`，controller done、ack stop 和最后一次
write-success flag 均置位；计数满足：

```text
vector commands, including stop = 5 * I + 3
vector results                = 5 * I + 2
ack commands, excluding stop  = 5 * I + 2
```

结论：controller、command FIFO、fake-ack consumer、result FIFO、stage timer stop 和
finalization 已排除死锁。该 artifact 仍不读取真实 ptr/matrix/vector datapath，fake-ack
结果不用于 PCG correctness、分段时间或性能结论，也不更新正式 `source.diff`。下一步
进入 `loader_drain level=1`，只恢复真实 strip ptr HBM 读取、matrix-length fanout 和
`PE_Param` drain。

## 2026-07-11 loader-drain level 1 软件与 XO 验证

固定参数：

```text
CUPER_CALLIPEPLA_PROBE_MODE=loader_drain
CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL=1
CLOCK_PERIOD=10.0
CUPER_CALLIPEPLA_KERNEL_FREQUENCY=100
build dir: cuper-tapa-pcg-callipepla-loader-monitor-level1-xo-build/
```

`thermal2_n16` software/TAPA simulation：

```text
MAX_ITERS=0:
  commands/results=3/2, ack=2/2, ptr_commands=2, PE rounds=1
  ptr_hbm_words=48, event=99, full=0, flags=0x3e, drops=0/0/0

MAX_ITERS=1:
  commands/results=8/7, ack=7/7, ptr_commands=3, PE rounds=2
  ptr_hbm_words=80, event=99, full=0, flags=0x3e, drops=0/0/0

MAX_ITERS=10:
  commands/results=53/52, ack=52/52, ptr_commands=12, PE rounds=11
  ptr_hbm_words=368, event=99, full=0, flags=0x3e, drops=0/0/0
```

该数据集 `Batch_num=1`，因此 word 计数满足
`16 + (I+1)*(Batch_num+1)*16`。同时回归：

- mode 2 `cmd_drain MAX_ITERS=0/1` 保持 `3/2`、`8/7` command/result，
  `command_full=0`、drop=`0/0`；
- 无 probe full graph `MAX_ITERS=0` 保持 `rr=110.4105301696`、diff=0；
- 无 probe full graph `MAX_ITERS=1` 保持 converged，
  `max_abs_diff=1.086781531434e-08`。

XO 生成日志：

```text
logs/cuper_tapa_pcg_callipepla_loader_monitor_level1_xo_20260711_003347.log
XO generated: 2026-07-11 00:42:50
```

XO/RTL 静态审查：

- `PcgCallipepla_Controller.v` 不含 `Status` / `m_axi_Status`；
- 只有 `PcgCallipepla_Probe_LoaderLevel1Monitor.v` 含 Status read/write port；
- `PcgCallipepla_Probe_MatrixLoaderStripDrain.v` 中
  `Matrix_data_read_addr_s_write=0`、`Matrix_data_read_data_s_read=0`，write request/resp
  也全部为 0；HLS report 仍显示 command 和 `Matrix_Len_Stream` read mux；
- ptr loader、PE drain、mode-3 monitor 和 controller 均完成 HLS，XO packaging 成功。

使用该 XO 启动 100 MHz link：

```text
tmux: project-xplus-cuper-tapa-pcg-callipepla-loader-monitor-level1-hw
log: logs/cuper_tapa_pcg_callipepla_loader_monitor_level1_hw_20260711_004344.log
status: Vitis link Run completed, impl Complete, VPL/POST-VPL 0 errors
```

最终构建结果：

```text
build dir: cuper-tapa-pcg-callipepla-loader-monitor-level1-xo-build/
file: 395bitstream/cuper-tapa-pcg-fpga-u55c-20260711-demo.xclbin
UUID: fdbc2e10-20ea-8e78-6b3c-72a01803cde1
SHA256: 0e0e7e832b24afff008665f45ea0020c9cb121cfcf3c2c0f1cf20ea13ee76fe0
INFO SHA256: 5527fd3c42add486a2a193bdc7d08e209efb0d91c89e9ec75aaae62fd83cd2ea
DATA/KERNEL/HBM: 100/500/450 MHz
Vitis link elapsed: 2h38m33s
```

routed timing report：

```text
cuper-tapa-pcg-callipepla-loader-monitor-level1-xo-build/
  reports/link/imp/impl_1_hw_bb_locked_timing_summary_routed.rpt

WNS 0.002 ns
TNS 0.000 ns
setup failing endpoints 0
WHS 0.009 ns
THS 0.000 ns
hold failing endpoints 0
All user specified timing constraints are met.
```

最终 xclbin info 确认 requested/achieved DATA 为 `100/100 MHz`，KERNEL 为
`500/500 MHz`，HBM 为 `450 MHz`。同步时用 `20260711-demo` 替换同主线
`20260710-demo` 文件；上一握手 artifact 的 Git 历史和服务器侧测试记录保留。

服务器侧待验收顺序：

1. `thermal2_n16 MAX_ITERS=0`，再跑 `MAX_ITERS=1`；
2. 通过后覆盖 `thermal2_n65536`、`thermal2_n131072`、`thermal2_n262144` 和完整
   `thermal2` 的 `0/1iter`；
3. 最后补 `thermal2_n16 MAX_ITERS=10/100`。

验收要求：`event=99`，vector command/result、ptr command、PE round 和 ptr HBM word
计数符合公式，`command_full=0`，controller/ack/loader event drop 均为 0，
controller/ack/ptr/PE done flags 全部置位，XRT error 为空且 CU 回到 IDLE。该 artifact
仍不做 PCG correctness 或性能结论，不更新正式 `source.diff`。

## 2026-07-11 loader-drain level 1 服务器侧验收

用户反馈本版已在服务器侧通过。按本轮约定的验收口径，结果为：

```text
event=99
vector command/result、ptr command、PE round、ptr HBM word 计数符合预期
command_full=0
controller/ack/loader event drop=0/0/0
controller/ack/ptr/PE done flags=1
XRT error empty
CU IDLE
```

本地没有同步服务器 raw log，本节按用户提供的服务器侧结论登记。该结果把真实 strip
ptr HBM 读取、matrix-length fanout、`PE_Param` drain 和 stop/finalization 定性为通过；
由于 Matrix_data、vector loader 和 SpMV core 仍未恢复，不做 PCG correctness 或性能
结论。下一定位边界为 `loader_drain level=2`：恢复真实 X/P vector loader 和向量 stream
drain，继续保持 Matrix_data request 关闭、SpMV core 未接入。

## 2026-07-11 loader-drain level 2 软件回归

构建参数：

```text
CUPER_CALLIPEPLA_PROBE_MODE=loader_drain
CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL=2
CLOCK_PERIOD=10.0
CUPER_CALLIPEPLA_KERNEL_FREQUENCY=100
build dir: cuper-tapa-pcg-callipepla-loader-monitor-level2-xo-build/
```

`thermal2_n16` software/TAPA simulation：

```text
MAX_ITERS=0:
  vector commands/rounds/HBM words=2/1/2
  ptr commands=2, PE rounds=1, ptr HBM words=48
  event=99, full=0, flags=0x7e, drops=0/0/0

MAX_ITERS=1:
  vector commands/rounds/HBM words=3/2/4
  ptr commands=3, PE rounds=2, ptr HBM words=80
  event=99, full=0, flags=0x7e, drops=0/0/0

MAX_ITERS=10:
  vector commands/rounds/HBM words=12/11/22
  ptr commands=12, PE rounds=11, ptr HBM words=368
  event=99, full=0, flags=0x7e, drops=0/0/0
```

计数公式：

```text
vector commands including stop = I + 2
vector rounds                 = I + 1
vector HBM double_v8 words    = (I + 1) * ceil(Column_num / 8)
```

同时回归：

- level 1 `MAX_ITERS=0/1` 保持 vector 扩展计数为 0，原 ptr/PE 计数、`flags=0x3e`
  和 `Status[50..63]` 语义不变；
- mode 2 `cmd_drain MAX_ITERS=0/1` 保持 `3/2`、`8/7` command/result，full/drop=0；
- 无 probe full graph `MAX_ITERS=0` 保持 `rr=110.4105301696`、diff=0；
- 无 probe full graph `MAX_ITERS=1` 保持 converged，
  `max_abs_diff=1.086781531434e-08`。

level 2 仍继续使用 fake vector-phase ack，Matrix_data request 和 SpMV core 未恢复；
软件 diff 不作为 PCG correctness 结论。下一步生成并审查独立 level-2 XO，确认
controller 无 Status、loader monitor 是唯一 Status writer、X/P 有真实 read request，
Matrix_data read request 仍全部为 0。

level-2 XO 已生成：

```text
XO: cuper-tapa-pcg-callipepla-loader-monitor-level2-xo-build/CuperPcgCallipepla.xo
generated: 2026-07-11 13:40:19
size: 437647 bytes
log: logs/cuper_tapa_pcg_callipepla_loader_monitor_level2_xo_20260711_133343.log
build exit code: 0
```

XO/RTL 静态审查：

- `PcgCallipepla_Controller.v` 不含 `Status` / `m_axi_Status`；
- 只有顶层 wrapper 和 `PcgCallipepla_Probe_LoaderMonitor.v` 含 Status AXI port，monitor
  是唯一子任务 Status master；
- `PcgCallipepla_Vector_Loader.v` 包含 X0/X1/P0/P1 四组 512-bit m_axi read channel，
  `ARVALID/RREADY` 均连接到真实 pipeline logic；HLS resource report 也保留四组 m_axi；
- `PcgCallipepla_Probe_MatrixLoaderStripDrain.v` 的 read/write request 和 response enable
  仍全部为常量 0；
- monitor RTL 保留 `Vector_Event_in`、vector command/round/HBM-word counters、event 64
  stop 判定以及 Status 47/48/49 写回；
- XO packaging 成功。下一步使用同一 XO 启动 100 MHz Vitis link。
