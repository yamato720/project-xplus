# Cuper Jacobi Iteration

`Cuper-jacobi-iteration` 是从 `DLC/Cuper` 拆出的独立 TAPA/HLS 实验目录。

当前阶段已经做了一版 Jacobi iteration demo：host 侧先把矩阵拆成 `A = D + R`，
保留 service 化 single SpMV 数据通路。kernel 读单个 `X` buffer 时先把 `x_old` 取负，
让 Cuper service 计算 `-R*x_old`，再由后级 update stage 计算 Jacobi 的新解。

它已经接入 `Project-XPlus` 根 `Makefile` 的 `cuper-jacobi-*` 转发 target；
源码和构建脚本仍保留在本目录，避免混进已有 Cuper/PCG 主线目录。

## 目录

```text
Cuper-jacobi-iteration/
  Makefile
  README.md
  cfg/
  data/
  docs/
  host/
  include/
  kernels/
  scripts/
  src/
  xrt.ini
```

## 当前目标

1. 保留 Cuper 的 16 HBM SpMV loader/core/accumulator/checker 数据通路。
2. 保留 PCG 路线里 command/stop 驱动的 SpMV service 边界。
3. 主要算法顶层是 `CuperJacobiIteration(...)`，当前行为是：

$$
x_i^{(k+1)}
= \left(b_i + (-Rx^{(k)})_i\right)\mathrm{diag\_inv}_i
$$

它和标准 Jacobi 公式等价：

$$
x^{(k+1)} = D^{-1}(b - R x^{(k)})
$$

4. 当前实现用单个 `X` buffer 原地更新。轮次由 `Jacobi_MasterController` 显式推进：
   每轮发矩阵/compute/update command，等 X 写回 done ack 后再进入下一轮；
   `Status[1]` 固定为 `0`。
5. `Metrics[4..7]` 追加 Jacobi stage cycle，用 host 打印 `[jacobi-stage-cycles]` 和
   `[jacobi-stage-ms]`。
6. host 支持 Matrix Market `.mtx` 输入，也支持 Project-XPlus CSR 目录
   `row_ptr.txt/col_idx.txt/values.txt/b.txt`。
7. 当前另有 debug-only `CuperJacobiMmapProbeOnly(...)` micro top，只写
   Status/Metrics/Debug mmap 后返回，用于排查 `Finish()`、BO sync 和 HBM 写回边界。

## 与上层工程的关系

- `Project-XPlus` 目前是母工程
- `DLC/Cuper` 继续作为原 TAPA Cuper / PCG 主线目录
- `DLC/Cuper-jacobi-iteration` 是同级实验目录，只新增本目录文件
- 默认 build 目录是 `cuper-jacobi-iteration-build/`

## 当前约定

- `host/` 放当前 Jacobi demo host、矩阵/CSR 数据加载和 CPU reference
- `kernels/` 放 `CuperJacobiIteration` 顶层、Jacobi controller/update 和 service SpMV helper
- `include/` 放 host/kernel 共享头
- `cfg/` 放 connectivity 和平台相关配置
- `scripts/` 放独立构建/运行脚本
- `src/` 放数据结构、golden、适配层等非顶层源码

## 当前测试状态

当前已经把 light-trace 的完整 `CuperJacobiIteration` full graph 同步为
`395bitstream/cuper-tapa-jacobi-u55c-20260615-demo.xclbin`。这版不是 mmap-only
probe，已经接入完整 Cuper SpMV service 和 Jacobi update。控制流已经从旧的
token/frame 自传播改成 `Jacobi_MasterController` 显式 command/ack；debug 保留
controller、ptr/vector loader、coeff loader、8 路 `pair_compute[0..7]`、pack writer
和 X HBM writer 的关键 Debug trace。该版已完成 demo-only 上板，routed timing 已在
150 MHz DATA clock 下收敛。另有 `20260616` wide-HBM 实验 artifact，把矩阵通道扩到
24 路并同步到 `395bitstream/`，但 routed timing 未收敛，尚未上板验证。上一版
`CuperJacobiMmapProbeOnly` split-bank probe 已通过 native XRT smoke，证明 kernel
launch、m_axi 写回和 BO sync 边界可用；该 probe 结果现在只作为历史边界记录。
详细测试流程见 `docs/testing.md`。

已记录数据：

| 数据集 | 迭代 | 状态 |
| --- | ---: | --- |
| `data/matrices/cant.mtx` | 2 | 当前 deadlock-debug 单 `X` ABI 通过，`Error Num=0` |
| `../../data/suitesparse/Schmid/csr/thermal2_n65536` | 1 | 当前 deadlock-debug 单 `X` ABI 通过，`Error Num=0` |
| `../../data/suitesparse/Schmid/csr/thermal2_n262144` | 1 | 早期 software run 通过，需用当前 root target 补跑 |

当前 demo bitstream：

| 文件 | UUID | SHA256 | 时序状态 |
| --- | --- | --- | --- |
| `395bitstream/cuper-tapa-jacobi-u55c-20260615-demo.xclbin` | `c37ecdbf-92ab-5d06-11bd-e2f9edc7f720` | `78c4ffdb9268aa5c1635bf2eefeed3b828e8a26e60ab3ccb8d795c9484d975a7` | `CuperJacobiIteration` master-controller light-trace full graph，DATA 150 MHz，WNS `0.003 ns`，已通过 demo-only 上板 |
| `395bitstream/cuper-tapa-jacobi-u55c-20260616-demo.xclbin` | `9b42ccc8-7b2f-e182-cb77-317084abdca8` | `84f3926deca697975525ddff84800e1140cd83535a8e3fdf5d0ea19efff35afa` | `JACOBI_WIDE_HBM=1`，24 路 Matrix_data，DATA 139 MHz，WNS `-0.501 ns`，待上板验证 |

## 常用命令

```bash
cd DLC/Cuper-jacobi-iteration
make build-host
make run-sw MATRIX=data/matrices/cant.mtx
MAX_ITERS=1 make run-sw MATRIX=../../data/suitesparse/Schmid/csr/thermal2_n262144
make build-xo
make link-xclbin
```

从 Project-XPlus 根目录也可以用转发 target：

```bash
make cuper-jacobi-build-host
make cuper-jacobi-run-sw MATRIX=DLC/Cuper-jacobi-iteration/data/matrices/cant.mtx
MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n262144
```

`build-xo` 的 TAPA top 是 `CuperJacobiIteration`，输出默认是
`cuper-jacobi-iteration-build/CuperJacobiIteration.xo`。

`JACOBI_TRACE_ISOTOPE=1` 当前启用非阻塞 Debug buffer/event stream，并把关键 task 的
最后 phase/lane/value 写到 `Debug[64 + source*4 ...]`。host 会在 `Finish()` 前先
同步并打印 Debug BO，因此 full graph 卡在 `Finish()` 时也能看到最后进度。旧
`JACOBI_DEADLOCK_DEBUG=1` 仍兼容；若要复现旧 entry probe 行为，需要额外设置
`JACOBI_BLOCKING_ENTRY_PROBE=1`。

硬件 debug 默认优先使用 `JACOBI_TRACE_LIGHT=1`。light trace 只接 controller、
ptr loader、vector loader、coeff loader、8 路 pair compute、pack writer、X HBM writer
共 14 个关键 stream，避免 full isotope 的 47 路
DebugMonitor 在 HLS 阶段消耗过高。当前源码还会在 hardware run 的 `Finish()` 前按
`JACOBI_PREFINISH_POLL_COUNT` / `JACOBI_PREFINISH_POLL_INTERVAL_MS` 周期同步
Debug BO；已同步的 `20260615-demo` xclbin 已是 master-controller light trace full graph
debug 版。

mmap-only micro probe：

```bash
make cuper-jacobi-build-mmap-probe-xrt-host
make cuper-jacobi-build-mmap-probe-xo
make cuper-jacobi-link-mmap-probe-xclbin
make cuper-jacobi-link-mmap-probe-xclbin-split
BITFILE=/path/to/CuperJacobiMmapProbeOnly.xclbin \
  ROW_NUM=16 MAX_ITERS=1 make cuper-jacobi-run-mmap-probe-xrt
```

默认软件仿真不需要 `BITFILE`。上板或 emulation 时通过 `BITFILE` 指定 xclbin。
