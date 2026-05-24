# 2026-05-24 Current SpMV Bitstream Snapshot

这个目录是 2026-05-24 的本地快照，保存当前两版 SpMV bitstream 以及对应源码。
GitHub 同步本 `README.md`、`.xclbin.info` 和 `sources/` 源码副本；
本目录下的 `.xclbin` 大文件由 `.gitignore` 忽略，不会上传。

## Scope

本快照只保存当前两个 SpMV bitstream：

| 文件 | 路线 | Kernel | UUID | DATA_CLK | HBM | SHA256 |
| --- | --- | --- | --- | ---: | ---: | --- |
| `bitstreams/cuper-tapa-spmv-u55c-20260522.xclbin` | TAPA Cuper / single SpMV | `Cuper` | `428b48ff-ec3b-e2d4-536b-97a8e654fea3` | 174 MHz | 448 MHz | `ec4bfcaf02463592e3c6732b05de86c6e0e264494e6c73b120a5751d789bd327` |
| `bitstreams/cuper-notapa-spmv-u55c-20260524.xclbin` | no-TAPA Cuper / single SpMV | `cuper_packed_spmv_kernel` | `61f098ef-cb9c-68dd-03d5-4d53b9dcd63d` | 108 MHz | 411 MHz | `c8f2291d1bbe90250675ee3d26c0bece09264d36710a029d61d134953c1dad2f` |

配套 `.xclbin.info` 也保存在 `bitstreams/` 下。

不包含 `395bitstream/cuper-notapa-pcg-fpga-u55c-20260522.xclbin`。它是旧版本
full FPGA-PCG bitstream，目前仍留在 `395bitstream/` 做历史对照。

## Source Snapshot

源码快照在 `sources/`：

| 路线 | 关键源码 |
| --- | --- |
| TAPA SpMV | `sources/DLC/Cuper/kernels/Cuper.cpp`, `sources/DLC/Cuper/include/`, `sources/DLC/Cuper/cfg/connectivity.cfg`, `sources/DLC/Cuper/scripts/` |
| no-TAPA SpMV | `sources/kernels/cuper_pcg_control_kernel.cpp`, `sources/cfg/connectivity_cuper_spmv_u55c.cfg`, `sources/host/cuper_notapa_pcg_xrt_main.cpp`, `sources/host/cuper_control_matrix.hpp` |
| Project-XPlus host/build glue | `sources/Makefile`, `sources/include/`, `sources/host/`, `sources/cfg/` |

已从快照里移除 Vitis/Vivado 生成目录、`.xo`、日志、报告和示例矩阵数据；
这里只保留复现 HLS/TAPA build 所需的源码、配置、脚本和 host glue。

## Local Files

```text
bitstreams/
  cuper-tapa-spmv-u55c-20260522.xclbin
  cuper-tapa-spmv-u55c-20260522.xclbin.info
  cuper-notapa-spmv-u55c-20260524.xclbin
  cuper-notapa-spmv-u55c-20260524.xclbin.info
sources/
  Makefile
  cfg/
  host/
  include/
  kernels/
  DLC/Cuper/
```

快照创建时的 repo commit：

```text
673e5e128f7069abea9454cd1309607efa589234
```

注意：创建快照时工作区已有未提交改动，`sources/` 反映的是创建快照当时的工作区文件内容。

## Git Policy

`.gitignore` 默认忽略所有 `.xclbin`，只对 `395bitstream/*.xclbin` 开例外。
因此本归档目录内的 bitstream 二进制留在本地，不会通过 GitHub 同步。
源码快照和 `.xclbin.info` 不再额外忽略，可以正常进入 Git。

```text
*.xclbin
!395bitstream/*.xclbin
```
