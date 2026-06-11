# 测试记录

当前记录覆盖 software/TAPA simulation，并记录第一版硬件 demo xclbin 的构建结果。
这版 routed timing 未收敛，还没有上板性能数据。

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

当前 timed 版 smoke：

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
Final diff: 0.73218
float_v16_packets=3904
spmv_update_packets=7808
spmv_update=143582
controller_total=143586
timer_total=143680
spmv_update_avg=71791
Error Num: 0
```

结论：当前 timed 版通过，CPU reference 和 TAPA software run 对齐。

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
Final buffer: 1
Iterations: 1
Final diff: 1.11631
float_v16_packets=4096
spmv_update_packets=4096
spmv_update=41698
controller_total=41701
timer_total=41703
spmv_update_avg=41698
Error Num: 0
```

结论：当前 timed 版通过，`Error Num=0`。

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

结论：早期 software run 通过；还需要用当前 timed 版和根 `Makefile`
`cuper-jacobi-run-sw` 目标补跑，补齐 `jacobi-stage-*` 计数。

### 2026-06-11 post-sync quick regression

命令：

```bash
make cuper-jacobi-regression-sw MODE=quick NO_BUILD=1 ALLOW_MISSING=1
```

日志目录：

```text
cuper-jacobi-iteration-build/regression/20260611_124944_quick/
```

摘要：

| case | state | status | final_buffer | iterations | final_diff | error_num | spmv_update_cycles |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `cant` | PASS | 1 | 0 | 2 | 0.73218 | 0 | 157889 |
| `thermal2_n65536` | PASS | 1 | 1 | 1 | 1.11631 | 0 | 84723 |

## 硬件 demo 构建记录

同步文件：

```text
395bitstream/cuper-tapa-jacobi-u55c-20260611-demo.xclbin
395bitstream/cuper-tapa-jacobi-u55c-20260611-demo.xclbin.info
```

关键字段：

```text
Kernel: CuperJacobiIteration
UUID: a7c95d3c-ec98-c287-67be-d81f71f7c95e
SHA256: a622e1600628e9c4ed34fe7dd7d5f2a2afcb374789fddaa4436b1ba9408e8172
DATA clock: 220 MHz
KERNEL clock: 500 MHz
HBM clock: 442 MHz
```

构建情况：

```text
tmux session: cuper_jacobi_iteration_hw_build
build dir: cuper-jacobi-iteration-build/
原始失败点: xclbinutil 包装阶段
恢复方式: unset XILINX_XRT 后用 Vitis 2022.2 自带 xclbinutil 从包装阶段恢复
```

原始失败不是 synthesis/place/route 中断。VPL 已经完成 implementation 和 bitstream
generation；失败发生在最后 `.xclbin` 封装时，本地 XRT 2.18 的 `xclbinutil`
缺少 `libboost_filesystem.so.1.83.0` 和 `libboost_program_options.so.1.83.0`。

时序状态：

```text
Timing constraints are not met.
WNS: -1.203 ns
TNS: -22607.805 ns
Failing endpoints: 58206
clk_kernel_00_unbuffered_net WNS: -1.203 ns
hbm_aclk WNS: -0.040 ns
```

因此当前 `.xclbin` 只是调试/同步 demo artifact，不是 timing-clean bitstream；
还未进行 `hw` 上板验证。

## 判定标准

- `Error Num=0` 是当前 software smoke 的功能通过条件。
- `Status=1` 表示达到 `MAX_ITERS` 后正常退出，不是错误。
- `Final buffer` 必须和迭代次数匹配：奇数轮通常写到 `X1`，偶数轮通常写回 `X0`。
- `Final diff` 应和 CPU reference 一致。
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
BITFILE=cuper-jacobi-iteration-build/CuperJacobiIteration.xclbin \
  MAX_ITERS=1 make cuper-jacobi-run-hw MATRIX=data/suitesparse/Schmid/csr/thermal2_n65536
```

当前 demo 已放入同步目录：

```text
395bitstream/cuper-tapa-jacobi-u55c-20260611-demo.xclbin
395bitstream/cuper-tapa-jacobi-u55c-20260611-demo.xclbin.info
```

上板后仍必须补：

- demo xclbin 的 UUID、SHA256、DATA/KERNEL/HBM clock。
- `cant.mtx`、`thermal2_n65536`、`thermal2_n262144` 的 `MAX_ITERS=1/2` 结果。
- `Status`、`Final buffer`、`Iterations`、`Final diff`、`jacobi-stage-*`、`Error Num`。
- `395bitstream/README.md` 和 HTML 报告。
