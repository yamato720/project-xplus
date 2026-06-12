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

注意：这个修复已经在 2026-06-12 重新生成硬件 bitstream，但还没有同步到
`395bitstream/`。当前 `395bitstream/cuper-tapa-jacobi-u55c-20260611-demo.xclbin`
仍是修复前 artifact。

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
生成，但仍不是 timing-clean bitstream，且还未同步到 `395bitstream/`、未上板验证。

## 硬件 demo 构建记录

同步文件：

```text
395bitstream/cuper-tapa-jacobi-u55c-20260611-demo.xclbin
395bitstream/cuper-tapa-jacobi-u55c-20260611-demo.xclbin.info
```

关键字段：

```text
Kernel: CuperJacobiIteration
ABI: JACOBI_DEADLOCK_DEBUG=1, single X buffer, Debug buffer on HBM[24]
UUID: b4664f5e-8cd6-0f7d-56ae-28384fce6400
SHA256: 1113701276f09545b2407d16823e5649d6e017a9fcef63a014838106612e8eb5
DATA clock: 169 MHz
KERNEL clock: 500 MHz
HBM clock: 450 MHz
DATA achieved: 169.2 MHz
```

构建情况：

```text
build dir: cuper-tapa-jacobi-u55c-20260611-deadlock-debug-build/
build log: cuper-tapa-jacobi-u55c-20260611-deadlock-debug-build/logs/build_hw_tmux.log
VPL: FINISHED, Run Status: impl Complete
v++ link: Run completed
total elapsed: 4h 46m 28s
```

时序状态：

```text
Timing constraints are not met.
Setup failing endpoints: 96241
Setup worst slack: -2.575 ns
Setup total violation: -56069.028 ns
Hold failing endpoints: 0
Hold worst slack: 0.005 ns
```

因此当前 `.xclbin` 只是调试/同步 demo artifact，不是 timing-clean bitstream；
还未进行 `hw` 上板验证。上一版同名 Jacobi demo UUID
`a7c95d3c-ec98-c287-67be-d81f71f7c95e` 已被覆盖，其 SHA256 为
`a622e1600628e9c4ed34fe7dd7d5f2a2afcb374789fddaa4436b1ba9408e8172`；旧测试结论只能
作为历史记录。

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
make cuper-jacobi-build-xo
make cuper-jacobi-link-xclbin
```

硬件 demo 运行：

```bash
BITFILE=395bitstream/cuper-tapa-jacobi-u55c-20260611-demo.xclbin \
  MAX_ITERS=1 make cuper-jacobi-run-hw MATRIX=data/suitesparse/Schmid/csr/thermal2_n65536
```

当前 demo 已放入同步目录：

```text
395bitstream/cuper-tapa-jacobi-u55c-20260611-demo.xclbin
395bitstream/cuper-tapa-jacobi-u55c-20260611-demo.xclbin.info
```

上板后仍必须补：

- `cant.mtx`、`thermal2_n65536`、`thermal2_n262144` 的 `MAX_ITERS=1/2` 结果。
- `Status`、`Final buffer`、`Iterations`、`Final diff`、`jacobi-stage-*`、`Error Num`。
- `395bitstream/README.md` 和 HTML 报告。
