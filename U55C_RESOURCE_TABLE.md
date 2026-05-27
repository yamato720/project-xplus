# Alveo U55C 资源表

本文记录 Project-XPlus 当前使用的 U55C / Vitis platform 资源口径，方便看实现报告和判断 HBM 绑定是否合理。

## 平台口径

| 项目 | 当前值 |
| --- | --- |
| Vitis platform | `xilinx_u55c_gen3x16_xdma_3_202210_1` |
| xpfm 路径 | `/home/pyx/project-x/xilinx-local/opt/xilinx/platforms/xilinx_u55c_gen3x16_xdma_3_202210_1/xilinx_u55c_gen3x16_xdma_3_202210_1.xpfm` |
| 硬件 XSA | `/home/pyx/project-x/xilinx-local/opt/xilinx/platforms/xilinx_u55c_gen3x16_xdma_3_202210_1/hw/hw.xsa` |
| FPGA family | `virtexuplusHBM` |
| FPGA device | `xcu55c` |
| Board part | `xcu55c-fsvh2892-2L-e` |
| 有效 SLR | `SLR0`, `SLR1`, `SLR2` |
| 默认 kernel clock | 300 MHz |
| 额外 scalable clock | 500 MHz |
| fixed freerun clock | 100 MHz |

## Vitis 平台可用资源

这组数来自 `platforminfo` 的 `Resource Availability`，通常更接近 Vitis 链接/实现报告里可用于用户动态区的资源分母。

| 资源 | Total | SLR0 | SLR1 | SLR2 | 备注 |
| --- | ---: | ---: | ---: | ---: | --- |
| LUT | 1,146,240 | 386,880 | 364,320 | 395,040 | 用户动态区可用 LUT |
| FF | 2,292,480 | 773,760 | 728,640 | 790,080 | 用户动态区可用触发器 |
| BRAM | 1,776 | 600 | 576 | 600 | Vitis 口径 BRAM block |
| DSP | 8,376 | 2,664 | 2,784 | 2,928 | DSP slice |

BRAM 粗略容量按 `1 BRAM = 36 Kb` 折算：

| 位置 | BRAM block | bit 容量 | byte 容量 |
| --- | ---: | ---: | ---: |
| Total | 1,776 | 63,936 Kb，约 64 Mbit | 7,992 KiB，约 7.8 MiB |
| SLR0 | 600 | 21,600 Kb，约 21.6 Mbit | 2,700 KiB，约 2.6 MiB |
| SLR1 | 576 | 20,736 Kb，约 20.7 Mbit | 2,592 KiB，约 2.5 MiB |
| SLR2 | 600 | 21,600 Kb，约 21.6 Mbit | 2,700 KiB，约 2.6 MiB |

注意：Vivado 也可能在部分报告中按 `RAMB18` 或折半 BRAM 口径显示，所以跨报告对比时要先确认单位。本文表格使用 `platforminfo` 输出的 BRAM block 数。

## hw.hpfm 元数据资源

这组数来自 platform XSA 内的 `hw.hpfm` 元数据，数值比 `platforminfo` 更大。看最终 bitstream 利用率时优先用上一节的 Vitis 平台可用资源，或直接用该次 routed utilization report 的分母。

| 资源 | hw.hpfm 数值 |
| --- | ---: |
| LUT | 1,303,680 |
| FF | 2,607,360 |
| BRAM | 2,016 |
| DSP | 9,024 |

## HBM/PLRAM 资源

| 资源 | 数量/范围 | SLR | 备注 |
| --- | --- | --- | --- |
| HBM bank | `HBM[0]` 到 `HBM[31]`，共 32 个 segment | `SLR0` | platform 暴露给 `sp=` 绑定的 HBM segment |
| HBM Max Masters | 每个 segment 32 | `SLR0` | 来自 `platforminfo` memory information |
| PLRAM | `PLRAM[0]` 到 `PLRAM[5]`，共 6 个 segment | `SLR0`/`SLR1`/`SLR2` | 当前 CuperPcg 配置未显式使用 |
| HOST memory | `HOST[0]` | `SLR2` | 当前 CuperPcg 配置未显式使用 |

## 当前 CuperPcg TAPA 配置的 HBM 绑定

来源：`cfg/connectivity_cuper_tapa_pcg_u55c.cfg`。

| HBM bank | 绑定对象 | 用途 |
| --- | --- | --- |
| `HBM[0]` | `SpElement_list_ptr`, `Matrix_data_0` | sparse slice 元数据；第 0 路矩阵数据 |
| `HBM[1]` 到 `HBM[15]` | `Matrix_data_1` 到 `Matrix_data_15` | Cuper 16 路矩阵数据并行输入 |
| `HBM[16]` | `B` | PCG 右端项向量 |
| `HBM[17]` | `M_inv` | Jacobi/对角预条件向量 |
| `HBM[18]` | `X` | PCG 解向量，兼作初始 `x0` 的 double 版本 |
| `HBM[19]` | `R` | PCG residual 向量 |
| `HBM[20]` | `Z` | 预条件后的 residual |
| `HBM[21]` | `P` | PCG 搜索方向向量 |
| `HBM[22]` | `AP_spmv` | packed FP32 的 `A*p`/SpMV 输出缓冲 |
| `HBM[23]` | 未显式绑定 | 当前配置空闲 |
| `HBM[24]` | `X_spmv` | packed FP32 的 `x` 输入副本，供 Cuper SpMV vector loader 使用 |
| `HBM[25]` | `P_spmv` | packed FP32 的 `p` 输入副本，供 Cuper SpMV vector loader 使用 |
| `HBM[26]` | `Metrics`, `Status` | PCG 输出指标和状态 |
| `HBM[27]` 到 `HBM[31]` | 未显式绑定 | 当前配置空闲 |

结论：当前 CuperPcg TAPA 配置吃满的是原 Cuper 的 16 路矩阵并行输入，不是 U55C 暴露的 32 个 HBM bank 全部打满。

## 看报告时的注意点

- `sw_emu`、HLS estimate、XO report 的资源数只能看趋势，不能当最终 bitstream 资源。
- 最终资源和频率以 `hw` build 的 routed utilization/timing report 为准。
- 如果报告分母和本文 `hw.hpfm` 数字不同，优先检查该报告使用的是 Vitis 平台可用资源、完整器件资源，还是某个 reconfigurable partition 的资源口径。
- HBM bank 数量不等于实际带宽已经吃满；还要看访问模式、burst、bank conflict、跨 SLR/NoC 路径和 kernel 内部是否能持续供数。
