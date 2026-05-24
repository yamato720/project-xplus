# Cuper 部署报告

本文档记录 `Project-XPlus/DLC/Cuper` 当前可用于部署的硬件与主机侧产物、运行入口、依赖条件和已知风险，目标是让后续接手者可以直接判断这份工程能否上板，以及应该如何启动。

## 1. 范围

- 工程目录：`/home/zgj/project-xplus/DLC/Cuper`
- 部署对象：`Cuper` 单 kernel HLS 设计
- 平台目标：`xilinx_u55c_gen3x16_xdma_3_202210_1`
- 本报告依据：
  - `build/cuper_host`
  - `Cuper.xclbin`
  - `Cuper.xclbin.info`
  - `Cuper.xclbin.link_summary`
  - `v++_Cuper.log`
  - `Cuper_2022.xclbin`
  - `Cuper_2022.xclbin.info`
  - `Cuper_2022.xclbin.link_summary`
  - `v++_Cuper_2022.log`
  - `cfg/connectivity.cfg`
  - `host/main.cpp`

## 2. 当前部署结论

截至当前目录状态，`DLC/Cuper` 已具备两套可部署 bitstream 产物：

- 主机侧可执行文件已存在：`build/cuper_host`
- `2023.1` 版 bitstream：`Cuper.xclbin`
- `2022.2` 版 bitstream：`Cuper_2022.xclbin`
- 两套产物都有对应 `xclbin.info` / `link_summary` / `v++` 日志
- `2022.2` 版 `v++_Cuper_2022.log` 显示 `Run completed`

当前可以把它视为一份“已有现成产物、可以直接尝试上板运行”的工程，而不是只停留在源代码阶段。

本次只整理了部署文档，没有重新执行上板运行，因此“产物存在”和“链接完成”已确认，但“本轮实际跑板结果”未重新验证。

## 3. 现有部署产物

| 产物 | 路径 | 当前状态 |
| --- | --- | --- |
| Host 可执行文件 | `build/cuper_host` | 已生成 |
| Kernel 中间产物 | `Cuper.xo` | 已生成 |
| 2022 版 XO | `Cuper_2022.xo` | 已存在 |
| 2023.1 版部署比特流 | `Cuper.xclbin` | 已生成 |
| 2023.1 版 xclbin 信息 | `Cuper.xclbin.info` | 已生成 |
| 2023.1 版链接摘要 | `Cuper.xclbin.link_summary` | 已生成 |
| 2023.1 版链接日志 | `v++_Cuper.log` | 已存在 |
| 2022.2 版部署比特流 | `Cuper_2022.xclbin` | 已复制回本目录 |
| 2022.2 版 xclbin 信息 | `Cuper_2022.xclbin.info` | 已复制回本目录 |
| 2022.2 版链接摘要 | `Cuper_2022.xclbin.link_summary` | 已复制回本目录 |
| 2022.2 版链接日志 | `v++_Cuper_2022.log` | 已复制回本目录 |

当前文件大小可作为部署前快速自检：

- `build/cuper_host`: `1,608,824` bytes
- `Cuper.xo`: `253,819` bytes
- `Cuper.xclbin`: `71,927,891` bytes
- `Cuper.xclbin.info`: `18,872` bytes
- `Cuper.xclbin.link_summary`: `32,003` bytes
- `Cuper_2022.xo`: `251,873` bytes
- `Cuper_2022.xclbin`: `68,694,962` bytes
- `Cuper_2022.xclbin.info`: `21,167` bytes
- `Cuper_2022.xclbin.link_summary`: `72,187` bytes
- `v++_Cuper_2022.log`: `24,274` bytes

## 4. 平台与时钟信息

当前目录里同时存在 `2023.1` 和 `2022.2` 两版 bitstream。若以当前已经切换成默认推荐部署目标的 `Cuper_2022.xclbin` 为准，`Cuper_2022.xclbin.info` 显示其面向如下平台：

- Platform VBNV: `xilinx_u55c_gen3x16_xdma_3_202210_1`
- Board: `u55c`
- Device: `xcu55c`

当前可见的主要时钟配置：

- `hbm_aclk`: `450 MHz`
- `KERNEL_CLK`: `500 MHz`
- `DATA_CLK`: `203 MHz`

其中 system clock 区段显示：

- `ulp_ucs_aclk_kernel_00` 请求 `250 MHz`，实际达到约 `203.7 MHz`
- `ulp_ucs_aclk_kernel_01` 请求 `500 MHz`，实际达到 `500 MHz`

这说明当前实现不是所有请求频率都完全达到，部署时应以 `xclbin.info` 中的 achieved frequency 为准，而不是只看命令行目标频率。

## 5. HBM 绑定概况

`cfg/connectivity.cfg` 当前定义为单 CU：

- `nk=Cuper:1`

主要 HBM 绑定如下：

- `SpElement_list_ptr -> HBM[0]`
- `Matrix_data_0..15 -> HBM[0]..HBM[15]`
- `X -> HBM[0]`
- `Y_out -> HBM[1]`

部署含义：

- 设计已经显式绑定到 HBM，不依赖纯自动映射
- `SpElement_list_ptr` 与 `X` 共用 `HBM[0]`
- 输出 `Y_out` 放在 `HBM[1]`

如果后续部署时观察到 HBM 访问瓶颈，需要优先回看这份绑定，而不是只查 kernel 本体。

## 6. 已确认的链接完成状态

推荐部署的 `2022.2` 版日志 `v++_Cuper_2022.log` 中已经出现以下关键信号：

- `Writing bitstream ./level0_i_ulp_my_rm_partial.bit...`
- `Created /tmp/cuper_tapa_2022/Cuper_2022.xclbin`
- `Run completed`

这说明当前 `Cuper_2022.xclbin` 不是半成品，至少已经完成 `link -> bitstream packaging -> xclbin output` 这条链路。

日志中同时存在一条 `CRITICAL WARNING: [v++-17-1396]`。本报告只确认它存在，没有在此展开逐条归因；部署时如果出现行为异常，应回看完整 `v++_Cuper_2022.log`。

## 7. Host 侧运行入口

`host/main.cpp` 当前的运行方式很直接：

- 命令行必须提供一个矩阵文件路径
- bitstream 通过环境变量 `BITFILE` 传入

入口约束来自程序本体：

```text
Usage: ./build/cuper_host <matrix_file>
```

如果 `BITFILE` 未设置，程序会打印 warning。

当前目录下已有可直接使用的数据样例：

- `data/matrices/cant.mtx`
- `data/matrices/nasa4704/nasa4704.mtx`
- `data/matrices/sit100/sit100.mtx`
- `data/matrices/webbase-1M/webbase-1M.mtx`

## 8. 建议的最小部署流程

### 8.1 直接使用现成产物运行

适用于当前目录里的 `build/cuper_host` 和 `Cuper_2022.xclbin` 都不需要重建的情况。

```bash
cd /home/zgj/project-xplus/DLC/Cuper
export BITFILE=$PWD/Cuper_2022.xclbin
./build/cuper_host data/matrices/sit100/sit100.mtx
```

如果要换数据集，只替换矩阵路径即可。

### 8.2 重新编译 host

当前 `DLC/Cuper/Makefile` 还是骨架，真正用于生成 `build/cuper_host` 的入口是 `CMakeLists.txt`。

```bash
cd /home/zgj/project-xplus/DLC/Cuper
cmake -S . -B build
cmake --build build -j
```

### 8.3 从 XO 重新链接出 xclbin

当前目录下已经有一个脚本：

- `scripts/build_hw_2022_from_xo.sh`

默认行为是：

- 输入 XO：`Cuper_2022.xo`
- 输出 xclbin：`Cuper_2022.xclbin`
- 使用 `VITIS_2022_ROOT=/tools/Xilinx2022/Vitis/2022.2`

调用示例：

```bash
cd /home/zgj/project-xplus/DLC/Cuper
scripts/build_hw_2022_from_xo.sh
```

如果想显式指定输入输出：

```bash
cd /home/zgj/project-xplus/DLC/Cuper
scripts/build_hw_2022_from_xo.sh \
  "$PWD/Cuper_2022.xo" \
  "$PWD/Cuper_2022.xclbin"
```

## 9. 部署依赖与环境要求

### 9.1 Host 构建依赖

`CMakeLists.txt` 反映出当前 host 构建至少依赖：

- `TAPA`
- `pthread`
- `XILINX_HLS` 头文件
- `$HOME/.rapidstream-tapa/usr/lib` 下的运行时库

当前 `CMakeLists.txt` 的默认 HLS 头路径回退到：

- `/tools/Xilinx2022/Vitis_HLS/2022.2`

### 9.2 Relink 依赖

`scripts/build_hw_2022_from_xo.sh` 反映出重新链接硬件至少依赖：

- `Vitis 2022.2`
- 平台 `xilinx_u55c_gen3x16_xdma_3_202210_1`
- `cfg/connectivity.cfg`
- `ANALYSIS_CFG`

注意，这个脚本默认引用：

- `/home/zgj/project-x-fresh/Project-XPlus/cfg/vivado_analysis_reports.cfg`

也就是说，这个部署脚本对外部路径有硬依赖，不是纯粹的目录内自包含脚本。

## 10. 当前风险与注意事项

### 10.1 工具链版本并不完全统一

当前目录里可见两套现成产物语义：

- `CMakeLists.txt` 已切到 `Vitis_HLS 2022.2` 默认路径
- `build_hw_2022_from_xo.sh` 默认使用 `Vitis 2022.2`
- `Cuper_2022.xclbin.info` 显示当前推荐部署的 `2022` 版 bitstream 由 `v++ 2022.2` 生成
- `Cuper.xclbin.info` 仍显示旧的 `Cuper.xclbin` 由 `v++ 2023.1` 生成

这说明源码侧默认配置已经切到 `2022.2`，而且目录里也已经放回了 `2022.2` 的现成 bitstream；但旧的 `2023.1` 产物仍然保留在同目录。后续如果要复现实验或交接，必须明确默认使用 `Cuper_2022.xclbin`，避免误用旧的 `Cuper.xclbin`。

### 10.2 Makefile 不是当前主入口

`DLC/Cuper/Makefile` 目前只提供骨架说明和目录打印，不是完整的 host/hw 构建入口。部署时不要误以为 `make` 能直接完成全流程。

### 10.3 部署脚本存在外部路径依赖

`build_hw_2022_from_xo.sh` 默认引用 `project-x-fresh` 下的分析配置文件。如果把 `DLC/Cuper` 单独拷走，这个脚本会失效，必须先修正 `ANALYSIS_CFG`。

### 10.4 本轮未重跑上板执行

本报告基于目录中现存产物和日志给出部署判断，没有在当前回合重新运行：

- `xbutil examine`
- 板卡烧录
- `build/cuper_host` 真机执行

因此该报告更接近“部署准备状态说明”，不是“本轮跑板验收报告”。

## 11. 建议的后续补强项

建议后续把下面几项补齐，这样 `DLC/Cuper` 才能从“能部署”进一步变成“好维护”：

1. 增加统一的 `run_hw.sh`，把 `BITFILE` 设置和样例矩阵路径封装起来。
2. 把 `build_hw_2022_from_xo.sh` 对外部 `ANALYSIS_CFG` 的依赖移回本目录。
3. 如果确认后续只保留 `2022.2`，可以进一步移走或重命名旧的 `Cuper.xclbin`，降低误用概率。
4. 在 `docs/` 下补一份实际跑板结果记录，至少包含矩阵、耗时、吞吐和正确性对比。

## 12. 结论

`DLC/Cuper` 当前已经不是空壳目录，而是具备现成 `host + xclbin + log + connectivity` 的可部署子工程。对于当前接手者，最短路径不是重新综合，而是直接使用已经复制回本目录的 `2022.2` 版产物：

```bash
cd /home/zgj/project-xplus/DLC/Cuper
export BITFILE=$PWD/Cuper_2022.xclbin
./build/cuper_host data/matrices/sit100/sit100.mtx
```

如果这条链路能在目标机器上跑通，就说明当前部署闭环成立；如果跑不通，优先排查运行环境、XRT、板卡状态和 `BITFILE` 指向，而不是先怀疑文档中列出的现成产物不存在。
