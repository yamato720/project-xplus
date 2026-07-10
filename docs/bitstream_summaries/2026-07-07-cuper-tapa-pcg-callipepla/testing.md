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
