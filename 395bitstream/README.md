# 395bitstream 对比说明

这个目录放的是当前需要在服务器上对比的两版 U55C xclbin。两版都和 Cuper/PCG 有关，但架构不同，性能口径不能混。

## 文件说明

| 文件 | 架构 | PCG 主循环 | SpMV | 适合观察 |
| --- | --- | --- | --- | --- |
| `Cuper_2022.xclbin` | TAPA Cuper SpMV + host PCG | host CPU | `DLC/Cuper/kernels/Cuper.cpp` TAPA task graph | 满血 Cuper SpMV 管线吞吐 |
| `cuper_pcg_control_packed16hbm_20260522.xclbin` | FPGA full PCG + Cuper packed 16 HBM 输入 | FPGA kernel | `kernels/cuper_pcg_control_kernel.cpp` 内部 packed SpMV | 省 host 往返后的整轮 PCG 性能 |

## 1. Cuper_2022.xclbin

定位：

```text
FPGA: 只做 Cuper SpMV
host: 做 PCG loop、dot、alpha/beta、x/r/z/p 更新、收敛判断
```

主要源码：

```text
DLC/Cuper/kernels/Cuper.cpp
host/cuper_tapa_pcg_main.cpp
host/cuper_pcg_solver.hpp
```

运行示例：

```bash
make cuper-tapa-pcg-host

make run-cuper-pcg-tapa \
  DATASET=/path/to/dataset \
  BITFILE=/path/to/Project-XPlus/395bitstream/Cuper_2022.xclbin \
  TAU=1e-8 \
  MAX_ITERS=1000
```

特点：

- SpMV 是原 TAPA Cuper 数据流，16 路 `Matrix_data_0..15`。
- PCG 每轮仍要 host 调用一次 SpMV，并把向量输入/输出在 host 和 FPGA 间同步。
- 大矩阵、SpMV 占主导时可能更有优势。
- 当前已知硬件频率较低，之前报告里 DATA clock 没到 300 MHz。

## 2. cuper_pcg_control_packed16hbm_20260522.xclbin

定位：

```text
host: 只准备数据并 launch 一次 kernel
FPGA: 做完整 PCG loop，包括 SpMV、dot、alpha/beta、x/r/z/p 更新、收敛判断
```

主要源码：

```text
host/cuper_control_matrix.hpp
host/cuper_control_xrt_host.cpp
kernels/cuper_pcg_control_kernel.cpp
cfg/connectivity_cuper_control_u55c.cfg
```

运行示例：

```bash
make cuper-control-xrt-host

./build/xplus_cuper_control_xrt_host \
  /path/to/Project-XPlus/395bitstream/cuper_pcg_control_packed16hbm_20260522.xclbin \
  /path/to/dataset \
  --tau 1e-8 \
  --max-iters 1000
```

特点：

- 矩阵输入已经是 Cuper packed 16 HBM 格式：
  - `matrix_data_0..15 -> HBM[0..15]`
  - `b/m_inv/x/r/z/p/ap/metrics/status -> HBM[16..23]`
- PCG 全流程在 FPGA kernel 内，没有每轮 host 往返。
- SpMV 不是原 TAPA task graph，而是 control kernel 内部的 packed SpMV + 本地 accumulator。
- 这版硬件已出 bitstream。当前构建里 v++ 选择 DATA clock 约 281 MHz，300 MHz 约束没有完全收敛。

## 对比时建议记录

每个数据集两版都记录：

```text
dataset
n / nnz
tau
max_iters
实际 iterations
总运行时间
kernel/SpMV 时间
host preprocessing 时间是否计入
residual_abs / residual_rel
status
```

对比口径建议：

- 如果比较端到端求解时间，把 host 预处理、数据搬运、kernel 运行都计入。
- 如果比较 FPGA 计算能力，单独记录 kernel 时间。
- `Cuper_2022.xclbin` 的 PCG 在 host，所以 host CPU 性能和 PCIe/XRT 往返会影响总时间。
- `cuper_pcg_control_packed16hbm_20260522.xclbin` 的 PCG 在 FPGA，所以更适合看多迭代场景下省掉 host 往返后的收益。

## 预期差异

粗略模型：

```text
Cuper_2022:
  每轮时间 ~= host/XRT 往返 + FPGA TAPA SpMV + host dot/update

packed16hbm control:
  总时间 ~= 一次 launch + 每轮 FPGA 内 SpMV + FPGA 内 dot/update
```

因此：

- 小/中矩阵或迭代次数多时，control 版可能更占优。
- 大矩阵且 SpMV 吞吐绝对主导时，TAPA Cuper 版可能更占优。
- 不要只看 MHz。旧版流水更强，新版 host 往返更少、频率更高。

