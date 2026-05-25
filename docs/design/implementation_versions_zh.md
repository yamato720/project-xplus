# Project-XPlus 实现版本索引

本文档只回答一个问题：当前仓库里每个实现版本到底是什么，PCG 控制逻辑在哪里，SpMV 用哪套 kernel，以及应该看哪些源码和报告。

## 版本总览

| 版本 | 菜单名称 | PCG 控制位置 | SpMV 位置 | 主要用途 |
| --- | --- | --- | --- | --- |
| 多 kernel 普通版 | 多 kernel 普通版 | host | 本地 C++/拆分 kernel 模型 | 算法基线和拆分流程参考 |
| 多 kernel 分块版 | 多 kernel 分块版 | host | block/bitmap SpMV | 默认 control-kernel 前的分块实验参考 |
| 默认 control-kernel 版 | 当前单 control-kernel 版 | FPGA kernel 内 | `pcg_control_kernel.cpp` 内部 | 当前默认 XRT/HLS 主线 |
| Cuper-PCG 软件版 | Cuper-PCG 版 | host | Cuper 风格 FP32 软件 SpMV | 验证 Cuper 数据格式和 PCG 外层流程 |
| Cuper-PCG TAPA 版 | Cuper-PCG TAPA 版 | host | `DLC/Cuper` TAPA kernel | 当前最新 TAPA Cuper 硬件实验 |
| Cuper-PCG TAPA 全 FPGA 版 | Cuper-PCG TAPA FPGA 版 | FPGA TAPA kernel 内 | `DLC/Cuper` TAPA Cuper SpMV task graph | 当前正在实现的满血 Cuper + FPGA 内 PCG 版本 |
| Cuper-PCG control-kernel 版 | Cuper-PCG control-kernel 版 | FPGA kernel 内 | Cuper column-batch/row-tile SpMV | 把 Cuper SpMV 和 PCG 控制合进一个 kernel 的实验版 |
| Cuper-PCG fullcuper control-kernel 版 | Cuper-PCG control-kernel 版 | FPGA kernel 内 | 手拆 TAPA Cuper 思路后的 16 HBM/512-bit/8-lane SpMV | 当前最接近满血 Cuper 进入 PCG kernel 的实验版 |

## 当前源码版本速查

这几个名字很容易混在一起，先按源码入口区分：

| 源码/目标 | 当前版本含义 | PCG 在哪里 | 备注 |
| --- | --- | --- | --- |
| `DLC/Cuper/kernels/Cuper.cpp` / `Cuper(...)` | 原始 TAPA Cuper SpMV | host | 只做 SpMV，不做 PCG 收敛、`alpha/beta` 或向量更新 |
| `DLC/Cuper/kernels/Cuper.cpp` / `CuperPcg(...)` | TAPA Cuper + FPGA 内 PCG 新版 | FPGA | 保留 TAPA Cuper 16 路 HBM SpMV task graph，并新增 FPGA 内 PCG controller |
| `host/cuper_tapa_pcg_main.cpp` | TAPA Cuper SpMV + host PCG | host | 服务器实测大矩阵很快的旧 TAPA 对照版 |
| `host/cuper_tapa_pcg_fpga_main.cpp` | 调用 `CuperPcg` 的 host | FPGA | host 只准备 Cuper 矩阵格式、向量 BO、启动一次 TAPA kernel |
| `kernels/cuper_pcg_control_kernel.cpp` / `cuper_packed_spmv_kernel` | no-TAPA single SpMV 16ch 路线 | host | 单次只做 `y=A*x`，用于和 TAPA SpMV 直接对比 |
| `kernels/cuper_pcg_control_kernel.cpp` / `cuper_packed_spmv_4ch_kernel` | no-TAPA single SpMV 4ch 降并发实验 | host | ABI 和 16ch 相同，但每次只并行 4 个 matrix channel，用于验证 Fmax 是否受 16 路 DATAFLOW 扇出限制 |
| `kernels/cuper_pcg_control_kernel.cpp` / `cuper_pcg_control_kernel` | HLS control-kernel / fullcuper 路线 | FPGA | 不是 TAPA task graph，是手拆 Cuper 思路后的 HLS kernel |
| `host/cuper_control_xrt_host.cpp` | 调用 `cuper_pcg_control_kernel` 的 XRT host | FPGA | 用于 `395bitstream/cuper-notapa-pcg-fpga-*.xclbin` 这批 no-TAPA FPGA-PCG bitstream |
| `kernels/pcg_control_kernel.cpp` | Project-XPlus 默认 control-kernel | FPGA | 默认 Jacobi-PCG 路线，SpMV 不是 Cuper/TAPA |

当前新增的 `CuperPcg` 不替代原来的 `Cuper`。同一个 `DLC/Cuper/kernels/Cuper.cpp`
里现在同时有两个顶层：

```text
Cuper     : TAPA Cuper SpMV only，给 host-PCG 旧对照版用
CuperPcg  : TAPA Cuper SpMV + FPGA 内 PCG，给当前满血 Cuper 移植实验用
```

## 当前 Cuper 主线构建目录

为了避免四条 Cuper 主线互相覆盖 `build/hw`、`build/sw_emu` 和 `_x_temp`，
当前 Makefile 默认把它们拆到四个独立目录：

| 主线 | 默认构建目录 | 典型产物 |
| --- | --- | --- |
| TAPA Cuper / single SpMV | `cuper-tapa-spmv-build/` | `Cuper_2022.xo`, `Cuper_2022.xclbin`, `cuper_host` |
| no-TAPA Cuper / single SpMV | `cuper-notapa-spmv-build/` | `hw/cuper_packed_spmv_kernel.xclbin` |
| no-TAPA Cuper / single SpMV 4ch 实验 | `cuper-notapa-spmv-4ch-build/` | `hw/cuper_packed_spmv_4ch_kernel.xclbin` |
| TAPA Cuper / FPGA-PCG | `cuper-tapa-fpga-pcg-build/` | `hw/CuperPcg.xo`, `hw/CuperPcg.xclbin` |
| no-TAPA Cuper / FPGA-PCG | `cuper-notapa-fpga-pcg-build/` | `hw/cuper_pcg_control_kernel.xclbin` |

历史目录 `build/` 仍保留给 archived/default Project-XPlus 路线使用；当前四个
Cuper 主线不再默认写入 `build/`。

## 1. 多 kernel 普通版

定位：

```text
host 控制 PCG 主循环
每轮调用/模拟 SpMV、dot、update_xrz、update_p 等拆分阶段
```

主要源码：

```text
host/main.cpp
host/multi_kernel_solver.hpp
kernels/spmv_csr_kernel.cpp
kernels/init_pcg_kernel.cpp
kernels/dot_kernel.cpp
kernels/update_xrz_kernel.cpp
kernels/update_p_kernel.cpp
```

特点：

- 适合看 Jacobi-PCG 的拆分流程。
- PCG 的 `alpha / beta / residual / convergence` 在 host 侧控制。
- 不是当前硬件 bitstream 主线。

常用命令：

```bash
make run-local
```

## 2. 多 kernel 分块版

定位：

```text
host 控制 PCG 主循环
SpMV 使用 block/bitmap 数据格式
```

主要源码：

```text
include/cg_common.hpp
host/multi_kernel_solver.hpp
kernels/*_kernel.cpp
```

特点：

- 用来验证 4x4 block/bitmap SpMV 和 PCG 组合。
- 后续默认 control-kernel 的矩阵格式和部分 SpMV 思路来自这里。
- PCG 控制仍在 host 侧。

## 3. 默认 control-kernel 版

定位：

```text
host launch 一次
FPGA kernel 内完成 init SpMV、r/z/p 初始化、PCG 主循环、alpha/beta、收敛判断和状态写回
```

主要源码：

```text
host/xrt_host.cpp
kernels/pcg_control_kernel.cpp
cfg/connectivity_u55c.cfg
```

特点：

- 这是 `Project-XPlus` 默认 XRT/HLS 主线。
- PCG 控制逻辑在 FPGA kernel 内，不需要 host 每轮发起多个 kernel。
- SpMV 是 Project-XPlus 自己的 block/window SpMV，不是 TAPA Cuper。

常用命令：

```bash
make build-sw
make run-xrt TARGET=sw_emu
make build-hw
make run-hw
```

相关文档：

```text
docs/design/jacobi_pcg_algorithm_flow_zh.md
docs/design/hls_source_walkthrough_zh.md
docs/design/spmv_windowed_dataflow_zh.md
```

## 4. Cuper-PCG 软件版

定位：

```text
host 控制 PCG 主循环
SpMV 用 Cuper column-batch/slice 思路的软件适配实现
```

主要源码：

```text
host/cuper_pcg_main.cpp
host/cuper_pcg_solver.hpp
```

特点：

- 不调用 TAPA kernel。
- 用于验证 Cuper 数据组织和 PCG 外层逻辑能否配合。
- PCG 的 `alpha / beta / r/z/p` 更新仍在 host 侧。

常用命令：

```bash
make run-cuper-pcg DATASET=data/suitesparse/Schmid/csr/thermal2_n1024
```

## 5. Cuper-PCG TAPA 版

定位：

```text
host 控制 PCG 主循环
每次 SpMV 调用 DLC/Cuper 的 TAPA kernel
```

主要源码：

```text
host/cuper_tapa_pcg_main.cpp
host/cuper_pcg_solver.hpp
DLC/Cuper/kernels/Cuper.cpp
DLC/Cuper/host/main.cpp
DLC/Cuper/cfg/connectivity.cfg
```

TAPA kernel 顶层参数：

```cpp
Cuper(
  SpElement_list_ptr,
  Matrix_data_0..15,
  X,
  Y_out,
  Batch_num,
  Matrix_len,
  Row_num,
  Column_num,
  Iteration_num
)
```

特点：

- 这是服务器实测大矩阵性能最好的旧对照版之一。
- 这个版本不是把 PCG 塞进 Cuper kernel。
- `DLC/Cuper/kernels/Cuper.cpp` 只做 Cuper 风格 SpMV。
- `host/cuper_tapa_pcg_main.cpp` 把 TAPA Cuper 包装成 `CuperTapaSpmv`。
- `host/cuper_pcg_solver.hpp` 仍在 host 侧执行 PCG 主循环。
- `Iteration_num` 是 Cuper kernel 内重复执行 SpMV 的计数/性能参数，不是 PCG 迭代控制。
- 如果性能对比里看到这个版本，应该理解为 `TAPA Cuper SpMV + host PCG`，不是全流程 FPGA PCG。

当前硬件产物：

```text
DLC/Cuper/Cuper_2022.xo
DLC/Cuper/Cuper_2022.xclbin
DLC/Cuper/build/vpp_tmp/link/int/partial.bit
```

当前已知 timing 状态：

```text
DATA_CLK 请求 300 MHz，实际约 174.7 MHz
KERNEL_CLK 500 MHz 达成
hbm_aclk 约 448 MHz
```

常用命令：

```bash
make run-cuper-pcg-tapa DATASET=data/suitesparse/Schmid/csr/thermal2_n16 MAX_ITERS=1 TAU=1e6
make cuper-hw-tmux
```

相关报告：

```text
Project-XS/example/project_xplus_hls/reports/xo_report.html
Project-XS/example/project_xplus_hls/reports/xo_report_analysis.html
```

## 6. Cuper-PCG TAPA 全 FPGA 版

定位：

```text
host launch 一次 CuperPcg
FPGA TAPA kernel 内完成 Cuper SpMV、Jacobi-PCG 初始化、PCG 主循环、
alpha/beta、x/r/z/p/ap 更新、收敛判断和状态写回
```

主要源码：

```text
host/cuper_tapa_pcg_fpga_main.cpp
DLC/Cuper/include/Cuper.h
DLC/Cuper/kernels/Cuper.cpp
cfg/connectivity_cuper_tapa_pcg_u55c.cfg
```

TAPA kernel 顶层参数：

```cpp
CuperPcg(
  SpElement_list_ptr,
  Matrix_data_0..15,
  B,
  M_inv,
  X,
  R,
  Z,
  P,
  AP,
  Metrics,
  Status,
  Batch_num,
  Matrix_len,
  Row_num,
  Column_num,
  Max_iters,
  Tau
)
```

特点：

- 这是当前正在移植的“满血 TAPA Cuper + FPGA 内 PCG”路线。
- SpMV 仍走 `DLC/Cuper/kernels/Cuper.cpp` 的 TAPA task graph 风格，不是 `kernels/cuper_pcg_control_kernel.cpp` 那条手拆 HLS 路线。
- `CuperPcg` 内新增 `Pcg_Controller`，负责 PCG 初始化、每轮发起 SpMV、计算 `alpha/beta`、更新 `x/r/z/p/ap`、写回 `metrics/status`。
- 矩阵仍是 Cuper 的 16 路 HBM 输入：`Matrix_data_0..15 -> HBM[0..15]`。
- PCG 向量和状态额外放在 HBM：`B/M_inv/X/R/Z/P/AP -> HBM[16..22]`，`Metrics/Status -> HBM[23]`。
- 当前显式用到 HBM[0..23]。它吃满的是原 Cuper 16 路矩阵并行度，不是 U55C 32 个 HBM bank 全部打满。
- host 只负责把 CSR 转成 Cuper 的 `SpElement`/HBM channel 格式，分配 BO，然后启动一次 `CuperPcg`。

常用命令：

```bash
make cuper-tapa-pcg-fpga-host
make run-cuper-pcg-tapa-fpga DATASET=data/generated/cgsolver/n512 TAU=1e-8 MAX_ITERS=100 DIFF_TOL=1e-3
make build-cuper-tapa-pcg TARGET=sw_emu
make build-cuper-tapa-pcg-hw
make cuper-tapa-pcg-hw-tmux
```

当前已验证状态：

```text
软件仿真已通过 n512 小数据集：
iter=41
status=converged
max_rel_diff 约 6.14e-7
```

当前硬件构建状态：

```text
build/hw/CuperPcg.xo 已生成
build/hw/CuperPcg.xclbin 正在 tmux 中做 Vitis/Vivado hw link
tmux session: project-xplus-cuper-tapa-pcg-hw
log: logs/cuper_tapa_pcg_hw_20260522_224057.log
```

注意事项：

- `CuperPcg` 是新实验路线，性能还没有服务器实测。
- 预期目标是保持 TAPA Cuper 大矩阵 SpMV 吞吐，同时去掉 host 每轮 PCG 控制和向量往返。
- 风险主要在综合/实现耗时、资源、时序，以及 PCG controller 是否限制原 TAPA Cuper 的流水。

## 7. Cuper-PCG control-kernel 版

定位：

```text
host launch 一次
FPGA kernel 内完成 Cuper SpMV、Jacobi-PCG 初始化、PCG 主循环、alpha/beta、收敛判断和状态写回
```

主要源码：

```text
host/cuper_control_local_main.cpp
host/cuper_control_xrt_host.cpp
host/cuper_control_matrix.hpp
kernels/cuper_pcg_control_kernel.cpp
cfg/connectivity_cuper_control_u55c.cfg
```

特点：

- 这是把 Cuper 风格 SpMV 和 PCG control-kernel 合并的实验版。
- PCG 控制逻辑在 FPGA kernel 内。
- 与 TAPA Cuper 版不同，它不是调用 `DLC/Cuper/kernels/Cuper.cpp` 的 TAPA task graph。
- 当前 `fullcuper` bitstream 是这条路线的最新硬件产物：把 TAPA Cuper 的数据组织和并行思路手拆进 `cuper_pcg_control_kernel.cpp`，不是继续走 TAPA。
- `fullcuper` 版使用 16 路 HBM matrix 输入、512-bit matrix word、每通道 8 lane、x broadcast、本地 4-bank slice cache 和 512 个 URAM accumulator。
- `fullcuper` 版的 PCG init、SpMV、dot、alpha/beta、x/r/z/p 更新、收敛判断都在同一个 FPGA kernel 内完成。

当前硬件产物：

```text
395bitstream/cuper-notapa-pcg-fpga-legacy-packed16hbm-u55c-20260522.xclbin
395bitstream/cuper-notapa-pcg-fpga-u55c-20260522.xclbin
395bitstream/cuper-notapa-pcg-fpga-u55c-20260522.xclbin.info
```

`fullcuper` 当前已知 timing 状态：

```text
DATA_CLK 请求 300 MHz，xclbin 记录为 215 MHz
KERNEL_CLK 500 MHz
hbm_aclk 450 MHz
routed timing 未干净收敛：WNS -1.305 ns，TNS -32582.684 ns
```

常用命令：

```bash
make build-cuper-control-sw
make run-cuper-control-xrt TARGET=sw_emu DATASET=data/suitesparse/Schmid/csr/thermal2_n16
make cuper-control-hw-tmux
```

运行 bitstream 示例：

```bash
make cuper-control-xrt-host

./build/xplus_cuper_control_xrt_host \
  395bitstream/cuper-notapa-pcg-fpga-u55c-20260522.xclbin \
  /path/to/dataset \
  --tau 1e-8 \
  --max-iters 1000
```

## 版本判断规则

如果不确定自己看到的是哪个版本，可以按下面几条判断：

1. kernel 名是 `Cuper`
   - 通常是 `DLC/Cuper` TAPA SpMV kernel。
   - PCG 不在这个 kernel 内。
2. kernel 名是 `cuper_pcg_control_kernel`
   - 是 Cuper-PCG control-kernel 版。
   - PCG 在 FPGA kernel 内。
3. kernel 名是 `pcg_control_kernel`
   - 是 Project-XPlus 默认 control-kernel 版。
   - PCG 在 FPGA kernel 内，但 SpMV 不是 TAPA Cuper。
4. 看到 `host/cuper_pcg_solver.hpp`
   - 这里是 host-side PCG 主循环的公共实现。
   - Cuper 软件版和 Cuper TAPA 版都会用到它。
5. 看到 `DLC/Cuper/kernels/Cuper.cpp`
   - 这是 TAPA Cuper SpMV task graph。
   - 不包含 `alpha / beta / residual / m_inv / status` 这些 PCG control-kernel 状态。

## 当前建议

- 看默认 HLS/XRT 主线：从 `pcg_control_kernel` 相关文档和源码开始。
- 看最新 TAPA 硬件结果：从 `Cuper-PCG TAPA 版` 开始，但要记住它是 `TAPA SpMV + host PCG`。
- 看“PCG 是否进 kernel”：只看 kernel 名和参数。带 `control` 的 PCG kernel 才是 PCG 在 FPGA 内部；纯 `Cuper` 不是。
