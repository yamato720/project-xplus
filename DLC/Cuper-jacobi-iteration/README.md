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
7. 正常主线不再保留 Debug BO、DebugMonitor、trace stream 或 mmap-only probe；
   只保留 `Status` 和 `Metrics` 写回。
8. 新增 `CuperSpmvServiceOnly(...)` 隔离实验顶层：只运行 Cuper SpMV 数据通路，
   不拆 `A=D+R`，不执行 Jacobi update，用于单独探索 24/32 路 Matrix_data 扩展。

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

当前源码是 no-debug 正常主线：`CuperJacobiIteration` 已接入完整 Cuper SpMV service
和 Jacobi update，控制流由 `Jacobi_MasterController` 显式 command/ack 推进。
`Metrics[4..7]` 仍保留 stage cycle 计时，用于对比 16 路、24 路和降频版本。
历史 debug/probe bitstream 只作为 `docs/testing.md` 里的排障记录，不再由当前代码构建。

已记录数据：

| 数据集 | 迭代 | 状态 |
| --- | ---: | --- |
| `data/matrices/cant.mtx` | 2 | no-debug 单 `X` ABI 可用于 software regression |
| `../../data/suitesparse/Schmid/csr/thermal2_n65536` | 1 | no-debug 单 `X` ABI 可用于 software regression |
| `../../data/suitesparse/Schmid/csr/thermal2_n262144` | 1 | 早期 software run 通过，需用当前 root target 补跑 |

当前 demo bitstream：

| 文件 | UUID | SHA256 | 时序状态 |
| --- | --- | --- | --- |
| `395bitstream/cuper-tapa-jacobi-u55c-20260615-demo.xclbin` | `c37ecdbf-92ab-5d06-11bd-e2f9edc7f720` | `78c4ffdb9268aa5c1635bf2eefeed3b828e8a26e60ab3ccb8d795c9484d975a7` | `CuperJacobiIteration` master-controller light-trace full graph，DATA 150 MHz，WNS `0.003 ns`，已通过 demo-only 上板 |
| `395bitstream/cuper-tapa-jacobi-u55c-20260616-demo.xclbin` | `aa594af3-f811-1b17-f507-fd504f93425e` | `232c5afeaf8e122f7b30e5b26e95553a40ea44556ea59723480cab1f77453f9c` | `JACOBI_WIDE_HBM=1` no-debug，24 路 Matrix_data，DATA 147 MHz，WNS `-0.120 ns`，待上板验证 |
| `395bitstream/cuper-tapa-jacobi-u55c-20260617-demo.xclbin` | `f2d71afc-b5f0-5b13-9b9f-a6283fe61e6a` | `61456e3bc652f56624f26c66f31200b4a85cfd310eaf142feba29133451fa977` | `CuperJacobiIteration` no-debug 16 路正常 ABI，DATA 150 MHz，WNS `0.003 ns`，待服务器上板 |
| `395bitstream/cuper-tapa-spmv-u55c-20260617-demo.xclbin` | `492f929f-4232-3a37-b7e0-3969b5052219` | `c4908d759c81c2d4b1202236ba611a2cdeb2ec3edeab595ec588efa799257705` | `CuperSpmvServiceOnly` 24 路 SpMV-only，DATA 141 MHz，WNS `-0.420 ns`，待服务器上板 |

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

默认软件仿真不需要 `BITFILE`。上板或 emulation 时通过 `BITFILE` 指定 xclbin。

## SpMV-only 宽 HBM 实验

`CuperSpmvServiceOnly` 只计算 `Y=A*X`。host 端用完整矩阵 A，默认令 `X=ones`，
并用 CSR CPU SpMV 校验输出。它复用本目录的 Cuper 数据格式和 loader/core/
accumulator/checker/writer，不进入 Jacobi controller 或 update。

24 路软件 smoke：

```bash
JACOBI_TOP=CuperSpmvServiceOnly JACOBI_SPMV_ONLY=1 JACOBI_HBM_CHANNELS=24 \
  make cuper-jacobi-build-host

JACOBI_TOP=CuperSpmvServiceOnly JACOBI_SPMV_ONLY=1 JACOBI_HBM_CHANNELS=24 \
  make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024
```

32 路软件 smoke：

```bash
JACOBI_TOP=CuperSpmvServiceOnly JACOBI_SPMV_ONLY=1 JACOBI_HBM_CHANNELS=32 \
  make cuper-jacobi-build-host

JACOBI_TOP=CuperSpmvServiceOnly JACOBI_SPMV_ONLY=1 JACOBI_HBM_CHANNELS=32 \
  make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024
```

硬件构建时使用同样三组环境变量。24 路 connectivity 将 `Matrix_data_0..23`
映射到 HBM[0..23]，`SpElement_list_ptr/X/Y_out/Status/Metrics` 分散到空闲 HBM；
32 路 connectivity 将 `Matrix_data_0..31` 映射到 HBM[0..31]，辅助 buffer 暂时
共享 HBM[28..31]，这是探索版的已知边界。

2026-06-17 已同步 24 路 SpMV-only xclbin 到
`395bitstream/cuper-tapa-spmv-u55c-20260617-demo.xclbin`。本机没有 Xilinx OpenCL
platform，无法上板；服务器侧测试时必须设置：

```bash
JACOBI_TOP=CuperSpmvServiceOnly
JACOBI_SPMV_ONLY=1
JACOBI_HBM_CHANNELS=24
BITFILE=395bitstream/cuper-tapa-spmv-u55c-20260617-demo.xclbin
```

32 路 SpMV-only 已通过 software simulation，但 VPL `create_bd` 失败：
`You have run out of port connections on /hmss_0. All 33 connections are used`。
这说明 32 个 Matrix_data m_axi 口加辅助 m_axi 口超过平台 HBM subsystem 连接数；
后续需要合并 top-level m_axi 端口后再继续做 32 路硬件。
