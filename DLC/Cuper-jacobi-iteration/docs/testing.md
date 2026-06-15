# Cuper Jacobi 测试流程

本文记录 `DLC/Cuper-jacobi-iteration` 当前 demo 的测试口径。当前阶段已有
software/TAPA simulation 结果；此前完整 `CuperJacobiIteration` graph 的 entry mmap
probe demo 会在 `Finish()` 阶段 timeout。之后的 `CuperJacobiMmapProbeOnly`
split-bank mmap-only probe 已通过 native XRT 上板 smoke，证明 kernel launch、
m_axi 写回、HBM bank 分配和 BO sync 边界可用。当前同步 demo 是
`JACOBI_TRACE_LIGHT=1` 的完整 `CuperJacobiIteration` full graph，
已经生成 `.xclbin` 并同步到 `395bitstream/`，routed timing 已收敛，尚未完成
上板 smoke。

## 1. 测试对象

当前顶层：

```text
CuperJacobiIteration
```

算法口径是普通 Jacobi iteration：

$$
x^{(k+1)} = D^{-1}(b - R x^{(k)})
$$

host 侧把矩阵拆成 `A = D + R`，kernel 侧读单个 `X` buffer 时先取负，让 Cuper SpMV
service 输出 `-R*x_old`，再由 update stage 计算：

$$
x_i^{(k+1)} = (b_i + (-R x^{(k)})_i)\mathrm{diag\_inv}_i
$$

它不是 Jacobi 预条件子 PCG，不计算 PCG 的 `alpha/beta`，也不更新 `r/z/p`。

## 2. 构建和运行

推荐优先使用一键 software regression，避免每个 case 的长输出进入对话上下文：

```bash
make cuper-jacobi-regression-sw MODE=quick
make cuper-jacobi-regression-sw MODE=full
```

`MODE=quick` 跑 `cant.mtx` 和 `thermal2_n65536`；`MODE=full` 额外跑
`thermal2_n262144`。完整日志写到
`cuper-jacobi-iteration-build/regression/<timestamp>_<mode>/`，终端只输出摘要。

从 Project-XPlus 根目录运行：

```bash
make cuper-jacobi-build-host
MAX_ITERS=2 make cuper-jacobi-run-sw MATRIX=DLC/Cuper-jacobi-iteration/data/matrices/cant.mtx
MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n65536
MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n262144
```

从本目录运行：

```bash
cd DLC/Cuper-jacobi-iteration
make build-host
MAX_ITERS=2 make run-sw MATRIX=data/matrices/cant.mtx
MAX_ITERS=1 make run-sw MATRIX=../../data/suitesparse/Schmid/csr/thermal2_n65536
MAX_ITERS=1 make run-sw MATRIX=../../data/suitesparse/Schmid/csr/thermal2_n262144
```

只跑某个 case 时：

```bash
make cuper-jacobi-regression-sw CASE=thermal2_n65536 NO_BUILD=1
make cuper-jacobi-regression-sw CASES="cant thermal2_n65536" NO_BUILD=1
```

`run-sw` 不需要 `BITFILE`。mmap-only micro probe 的历史边界验证仍可用 native XRT
runner 复现，但当前同名 demo 文件已经被 full graph 覆盖；若要复现该历史 probe，
需要使用对应归档或重新构建 `CuperJacobiMmapProbeOnly` xclbin：

```bash
BITFILE=/path/to/CuperJacobiMmapProbeOnly.xclbin \
  ROW_NUM=16 MAX_ITERS=1 make cuper-jacobi-run-mmap-probe-xrt
```

## 3. 当前记录数据

| 数据集 | 矩阵规模 | 迭代 | 当前记录 | 关键输出 |
| --- | --- | ---: | --- | --- |
| `cant.mtx` | N=62,451, NNZ=4,007,383, R NNZ=3,944,932 | 2 | 当前 deadlock-debug 单 `X` ABI 通过 | `Status=1`, `Final buffer=0`, `Iterations=2`, `Final diff=0`, `Error Num=0` |
| `thermal2_n65536` | N=65,536, NNZ=437,000, R NNZ=371,464 | 1 | 当前 deadlock-debug 单 `X` ABI 通过 | `Status=1`, `Final buffer=0`, `Iterations=1`, `Final diff=0`, `Error Num=0` |
| `thermal2_n262144` | N=262,144, NNZ=1,748,980 | 1 | 早期 software run 通过 | `Final diff=1.41496`, `Error Num=0`；还需用当前 root target 补跑 |

## 3.1 当前 demo bitstream

| 项目 | 内容 |
| --- | --- |
| 同步文件 | `395bitstream/cuper-tapa-jacobi-u55c-20260614-demo.xclbin` |
| 构建目录 | `cuper-jacobi-iteration-build/` |
| Kernel | `CuperJacobiIteration` |
| ABI | light-trace full graph；`B` HBM[20]，`Diag_inv` HBM[21]，`X` HBM[22]，`Status/Metrics/Debug` HBM[24]/HBM[25]/HBM[26] |
| UUID | `3fc9b8f4-901b-008f-8bc9-26ea3bf6f0c1` |
| SHA256 | `4d1fb090afebcf75d8087156665d969f02105813f984935feb8818c31afc38ab` |
| DATA / KERNEL / HBM clock | `150 MHz` / `500 MHz` / `450 MHz` |
| 时序状态 | 已收敛：WNS `0.003 ns`，TNS `0.000 ns`，setup failing endpoints `0`；hold worst slack `0.009 ns` |

Vitis link 已完成 implementation 和 `.xclbin` 封装，`Run completed`；构建总耗时
`3h 52m 13s`，汇总日志在
`cuper-jacobi-iteration-build/logs/build_hw_tmux.log`。这版已同步到 `395bitstream/`，
覆盖上一版同名 `20260614` 15 路 light-trace full graph demo，但还没有完成板上
smoke。上一版 15 路 demo UUID 为 `ef3b1102-90ec-551a-d1e9-55fb6c023da5`，DATA clock
为 `164 MHz`，timing 未收敛：WNS `-2.764 ns`，TNS `-70810.594 ns`。再上一版
7 路 demo UUID 为 `6dfaf1e3-9707-7f46-b914-1f59ca240993`，
其上板 `thermal2_n16` 和 `thermal2_n1024` 已通过、`thermal2_n65536` 仍卡
`Finish()`；该结果只对应旧 UUID。再上一版 no-debug demo UUID 为
`b233c1af-6ba7-ebc5-8a5b-c56d348c53c7`，构建完成但未板测；mmap-only probe UUID 为
`380f9de1-e5c1-66ab-b888-db99d2ef3523`，native XRT smoke 通过。旧测试结论只作为历史记录。

2026-06-13 已完成上一版 split-bank mmap-only demo 的 native XRT 上板 smoke：

```text
logs: logs/jacobi_mmap_probe_hw_20260613_214342/
ROW_NUM=16 MAX_ITERS=1: rc=0, wait_state=COMPLETED
ROW_NUM=1024 MAX_ITERS=1: rc=0, wait_state=COMPLETED
```

关键写回：

```text
ROW_NUM=16:
  Status[0..3]=1245921841,16,1,16
  Status[8..11]=1245921841,16,1,1
  Metrics[8..11]=1245921841,16,1,1
  Debug[48..51]=1245921841,11,1,8192

ROW_NUM=1024:
  Status[0..3]=1245921841,1024,1,1024
  Status[8..11]=1245921841,1024,1,64
  Metrics[8..11]=1245921841,1024,1,64
  Debug[48..51]=1245921841,11,1,8192
```

`1245921841` 是 probe magic `0x4a434231`。wait 前的 sample sync 已能读到这些值，
最终 `run.wait()` 也返回 `COMPLETED`。因此“kernel 没启动 / output mmap 完全写不回 /
native XRT BO sync 不通”不再是当前最优先嫌疑；完整 graph 的问题应继续看
TAPA/FRT `Finish()`、完整 graph stop/drain、debug 阻塞写对 graph 的影响，以及旧
full graph artifact 的 timing violation。

## 3.2 full graph debug 边界调整

2026-06-13 mmap-only micro probe 通过后，完整 graph 不再默认使用入口阻塞式 mmap
probe：

- `JACOBI_DEADLOCK_DEBUG=1` 仍启用 `Debug` buffer 和 event stream，但
  `Jacobi_DebugMonitor` 不再在入口阻塞写 `Debug[0]` / `Debug[48..51]`。
- `Jacobi_RoundDispatcher` 不再默认阻塞写 `Status[8..11]` /
  `Metrics[8..11]`。
- 若要复现旧入口 probe，需要同时设置
  `JACOBI_DEADLOCK_DEBUG=1 JACOBI_BLOCKING_ENTRY_PROBE=1`。
- host 现在把 `Status`、`Metrics`、`Debug` 初始化为 sentinel，并用
  `read_write_mmap` 传参。若上板卡在 `Finish()` 且 pre-Finish dump 仍是 sentinel，
  说明 kernel 没覆盖这些槽位；若变成 0 或部分改变，则说明 D2H/写回边界已经穿透。

本轮源码验证：

```bash
git diff --check
bash -n DLC/Cuper-jacobi-iteration/scripts/build_xo_u55c.sh \
       DLC/Cuper-jacobi-iteration/scripts/link_xclbin_u55c.sh \
       DLC/Cuper-jacobi-iteration/scripts/build_host.sh \
       DLC/Cuper-jacobi-iteration/scripts/run_hw.sh
python3 -m py_compile DLC/Cuper-jacobi-iteration/scripts/launcher.py
make cuper-jacobi-build-host
MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n16
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-build-host
JACOBI_DEADLOCK_DEBUG=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n16
make cuper-jacobi-build-host
```

结果：

| 模式 | 数据集 | 结果 |
| --- | --- | --- |
| no-debug full graph software | `thermal2_n16 MAX_ITERS=1` | `Status=1`, `Iterations=1`, `Error Num=0` |
| nonblocking-debug full graph software | `thermal2_n16 MAX_ITERS=1` | `Status=1`, `Iterations=1`, `Error Num=0`，无 `Debug_Event_Stream` leftover 警告 |

这条记录对应 2026-06-13 的 no-debug/nonblocking-debug 边界。当前硬件 debug 默认使用
后面的 `JACOBI_TRACE_LIGHT=1`，并且仍保持不带 `JACOBI_BLOCKING_ENTRY_PROBE`；
如果仍卡 `Finish()`，用 pre-Finish dump 和 trace slot 判断
`Status/Metrics/Debug` 是否被 kernel 覆盖。

## 3.2.1 isotope trace debug

2026-06-14 新增 `JACOBI_TRACE_ISOTOPE=1` 调试 ABI，用来像“同位素标记”一样记录
每条关键数据流最后到达的位置。它和旧 `JACOBI_DEADLOCK_DEBUG=1` 共用
`JACOBI_TRACE_ENABLED`。full isotope 覆盖 47 路 source，软件仿真可用；硬件定位
默认优先使用后面的 `JACOBI_TRACE_LIGHT=1`。

trace 版只由业务 task 非阻塞 `try_write` 事件，`Jacobi_DebugMonitor` 单独写
`Debug` BO。host 已经把 TAPA invoke 拆成
`WriteToDevice -> Exec -> ReadFromDevice -> pre-Finish dump -> Finish`，因此即使
full graph 卡在 `Finish()`，也能在 `Finish()` 前读出 Debug BO 快照。

Debug BO 当前大小为 256 个 `int`，主要槽位：

```text
Debug[0]                 heartbeat / entry magic
Debug[1]                 event_count
Debug[2]                 packed last event: source, phase, lane
Debug[3]                 last event value
Debug[5]                 Debug BO write issue count
Debug[6]                 Debug BO write response count
Debug[7]                 Debug monitor stop_seen
Debug[48..51]            magic, source_count, entry_phase, stop_drain_cycles
Debug[64 + source*4 + 0] source last phase
Debug[64 + source*4 + 1] source last lane / wait code
Debug[64 + source*4 + 2] source last value / progress count
Debug[64 + source*4 + 3] source last event_count
```

当前 source 编号覆盖：

```text
1 dispatcher
2 ptr_loader
3 vector_loader
4..19 matrix_loader[0..15]
20..35 accumulator[0..15]
36 frame_fork
37 coeff_loader
38..45 pair_compute[0..7]
46 pack_writer
47 x_hbm_writer
```

构建命令：

```bash
JACOBI_TRACE_ISOTOPE=1 make cuper-jacobi-build-host
JACOBI_TRACE_ISOTOPE=1 make cuper-jacobi-build-xo
JACOBI_TRACE_ISOTOPE=1 make cuper-jacobi-link-xclbin
```

trace 硬件连接会把 `Status/Metrics/Debug` 分到 `HBM[24]/HBM[25]/HBM[26]`，沿用
`CuperJacobiMmapProbeOnly` split-bank smoke 已验证过的 mmap 边界。当前 trace host
会在 `Finish()` 前做周期性 `ReadFromDevice()`：首次等待
`JACOBI_PREFINISH_SAMPLE_DELAY_MS=250` 毫秒，之后按
`JACOBI_PREFINISH_POLL_INTERVAL_MS=1000` 毫秒采样，最多
`JACOBI_PREFINISH_POLL_COUNT=60` 次；如果采样中看到 `Status[0]` 已被 kernel 覆盖，
会提前进入 `Finish()`。默认每 `JACOBI_PREFINISH_FULL_DUMP_INTERVAL=10` 次采样打印
一次完整 Debug source 表，设为 0 可只保留简短 summary。

已跑软件验证：

```bash
JACOBI_TRACE_ISOTOPE=1 make cuper-jacobi-build-host
JACOBI_TRACE_ISOTOPE=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n16
JACOBI_TRACE_ISOTOPE=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024
```

结果：

| 模式 | 数据集 | 结果 |
| --- | --- | --- |
| trace full graph software | `thermal2_n16 MAX_ITERS=1` | `Status=1`, `Iterations=1`, `Error Num=0`; Debug[48..51]=`1245921841,47,1,8192` |
| trace full graph software | `thermal2_n1024 MAX_ITERS=1` | `Status=1`, `Iterations=1`, `Error Num=0`; 47 个 source 槽位均可见 |

### 3.2.2 light trace debug

full isotope 版在硬件构建中代价过高：47 路 trace stream 会让 `Jacobi_DebugMonitor`
带上大量 FIFO/peek 端口，HLS/resource synthesis 阶段曾出现单个 `vitis_hls` 进程接近
70GB RSS。当前硬件 debug 默认改用 `JACOBI_TRACE_LIGHT=1`。

light trace 仍保留 Debug BO、pre-Finish dump 和 `Debug[48..51]` probe。当前已同步的
`20260614-demo` xclbin 接 16 个 light trace stream，额外接入 matrix loader0 首拍和
8 路 pair compute，用来定位 update 阶段是在等 matrix/pointer/vector、accumulator、
coeff，还是 pack/writeback：

```text
1 dispatcher
2 ptr_loader
3 vector_loader
4 matrix_loader0
36 frame_fork
37 coeff_loader
38..45 pair_compute[0..7]
46 pack_writer
47 x_hbm_writer
```

已跑软件验证：

```bash
JACOBI_TRACE_LIGHT=1 make cuper-jacobi-build-host
JACOBI_TRACE_LIGHT=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n16
JACOBI_TRACE_LIGHT=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024
JACOBI_TRACE_LIGHT=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n65536
```

结果：

| 模式 | 数据集 | 结果 |
| --- | --- | --- |
| 7 路 light trace full graph software | `thermal2_n16 MAX_ITERS=1` | `Status=1`, `Iterations=1`, `Error Num=0`; Debug[48..51]=`1245921841,7,1,8192` |
| 7 路 light trace full graph software | `thermal2_n1024 MAX_ITERS=1` | `Status=1`, `Iterations=1`, `Error Num=0`; 7 个关键 source 槽位均可见 |
| 16 路 light trace full graph software | `thermal2_n1024 MAX_ITERS=1` | `Status=1`, `Iterations=1`, `Error Num=0`; matrix loader0 首拍和 pair_compute[0..7] 槽位均可见 |
| 16 路 light trace full graph software | `thermal2_n65536 MAX_ITERS=1` | `Status=1`, `Iterations=1`, `Error Num=0`; matrix loader0、pair_compute[0..7]、pack_writer、x_hbm_writer 均进入 stop |

硬件构建命令：

```bash
JACOBI_TRACE_ISOTOPE=0 JACOBI_TRACE_LIGHT=1 FORCE=1 make cuper-jacobi-hw-tmux
```

该命令已生成并同步当前 `20260614-demo` xclbin；这只是 debug demo，尚未上板
smoke。

## 3.3 finite pair compute 源码验证

2026-06-12 复测当时的 395 Jacobi demo 后，`thermal2_n1024 MAX_ITERS=1` 仍然卡在
`after ReadFromDevice before Finish`。当前源码已按
`finish_nonreturn_monitoring_points.md` 的建议，把 8 个 `Jacobi_UpdatePairCompute`
从 `tapa::detach` 无限循环改成 frame/stop 驱动的有限 task：

```text
Jacobi_UpdateFrameFork -> Update_Pair_Frame_Stream[0..7]
Jacobi_UpdatePairCompute[0..7] 收到 stop frame 后 return
```

已跑验证：

```bash
make cuper-jacobi-build-host
make cuper-jacobi-regression-sw MODE=quick NO_BUILD=1 ALLOW_MISSING=1
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-build-host
JACOBI_DEADLOCK_DEBUG=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024
JACOBI_DEADLOCK_DEBUG=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n16
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-build-xo
```

结果：

```text
quick regression: pass=2 fail=0 skip=0
thermal2_n1024 MAX_ITERS=1 debug ABI software/TAPA simulation: Error Num=0
thermal2_n16 MAX_ITERS=1 debug ABI software/TAPA simulation: Error Num=0
XO: cuper-jacobi-iteration-build/CuperJacobiIteration.xo generated
```

这版源码随后曾重新生成 `.xclbin` 并同步为当时的
`395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin`；该 artifact 已包含
finite pair compute 改动。

## 3.4 finite pair compute 硬件构建

构建目录：

```text
cuper-tapa-jacobi-u55c-20260612-finite-pair-debug-build/
```

关键结果：

```text
xclbin: cuper-tapa-jacobi-u55c-20260612-finite-pair-debug-build/CuperJacobiIteration.xclbin
sync: 395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin
UUID: 6ad9f2dd-d23f-6ab2-c8bb-1129f00d27bb
SHA256: e981baf0f809065674f9bc696095bfa0d2e816ffb281c3dfe6dfeb8e8990a145
DATA clock: 182 MHz
KERNEL clock: 500 MHz
HBM clock: 450 MHz
DATA achieved: 182.9 MHz
Run status: impl Complete, Run completed
Total elapsed: 4h 19m 47s
```

时序状态：

```text
Timing constraints are not met.
Setup failing endpoints: 86957
Setup worst slack: -2.134 ns
Setup total violation: -46314.336 ns
Hold failing endpoints: 0
Hold worst slack: 0.005 ns
```

这版已搬到 Jacobi demo 槽，但仍不是 timing-clean bitstream；还没有完成板上
`hw` 验证。

## 3.5 pre-Finish/empty-R 源码验证和硬件构建

当时上一版 finite-pair demo 上板 `thermal2_n16 MAX_ITERS=1` 仍 timeout，host 停在
`after ReadFromDevice before Finish`；probe 显示 CU 已 `IDLE`，firewall GOOD，
但存在 `[CuperJacobiIter]` D 状态线程。本轮改动：

```text
host/main.cpp: Finish() 前打印 Status/Metrics/Debug BO 快照。
jacobi_vector_loader.hpp: Batch_num==0 时不读 X、不写 Vector_X_Stream。
spmv_service_drains.hpp: Batch_num==0 时链尾 X drain expected_packets=0。
```

已跑验证：

```text
make cuper-jacobi-build-host: passed
thermal2_n16 MAX_ITERS=1 software/TAPA simulation: Error Num=0
quick regression: pass=2 fail=0 skip=0
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-build-host: passed
thermal2_n16 MAX_ITERS=1 debug ABI software/TAPA simulation: Error Num=0
thermal2_n1024 MAX_ITERS=1 debug ABI software/TAPA simulation: Error Num=0
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-build-xo: generated CuperJacobiIteration.xo
```

硬件构建：

```text
build dir: cuper-tapa-jacobi-u55c-20260613-prefinish-empty-r-debug-build/
sync: 395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin
UUID: 5c9f0e72-5ea9-7142-1e90-690b72d30557
SHA256: 0d300c1f55c21078f1f24d5e551228ccc75855331585d6669bc3e15ac31b9c26
DATA/KERNEL/HBM: 175/500/427 MHz
DATA achieved: 175.2 MHz
timing: not met, WNS -2.373 ns, TNS -51779.359 ns, failing endpoints 91026
elapsed: 3h 47m 24s
```

这版已经覆盖 Jacobi demo 槽。后续上板验证显示它仍然不能正常返回，见下一节。

## 3.6 pre-Finish/empty-R 上板失败与入口 mmap probe

服务器侧复测上一版 pre-Finish/empty-R 395 demo：

```text
bitstream: 395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin
UUID: 5c9f0e72-5ea9-7142-1e90-690b72d30557
SHA256: 0d300c1f55c21078f1f24d5e551228ccc75855331585d6669bc3e15ac31b9c26
logs: logs/jacobi_prefinish_empty_r_hw_20260613_112806/
```

结果：

```text
thermal2_n16 MAX_ITERS=1: rc=124, 120s timeout
thermal2_n1024 MAX_ITERS=1: rc=124, 120s timeout
host stop point: [tapa-invoke] after ReadFromDevice before Finish
prefinish Status[0..2]: 0,0,0
prefinish Metrics[0..7]: 0,0,0,0,0,0,0,0
prefinish Debug: heartbeat=0, event_count=0, stop_marker=0
xbutil CU status: IDLE
firewall: GOOD
```

这个结果说明当前失败不再像单纯 PL 业务数据流还在 RUN。`ReadFromDevice()` 已经执行，
但 Status/Metrics/Debug 没有任何 kernel 写回痕迹；下一步要先区分 kernel 入口 task
是否启动、HBM[24] 上 mmap 写回是否可见，以及 `ReadFromDevice`/`Finish` 迁移顺序
是否掩盖了已写数据。

当前源码已加入 debug-only 入口 mmap probe，仍由 `JACOBI_DEADLOCK_DEBUG=1` 控制：

```text
Jacobi_DebugMonitor:
  Debug[0]       = 0x4a434231，阻塞写并等待 write response
  Debug[48..51]  = magic, debug stream count, entry phase, stop drain cycles

Jacobi_RoundDispatcher:
  Status[8..11]  = magic, Row_num, Max_iters, float_v16 packet count
  Metrics[8..11] = magic, Row_num, Max_iters, float_v16 packet count
```

host 现在会在 `Finish()` 前打印：

```text
[jacobi-prefinish-probe] Status[8..11]=...
[jacobi-deadlock-probe] Debug[48..51]=...
```

并在正常返回后额外打印：

```text
[jacobi-final-probe] Status[8..11]=...
```

这版源码验证：

```bash
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-build-host
JACOBI_DEADLOCK_DEBUG=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n16
JACOBI_DEADLOCK_DEBUG=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-build-xo
```

关键结果：

```text
thermal2_n16 software/TAPA simulation: Error Num=0
  [jacobi-final-probe] Status[8..11]=1245921841,16,1,1
  [jacobi-final-probe] Metrics[8..11]=1245921841,16,1,1
  [jacobi-deadlock-probe] Debug[48..51]=1245921841,11,1,8192

thermal2_n1024 software/TAPA simulation: Error Num=0
  [jacobi-final-probe] Status[8..11]=1245921841,1024,1,64
  [jacobi-final-probe] Metrics[8..11]=1245921841,1024,1,64
  [jacobi-deadlock-probe] Debug[48..51]=1245921841,11,1,8192

XO: cuper-jacobi-iteration-build/CuperJacobiIteration.xo generated
```

这一步先证明源码和 XO 能通过。随后已用 entry mmap probe 源码重新生成完整
`.xclbin` 并同步到 Jacobi demo 槽，见下一节。

## 3.7 entry mmap probe 硬件构建

构建结果：

```text
build dir: cuper-tapa-jacobi-u55c-20260613-entry-mmap-probe-debug-build/
build log: cuper-tapa-jacobi-u55c-20260613-entry-mmap-probe-debug-build/logs/build_hw_tmux.log
xclbin: cuper-tapa-jacobi-u55c-20260613-entry-mmap-probe-debug-build/CuperJacobiIteration.xclbin
sync: 395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin
UUID: 7bf54cce-83a3-b7e7-97a9-719446658c03
SHA256: 775d1da4c1c2f51ec58e0569950f618eb159481bf3eddea4e27b8f6a4da9eb24
DATA clock: 175 MHz
KERNEL clock: 500 MHz
HBM clock: 450 MHz
DATA achieved: 175.9 MHz
VPL: FINISHED, Run Status: impl Complete
v++ link: Run completed
total elapsed: 4h 2m 20s
```

时序状态：

```text
Timing constraints are not met.
Setup failing endpoints: 101235
Setup worst slack: -2.350 ns
Setup total violation: -60974.352 ns
Hold failing endpoints: 0
Hold worst slack: 0.009 ns
```

结论：这版已经包含入口 mmap probe，并已同步到 `395bitstream/` 的 Jacobi demo 槽。
它仍不是 timing-clean bitstream。新版上板 smoke 结果见下一节；上一版
pre-Finish/empty-R demo 的 `Finish()` 不返回结论只对应旧 UUID。

## 3.8 entry mmap probe 上板失败记录

服务器侧复测当前 entry mmap probe 395 demo：

```text
bitstream: 395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin
UUID: 7bf54cce-83a3-b7e7-97a9-719446658c03
SHA256: 775d1da4c1c2f51ec58e0569950f618eb159481bf3eddea4e27b8f6a4da9eb24
logs: logs/jacobi_entry_mmap_probe_hw_20260613_171648/
```

结果：

```text
thermal2_n16 MAX_ITERS=1: rc=124, 120s timeout
thermal2_n1024 MAX_ITERS=1: rc=124, 120s timeout
host stop point: [tapa-invoke] after ReadFromDevice before Finish
prefinish Status[0..2]: 0,0,0
prefinish Metrics[0..7]: 0,0,0,0,0,0,0,0
prefinish Status[8..11]: 0,0,0,0
prefinish Metrics[8..11]: 0,0,0,0
prefinish Debug[48..51]: 0,0,0,0
```

`thermal2_n16` 是 `R NNZ=0` 的 empty-R case；`thermal2_n1024` 是
`R NNZ=5338` 的非空 R case。两个 case 均看不到入口 mmap probe 写回，因此当前失败
不能继续只按 empty-R 特例解释。timeout 后 CU 通常显示 `IDLE`，firewall `GOOD`，
所以也不能直接定性为 PL 内部 dataflow 死锁。

本轮详细分析和下一版修改建议见：

```text
DLC/Cuper-jacobi-iteration/docs/entry_mmap_probe_failure_analysis.md
```

## 3.9 mmap-only micro top 与 native XRT runner

已按 `entry_mmap_probe_failure_analysis.md` 的 P0/P1 建议新增独立 debug top 和
native XRT runner：

```text
CuperJacobiMmapProbeOnly
host/mmap_probe_xrt.cpp
scripts/run_mmap_probe_xrt.sh
```

`CuperJacobiMmapProbeOnly` 只写 Status/Metrics/Debug 的固定槽位并等待 write response
后返回，不接入 Cuper SpMV service、Jacobi update、stage timer、debug event stream
或 feedback token。它用于先验证 mmap 写回、HBM bank 分配和 runtime wait/sync 边界。

新增 root target：

```bash
make cuper-jacobi-build-mmap-probe-xrt-host
make cuper-jacobi-build-mmap-probe-xo
make cuper-jacobi-link-mmap-probe-xclbin
make cuper-jacobi-link-mmap-probe-xclbin-split
BITFILE=/path/to/CuperJacobiMmapProbeOnly.xclbin \
  ROW_NUM=16 MAX_ITERS=1 make cuper-jacobi-run-mmap-probe-xrt
```

其中 `link-mmap-probe-xclbin` 把 Status/Metrics/Debug 都接到 HBM[24]；
`link-mmap-probe-xclbin-split` 使用 HBM[24]/HBM[25]/HBM[26]，用于区分同 bank 三个
m_axi master 的问题。

已完成本地验证：

```text
make cuper-jacobi-build-mmap-probe-xrt-host: passed
make cuper-jacobi-build-mmap-probe-xo: generated cuper-jacobi-iteration-build/CuperJacobiMmapProbeOnly.xo
split-bank xclbin native XRT hw smoke: ROW_NUM=16/1024 both rc=0, wait_state=COMPLETED
```

当前 deadlock-debug 单 `X` ABI 已记录的计数。最新一次 quick regression 日志在
`cuper-jacobi-iteration-build/regression/20260612_124905_quick/`：

| 数据集 | `float_v16_packets` | `spmv_update_packets` | `spmv_update` cycles | `controller_total` cycles | `timer_total` cycles | `spmv_update_avg` cycles |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `cant.mtx` / `MAX_ITERS=2` | 3,904 | 7,808 | 103,856 | 103,863 | 103,918 | 51,928 |
| `thermal2_n65536` / `MAX_ITERS=1` | 4,096 | 4,096 | 36,081 | 36,088 | 38,155 | 36,081 |

## 4. 输出字段

host 会打印这些 Jacobi 专用字段：

```text
[Jacobi On FPGA] Status
[Jacobi On FPGA] Final buffer
[Jacobi On FPGA] Iterations
[Jacobi On FPGA] Final diff
[jacobi-timing-work]
[jacobi-stage-cycles]
[jacobi-stage-ms]
[Verification] Error Num
```

`Status` 和 `Metrics` 当前布局：

| 字段 | 含义 |
| --- | --- |
| `Status[0]` | 退出状态：`0` converged，`1` max-iter，`2` breakdown |
| `Status[1]` | 最终解所在 buffer：当前单 `X` ABI 固定为 `0` |
| `Status[2]` | 已完成 Jacobi 迭代轮数 |
| `Metrics[0]` | 当前固定迭代版本暂为占位 0；数值对齐看 host `Error Num` |
| `Metrics[1]` | 已完成 Jacobi 迭代轮数 |
| `Metrics[2]` | 每轮 `float_v16` 包数 |
| `Metrics[3]` | 已处理的 SpMV/update 包数累计 |
| `Metrics[4]` | SpMV+update 累计 cycle |
| `Metrics[5]` | controller 主体累计 cycle |
| `Metrics[6]` | timer 存活总 cycle |
| `Metrics[7]` | 平均每轮 SpMV+update cycle |

一键 regression 的摘要文件：

| 文件 | 内容 |
| --- | --- |
| `summary.md` | 人读表格，包含每个 case 的 PASS/FAIL 和关键字段 |
| `summary.tsv` | 脚本/表格工具读取的扁平指标 |
| `<case>.log` | 单个 case 的完整 host/TAPA 输出 |

## 5. 后续补测

后续硬件测试按下面顺序补记录：

1. 先跑 `make cuper-jacobi-build-host` 和 software smoke，确认 host/ABI 没坏。
2. 历史 `CuperJacobiMmapProbeOnly` split-bank xclbin 已用 native XRT runner
   通过 `ROW_NUM=16/1024` smoke；后续如改 runner 或 bank 分配再重建并复测该边界。
3. 当前同步的完整 `CuperJacobiIteration` light-trace xclbin 已生成且 timing clean；
   上板先从 `thermal2_n16 MAX_ITERS=1` 和 `thermal2_n1024 MAX_ITERS=1` smoke 开始。
4. 再补 `cant.mtx`、`thermal2_n65536`、`thermal2_n262144` 的 `MAX_ITERS=1/2`。
5. 对完整 graph 记录 `Status`、`Final buffer`、`Iterations`、`Final diff`、
   `jacobi-stage-*` 和 `Error Num`。
6. 同步更新 `docs/bitstream_summaries/2026-06-10-cuper-tapa-jacobi-iteration/`
   和 `395bitstream/README.md`。
