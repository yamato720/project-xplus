# 测试记录

当前记录覆盖 software/TAPA simulation，并记录 deadlock-debug ABI 硬件 demo xclbin 的
构建和同步结果。这版 routed timing 未收敛，还没有上板性能数据。

## 已执行命令

推荐以后优先使用一键 regression，减少对话上下文里的长输出：

```bash
make cuper-jacobi-regression-sw MODE=quick
make cuper-jacobi-regression-sw MODE=full
```

日志目录：

```text
cuper-jacobi-iteration-build/regression/<timestamp>_<mode>/
```

其中 `summary.md` 和 `summary.tsv` 是摘要，`<case>.log` 是单个 case 的完整输出。

构建 host：

```bash
make cuper-jacobi-build-host
```

当前 deadlock-debug 单 `X` ABI smoke：

```bash
MAX_ITERS=2 make cuper-jacobi-run-sw MATRIX=DLC/Cuper-jacobi-iteration/data/matrices/cant.mtx
MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n65536
```

早期功能 smoke：

```bash
cd DLC/Cuper-jacobi-iteration
MAX_ITERS=1 make run-sw MATRIX=../../data/suitesparse/Schmid/csr/thermal2_n262144
```

## 当前结果

### `cant.mtx`

命令：

```bash
MAX_ITERS=2 make cuper-jacobi-run-sw MATRIX=DLC/Cuper-jacobi-iteration/data/matrices/cant.mtx
```

关键输出：

```text
Matrix: 62451 x 62451
NNZ: 4007383
R NNZ: 3944932
CPU final diff: 0.73218
CPU iterations: 2
FPGA Status: 1
Final buffer: 0
Iterations: 2
Final diff: 0
float_v16_packets=3904
spmv_update_packets=7808
spmv_update=103856
controller_total=103863
timer_total=103918
spmv_update_avg=51928
Error Num: 0
```

结论：当前 deadlock-debug 单 `X` ABI 通过，CPU reference 和 TAPA software run 对齐。
当前固定迭代版本不做收敛判断，kernel `Final diff` 字段暂为占位 0，数值检查看
`Error Num=0`。

### `thermal2_n65536`

命令：

```bash
MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n65536
```

关键输出：

```text
Matrix: 65536 x 65536
NNZ: 437000
R NNZ: 371464
CPU final diff: 1.11631
CPU iterations: 1
FPGA Status: 1
Final buffer: 0
Iterations: 1
Final diff: 0
float_v16_packets=4096
spmv_update_packets=4096
spmv_update=36081
controller_total=36088
timer_total=38155
spmv_update_avg=36081
Error Num: 0
```

结论：当前 deadlock-debug 单 `X` ABI 通过，`Error Num=0`。

### `thermal2_n262144`

命令：

```bash
cd DLC/Cuper-jacobi-iteration
MAX_ITERS=1 make run-sw MATRIX=../../data/suitesparse/Schmid/csr/thermal2_n262144
```

关键输出：

```text
Final diff: 1.41496
Error Num: 0
```

结论：早期 software run 通过；还需要用当前根 `Makefile`
`cuper-jacobi-run-sw` 目标补跑，补齐 `jacobi-stage-*` 计数。

### 2026-06-12 post-sync quick regression

命令：

```bash
make cuper-jacobi-regression-sw MODE=quick NO_BUILD=1 ALLOW_MISSING=1
```

日志目录：

```text
cuper-jacobi-iteration-build/regression/20260612_124905_quick/
```

摘要：

| case | state | status | final_buffer | iterations | final_diff | error_num | spmv_update_cycles |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `cant` | PASS | 1 | 0 | 2 | 0 | 0 | 103856 |
| `thermal2_n65536` | PASS | 1 | 0 | 1 | 0 | 0 | 36081 |

### 2026-06-12 chain-tail drain fix software check

这轮排查到 `SpmvService_DestroyFloatV16` 存在硬件退出竞态：旧逻辑在尾端
`Vector_X_Stream` 暂时为空时可以先读 stop 并 return，后续 Core15 若继续转发残余
X 包，就会因为链尾无人消费而卡住。已改为按 `ceil(Column_num / 16) * Max_iters`
精确 drain 完所有应到达链尾的 X 包，再允许 stop 退出。

验证命令：

```bash
make cuper-jacobi-build-host
make cuper-jacobi-regression-sw MODE=quick NO_BUILD=1 ALLOW_MISSING=1
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-build-host
JACOBI_DEADLOCK_DEBUG=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024
```

关键结果：

```text
quick regression: pass=2 fail=0
thermal2_n1024 MAX_ITERS=1 debug ABI software/TAPA simulation: Error Num=0
```

注意：这个修复已经在 2026-06-12 重新生成硬件 bitstream，并同步到
`395bitstream/cuper-tapa-jacobi-u55c-20260612-demo.xclbin`。2026-06-11 demo
只保留为历史记录，不再是当前 Jacobi demo 槽文件。

### 2026-06-12 tail-drain debug hardware build

构建命令：

```bash
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-build-host \
  CUPER_JACOBI_BUILD_DIR=/home/pyx/project-x/Project-XPlus/cuper-tapa-jacobi-u55c-20260612-tail-drain-debug-build

JACOBI_DEADLOCK_DEBUG=1 MAX_ITERS=1 make cuper-jacobi-run-sw \
  CUPER_JACOBI_BUILD_DIR=/home/pyx/project-x/Project-XPlus/cuper-tapa-jacobi-u55c-20260612-tail-drain-debug-build \
  MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024

JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-hw-tmux \
  CUPER_JACOBI_BUILD_DIR=/home/pyx/project-x/Project-XPlus/cuper-tapa-jacobi-u55c-20260612-tail-drain-debug-build
```

构建结果：

```text
build dir: cuper-tapa-jacobi-u55c-20260612-tail-drain-debug-build/
build log: cuper-tapa-jacobi-u55c-20260612-tail-drain-debug-build/logs/build_hw_tmux.log
xclbin: cuper-tapa-jacobi-u55c-20260612-tail-drain-debug-build/CuperJacobiIteration.xclbin
UUID: 401e53eb-a68f-55fb-78f8-5553f14edcd2
SHA256: 46272395b4f4cef1a977767225080dfe2194fed3cf55baccbb5e4eec68e82e2f
DATA clock: 161 MHz
KERNEL clock: 500 MHz
HBM clock: 442 MHz
v++ link: Run completed
total elapsed: 4h 5m 6s
```

前置 software smoke：

```text
thermal2_n1024 MAX_ITERS=1 debug ABI software/TAPA simulation: Error Num=0
```

时序状态：

```text
Timing constraints are not met.
Setup failing endpoints: 105708
Setup worst slack: -2.842 ns
Setup total violation: -74910.742 ns
Hold failing endpoints: 0
Hold worst slack: 0.004 ns
```

结论：这版已经包含 `SpmvService_DestroyFloatV16` 链尾 drain 修复并完成 `.xclbin`
生成，且已同步到 `395bitstream/` 的 Jacobi demo 槽。但它仍不是 timing-clean
bitstream，也还未上板验证。

### 2026-06-12 finite pair compute source/XO check

2026-06-12 服务器侧复测
`395bitstream/cuper-tapa-jacobi-u55c-20260612-demo.xclbin` 后，`thermal2_n1024`
`MAX_ITERS=1` 仍卡在：

```text
[tapa-invoke] after ReadFromDevice before Finish
```

这说明 tail-drain 修复版仍存在 `Finish` 不返回问题，不能只按链尾 X drain 解释。
本轮按 `DLC/Cuper-jacobi-iteration/docs/finish_nonreturn_monitoring_points.md`
的优先级，先去掉 `Jacobi_UpdatePairCompute[0..7]` 的 `tapa::detach` 无限循环：

```text
Jacobi_UpdateFrameFork -> Update_Pair_Frame_Stream[0..7]
Jacobi_UpdatePairCompute[0..7] 按 frame 处理固定数量，收到 stop frame 后 return
```

验证命令：

```bash
make cuper-jacobi-build-host
make cuper-jacobi-regression-sw MODE=quick NO_BUILD=1 ALLOW_MISSING=1
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-build-host
JACOBI_DEADLOCK_DEBUG=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024
JACOBI_DEADLOCK_DEBUG=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n16
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-build-xo
```

关键结果：

```text
quick regression: pass=2 fail=0 skip=0
thermal2_n1024 MAX_ITERS=1 debug ABI software/TAPA simulation: Error Num=0
thermal2_n16 MAX_ITERS=1 debug ABI software/TAPA simulation: Error Num=0
debug ABI XO generated: cuper-jacobi-iteration-build/CuperJacobiIteration.xo
```

quick regression 日志：

```text
cuper-jacobi-iteration-build/regression/20260612_202606_quick/
```

这一步先证明源码和 XO 能通过。随后已经用同一 finite-pair-compute 源码重新生成
`.xclbin` 并同步到 Jacobi demo 槽，见下一节。

### 2026-06-13 finite pair debug hardware build

构建目标：把 `Jacobi_UpdatePairCompute[0..7]` 的 finite frame/stop 修改打进完整
hardware xclbin，用于替换上一版 tail-drain-only demo artifact。

构建结果：

```text
build dir: cuper-tapa-jacobi-u55c-20260612-finite-pair-debug-build/
build log: cuper-tapa-jacobi-u55c-20260612-finite-pair-debug-build/logs/build_hw_tmux.log
xclbin: cuper-tapa-jacobi-u55c-20260612-finite-pair-debug-build/CuperJacobiIteration.xclbin
sync: 395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin
UUID: 6ad9f2dd-d23f-6ab2-c8bb-1129f00d27bb
SHA256: e981baf0f809065674f9bc696095bfa0d2e816ffb281c3dfe6dfeb8e8990a145
DATA clock: 182 MHz
KERNEL clock: 500 MHz
HBM clock: 450 MHz
DATA achieved: 182.9 MHz
VPL: FINISHED, Run Status: impl Complete
v++ link: Run completed
total elapsed: 4h 19m 47s
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

结论：这版已经包含 tail-drain 修复和 finite pair compute stop-frame 修复，并已同步到
`395bitstream/` 的 Jacobi demo 槽。但它仍不是 timing-clean bitstream，也还未完成
板上验证。

### 2026-06-13 finite pair demo board smoke

服务器侧复测上一版同名 finite-pair demo：

```text
bitfile: 395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin
UUID: 6ad9f2dd-d23f-6ab2-c8bb-1129f00d27bb
case: thermal2_n16, MAX_ITERS=1, JACOBI_DEADLOCK_DEBUG=1
result: timeout, rc=124
host stop point: [tapa-invoke] after ReadFromDevice before Finish
```

probe 结果：

```text
xbutil dynamic-regions: CuperJacobiIteration_1 Usage 1 Status (IDLE)
firewall: GOOD
kernel thread: [CuperJacobiIter] D state
```

判断：这不像 CU 仍处于 RUN 的纯 PL 业务数据流死锁，更像 TAPA/FRT `Finish()` 或
XRT exec 清理路径未返回。由于 `thermal2_n16` 的 `R NNZ=0`、`Slice Num=0`，空 R
路径的 X loader/drain 协议仍被列为高优先级怀疑点。

### 2026-06-13 pre-Finish/empty-R source and XO check

本轮源码改动：

```text
host/main.cpp:
  拆开 WriteToDevice -> Exec -> ReadFromDevice -> pre-Finish dump -> Finish。
  如果 Finish() 不返回，host 也能先打印 Status/Metrics/Debug BO 快照。

jacobi_vector_loader.hpp:
  Batch_num==0 时不读 X、不写 Vector_X_Stream；空 R 数学上直接使用 -R*x=0。

spmv_service_drains.hpp:
  Batch_num==0 时链尾 X drain expected_packets=0，不再等待不会被 Core 转发的 X 包。
```

已跑验证：

```bash
make cuper-jacobi-build-host
MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n16
make cuper-jacobi-regression-sw MODE=quick NO_BUILD=1 ALLOW_MISSING=1
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-build-host
JACOBI_DEADLOCK_DEBUG=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n16
JACOBI_DEADLOCK_DEBUG=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-build-xo
```

关键结果：

```text
thermal2_n16 MAX_ITERS=1 software/TAPA simulation: Error Num=0
quick regression: pass=2 fail=0 skip=0
thermal2_n16 MAX_ITERS=1 debug ABI software/TAPA simulation: Error Num=0
thermal2_n1024 MAX_ITERS=1 debug ABI software/TAPA simulation: Error Num=0
debug ABI XO generated: cuper-jacobi-iteration-build/CuperJacobiIteration.xo
```

quick regression 日志：

```text
cuper-jacobi-iteration-build/regression/20260613_014101_quick/
```

### 2026-06-13 pre-Finish/empty-R debug hardware build

构建结果：

```text
build dir: cuper-tapa-jacobi-u55c-20260613-prefinish-empty-r-debug-build/
build log: cuper-tapa-jacobi-u55c-20260613-prefinish-empty-r-debug-build/logs/build_hw_tmux.log
xclbin: cuper-tapa-jacobi-u55c-20260613-prefinish-empty-r-debug-build/CuperJacobiIteration.xclbin
sync: 395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin
UUID: 5c9f0e72-5ea9-7142-1e90-690b72d30557
SHA256: 0d300c1f55c21078f1f24d5e551228ccc75855331585d6669bc3e15ac31b9c26
DATA clock: 175 MHz
KERNEL clock: 500 MHz
HBM clock: 427 MHz
DATA achieved: 175.2 MHz
VPL: FINISHED, Run Status: impl Complete
v++ link: Run completed
total elapsed: 3h 47m 24s
```

时序状态：

```text
Timing constraints are not met.
Setup failing endpoints: 91026
Setup worst slack: -2.373 ns
Setup total violation: -51779.359 ns
Hold failing endpoints: 0
Hold worst slack: 0.009 ns
```

结论：这版已经包含 pre-Finish debug dump 和 `Batch_num==0` 空 R no-X-read/drain
修复，并已同步到 `395bitstream/` 的 Jacobi demo 槽。但它仍不是 timing-clean
bitstream；后续最小上板 smoke 仍卡在 `Finish()` 收尾。

### 2026-06-13 pre-Finish/empty-R demo 上板失败

测试对象：

```text
395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin
UUID: 5c9f0e72-5ea9-7142-1e90-690b72d30557
SHA256: 0d300c1f55c21078f1f24d5e551228ccc75855331585d6669bc3e15ac31b9c26
logs: logs/jacobi_prefinish_empty_r_hw_20260613_112806/
```

关键结果：

```text
thermal2_n16 MAX_ITERS=1: rc=124, 120s timeout
thermal2_n1024 MAX_ITERS=1: rc=124, 120s timeout
host stop point: [tapa-invoke] after ReadFromDevice before Finish
[jacobi-prefinish] Status[0..2]=0,0,0 Metrics[0..7]=0,0,0,0,0,0,0,0
[jacobi-deadlock-debug] heartbeat=0 event_count=0 stop_marker=0
xbutil CU status while host blocked: IDLE
firewall: GOOD
```

判定：这次不是“PL compute unit 仍显示 RUN 的业务数据流死锁”。更可疑的是 kernel
入口 task 没有可观察写回、HBM[24] mmap 写回/迁移不可见，或者 TAPA/FRT
`Finish()` / XRT exec 清理路径异常。

### 2026-06-13 entry mmap probe 源码/XO 验证

当前源码增加 debug-only 入口探针：

```text
Debug[0]       = 0x4a434231
Debug[48..51]  = magic, stream_count, entry_phase, stop_drain_cycles
Status[8..11]  = magic, Row_num, Max_iters, float_v16 packet count
Metrics[8..11] = magic, Row_num, Max_iters, float_v16 packet count
```

验证命令：

```bash
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-build-host
JACOBI_DEADLOCK_DEBUG=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n16
JACOBI_DEADLOCK_DEBUG=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-build-xo
```

关键输出：

```text
thermal2_n16: Error Num=0
[jacobi-final-probe] Status[8..11]=1245921841,16,1,1
[jacobi-final-probe] Metrics[8..11]=1245921841,16,1,1
[jacobi-deadlock-probe] Debug[48..51]=1245921841,11,1,8192

thermal2_n1024: Error Num=0
[jacobi-final-probe] Status[8..11]=1245921841,1024,1,64
[jacobi-final-probe] Metrics[8..11]=1245921841,1024,1,64
[jacobi-deadlock-probe] Debug[48..51]=1245921841,11,1,8192

XO: cuper-jacobi-iteration-build/CuperJacobiIteration.xo generated
```

这一步先证明源码和 XO 能通过。随后已用 entry mmap probe 源码重新生成完整
`.xclbin` 并同步到 Jacobi demo 槽，见下一节。

### 2026-06-13 entry mmap probe debug hardware build

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
它仍不是 timing-clean bitstream，当前还没有新版上板 smoke；上一版
pre-Finish/empty-R demo 的 `Finish()` 不返回结论只对应旧 UUID。

## 当前硬件 demo 同步记录

同步文件：

```text
395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin
395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin.info
```

关键字段：

```text
Kernel: CuperJacobiIteration
ABI: JACOBI_DEADLOCK_DEBUG=1, single X buffer, Debug buffer on HBM[24]
UUID: 7bf54cce-83a3-b7e7-97a9-719446658c03
SHA256: 775d1da4c1c2f51ec58e0569950f618eb159481bf3eddea4e27b8f6a4da9eb24
DATA clock: 175 MHz
KERNEL clock: 500 MHz
HBM clock: 450 MHz
DATA achieved: 175.9 MHz
```

构建情况：

```text
build dir: cuper-tapa-jacobi-u55c-20260613-entry-mmap-probe-debug-build/
build log: cuper-tapa-jacobi-u55c-20260613-entry-mmap-probe-debug-build/logs/build_hw_tmux.log
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

因此当前 `.xclbin` 只是调试/同步 demo artifact，不是 timing-clean bitstream；
当前 entry mmap probe demo 还没有做新版上板 smoke。上一版同名 pre-Finish/empty-R
Jacobi demo UUID 为 `5c9f0e72-5ea9-7142-1e90-690b72d30557`，SHA256 为
`0d300c1f55c21078f1f24d5e551228ccc75855331585d6669bc3e15ac31b9c26`；旧测试结论只能
作为历史记录，不能套用到当前 `.xclbin` 文件。

## 判定标准

- `Error Num=0` 是当前 software smoke 的功能通过条件。
- `Status=1` 表示达到 `MAX_ITERS` 后正常退出，不是错误。
- 当前 deadlock-debug ABI 使用单个原地更新 `X` buffer，`Final buffer` 固定为 `0`。
- 当前固定迭代版本没有做收敛判断，kernel `Final diff` 字段暂为占位 0；数值是否对齐
  以 host comparison 的 `Error Num=0` 为准。
- `jacobi-stage-cycles` 当前用于相对调试；没有硬件 clock 前，不作为上板性能结论。

## 后续测试流程

硬件 demo 前：

```bash
make cuper-jacobi-regression-sw MODE=full
```

后续硬件 demo 构建：

```bash
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-build-xo
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-link-xclbin
```

当前同步 demo 的硬件运行命令：

```bash
BITFILE=395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin \
  MAX_ITERS=1 make cuper-jacobi-run-hw MATRIX=data/suitesparse/Schmid/csr/thermal2_n65536
```

当前 demo 已放入同步目录：

```text
395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin
395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin.info
```

当前 entry mmap probe demo 上板后仍必须补：

- `cant.mtx`、`thermal2_n65536`、`thermal2_n262144` 的 `MAX_ITERS=1/2` 结果。
- `Status`、`Final buffer`、`Iterations`、`Final diff`、`jacobi-stage-*`、`Error Num`。
- `395bitstream/README.md` 和 HTML 报告。
