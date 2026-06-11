# Cuper Jacobi 测试流程

本文记录 `DLC/Cuper-jacobi-iteration` 当前 demo 的测试口径。当前阶段已有
software/TAPA simulation 结果，并生成了第一版 demo xclbin；但 routed timing 未收敛，
也还没有上板性能数据。

## 1. 测试对象

当前顶层：

```text
CuperJacobiIteration
```

算法口径是普通 Jacobi iteration：

$$
x^{(k+1)} = D^{-1}(b - R x^{(k)})
$$

host 侧把矩阵拆成 `A = D + R`，kernel 侧读 `X0/X1` 时先取负，让 Cuper SpMV service
输出 `-R*x_old`，再由 update stage 计算：

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
BITFILE=cuper-jacobi-iteration-build/CuperJacobiIteration.xclbin \
  MAX_ITERS=1 make cuper-jacobi-run-hw MATRIX=data/suitesparse/Schmid/csr/thermal2_n65536
```

## 3. 当前记录数据

| 数据集 | 矩阵规模 | 迭代 | 当前记录 | 关键输出 |
| --- | --- | ---: | --- | --- |
| `cant.mtx` | N=62,451, NNZ=4,007,383, R NNZ=3,944,932 | 2 | 当前 timed 版通过 | `Status=1`, `Final buffer=0`, `Iterations=2`, `Final diff=0.73218`, `Error Num=0` |
| `thermal2_n65536` | N=65,536, NNZ=437,000, R NNZ=371,464 | 1 | 当前 timed 版通过 | `Status=1`, `Final buffer=1`, `Iterations=1`, `Final diff=1.11631`, `Error Num=0` |
| `thermal2_n262144` | N=262,144, NNZ=1,748,980 | 1 | 早期 software run 通过 | `Final diff=1.41496`, `Error Num=0`；还需用当前 timed/root target 补跑 |

## 3.1 当前 demo bitstream

| 项目 | 内容 |
| --- | --- |
| 同步文件 | `395bitstream/cuper-tapa-jacobi-u55c-20260611-demo.xclbin` |
| 构建目录 | `cuper-jacobi-iteration-build/` |
| Kernel | `CuperJacobiIteration` |
| UUID | `a7c95d3c-ec98-c287-67be-d81f71f7c95e` |
| SHA256 | `a622e1600628e9c4ed34fe7dd7d5f2a2afcb374789fddaa4436b1ba9408e8172` |
| DATA / KERNEL / HBM clock | `220 MHz` / `500 MHz` / `442 MHz` |
| 时序状态 | 未收敛：WNS `-1.203 ns`，TNS `-22607.805 ns`，failing endpoints `58206` |

原始 tmux 构建完成了 implementation 和 bitstream generation，但最后的
`xclbinutil` 包装阶段调用了本地 XRT 2.18 工具，因缺少 Boost 1.83 动态库失败。
随后通过 Vitis 2022.2 自带 `xclbinutil` 从包装阶段恢复，成功生成当前 demo xclbin。

这版还没有做 `hw` 上板运行；同步到 `395bitstream/` 只是为了保留和分发当前调试
artifact，不能作为 timing-clean 标准 bitstream。

当前 timed 版已记录的计数。最新一次 quick regression 日志在
`cuper-jacobi-iteration-build/regression/20260611_124944_quick/`：

| 数据集 | `float_v16_packets` | `spmv_update_packets` | `spmv_update` cycles | `controller_total` cycles | `timer_total` cycles | `spmv_update_avg` cycles |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `cant.mtx` / `MAX_ITERS=2` | 3,904 | 7,808 | 157,889 | 157,894 | 158,304 | 78,944 |
| `thermal2_n65536` / `MAX_ITERS=1` | 4,096 | 4,096 | 84,723 | 84,727 | 84,951 | 84,723 |

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
| `Status[1]` | 最终解所在 buffer：`0` 表示 `X0`，`1` 表示 `X1` |
| `Status[2]` | 已完成 Jacobi 迭代轮数 |
| `Metrics[0]` | 最后一轮 `diff_max` |
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
2. 对当前 `cuper-tapa-jacobi-u55c-20260611-demo.xclbin` 先做最小上板 smoke；
   若 timing fail 导致不稳定，再先回到降频或优化 update path。
3. 上板先跑 `cant.mtx`、`thermal2_n65536`、`thermal2_n262144` 的 `MAX_ITERS=1/2`。
4. 记录 `Status`、`Final buffer`、`Iterations`、`Final diff`、`jacobi-stage-*`
   和 `Error Num`。
5. 同步更新 `docs/bitstream_summaries/2026-06-10-cuper-tapa-jacobi-iteration/`
   和 `395bitstream/README.md`。
