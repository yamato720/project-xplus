# Cuper Jacobi 测试流程

本文记录 `DLC/Cuper-jacobi-iteration` 当前 demo 的测试口径。当前阶段已有
software/TAPA simulation 结果，并生成了 finite-pair deadlock-debug ABI demo
xclbin；但 routed timing 未收敛，也还没有上板性能数据。

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

`run-sw` 不需要 `BITFILE`。后续硬件候选生成后，再用：

```bash
make cuper-jacobi-build-xo
make cuper-jacobi-link-xclbin
BITFILE=395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin \
  MAX_ITERS=1 make cuper-jacobi-run-hw MATRIX=data/suitesparse/Schmid/csr/thermal2_n65536
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
| 同步文件 | `395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin` |
| 构建目录 | `cuper-tapa-jacobi-u55c-20260612-finite-pair-debug-build/` |
| Kernel | `CuperJacobiIteration` |
| ABI | `JACOBI_DEADLOCK_DEBUG=1`，单 `X` buffer，`Debug` HBM[24] |
| UUID | `6ad9f2dd-d23f-6ab2-c8bb-1129f00d27bb` |
| SHA256 | `e981baf0f809065674f9bc696095bfa0d2e816ffb281c3dfe6dfeb8e8990a145` |
| DATA / KERNEL / HBM clock | `182 MHz` / `500 MHz` / `450 MHz` |
| 时序状态 | 未收敛：WNS `-2.134 ns`，TNS `-46314.336 ns`，failing endpoints `86957`；hold 无 failing endpoints |

Vitis link 已完成 implementation 和 `.xclbin` 封装，`Run completed`；总耗时
`4h 19m 47s`，构建日志在
`cuper-tapa-jacobi-u55c-20260612-finite-pair-debug-build/logs/build_hw_tmux.log`。

这版还没有做 `hw` 上板运行；同步到 `395bitstream/` 只是为了保留和分发当前调试
artifact，不能作为 timing-clean 标准 bitstream。上一版同主线 demo 是
`20260612` tail-drain-only artifact，UUID
`401e53eb-a68f-55fb-78f8-5553f14edcd2`，旧测试结论只作为历史记录。

## 3.2 finite pair compute 源码验证

2026-06-12 复测当前 395 demo 后，`thermal2_n1024 MAX_ITERS=1` 仍然卡在
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

这版源码随后已重新生成 `.xclbin` 并同步为当前
`395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin`；该 artifact 已包含
finite pair compute 改动。

## 3.3 finite pair compute 硬件构建

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

后续硬件 demo 生成后，按下面顺序补测试记录：

1. 先跑 `make cuper-jacobi-build-host` 和 software smoke，确认 host/ABI 没坏。
2. 对当前 `cuper-tapa-jacobi-u55c-20260613-demo.xclbin` 先做最小上板 smoke；
   若仍卡在 Finish 或 timing fail 导致不稳定，再先回到降频或优化 update path。
3. 上板先跑 `cant.mtx`、`thermal2_n65536`、`thermal2_n262144` 的 `MAX_ITERS=1/2`。
4. 记录 `Status`、`Final buffer`、`Iterations`、`Final diff`、`jacobi-stage-*`
   和 `Error Num`。
5. 同步更新 `docs/bitstream_summaries/2026-06-10-cuper-tapa-jacobi-iteration/`
   和 `395bitstream/README.md`。
