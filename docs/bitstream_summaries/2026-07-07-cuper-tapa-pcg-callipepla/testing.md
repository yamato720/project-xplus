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

- 已用低频重试生成并同步 demo，下一步是 demo-only 上板 smoke。
- 如果上板出现卡死或 timing 相关异常，再减少 16 路 service 周边局部 fanout/stream
  depth 或做更明确的 SLR/port 布局约束。
- 按 demo-only 规则补 `thermal2_n16` init-only / 1iter 上板 smoke。
- 未完成上板性能确认前不更新正式 `source.diff`。

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
INFO SHA256: 10b9c0b93671abf03c06592d0f2ed28b29c5376971fe1f9af37232126068d185
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
该 xclbin 只作为 demo 候选同步，尚未上板，不晋级标准版。
