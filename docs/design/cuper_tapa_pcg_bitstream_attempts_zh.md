# Cuper / CuperPcg 比特流构建尝试记录

本文记录 2026-05-21 到 2026-05-24 期间围绕 Cuper-PCG 的几次 U55C
bitstream 尝试。重点是区分已经能生成的版本、仍在失败的 TAPA 全 FPGA 版、
失败阶段，以及后续不要重复踩的路线。

## 总结

当前已经能生成并用于服务器实测的主要是两类：

| 产物 | 架构 | 状态 |
| --- | --- | --- |
| `395bitstream/cuper-tapa-spmv-u55c-20260522.xclbin` | TAPA Cuper SpMV，PCG 在 host | 可用，服务器实测大矩阵最快 |
| `395bitstream/cuper-notapa-pcg-fpga-legacy-packed16hbm-u55c-20260522.xclbin` | HLS control-kernel，Cuper packed SpMV + FPGA 内 PCG | 可用 |
| `395bitstream/cuper-notapa-pcg-fpga-u55c-20260522.xclbin` | HLS control-kernel，手拆 Cuper 数据流 + FPGA 内 PCG | 可用，但频率/效率不如 TAPA Cuper SpMV 版 |

当前仍在攻关的是：

```text
DLC/Cuper/kernels/Cuper.cpp / CuperPcg(...)
```

也就是保留 TAPA Cuper 16 路 HBM SpMV task graph，同时把 PCG 初始化、迭代、
`alpha/beta`、`x/r/z/p/ap` 更新和收敛判断放进同一个 TAPA kernel 内。

这条路线的软件仿真已经通过小数据集，但硬件实现多次失败在 `vpl impl`
的后段 routed net verification。结论很明确：这不是缺 U55C platform、缺
XRT/Vitis 库，也不是 host 端编译问题；日志已经进入 Vivado placement/routing，
失败原因是动态区布线压力和 partially-conflicted nets。

## 可用 bitstream 记录

### 1. TAPA Cuper SpMV + host PCG

文件：

```text
395bitstream/cuper-tapa-spmv-u55c-20260522.xclbin
```

对应源码：

```text
DLC/Cuper/kernels/Cuper.cpp / Cuper(...)
host/cuper_tapa_pcg_main.cpp
host/cuper_pcg_solver.hpp
```

特点：

- FPGA 只做 Cuper SpMV。
- PCG 主循环、dot、`alpha/beta`、向量更新和收敛判断都在 host。
- 服务器实测显示，大矩阵下即使频率较低，也比当前 FPGA 内 PCG 的 HLS
  control-kernel 版本快很多。
- 这是目前最能代表“满血 TAPA Cuper SpMV 数据流”的对照版本。

### 2. packed16hbm control-kernel 全 FPGA PCG

文件：

```text
395bitstream/cuper-notapa-pcg-fpga-legacy-packed16hbm-u55c-20260522.xclbin
```

构建日志：

```text
logs/cuper_control_full_hbm_hw_20260522_121941.log
```

对应源码：

```text
kernels/cuper_pcg_control_kernel.cpp
host/cuper_control_xrt_host.cpp
cfg/connectivity_cuper_control_u55c.cfg
```

构建结果：

```text
Run vpl: Step impl: Completed
Run vpl: FINISHED. Run Status: impl Complete!
```

实现耗时摘要：

```text
placement: 00h 43m 07s
routing:   01h 27m 38s
bitgen:    00h 23m 47s
```

特点：

- host launch 一次，PCG 全流程在 FPGA kernel 内。
- 矩阵端口使用 16 路 HBM。
- SpMV 是 control-kernel 内部实现，不是原 TAPA task graph。

### 3. fullcuper control-kernel 全 FPGA PCG

文件：

```text
395bitstream/cuper-notapa-pcg-fpga-u55c-20260522.xclbin
395bitstream/cuper-notapa-pcg-fpga-u55c-20260522.xclbin.info
```

构建日志：

```text
logs/cuper_control_full_tapa_hw_20260522_170856.log
```

对应源码：

```text
kernels/cuper_pcg_control_kernel.cpp
host/cuper_control_xrt_host.cpp
cfg/connectivity_cuper_control_u55c.cfg
```

构建结果：

```text
Run vpl: Step impl: Completed
Run vpl: FINISHED. Run Status: impl Complete!
```

实现耗时摘要：

```text
placement: 01h 02m 56s
routing:   01h 28m 57s
bitgen:    00h 37m 21s
```

特点：

- 这版不是 TAPA，而是把 Cuper 的数据组织和并行思路手拆进普通 HLS
  control-kernel。
- host launch 一次，PCG 全流程在 FPGA kernel 内。
- 使用 16 路 HBM matrix 输入、512-bit matrix word、每通道 8 lane、
  x broadcast、本地 slice cache 和 URAM accumulator。
- 能出 bitstream，但服务器实测大矩阵效率仍明显不如 TAPA Cuper SpMV +
  host PCG。

## TAPA CuperPcg 全 FPGA 版尝试

### 目标架构

源码入口：

```text
DLC/Cuper/kernels/Cuper.cpp / CuperPcg(...)
host/cuper_tapa_pcg_fpga_main.cpp
cfg/connectivity_cuper_tapa_pcg_u55c.cfg
```

目标：

```text
host launch 一次 CuperPcg
FPGA TAPA kernel 内完成：
  Cuper SpMV
  Jacobi/PCG 初始化
  PCG 主循环
  alpha/beta
  x/r/z/p/ap 更新
  residual/status/metrics 写回
```

HBM 映射：

```text
SpElement_list_ptr: HBM[0]
Matrix_data_0..15: HBM[0..15]
B:                 HBM[16]
M_inv:             HBM[17]
X:                 HBM[18]
R:                 HBM[19]
Z:                 HBM[20]
P:                 HBM[21]
AP:                HBM[22]
Metrics/Status:    HBM[23]
```

当前显式用到 HBM[0..23]。矩阵 SpMV 继承 Cuper 的 16 路并行输入；PCG
向量和状态使用额外 HBM bank。它不是 32 个 HBM bank 全部打满。

软件仿真状态：

```text
数据集: data/generated/cgsolver/n512
TAU: 1e-8
MAX_ITERS: 100
结果: iter=41, status=converged
max_rel_diff 约 6.14e-7
```

说明功能路径基本成立，硬件失败主要是实现压力。

### 尝试 A：初始 TAPA CuperPcg hw link

日志：

```text
logs/cuper_tapa_pcg_hw_20260522_224057.log
```

失败阶段：

```text
Run vpl: Step impl: Failed
Run vpl: FINISHED. Run Status: impl ERROR
```

实现耗时摘要：

```text
placement: 01h 18m 51s
routing 到失败: 约 04h 49m
```

关键报错：

```text
Routing results verification failed due to partially-conflicted nets
level0_i/ulp/CuperPcg_1/inst/Matrix_A_Stream_7/bram.unit/q_tmp[160]
level0_i/ulp/CuperPcg_1/inst/Matrix_A_Stream_3/bram.unit/show_ahead
level0_i/ulp/CuperPcg_1/inst/Matrix_A_Stream_3/bram.unit/q_buf[...]
```

判断：

- 冲突集中在 `Matrix_A_Stream_*` 的 TAPA stream BRAM/FIFO 附近。
- 第一轮主要是 Cuper SpMV 数据流内部 FIFO/BRAM 布线压力。
- 后续处理方向是减小 CuperPcg 专用 FIFO depth，并放宽 TAPA clock period。

### 尝试 B：降低 FIFO depth + 默认 routing

日志：

```text
logs/cuper_tapa_pcg_hw_reroute_20260523_141256.log
```

主要修改：

```text
CUPER_TAPA_PCG_CLOCK_PERIOD ?= 3.3
降低 CuperPcg 内部若干 stream depth
不使用额外 Explore routing directive
```

失败阶段：

```text
Run vpl: Step impl: Failed
Run vpl: FINISHED. Run Status: impl ERROR
```

实现耗时摘要：

```text
placement: 01h 34m 00s
routing 到失败: 约 07h 42m
总 elapsed: 10h 57m 19s
```

关键报错：

```text
Routing results verification failed due to partially-conflicted nets
level0_i/ulp/CuperPcg_1/inst/Pcg_Controller_0/B_m_axi_U/load_unit/buff_rdata/U_fifo_mem/dout[23]
level0_i/ulp/CuperPcg_1/inst/Pcg_Controller_0/dadddsub_64ns_64ns_64_8_full_dsp_1_U80/grp_fu_2086_p2[61]
level0_i/ulp/CuperPcg_1/inst/Pcg_Controller_0/grp_Pcg_Controller_Pipeline_init_vectors_fu_942/p_175_in
level0_i/ulp/CuperPcg_1/inst/Pcg_Controller_0/grp_Pcg_Controller_Pipeline_init_vectors_fu_942/reg_2252[30]
```

判断：

- 这次不再主要卡在 `Matrix_A_Stream_*`，说明 FIFO depth 调整有效。
- 剩余冲突转移到 `Pcg_Controller_0`，尤其是 B HBM 读、FP64 add/sub、
  `init_vectors` pipeline。
- 这轮比初始版本更接近成功，失败 nets 很少，说明方向基本正确。

### 尝试 C：加入 Explore / AggressiveExplore routing 策略

日志：

```text
logs/cuper_tapa_pcg_hw_route_explore_20260524_003840.log
```

主要修改：

```text
在 connectivity cfg 中尝试 [vivado] route/phys_opt Explore 类 directive
移除 init 阶段 AP 写回
```

失败阶段：

```text
Run vpl: Step impl: Failed
Run vpl: FINISHED. Run Status: impl ERROR
```

实现耗时摘要：

```text
placement: 02h 01m 27s
routing 到失败: 约 07h 10m
总 elapsed: 10h 49m 12s
```

关键报错：

```text
Routing results verification failed due to partially-conflicted nets
level0_i/ulp/CuperPcg_1/inst/Pcg_Controller_0/grp_Pcg_Controller_Pipeline_update_xrz_fu_993/din0_buf1[4]_i_6__2_n_4
level0_i/ulp/CuperPcg_1/inst/Pcg_Controller_0/grp_Pcg_Controller_Pipeline_update_xrz_fu_993/din1_buf1[43]_i_6__1_n_4
...
```

runme 中的 overlap 摘要：

```text
Number of Node Overlaps = 3577
Design is not legally routed. There are 3577 node overlaps.
```

判断：

- Explore 类策略没有改善，反而明显变差。
- 冲突集中到 `Pcg_Controller_Pipeline_update_xrz`。
- 后续不应继续把 Explore 当作主要解法；更应该降低 controller
  pipeline 压力或拆分 controller。

### 尝试 D：controller update loop II=2

日志：

```text
logs/cuper_tapa_pcg_hw_controller_ii2_20260524_115857.log
```

主要修改：

```text
Pcg_Controller 内 update_xrz loop: pipeline II=2
Pcg_Controller 内 update_p loop:   pipeline II=2
移除 Explore routing directive，回到默认 routing
```

当前状态：

```text
截至本文记录时仍在 tmux 中运行
tmux session: project-xplus-cuper-tapa-pcg-hw-ii2
当前阶段: TAPA/Vivado synthesis，尚未进入最终 v++ link routing
```

判断：

- 这是对尝试 C 中 `update_xrz` 拥塞的直接处理。
- 代价是 PCG 向量更新吞吐下降，但这些阶段通常不应该比 SpMV 更主导。
- 如果仍失败且冲突继续集中在 `Pcg_Controller`，下一步应继续降低 controller
  局部并发，或把 PCG controller 拆成更多 TAPA task，而不是只换 Vivado
  routing strategy。

## 失败模式归纳

几次失败都具有相同性质：

```text
v++ compile / TAPA pack 能过
system link 能进入 Vivado
placement 能完成
routing 走到后段
最后 routed net verification 失败
```

因此可以排除：

- U55C platform 文件缺失。
- XRT/Vitis 基础库缺失。
- host 编译器或 g++ 链接问题。
- TAPA C++ 前端无法综合。

主要问题是：

- TAPA Cuper 原本的 16 路 HBM SpMV task graph 已经较重。
- 新增 FPGA 内 PCG 后，`Pcg_Controller` 同时承担 HBM 读写、FP64
  add/mul/div、dot accumulation、向量更新和多轮控制。
- `Pcg_Controller` 的单 task 内部逻辑和 HBM 访问集中，容易形成局部布线热点。
- Explore routing 对这个设计不稳定，至少这次尝试导致 node overlaps 从接近收敛变成 3577。

## 后续建议

短期可继续试的低风险方向：

1. 如果 II=2 仍失败，把 `update_xrz/update_p` 进一步放宽到 II=4。
2. 对 `init_vectors`、`consume_ap` 也考虑 II=2/4，因为失败日志里也出现过
   `init_vectors` 和 FP64 add/sub。
3. 继续保持较小的 TAPA FIFO depth，避免回到 `Matrix_A_Stream_*` BRAM
   冲突。
4. 不优先继续尝试 Explore / AggressiveExplore；这条已经有反例。

中期结构性方向：

1. 把 `Pcg_Controller` 拆成多个 TAPA task：
   - init task
   - AP consume / dot task
   - x/r/z update task
   - p update task
   - scalar control task
2. 减少 controller 单点 HBM 端口和 FP64 运算拥挤度。
3. 研究是否能去掉 `AP` HBM 中间数组，把 SpMV 输出流直接接到 dot/update
   阶段，减少一次 HBM 写读和 controller 负担。
4. 如果数值要求允许，再评估 PCG controller 内部是否可以部分使用 FP32
   或混合精度；当前默认按 FP64 PCG 处理。

## 当前结论

`cuper-tapa-spmv-u55c-20260522.xclbin` 的性能优势来自原 TAPA Cuper SpMV 数据流。现在要做的
不是继续优化已经可用的 host-PCG 对照版，而是把 FPGA 内 PCG 做到不破坏
TAPA Cuper 的布线和吞吐。

截至本文记录，TAPA CuperPcg 全 FPGA 版功能上已经能软件仿真通过，但硬件
route 尚未成功。失败集中在 TAPA stream BRAM 和后续 `Pcg_Controller` 局部
布线热点，其中 `Pcg_Controller` 是当前最需要继续拆分或降并发的模块。
