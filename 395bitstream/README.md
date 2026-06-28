# 395bitstream 对比说明

这个目录放 U55C 上需要保留/对比的 Project-XPlus Cuper xclbin。文件名按五条主线统一：

```text
cuper-{tapa|notapa}-{spmv|pcg-fpga}-u55c-YYYYMMDD.xclbin
cuper-tapa-jacobi-u55c-YYYYMMDD.xclbin
```

同步目录常态可以保留八个成品槽位：五个标准 bitstream，加三个带 `-demo` 后缀的
当前候选 bitstream。三个 demo 槽位分别用于 `cuper-tapa-spmv` single SpMV 候选、
`cuper-tapa-pcg` full-PCG 候选和 `cuper-tapa-jacobi` Jacobi iteration 候选；
新 demo 进入同一主线槽位时优先覆盖旧 demo 文件。五个标准版
只有在用户明确确认满意后才会归档旧版并晋级替换。当前 `395bitstream/` 已将
2026-06-01 以前的历史 bitstream 移入归档，只保留 6 月以来仍在对比/上板测试窗口内的
Jacobi 和 SpMV demo/实验 artifact；`cuper-tapa-jacobi` 还没有标准 bitstream。
5 月标准/旧 demo 文件归档在
`bitstream_archive/2026-06-22-pre-june-395bitstream-cleanup/`。

如果某个文件带 `legacy`，说明它不是当前五条主线的首选版本，只作为历史对照保留。

## 当前文件

| 文件 | 主线 | PCG 主循环 | SpMV 实现 | 状态 |
| --- | --- | --- | --- | --- |
| 已归档 | TAPA Cuper / single SpMV | host 或不跑 PCG | `DLC/Cuper/kernels/Cuper.cpp` / `Cuper` | 原 `cuper-tapa-spmv-u55c-20260522.xclbin` 已移入 `bitstream_archive/2026-06-22-pre-june-395bitstream-cleanup/` |
| 已归档 | TAPA Cuper / FPGA-PCG | FPGA kernel | `DLC/Cuper/kernels/Cuper.cpp` / `CuperPcg` | 原 `cuper-tapa-pcg-fpga-u55c-20260525.xclbin` 已移入 `bitstream_archive/2026-06-22-pre-june-395bitstream-cleanup/` |
| 已归档 | no-TAPA Cuper / single SpMV | host 或不跑 PCG | `kernels/cuper_pcg_control_kernel.cpp` / `cuper_packed_spmv_kernel` | 原 `cuper-notapa-spmv-u55c-20260524.xclbin` 已移入 `bitstream_archive/2026-06-22-pre-june-395bitstream-cleanup/` |
| 已归档 | no-TAPA Cuper / FPGA-PCG | FPGA kernel | `kernels/cuper_pcg_control_kernel.cpp` / `cuper_pcg_control_kernel` | 原 `cuper-notapa-pcg-fpga-u55c-20260522.xclbin` 已移入 `bitstream_archive/2026-06-22-pre-june-395bitstream-cleanup/` |
| 暂无标准文件 | TAPA Cuper / Jacobi iteration | FPGA kernel | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperJacobiIteration` | 第五主线已接入源码和软件测试，当前只有 demo 候选 |
| 已归档 | TAPA Cuper / single SpMV demo | host 或不跑 PCG | `DLC/Cuper/kernels/Cuper.cpp` / `CuperPcgSpmv` | 原 `cuper-tapa-spmv-u55c-20260528-demo.xclbin` 已移入 `bitstream_archive/2026-06-22-pre-june-395bitstream-cleanup/` |
| `cuper-tapa-spmv-u55c-20260617-demo.xclbin` | TAPA Cuper / single SpMV demo | host 或不跑 PCG | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperSpmvServiceOnly` | 24 路 Cuper SpMV service-only 实验，服务器侧性能提升不足，保留为宽 HBM 边界 |
| `cuper-tapa-spmv-u55c-20260618-strip16-demo.xclbin` | TAPA Cuper / single SpMV demo | host 或不跑 PCG | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperSpmvServiceOnly` | 16 路 per-HBM 去 padding 实验，服务器侧完整 `thermal2` 已通过并为当前 SpMV 实验最佳 |
| `cuper-tapa-spmv-u55c-20260622-strip16-window16-demo.xclbin` | TAPA Cuper / single SpMV demo | host 或不跑 PCG | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperSpmvServiceOnly` | 16 路 strip padding + accumulator window=16，Accumulator 主循环 II=1，150 MHz routed timing clean，等待服务器上板 |
| `cuper-tapa-spmv-u55c-20260622-strip16-window14-demo.xclbin` | TAPA Cuper / single SpMV demo | host 或不跑 PCG | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperSpmvServiceOnly` | 16 路 strip padding + accumulator window=14，最小 II=1 window，150 MHz routed timing clean，等待服务器上板 |
| `cuper-tapa-spmv-u55c-20260622-strip16-window14-300m-noprogress-demo.xclbin` | TAPA Cuper / single SpMV experiment | host 或不跑 PCG | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperSpmvServiceOnly` | 300 MHz no-progress 实验 artifact，impl complete 但 timing 未收敛，自动降频到 DATA 202 MHz，仅用于服务器侧风险测试 |
| `cuper-tapa-spmv-u55c-20260622-strip16-window16-300m-noprogress-demo.xclbin` | TAPA Cuper / single SpMV experiment | host 或不跑 PCG | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperSpmvServiceOnly` | 300 MHz no-progress 实验 artifact，impl complete 但 timing 未收敛，自动降频到 DATA 192 MHz，仅用于服务器侧风险测试 |
| `cuper-tapa-spmv-u55c-20260618-compact16-demo.xclbin` | TAPA Cuper / single SpMV demo | host 或不跑 PCG | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperSpmvServiceOnly` | 16 路 PE-lane compact 打包实验，功能通过但性能严重退步 |
| `cuper-tapa-spmv-u55c-20260618-lanereal16-demo.xclbin` | TAPA Cuper / single SpMV demo | host 或不跑 PCG | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperSpmvServiceOnly` | 16 路 lane-static real/batch 固定 lane 协议实验，服务器侧完整 `thermal2` 已通过；替代 compact16 主报告线，但性能仍慢于 strip16 |
| `cuper-tapa-spmv-u55c-20260620-ooobank16-demo.xclbin` | TAPA Cuper / single SpMV demo | host 或不跑 PCG | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperSpmvServiceOnly` | 16 路 lane-static real + RTL owner-bank 乱序 accumulator latency=12 修复版，已同步到 SpMV demo 槽，等待服务器上板 |
| `cuper-tapa-spmv-u55c-20260621-ooobank16-progress-demo.xclbin` | TAPA Cuper / single SpMV demo | host 或不跑 PCG | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperSpmvServiceOnly` | 16 路 RTL owner-bank progress 版，fadd RTL/IP Tcl 随 hotpatch 打包，完整 xclbin 已生成并同步，等待服务器上板 |
| `cuper-tapa-spmv-u55c-20260622-ooobank16-heartbeat-clean-demo.xclbin` | TAPA Cuper / single SpMV demo | host 或不跑 PCG | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperSpmvServiceOnly` | 16 路 RTL owner-bank heartbeat-clean 版，150 MHz routed timing clean，已同步等待服务器上板 |
| `cuper-tapa-spmv-u55c-20260623-scoreboard16-demo.xclbin` | TAPA Cuper / single SpMV experiment | host 或不跑 PCG | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperSpmvServiceOnly` | 16 路 RTL issue scoreboard + HLS accumulator debug 版，impl complete 但 timing 未收敛，DATA clock 自动降到 39 MHz，仅用于服务器侧功能/监测边界验证 |
| `cuper-tapa-spmv-u55c-20260627-original8-demo.xclbin` | TAPA Cuper / single SpMV experiment | host 或不跑 PCG | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperSpmvServiceOnly` | 8 路原版 Cuper SpMV-only，服务器侧完整 `thermal2` 已通过，完整点 `2.74379 ms` |
| `cuper-tapa-spmv-u55c-20260627-strip8-demo.xclbin` | TAPA Cuper / single SpMV experiment | host 或不跑 PCG | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperSpmvServiceOnly` | 8 路 per-HBM strip padding，服务器侧完整 `thermal2` 已通过，完整点 `2.71420 ms` |
| `cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-demo.xclbin` | TAPA Cuper / single SpMV experiment | host 或不跑 PCG | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperSpmvServiceOnly` | 8 路 lane-static real + RTL issue scoreboard + HLS scheduled accumulator，`thermal2_n16` 通过但 `thermal2_n1024` 300s timeout，不建议继续 sweep |
| `cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-headreg-demo.xclbin` | TAPA Cuper / single SpMV experiment | host 或不跑 PCG | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperSpmvServiceOnly` | 8 路 lane-static real + RTL issue scoreboard head-register 修正版，150 MHz routed timing clean，作为旧 timeout 失败边界保留 |
| `cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-donedrain-demo.xclbin` | TAPA Cuper / single SpMV experiment | host 或不跑 PCG | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperSpmvServiceOnly` | 8 路 lane-static real + RTL issue scoreboard done-drain 修正版，`thermal2_n16` 通过但 `thermal2_n1024` 300s timeout，done-drain 未解决旧卡死 |
| 已归档 | TAPA Cuper / FPGA-PCG demo | FPGA kernel | `DLC/Cuper/kernels/Cuper.cpp` / `CuperPcg` | 原 `cuper-tapa-pcg-fpga-u55c-20260531-demo.xclbin` 已移入 `bitstream_archive/2026-06-22-pre-june-395bitstream-cleanup/` |
| `cuper-tapa-jacobi-u55c-20260615-demo.xclbin` | TAPA Cuper / Jacobi iteration demo | FPGA kernel | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperJacobiIteration` | master-controller full graph light-trace debug demo，150 MHz timing-clean，demo-only 上板已通过单轮和完整固定轮数，未晋级标准 |
| `cuper-tapa-jacobi-u55c-20260616-demo.xclbin` | TAPA Cuper / Jacobi wide-HBM experiment | FPGA kernel | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperJacobiIteration` | 24 路 Matrix_data wide-HBM no-debug 实验版，服务器侧 smoke 已失败，保留为失败边界 artifact |
| `cuper-tapa-jacobi-u55c-20260617-demo.xclbin` | TAPA Cuper / Jacobi iteration demo | FPGA kernel | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperJacobiIteration` | 16 路 light-trace restore 候选，待服务器上板；`20260615-demo` 仍是已验证 demo |

TAPA Cuper / Jacobi iteration 当前主线记录：

```text
DLC/Cuper-jacobi-iteration/
```

这条主线的顶层是 `CuperJacobiIteration`，普通 Jacobi 迭代公式为
`x_next=D^{-1}(b-Rx_old)`，不是 Jacobi 预条件子 PCG。当前代码已经接入根
`Makefile` 的 `cuper-jacobi-*` 目标，并完成 software/TAPA simulation smoke。
2026-06-15 master-controller demo 已完成 demo-only 上板：`MAX_ITERS=1` 从
`thermal2_n16` 到完整 `thermal2` 全部返回；完整固定轮数在 `thermal2_n1024`、
`thermal2_n65536`、`thermal2_n131072`、`thermal2_n262144` 和完整 `thermal2`
分别按 `451/743/842/900/24409` 轮运行并通过校验。
当前已有一个 demo 候选进入 Jacobi demo 槽，但还不是标准 bitstream。版本记录见
`docs/bitstream_summaries/2026-06-10-cuper-tapa-jacobi-iteration/`。

TAPA Cuper / Jacobi iteration 当前已验证 demo 文件：

```text
cuper-tapa-jacobi-u55c-20260615-demo.xclbin
```

`cuper-tapa-jacobi-u55c-20260617-demo.xclbin` 是 16 路 `JACOBI_TRACE_LIGHT=1`
restore 候选，覆盖了此前同名 no-debug 失败 artifact。该版 UUID 为
`e24b74ad-ec70-f13f-98a9-e6f1cf7676ed`，SHA256 为
`59956a30907259784712e66fa06ff44a8166e5afb8ebffb97018634851000b15`。最终
xclbin info 中 DATA/KERNEL/HBM clock 为 `150/500/449 MHz`；routed timing 有轻微
setup violation：WNS `-0.005 ns`，TNS `-0.010 ns`，setup failing endpoints `2`，
违例在 `hbm_aclk`。构建目录为 `cuper-jacobi-lighttrace-restore-build/`，构建日志为
`cuper-jacobi-lighttrace-restore-build/logs/build_hw_tmux.log`。该版已同步到
Jacobi demo 槽，等待服务器上板；当前已验证 demo 仍是 `20260615-demo`。

`20260615-demo` 是 `CuperJacobiIteration` master-controller full graph light-trace 硬件 debug demo，
接入完整 Jacobi dataflow、Cuper SpMV service 和 Jacobi update。当前控制流取消旧的
`RoundToken`/`FeedbackToken` 自循环和 `UpdateFrameFork`，改由
`Jacobi_MasterController` 每轮显式发矩阵/compute/update command，并等待
`Jacobi_XHbmWriter` 的 done ack 后进入下一轮或统一 stop。`JACOBI_TRACE_LIGHT=1`
保留 controller、ptr/vector loader、coeff loader、8 路 `pair_compute[0..7]`、
pack writer 和 X HBM writer 的 14 路关键进度 trace。它覆盖同主线 Jacobi demo
槽，但不替换任何标准文件；当前 `cuper-tapa-jacobi` 仍然没有标准 bitstream。
demo xclbin UUID 为 `c37ecdbf-92ab-5d06-11bd-e2f9edc7f720`，SHA256 为
`78c4ffdb9268aa5c1635bf2eefeed3b828e8a26e60ab3ccb8d795c9484d975a7`。
最终 xclbin info 中 DATA clock 为 150 MHz，KERNEL clock 为 500 MHz，
HBM clock 为 450 MHz。构建目录为 `cuper-jacobi-iteration-build/`，构建日志为
`cuper-jacobi-iteration-build/logs/build_hw_tmux.log`。

当前 light-trace ABI 把 `SpElement_list_ptr` 和 `Matrix_data_0` 映射到 HBM[0]，
`Matrix_data_1..15` 映射到 HBM[1..15]，`B` 在 HBM[20]，`Diag_inv` 在 HBM[21]，
`X` 在 HBM[22]，`Status` 在 HBM[24]，`Metrics` 在 HBM[25]，`Debug` 在 HBM[26]。
VPL implementation 和 `.xclbin` 封装都已完成，`Run completed`，v++ link 总耗时
`3h 27m 40s`。routed timing 已收敛：WNS `0.003 ns`，TNS `0.000 ns`，
setup failing endpoints `0`，hold worst slack `0.009 ns`。

2026-06-15 已按 demo-only 口径完成上板测试，日志在
`logs/jacobi_full_graph_hw_20260615_223100_master_controller/`。单轮 Jacobi
`MAX_ITERS=1` 覆盖 `thermal2_n16`、`thermal2_n1024`、`thermal2_n4096`、
`thermal2_n16384`、`thermal2_n65536`、`thermal2_n131072`、`thermal2_n262144`
和完整 `thermal2`，均为 `rc=0`、`Correctness Verification: Passed`、
`Error Num=0`。完整固定轮数使用 CPU reference 到 `tau=1e-5` 的轮数设置
`MAX_ITERS`：`thermal2_n1024` 451 轮 `2.6644 ms`，`thermal2_n65536` 743 轮
`182.212 ms`，`thermal2_n131072` 842 轮 `411.684 ms`，`thermal2_n262144`
900 轮 `882.205 ms`，完整 `thermal2` 24409 轮 `113035 ms`。当前硬件仍是固定轮数，
`Status=1` 表示跑到 `MAX_ITERS`，不是硬件内部 early-exit。

TAPA Cuper / Jacobi iteration wide-HBM 实验文件：

```text
cuper-tapa-jacobi-u55c-20260616-demo.xclbin
```

这版在 `20260615` master-controller full graph 基础上，用 `JACOBI_WIDE_HBM=1`
把 Cuper 主矩阵通道从 16 路扩到 24 路。HBM 分配为：
`Matrix_data_0..23` 映射到 HBM[0..23]，`SpElement_list_ptr/B/Diag_inv/X/Status`
共享 HBM[30]，`Metrics` 使用 HBM[31]。该 no-debug artifact 的 UUID 为
`aa594af3-f811-1b17-f507-fd504f93425e`，SHA256 为
`232c5afeaf8e122f7b30e5b26e95553a40ea44556ea59723480cab1f77453f9c`。
最终 xclbin info 中 DATA/KERNEL/HBM clock 为 `147/500/450 MHz`；构建时 link
请求频率为 150 MHz。构建目录为 `cuper-jacobi-wide-hbm-build/`。routed timing
仍有 setup violation：WNS `-0.120 ns`，TNS `-2.169 ns`，setup failing endpoints
`64`。服务器侧最小 smoke 已失败，当前只保留为失败边界 artifact，不替换
`20260615` 已板测通过 demo 的结论。

TAPA Cuper / SpMV-only 24 路服务实验文件：

```text
cuper-tapa-spmv-u55c-20260617-demo.xclbin
```

这版来自 `DLC/Cuper-jacobi-iteration` 的 `CuperSpmvServiceOnly` 顶层，只计算
`Y=A*X`，不拆 `A=D+R`，不取负 `X`，也不执行 Jacobi update。它用于隔离验证 Cuper
SpMV service 扩到 24 路 Matrix_data 的硬件边界。UUID 为
`492f929f-4232-3a37-b7e0-3969b5052219`，SHA256 为
`c4908d759c81c2d4b1202236ba611a2cdeb2ec3edeab595ec588efa799257705`。
DATA/KERNEL/HBM clock 为 `141/500/450 MHz`；routed timing 未收敛：
WNS `-0.420 ns`，TNS `-359.841 ns`，setup failing endpoints `2281`。构建目录为
`cuper-jacobi-spmv-only-24hbm-build/`。服务器侧测试必须设置
`JACOBI_TOP=CuperSpmvServiceOnly JACOBI_SPMV_ONLY=1 JACOBI_HBM_CHANNELS=24`，并使用
`cuper_jacobi_host` 的 SpMV-only 分支。

同一源码下的 32 路 SpMV-only 构建在 VPL `create_bd` 阶段失败，原因是 U55C HBM
subsystem port connection 用满：`You have run out of port connections on /hmss_0.
All 33 connections are used`。因此 32 路没有 `.xclbin` 可同步，后续若要继续探索，
需要先减少 top-level `m_axi` 端口数，而不是只调整 HBM bank 映射。

TAPA Cuper / SpMV-only 16 路 per-HBM 去 padding 实验文件：

```text
cuper-tapa-spmv-u55c-20260618-strip16-demo.xclbin
```

这版仍是 `CuperSpmvServiceOnly`，仍只计算 `Y=A*X`，但打开
`JACOBI_SPMV_STRIP_PADDING=1`。host 打包时为每个 HBM channel 生成独立 batch
边界和独立 `Matrix_len`，kernel 端 `CuperSpmvOnly_StripPtrLoader`、
`CuperSpmvOnly_MatrixLoaderStrip` 和 `CuperSpmvOnly_CoreStrip` 按 per-HBM 边界消费
矩阵流，从而剔除跨 HBM channel 的 batch 尾部 padding。它不改变 512-bit beat
格式、每 beat 8 个 SpElement slot、乘加、accumulator、checker 或 sort tree。

UUID 为 `d10e19f2-b1dd-72e8-cfb3-860a503a20f7`，SHA256 为
`0b74e6bafd8def47f66f0592464fd4d9f2baa33908b78525bf60762f37f87156`。DATA/KERNEL/HBM
clock 为 `207/500/436 MHz`；routed timing 未收敛：WNS `-1.488 ns`，
TNS `-24182.635 ns`，setup failing endpoints `62619`，违例在
`clk_kernel_00_unbuffered_net`。构建目录为 `cuper-jacobi-spmv-strip16-build/`，
构建日志为 `cuper-jacobi-spmv-strip16-build/logs/build_hw_tmux.log`。本机已完成
software simulation：`thermal2_n1024`、`thermal2_n4096`、`thermal2_n65536` 和完整
`thermal2` 均 `Error Num=0`；对应矩阵读取 beat 节省约 `8.12%`、`4.08%`、`4.70%`
和 `5.91%`。服务器侧测试必须设置
`JACOBI_TOP=CuperSpmvServiceOnly JACOBI_SPMV_ONLY=1 JACOBI_HBM_CHANNELS=16
JACOBI_SPMV_STRIP_PADDING=1`。

TAPA Cuper / SpMV-only 16 路 strip accumulator window 调参实验文件：

```text
cuper-tapa-spmv-u55c-20260622-strip16-window16-demo.xclbin
cuper-tapa-spmv-u55c-20260622-strip16-window14-demo.xclbin
```

这两版都基于 `CuperSpmvServiceOnly` + `JACOBI_SPMV_STRIP_PADDING=1`，只改变
`JACOBI_SPMV_ACC_WINDOW`。host Reordering 用同一个 window 拉开同 row 的累加距离，
HLS accumulator 侧也用同一个 window 作为 `local_part_Y_ping/pong` 的 dependence
distance，因此两版的 `cuper_acc_accumulate` 主循环都达到 II=1。它们是
`20260618-strip16-demo` 的调参候选，不覆盖该版已板测结论。

同步版本信息：

```text
window=16:
  file: 395bitstream/cuper-tapa-spmv-u55c-20260622-strip16-window16-demo.xclbin
  UUID: 107f5272-85e9-2adc-609e-054870d605d9
  SHA256: 4ef21daf80da3fff0115e0fa68483889ca7505712513736461a066e5668d2e7f
  DATA/KERNEL/HBM clock: 150 / 500 / 450 MHz
  Accumulator II: 1
  Accumulator accumulate HLS estimate: 1.589 ns
  Routed timing: WNS 0.000 ns, TNS 0.000 ns, setup failing endpoints 0
  Build log: cuper-jacobi-spmv-strip16-window16-hw-150m-build/logs/strip16_window16_hw_20260621_223123.log

window=14:
  file: 395bitstream/cuper-tapa-spmv-u55c-20260622-strip16-window14-demo.xclbin
  UUID: b254959a-353b-a7bd-119c-7952f2a01af3
  SHA256: 61cf8be611421703f141422ba46f064eb899de15df54797559ca172d4ebd04bd
  DATA/KERNEL/HBM clock: 150 / 500 / 450 MHz
  Accumulator II: 1
  Accumulator accumulate HLS estimate: 2.727 ns
  Routed timing: WNS 0.003 ns, TNS 0.000 ns, setup failing endpoints 0
  Build log: cuper-jacobi-spmv-strip16-window14-hw-150m-build/logs/strip16_window14_hw_delayed_20260621_223123.log
```

服务器侧测试设置为：

```text
JACOBI_TOP=CuperSpmvServiceOnly JACOBI_SPMV_ONLY=1 JACOBI_HBM_CHANNELS=16
JACOBI_SPMV_STRIP_PADDING=1 JACOBI_SPMV_ACC_WINDOW={16|14}
```

TAPA Cuper / SpMV-only 16 路 strip accumulator window 300 MHz no-progress 实验文件：

```text
cuper-tapa-spmv-u55c-20260622-strip16-window14-300m-noprogress-demo.xclbin
cuper-tapa-spmv-u55c-20260622-strip16-window16-300m-noprogress-demo.xclbin
```

这两版仍基于 `CuperSpmvServiceOnly` + `JACOBI_SPMV_STRIP_PADDING=1`，用于单独测试
去掉 progress snapshot 细粒度事件后，300 MHz link 目标下最终 xclbin 的自动降频和
服务器侧功能边界。它们不是标准候选，也不覆盖上面 150 MHz timing-clean 的
window14/window16 调参记录。

同步版本信息：

```text
window=14, 300 MHz no-progress:
  file: 395bitstream/cuper-tapa-spmv-u55c-20260622-strip16-window14-300m-noprogress-demo.xclbin
  UUID: f13edc28-7dd1-d7aa-ab66-e51c52cf31b7
  SHA256: 10c7c11c2b461b8c4b376f6eb3cceeee754722ab883d32d9b6cb89b6289733d5
  Requested kernel clock: 300 MHz
  DATA/KERNEL/HBM clock: 202 / 500 / 450 MHz
  Achieved kernel clock: 202.5 MHz
  Routed timing: WNS -1.604 ns, TNS -31426.979 ns, setup failing endpoints 74375
  Build log: cuper-jacobi-spmv-strip16-window14-300m-noprogress-build/logs/build_hw_tmux.log

window=16, 300 MHz no-progress:
  file: 395bitstream/cuper-tapa-spmv-u55c-20260622-strip16-window16-300m-noprogress-demo.xclbin
  UUID: 525491e1-725b-4c9c-df09-cf555a26ba37
  SHA256: 5de4d204bf28528693d13c69b5873f19877120e69d511c2ab0b771a4152298f1
  Requested kernel clock: 300 MHz
  DATA/KERNEL/HBM clock: 192 / 500 / 447 MHz
  Achieved kernel clock: 192.1 MHz
  Routed timing: WNS -1.870 ns, TNS -32768.406 ns, setup failing endpoints 69785
  Build log: cuper-jacobi-spmv-strip16-window16-300m-noprogress-build/logs/build_hw_tmux.log
```

服务器侧测试设置仍为：

```text
JACOBI_TOP=CuperSpmvServiceOnly JACOBI_SPMV_ONLY=1 JACOBI_HBM_CHANNELS=16
JACOBI_SPMV_STRIP_PADDING=1 JACOBI_SPMV_ACC_WINDOW={14|16}
```

注意：这两版虽然生成了 xclbin，但 routed timing 明确未满足 300 MHz 目标，Vitis 对
DATA clock 做了 auto-frequency scaling。它们只用于上板风险测试和确认 no-progress
路径，不建议晋级标准，也不更新正式 `source.diff`。

TAPA Cuper / SpMV-only 16 路 PE-lane compact 实验文件：

```text
cuper-tapa-spmv-u55c-20260618-compact16-demo.xclbin
```

这版仍是 `CuperSpmvServiceOnly`，只计算 `Y=A*X`，不进入 Jacobi update。它打开
`JACOBI_SPMV_COMPACT_PE=1`，host 在每个 HBM channel 的 batch 内把 8 条 PE lane
重新紧凑打包，并把原 lane id 写进 `rowIdx[16:14]`；`rowIdx[13:0]` 保留原 Cuper
行号编码，`rowIdx[17]` 仍作为 padding 标记。kernel 端使用 compact core 和
accumulator 解 lane tag，再把贡献累加回对应 PE lane。

这版用于验证“每个 HBM 内部先消 PE-lane padding”的协议边界；它还没有消除
PE 内部由 reorder 顺序造成的 `reorder_holes`，且当前 compact accumulator 为了先保证
功能正确，在 512-bit beat 内按 slot 串行分发，未必能直接带来硬件加速。

UUID 为 `7f1e6302-e2a1-05e5-ab24-42a81b9f1488`，SHA256 为
`2ec7758129ea44dfadd617b97587030de27a0f20d44b56f4bb727749768186b6`。DATA/KERNEL/HBM
clock 为 `200/500/448 MHz`；routed timing 只有 HBM clock 轻微 setup violation：
WNS `-0.006 ns`，TNS `-0.007 ns`，setup failing endpoints `2`。DATA 和 KERNEL
clock 分别为 WNS `0.027 ns` / `0.518 ns`。构建目录为
`cuper-jacobi-spmv-compact16-build/`，构建日志为
`cuper-jacobi-spmv-compact16-build/logs/build_hw_tmux.log`，v++ link 总耗时
`3h 55m 24s`。

HBM 映射为：`Matrix_data_0..15` 使用 HBM[0..15]，`SpElement_list_ptr` 使用
HBM[16]，`X` 使用 HBM[17]，`Y_out` 使用 HBM[18]，`Status` 使用 HBM[30]，
`Metrics` 使用 HBM[31]。本机 software simulation 已通过 `thermal2_n1024`、
`thermal2_n65536` 和完整 `thermal2`，对应 compact 读取 beat 节省约 `15.85%`、
`11.16%` 和 `13.54%`。服务器侧测试必须设置
`JACOBI_TOP=CuperSpmvServiceOnly JACOBI_SPMV_ONLY=1 JACOBI_HBM_CHANNELS=16
JACOBI_SPMV_COMPACT_PE=1`。

TAPA Cuper / SpMV-only 16 路 lane-static real/batch 实验文件：

```text
cuper-tapa-spmv-u55c-20260618-lanereal16-demo.xclbin
```

这版仍是 `CuperSpmvServiceOnly`，只计算 `Y=A*X`，不进入 Jacobi update。它打开
`JACOBI_SPMV_LANE_STATIC_REAL=1`，host 在每个 HBM channel、每个 batch 内按固定
lane 保留真实元素，并保持 `slot p -> lane p` 的映射。它的目标是去掉 PE 内部
`Reordering` 补出来的空洞，同时避免 compact16 的动态 lane tag 回填后端。

注意：当前实现是 `lane-static real/batch`，不是最终形态的
`lane-static real/stream`。原因是现有 `CoreStrip` 仍按 column batch 装载 X，
所以这一版先保持 batch 边界。kernel 端复用 strip-style ptr/loader/core 和普通
`Accumulator`，没有采用 compact16 的动态 lane accumulator。

UUID 为 `98358acf-f40e-4f2f-b77f-4a25c24f4473`，SHA256 为
`c8ef2426248a1acd4d02a75da39d72439c1cabdd12450428cfa83ce0baf1b49d`。
DATA/KERNEL/HBM clock 为 `197/500/450 MHz`；routed timing 未完全收敛：
WNS `-0.073 ns`，TNS `-4.957 ns`，setup failing endpoints `215`，违例在
`clk_kernel_00_unbuffered_net`。构建目录为
`cuper-jacobi-spmv-lanereal16-build/`，构建日志为
`cuper-jacobi-spmv-lanereal16-build/logs/build_hw_tmux.log`，v++ link 总耗时
`4h 13m 41s`。

HBM 映射为：`Matrix_data_0..15` 使用 HBM[0..15]，`SpElement_list_ptr` 使用
HBM[16]，`X` 使用 HBM[17]，`Y_out` 使用 HBM[18]，`Status` 使用 HBM[30]，
`Metrics` 使用 HBM[31]。本机 software simulation 已通过 `thermal2_n1024` 和
`thermal2_n65536`：`thermal2_n1024` matrix read beats 从 `2624` 降到 `883`，
`thermal2_n65536` 从 `68464` 降到 `57472`，节省 `16.0552%`。HLS 同时显示
`Accumulator_Pipeline_cuper_acc_accumulate` 达成 II=`5`，而 strip16 为 II=`2`。

2026-06-18 服务器侧已完成 demo-only 上板 sweep，日志为：

```text
logs/spmv_lanereal16_hw_sweep_20260618_233431/
```

`thermal2_n16` 到完整 `thermal2` 共 8 个数据集全部 `rc=0`、`Status=1`、
`Correctness Verification: Passed`、`Error Num=0`。完整 `thermal2` 时间为
`2.35566 ms`，吞吐为 `7.2848 GFLOP/s`，matrix read beats 从 `1,373,424` 降到
`1,151,370`，节省 `16.1679%`。该结果已经替代 HTML 主报告里前一条异常慢的
compact16 曲线；但它仍慢于 strip16 的完整 `thermal2` `1.29158 ms` /
`13.2865 GFLOP/s`，因此不建议晋级标准，也不覆盖当前有效 `source.diff`。

服务器侧测试设置为：

```text
JACOBI_TOP=CuperSpmvServiceOnly JACOBI_SPMV_ONLY=1 JACOBI_HBM_CHANNELS=16
JACOBI_SPMV_LANE_STATIC_REAL=1
```

TAPA Cuper / SpMV-only 16 路 RTL owner-bank 乱序累加实验文件：

```text
cuper-tapa-spmv-u55c-20260620-ooobank16-demo.xclbin
```

这版仍是 `CuperSpmvServiceOnly`，只计算 `Y=A*X`，不进入 Jacobi update。它继续使用
`JACOBI_SPMV_LANE_STATIC_REAL=1` 的真实元素固定 lane 打包，并打开
`JACOBI_SPMV_OOO_ACCUMULATE=1` 与 `JACOBI_SPMV_OOO_ACCUMULATE_RTL=1`。核心变化是把
后端 accumulator 从 HLS lane-static 路径替换成 TAPA 自定义 RTL：
16 个 owner bank，每个 owner bank 内部接 8 条 pair-lane 输入，并通过 round-robin
仲裁输出到 `TaggedScatterWriterOoo`。

生成文件：

```text
395bitstream/cuper-tapa-spmv-u55c-20260620-ooobank16-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260620-ooobank16-demo.xclbin.info
```

当前同步版本信息：

```text
Kernel: CuperSpmvServiceOnly
UUID: 58158740-a7ef-a803-0da5-1fd8b3206253
SHA256: 5e3f2e863cba4efca519ce18f9e9e735f05084af1242cd493e079c1844f777b3
DATA/KERNEL/HBM clock: 150 / 500 / 445 MHz
Timing: WNS -0.024 ns, TNS -0.086 ns, setup failing endpoints 8
```

HBM 映射为：`Matrix_data_0..15` 使用 HBM[0..15]，`SpElement_list_ptr` 使用
HBM[16]，`X` 使用 HBM[17]，`Y_out` 使用 HBM[18]，`Status` 使用 HBM[30]，
`Metrics` 使用 HBM[31]。当前同步版构建目录为
`cuper-tapa-spmv-ooobank16-lat12-hw-150m-20260621-build/`，构建日志为
`cuper-tapa-spmv-ooobank16-lat12-hw-150m-20260621-build/logs/build_hw_tmux.log`，
v++ link 总耗时 `3h 59m 3s`。

资源压力：CLB LUT `25.83%`，CLB `51.17%`，BRAM Tile `65.48%`，URAM `53.33%`，
DSP `7.70%`，Total SLLs `24473`。相比先前 128 owner-lane RTL 方案，这版通过把
8 条 pair-lane 收进一个 owner-bank RTL wrapper，显著降低了 BRAM/布局压力，并完成
了 routed xclbin 生成。

本机已完成 Vivado/xsim 后端数据集仿真：`thermal2_n4096`、`thermal2_n16384` 和
`thermal2_n65536` 均通过；`thermal2_n131072` 暂时撞到 testbench
`MAX_ROWS=65536` 上限，不是 RTL 计算失败。`thermal2_n16` 和 `thermal2_n1024`
此前也已通过同一路径。当前修复点是把 lane accumulator 的浮点加法 pipeline 对齐到
Vivado floating_point IP/xsim 实测的 12 cycle。服务器上板 sweep 尚未执行；因此
当前只作为 SpMV-only demo artifact，同步用于测试，不建议晋级标准，也不覆盖当前
有效 `source.diff`。

该同名 demo 槽此前有两版失败/待测历史：旧 UUID
`22b0a282-c282-cfaf-e45a-f8bebf4cc644` 在服务器侧表现为 `thermal2_n16` 返回但
输出全 0、`thermal2_n1024` 卡在 `after ReadFromDevice before Finish`；随后
`e6998763-ba80-b12f-f180-5099ba926c6d` 修复了 owner-bank 输出计数闭合和 lane tag
显式传递，但仍未包含本轮 floating_point latency=12 修复。旧结论只对应旧 UUID，
不对应当前同步文件。

TAPA Cuper / SpMV-only 16 路 RTL owner-bank progress 实验文件：

```text
cuper-tapa-spmv-u55c-20260621-ooobank16-progress-demo.xclbin
```

这版仍是 `CuperSpmvServiceOnly` 的 single SpMV 实验 artifact。它保留
`JACOBI_SPMV_LANE_STATIC_REAL=1`、`JACOBI_SPMV_OOO_ACCUMULATE=1` 和
`JACOBI_SPMV_OOO_ACCUMULATE_RTL=1`，并把 owner-bank fadd wrapper 与对应 Vivado IP
Tcl 一起放进 hotpatch/pack 流程，避免只替换 bank/lane RTL 时漏掉 floating point IP
生成脚本。该版未替换 `20260620-ooobank16-demo`，作为 progress 候选单独保留。

生成文件：

```text
395bitstream/cuper-tapa-spmv-u55c-20260621-ooobank16-progress-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260621-ooobank16-progress-demo.xclbin.info
```

同步版本信息：

```text
Kernel: CuperSpmvServiceOnly
UUID: a185de61-58e4-663a-3cc2-202fcc526665
SHA256: de6aead23348eddb023d7b62dea3be56594d74fc4ad87ce41d493d135797f378
DATA/KERNEL/HBM clock: 142 / 500 / 450 MHz
Timing: WNS -0.350 ns, TNS -8.524 ns, setup failing endpoints 97
```

HBM 映射为：`Matrix_data_0..15` 使用 HBM[0..15]，`SpElement_list_ptr` 使用
HBM[16]，`X` 使用 HBM[17]，`Y_out` 使用 HBM[18]，`Status` 使用 HBM[30]，
`Metrics` 使用 HBM[31]。构建目录为
`cuper-tapa-spmv-ooobank16-progress-hw-150m-20260621-build/`，构建日志为
`cuper-tapa-spmv-ooobank16-progress-hw-150m-20260621-build/logs/build_hw_tmux.log`。
VPL implementation 和 xclbin 封装完成，POST-VPL `0 errors`，v++ link 总耗时
`4h 44m 52s`。服务器上板 sweep 尚未执行。

TAPA Cuper / SpMV-only 16 路 RTL owner-bank heartbeat-clean 实验文件：

```text
cuper-tapa-spmv-u55c-20260622-ooobank16-heartbeat-clean-demo.xclbin
```

这版仍是 `CuperSpmvServiceOnly` 的 single SpMV 实验 artifact，继续使用
`JACOBI_SPMV_LANE_STATIC_REAL=1`、`JACOBI_SPMV_OOO_ACCUMULATE=1` 和
`JACOBI_SPMV_OOO_ACCUMULATE_RTL=1`。相对 `20260621-ooobank16-progress-demo`，这版
来自后续 heartbeat-clean RTL 构建，用于服务器侧验证此前 RTL owner-bank 版本的
清理/监测边界。该版不替换旧 progress 文件，作为新的 SpMV-only demo 单独同步。

生成文件：

```text
395bitstream/cuper-tapa-spmv-u55c-20260622-ooobank16-heartbeat-clean-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260622-ooobank16-heartbeat-clean-demo.xclbin.info
```

同步版本信息：

```text
Kernel: CuperSpmvServiceOnly
UUID: a87434fe-0abf-57ff-a272-98407dfdf44d
SHA256: cbb2caab1406c79cf01826639168fe0f08611ceae261d4f078bfc08a27a875f2
DATA/KERNEL/HBM clock: 150 / 500 / 450 MHz
Timing: WNS 0.003 ns, TNS 0.000 ns, setup failing endpoints 0
```

HBM 映射为：`Matrix_data_0..15` 使用 HBM[0..15]，`SpElement_list_ptr` 使用
HBM[16]，`X` 使用 HBM[17]，`Y_out` 使用 HBM[18]，`Status` 使用 HBM[30]，
`Metrics` 使用 HBM[31]。构建目录为
`cuper-tapa-spmv-ooobank16-heartbeat-clean-hw-150m-20260622-build/`，构建日志为
`cuper-tapa-spmv-ooobank16-heartbeat-clean-hw-150m-20260622-build/logs/build_hw_tmux.log`。
VPL implementation 和 xclbin 封装完成，POST-VPL `0 errors`，v++ link 总耗时
`4h 30m 55s`。服务器上板 sweep 尚未执行。

TAPA Cuper / SpMV-only 16 路 RTL issue scoreboard 实验文件：

```text
cuper-tapa-spmv-u55c-20260623-scoreboard16-demo.xclbin
```

这版仍是 `CuperSpmvServiceOnly` 的 single SpMV 实验 artifact，使用
`JACOBI_SPMV_LANE_STATIC_REAL=1`、`JACOBI_SPMV_OOO_ACCUMULATE=1` 和
`JACOBI_SPMV_OOO_SCOREBOARD_RTL=1`。它只把 owner 内 8 条 lane FIFO 的 issue
scoreboard 替换成 RTL，FP32 加法、URAM partial sum 和最终 tagged writer 仍由 HLS
实现；`JACOBI_SPMV_SCOREBOARD_DEBUG=1` 打开 core/issue/acc lane pulse 计数表。

生成文件：

```text
395bitstream/cuper-tapa-spmv-u55c-20260623-scoreboard16-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260623-scoreboard16-demo.xclbin.info
```

同步版本信息：

```text
Kernel: CuperSpmvServiceOnly
UUID: 8ab0b8d1-2889-e7b2-38c0-4026db80679e
SHA256: bbe249cfc1d974e8111584d1ef76fc64b5b084247e4cffef7453d1524cc9d8fd
DATA/KERNEL/HBM clock: 39 / 500 / 450 MHz
Timing: WNS -18.950 ns, TNS -32059.350 ns, setup failing endpoints 28858
```

HBM 映射为：`Matrix_data_0..15` 使用 HBM[0..15]，`SpElement_list_ptr` 使用
HBM[16]，`X` 使用 HBM[17]，`Y_out` 使用 HBM[18]，`Status` 使用 HBM[30]，
`Metrics` 和 `Debug` 使用 HBM[31]。构建目录为
`cuper-tapa-spmv-scoreboard-debug-build/`，构建日志为
`cuper-tapa-spmv-scoreboard-debug-build/logs/build_hw_tmux.log`。
VPL implementation 和 xclbin 封装完成，POST-VPL `0 errors`，v++ link 总耗时
`4h 26m 42s`。由于 routed timing 大幅未收敛且 DATA clock 自动降到 39 MHz，该版只
适合服务器侧功能/监测边界验证，不作为性能候选。

TAPA Cuper / SpMV-only 8 路构建批次：

```text
cuper-tapa-spmv-u55c-20260627-original8-demo.xclbin
cuper-tapa-spmv-u55c-20260627-strip8-demo.xclbin
cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-demo.xclbin
cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-headreg-demo.xclbin
cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-donedrain-demo.xclbin
```

这些 artifact 都来自 `DLC/Cuper-jacobi-iteration` 的 `CuperSpmvServiceOnly` 顶层，目标是把
Matrix_data/Core/Accumulator 缩到 8 路 HBM，先确认较窄并行度下 150 MHz 构建和
RTL 记分板分支是否能稳定收敛。HBM 映射为：`Matrix_data_0..7` 使用 HBM[0..7]，
`SpElement_list_ptr` 使用 HBM[8]，`X` 使用 HBM[9]，`Y_out` 使用 HBM[10]，
`Status` 使用 HBM[30]，`Metrics` 使用 HBM[31]。`lanereal8-scoreboard-headreg`
是针对旧 `lanereal8-scoreboard` timeout 的 RTL wrapper head 缓存修正版；
`lanereal8-scoreboard-donedrain` 在 head-register wrapper 上继续把 done token 改成
per-lane drain barrier。它们都不覆盖旧失败 artifact。所有文件均为 demo artifact，
不替换已验证的 16 路 strip/lanereal 记录，也不作为标准 bitstream。

同步版本信息：

```text
original8:
  file: 395bitstream/cuper-tapa-spmv-u55c-20260627-original8-demo.xclbin
  UUID: c4fbfa69-5f20-f3d8-3e7f-c514119625e6
  SHA256: a32eeece95bf9664cb9afc4dbea20cdb46b47bfb58b5bb9f6f01e726e648c90f
  DATA/KERNEL/HBM clock: 150 / 500 / 450 MHz
  Routed timing: WNS 0.003 ns, TNS 0.000 ns, setup failing endpoints 0
  Build log: cuper-jacobi-spmv-original8-hw-150m-build/logs/build_hw_tmux.log
  v++ link elapsed: 2h 18m 21s

strip8:
  file: 395bitstream/cuper-tapa-spmv-u55c-20260627-strip8-demo.xclbin
  UUID: 14f04872-7324-2970-12bd-135e3e8eb55b
  SHA256: d615702025dc041cd61cd858d2219660230e66f346fd16719e4a34758385aad3
  DATA/KERNEL/HBM clock: 150 / 500 / 450 MHz
  Routed timing: WNS 0.003 ns, TNS 0.000 ns, setup failing endpoints 0
  Build log: cuper-jacobi-spmv-strip8-hw-150m-build/logs/build_hw_tmux.log
  v++ link elapsed: 2h 35m 56s

lanereal8-scoreboard:
  file: 395bitstream/cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-demo.xclbin
  UUID: 9f6a0133-2169-15f9-d6ec-752ecd8eaca0
  SHA256: 0268f72b8438b84a55448bd02eb9a485010e745a55fcbf65234dfd43ca473453
  DATA/KERNEL/HBM clock: 150 / 500 / 450 MHz
  Routed timing: WNS 0.003 ns, TNS 0.000 ns, setup failing endpoints 0
  Build log: cuper-jacobi-spmv-lanereal8-scoreboard-hw-150m-build/logs/build_hw_tmux.log
  v++ link elapsed: 2h 52m 37s

lanereal8-scoreboard-headreg:
  file: 395bitstream/cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-headreg-demo.xclbin
  UUID: 39eb30df-2178-51aa-3333-f1cbb9a0e389
  SHA256: 1ac7664203e0eb3b6bd8bd35a932ecd8a1afdfb81d34dedf6cc26f98663870e1
  DATA/KERNEL/HBM clock: 150 / 500 / 450 MHz
  Routed timing: WNS 0.003 ns, TNS 0.000 ns, setup failing endpoints 0
  Build log: cuper-jacobi-spmv-lanereal8-scoreboard-headreg8-hw-150m-build/logs/v++_CuperSpmvServiceOnly.log
  v++ link elapsed: 2h 59m 18s

lanereal8-scoreboard-donedrain:
  file: 395bitstream/cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-donedrain-demo.xclbin
  UUID: e90660d3-9efa-9066-7517-4547fc21097f
  SHA256: 94d60c0d10dfd6e5384ec0e305e6836a4531ea82d74a24375d054c5546e1d7ad
  DATA/KERNEL/HBM clock: 150 / 500 / 450 MHz
  Routed timing: WNS 0.003 ns, TNS 0.000 ns, setup failing endpoints 0
  Build log: cuper-jacobi-spmv-lanereal8-scoreboard-donedrain8-hw-150m-build/logs/build_hw_tmux.live.log
  v++ link elapsed: 3h 15m 0s
```

original8、strip8 和 lanereal8-scoreboard 构建前已完成对应 8-HBM 软件 smoke，结果均为
`Error Num=0`。headreg 和 donedrain 修正版均已通过 RTL wrapper Verilator、vector
primitive Verilator 和 scoreboard dataset C++/Verilator smoke。既有服务器侧上板结果：

| 数据集 | original8 FPGA ms | strip8 FPGA ms | lanereal8-scoreboard | lanereal8-scoreboard-donedrain |
| --- | ---: | ---: | --- | --- |
| `thermal2_n16` | 0.074269 | 0.079358 | 0.080481, PASS | 0.081832, PASS |
| `thermal2_n1024` | 0.077615 | 0.083637 | timeout 300s | timeout 300s, rc=124 |
| `thermal2_n4096` | 0.089127 | 0.087664 | 未继续 | 未继续 |
| `thermal2_n16384` | 0.109745 | 0.124493 | 未继续 | 未继续 |
| `thermal2_n65536` | 0.216074 | 0.222817 | 未继续 | 未继续 |
| `thermal2_n131072` | 0.345646 | 0.365984 | 未继续 | 未继续 |
| `thermal2_n262144` | 0.632533 | 0.622525 | 未继续 | 未继续 |
| `thermal2` | 2.74379 | 2.71420 | 未继续 | 未继续 |

结论：8-HBM original/strip 基础路径可跑完整 `thermal2`，strip8 只在部分中大规模点略快，
整体没有超过已验证的 16 路 strip/lanereal 线；`lanereal8-scoreboard` 和
`lanereal8-scoreboard-donedrain` 都在 `thermal2_n16` 能返回但 `thermal2_n1024`
300s timeout，失败集中在 RTL issue scoreboard + HLS scheduled accumulator 分支。
donedrain 版的 n1024 debug 仍停在 `after ReadFromDevice before Finish`，三次
pre-finish 采样里 `Status/Metrics` 仍是初始化哨兵，`progress_magic valid=0`，
`Y[0..15]` 全 0。当前不建议继续上板 sweep，也不更新正式 `source.diff`。

headreg 修正版将 owner scoreboard wrapper 改成每 lane 先缓存 FIFO head，再把缓存 head
交给 issue scoreboard。上游 FIFO read 只发生在填充空 head 时，下游 pop 只清本地缓存，
避免 downstream backpressure 或 RAW hazard bubble 期间重复读取、丢 head 或用组合
`s_dout` 采到已变化数据。donedrain 修正版在此基础上又把 done token 变成 per-lane
drain barrier，但服务器侧 `thermal2_n1024` 仍 300s timeout。下一步若继续该分支，
应单独构建 `JACOBI_SPMV_SCOREBOARD_DEBUG=1` 版本，用 core/issue/acc lane counters
定位卡在 core、RTL issue、scheduled accumulator 还是 scatter writer。

下面几段是此前 Jacobi demo 槽位的历史记录，不对应上面的 SpMV-only 实验文件。
`20260614` timing-clean light-trace full graph demo UUID 为
`3fc9b8f4-901b-008f-8bc9-26ea3bf6f0c1`，SHA256 为
`4d1fb090afebcf75d8087156665d969f02105813f984935feb8818c31afc38ab`。该版仍使用旧的
token/frame 自传播控制路径，150 MHz timing 已收敛，但服务器侧最小
`thermal2_n16 MAX_ITERS=1` 上板仍卡 `Finish()`；该失败结论只对应旧 UUID。

它覆盖的再上一版同名 `20260614` 15 路 light-trace full graph demo UUID 为
`ef3b1102-90ec-551a-d1e9-55fb6c023da5`，SHA256 为
`ba3db5ae3cc0e2720425097eec7110cd59bcc0b2b4a62608204046e0c5c7feb2`。上一版 DATA clock
为 164 MHz，routed timing 未收敛：WNS `-2.764 ns`，TNS `-70810.594 ns`，
setup failing endpoints `101497`。该失败风险只对应旧 UUID。

它覆盖的上一版同名 `20260614` 7 路 light-trace full graph demo UUID 为
`6dfaf1e3-9707-7f46-b914-1f59ca240993`，SHA256 为
`4f162b092f73cf6cf9c07a74af24d2545f8dec13ba0f59565e45d5206735c1f5`。服务器侧复测该版
后，`thermal2_n16` 和 `thermal2_n1024` 的 `MAX_ITERS=1` 已返回通过，
`thermal2_n65536` 仍卡在 `Finish()`；该结果只对应旧 UUID。

再上一版 `20260613` no-debug full graph demo UUID 为
`b233c1af-6ba7-ebc5-8a5b-c56d348c53c7`，SHA256 为
`1ed33e0b1d6929b388a64b85c5f70187d082e867c4ab1288d84f1adb6a80092a`。上一版是完整
`CuperJacobiIteration` no-debug graph，routed timing 未收敛：WNS `-1.480 ns`，
TNS `-26306.850 ns`，setup failing endpoints `68234`。它尚未完成上板 smoke，
该构建结论只作为历史记录。

再上一版同名 `20260613` mmap-only split-bank probe demo UUID 为
`380f9de1-e5c1-66ab-b888-db99d2ef3523`，SHA256 为
`7f0ff7e5b7999d77174105ea5cf0d44629a0b9a43521c8efdc29a70ace5d77f1`。上一版是
`CuperJacobiMmapProbeOnly`，只写 `Status`、`Metrics`、`Debug` 的固定槽位并等待
m_axi write response 后返回，不接入完整 Jacobi dataflow。2026-06-13 native XRT
上板 smoke 已通过，日志在 `logs/jacobi_mmap_probe_hw_20260613_214342/`：
`ROW_NUM=16` 和 `ROW_NUM=1024` 均为 `rc=0`、`wait_state=COMPLETED`，wait 前 sample
sync 已能读到 probe magic `1245921841` (`0x4a434231`)。这说明当时板卡、kernel
launch、split-bank m_axi 写回和 native XRT BO sync 边界可用，但该结论只作为历史
边界记录，不对应当前 full graph `.xclbin`。

再上一版同名 `20260613` entry mmap probe debug demo UUID 为
`7bf54cce-83a3-b7e7-97a9-719446658c03`，SHA256 为
`775d1da4c1c2f51ec58e0569950f618eb159481bf3eddea4e27b8f6a4da9eb24`。上一版是完整
`CuperJacobiIteration` graph 上的入口阻塞 mmap probe，routed timing 未收敛：
WNS `-2.350 ns`，TNS `-60974.352 ns`，failing endpoints `101235`。服务器侧上板
`thermal2_n16` 与 `thermal2_n1024` 的 `MAX_ITERS=1` 均为 120s timeout，host 停在
`[tapa-invoke] after ReadFromDevice before Finish`，且 Status[8..11]、
Metrics[8..11]、Debug[48..51] 入口 probe 全 0。该失败结论只对应旧 UUID，不能
套用到当前 full graph light-trace `.xclbin` 文件。再上一版同名 pre-Finish/empty-R demo UUID 为
`5c9f0e72-5ea9-7142-1e90-690b72d30557`，SHA256 为
`0d300c1f55c21078f1f24d5e551228ccc75855331585d6669bc3e15ac31b9c26`。上一版上板最小
smoke 显示 `thermal2_n16` 和 `thermal2_n1024` 的 `MAX_ITERS=1` 均停在
`[tapa-invoke] after ReadFromDevice before Finish`，rc=124；pre-Finish dump 中
Status/Metrics/Debug 全 0，probe 期间 CU `Status (IDLE)`、firewall GOOD。该失败
结论只对应上一版 UUID，不能套用到当前这个 `.xclbin` 文件。再上一版同名
finite-pair demo UUID 为 `6ad9f2dd-d23f-6ab2-c8bb-1129f00d27bb`，SHA256 为
`e981baf0f809065674f9bc696095bfa0d2e816ffb281c3dfe6dfeb8e8990a145`。

被覆盖的上一版同主线 Jacobi demo 是
`cuper-tapa-jacobi-u55c-20260612-demo.xclbin`，UUID 为
`401e53eb-a68f-55fb-78f8-5553f14edcd2`，SHA256 为
`46272395b4f4cef1a977767225080dfe2194fed3cf55baccbb5e4eec68e82e2f`。该版只包含
tail-drain 修复，上板 `thermal2_n1024 MAX_ITERS=1` 仍卡在 Finish 不返回。再上一版
同主线 Jacobi demo 是 `cuper-tapa-jacobi-u55c-20260611-demo.xclbin`，UUID 为
`b4664f5e-8cd6-0f7d-56ae-28384fce6400`，SHA256 为
`1113701276f09545b2407d16823e5649d6e017a9fcef63a014838106612e8eb5`。更早同名
demo UUID 为 `a7c95d3c-ec98-c287-67be-d81f71f7c95e`，SHA256 为
`a622e1600628e9c4ed34fe7dd7d5f2a2afcb374789fddaa4436b1ba9408e8172`。旧 testing
结论只作为历史记录，不能套用到当前这个 `.xclbin` 文件。2026-06-12 tail-drain
demo 的 routed timing 记录为 WNS `-2.842 ns`，TNS `-74910.742 ns`，failing
endpoints `105708`；2026-06-11 demo 的 routed timing 记录为 WNS `-2.575 ns`，
TNS `-56069.028 ns`，failing endpoints `96241`。

TAPA Cuper / FPGA-PCG 当前归档文件：

```text
cuper-tapa-pcg-fpga-u55c-20260525.xclbin
```

这版是 `CuperPcg`，即保留 TAPA Cuper SpMV task graph，同时把 PCG 初始化、
迭代、`alpha/beta`、向量更新和收敛判断放进 FPGA kernel。当前归档版为
2026-05-26 20:31 生成的 timed-debug build，stage counter 可读。xclbin
UUID 为 `51132100-b217-df93-f4dd-05bfc169f820`，SHA256 为
`8733b618312d1d17bee8123e512eec14f0ca831b6eca1372b3c22e6be11ae301`。
最终 xclbin info 中 DATA clock 为 213 MHz，KERNEL clock 为 500 MHz，
HBM clock 为 437 MHz。替换前版本已归档到
`bitstream_archive/2026-05-26-tapa-pcg-pre-timed-debug/`。

TAPA Cuper / single SpMV 当前 demo 候选文件：

```text
cuper-tapa-spmv-u55c-20260528-demo.xclbin
```

这版是 `CuperPcgSpmv`。它属于 `cuper-tapa-spmv` demo 候选，未替换当前标准
`cuper-tapa-spmv-u55c-20260522.xclbin`。demo xclbin UUID 为
`c95c1dfc-20ca-9152-279e-bafdf35fdc3d`，SHA256 为
`19d227179db7f22adfd12e78da119a99d102c59ebe25df686a652c6715ea95f2`。
最终 xclbin info 中 DATA clock 为 147 MHz，KERNEL clock 为 500 MHz，
HBM clock 为 418 MHz。构建日志为
`logs/cuper_tapa_pcg_spmv_hw_parallel_20260528_222446.log`，版本记录见
`docs/bitstream_summaries/2026-05-28-cuper-tapa-spmv-single-optimization/`。
该 demo 当前仍保留在 `395bitstream/` 的 single SpMV demo 槽；其 `.xclbin.info`
同时在 `bitstream_archive/2026-05-29-tapa-pcg-spmv-demo-candidates/` 留有文字归档。

该 single-SpMV demo 只保留历史 `CuperPcgSpmv` kernel 名和 demo/host 入口，
内部回到和满血 `Cuper(...)` 一致的 one-shot Cuper SpMV task graph，不再复用
`pcg_spmv_service.hpp` 的 command/stop/service 控制壳。它用于和
`cuper-tapa-spmv-u55c-20260522.xclbin` 标准曲线比较 `spmv_avg`、timeout 边界
和 diff。2026-05-28 上板 timeout 结论只对应旧 service 抽出版
`08f1f2dc-8c44-007f-a0a5-4dce1236ddd9`，不再对应当前这个 demo 文件。

2026-05-29 已按 demo-only 口径上板测试当前 UUID
`c95c1dfc-20ca-9152-279e-bafdf35fdc3d`，日志在
`logs/codex_two_demo_test_20260529_1300/`。本轮没有重跑当时已有四个标准 bitstream。
`thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、`thermal2_n262144`
和完整 `thermal2` 均返回且 diff 通过；完整 `thermal2` 的
`spmv_avg=1.781541 ms`，`gflops=9.632462`。共同成功点上它比 standalone TAPA
Cuper SpMV 标准略慢约 2.7% 到 8.1%，但成功边界从标准旧记录的
`thermal2_n131072` 扩到完整 `thermal2`。该 demo 未晋级为标准版。

TAPA Cuper / FPGA-PCG 当前 demo 候选文件：

```text
cuper-tapa-pcg-fpga-u55c-20260531-demo.xclbin
```

这版是 2026-05-31 新生成的 `CuperPcg` packed timing 实验 demo。它不替换当前
标准 `cuper-tapa-pcg-fpga-u55c-20260525.xclbin`。demo xclbin UUID 为
`f5b4fb4b-d7cc-f559-b5ba-29e2e6a88668`，SHA256 为
`a8df40e1bf21774c7608c329fd591012b84744a18dcf4e8b0dd36672d64ccf72`。
最终 xclbin info 中 DATA clock 为 172 MHz，KERNEL clock 为 500 MHz，
HBM clock 为 405 MHz。构建日志为
`logs/cuper_tapa_pcg_packed_timing_hw_20260531_163554.log`，构建目录为
`cuper-tapa-pcg-fpga-u55c-20260531-packed-timing-build/`，版本记录见
`docs/bitstream_summaries/2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/`。

2026-05-31 已完成该 demo 的 init-only 与 `MAX_ITERS=1` demo-only 上板测试，
日志在 `logs/codex_packed_timing_demo_test_20260531_195109_proper/`。本轮没有重跑
当时已有四个标准 bitstream。`thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、
`thermal2_n262144` 和完整 `thermal2` 的 init-only 与 1iter 均返回，direct ctrl
均为 `0x4 -> 0xe`，数值校验通过。完整 `thermal2` 上 init-only
`kernel_reported=302.744196 ms`，1iter `kernel_reported=944.123210 ms`；
`thermal2_n262144` 上 1iter `kernel_reported=210.319328 ms`。该版比归档的
controller-split demo 略快，但共同成功点仍慢于 TAPA full-PCG 标准版，因此暂不建议
晋级为标准版。注意本版 `MAX_ITERS=0` 会映射为
`effective_max_iters=max(4*N, 1000)`，不能再当 init-only 使用；init-only 仍使用
`TAU=1e100 MAX_ITERS=1 DIFF_TOL=1e-1`。

TAPA Cuper / FPGA-PCG 已归档 demo 候选文件：

```text
cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin
```

这版是 2026-05-31 同名覆盖的 `CuperPcg` controller-split 实验 demo。它不替换
当前标准 `cuper-tapa-pcg-fpga-u55c-20260525.xclbin`。demo xclbin UUID 为
`1d536c39-f561-340b-7efc-ac2c8440543d`，SHA256 为
`bc58605b36c98b29d84ce14939b95f8fc6b84bb7a505007fda95458545a349b8`。
最终 xclbin info 中 DATA clock 为 211 MHz，KERNEL clock 为 500 MHz，
HBM clock 为 450 MHz。构建日志为
`logs/cuper_tapa_pcg_controller_split_hw_20260531_020548.log`，构建目录为
`cuper-tapa-pcg-controller-split-build/`，版本记录见
`docs/bitstream_summaries/2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/`。
该 demo 已归档到
`bitstream_archive/2026-05-31-tapa-pcg-controller-split-demo/`，当前同步目录里不再
保留该 full-PCG demo 文件。

2026-05-31 已完成 controller-split demo 的 `hw` bitstream 构建、同步和
demo-only 上板测试。日志在
`logs/codex_controller_split_demo_test_20260531_140333/`。该版主要把
`dot_p_ap` 融入 `iter_spmv_stream` 接收路径，并把 `update_xr` / `update_p`
拆成 compute/store 两段，HLS 报告中这两段内部均达到 II=1；
`update_z_reduce` 与 `iter_dot_p_ap_lanes` 仍保留 FP64 reduction 的 II=5 代价。

本轮没有重跑当时已有四个标准 bitstream。`thermal2_n16`、`thermal2_n65536`、
`thermal2_n131072`、`thermal2_n262144` 和完整 `thermal2` 的 init-only 与
`MAX_ITERS=1` 均返回，direct ctrl 均为 `0x4 -> 0xe`，数值校验通过。完整
`thermal2` 上 init-only `kernel_reported=361.421363 ms`，1iter
`kernel_reported=954.077900 ms`。相对上一 II=1 controller 实验 UUID
`0170fa86-6e62-cfc9-aa66-2d330dd72cf2`，1iter 明显改善；但共同成功点仍略慢于
当前 TAPA full-PCG 标准版和上一 demo，因此该 demo 仍不建议按性能目标晋级为
标准版。注意本版 `dot_p_ap` 已合入 `iter_spmv` 计时，不能直接沿用旧分段口径。

随后又补跑一组完整 PCG full-run，不传 `MAX_ITERS=1`，并用
`KERNEL_TIMEOUT_SEC=0` 禁用 host 默认 60 秒轮询超时。日志在
`logs/codex_controller_split_fullrun_20260531_142400/`。
`thermal2_n16` 到 `thermal2_n262144` 分别跑到
`1/60/81/96/104/113/120` 次并收敛；`thermal2_n262144` 的
`kernel_reported=15263.805830 ms`，接近 TAPA 标准版旧记录
`14418.306 ms`，明显快于 2026-05-29 旧 demo 的 `39491.638 ms`。完整
`thermal2` 禁用 host 超时后约 490 秒仍为 `ctrl=0x0`，已按用户要求停止；
该点记录为未完成，不作为收敛失败结论。

上一版 II=1 controller 实验 demo 已被覆盖；其构建目录为
`cuper-tapa-pcg-ii1-build/`，版本记录见
`docs/bitstream_summaries/2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/`。
该旧 UUID 为 `0170fa86-6e62-cfc9-aa66-2d330dd72cf2`，SHA256 为
`ec3a98b09d662611ce50c4c484cb6b55ad2e7dbcd712a0b6d7833b38e4579fc8`，
DATA/KERNEL/HBM 为 `223/500/444 MHz`。2026-05-31 已完成该旧 UUID 的
demo-only 上板测试，日志在
`logs/codex_ii1_demo_test_20260531_011314/`。本轮没有重跑当时已有四个标准 bitstream。
`thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、`thermal2_n262144`
和完整 `thermal2` 的 init-only 与 `MAX_ITERS=1` 均返回，direct ctrl 均为
`0x4 -> 0xe`，数值校验通过。完整 `thermal2` 上 init-only
`kernel_reported=353.028139 ms`，1iter `kernel_reported=1767.825376 ms`。
相对 2026-05-29 旧 UUID `086a3345-ddf0-ffdd-b260-16ca5fa5223a`，1iter 有改善；
但共同成功点仍明显慢于当前 TAPA full-PCG 标准版，因此该旧 demo 未按性能目标
晋级为标准版。该结论只作为历史记录，不对应当前同步目录里的新 `.xclbin` 文件。

被移出旧 single demo 槽的 full-PCG packed feed/AP demo 已本地归档到：

```text
bitstream_archive/2026-05-28-tapa-pcg-packed-ap-demo-before-spmv-demo/
```

2026-05-29 两个 demo 候选的历史测试结论记录在：

```text
docs/bitstream_summaries/2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/
docs/bitstream_summaries/2026-05-28-cuper-tapa-spmv-single-optimization/
```

## 运行入口

TAPA Cuper / single SpMV：

```bash
make cuper-tapa-pcg-host
make run-cuper-tapa-spmv \
  TARGET=hw \
  DATASET=/path/to/dataset \
  BITFILE=395bitstream/cuper-tapa-spmv-u55c-20260522.xclbin
```

no-TAPA Cuper / single SpMV：

```bash
make cuper-notapa-pcg-xrt-host
make run-cuper-notapa-spmv-xrt \
  TARGET=hw \
  DATASET=/path/to/dataset
```

默认会使用：

```text
cuper-pcg-notapa/hw/cuper_packed_spmv_kernel.xclbin
```

如需直接指定本目录归档 bitstream，可运行 host：

```bash
./build/xplus_cuper_notapa_pcg_xrt_host \
  395bitstream/cuper-notapa-spmv-u55c-20260524.xclbin \
  /path/to/dataset \
  --spmv-only
```

no-TAPA Cuper / FPGA-PCG：

```bash
make cuper-control-xrt-host
./build/xplus_cuper_control_xrt_host \
  395bitstream/cuper-notapa-pcg-fpga-u55c-20260522.xclbin \
  /path/to/dataset \
  --tau 1e-8 \
  --max-iters 1000
```

TAPA Cuper / FPGA-PCG：

```bash
make cuper-tapa-pcg-fpga-host
make run-cuper-pcg-tapa-fpga \
  TARGET=hw \
  DATASET=/path/to/dataset \
  BITFILE=395bitstream/cuper-tapa-pcg-fpga-u55c-20260525.xclbin \
  TAU=1e-8 \
  MAX_ITERS=1000
```

## 口径说明

- `spmv` 版只比较 Cuper SpMV kernel。TAPA 版和 no-TAPA 版都可以用 `--spmv-only` 跑纯 SpMV。
- `pcg-fpga` 版把 PCG 控制、dot、alpha/beta、向量更新和收敛判断放进 FPGA kernel。
- TAPA single SpMV 的旧兼容 host-PCG 路径仍可用，但不算当前五条主线里的 FPGA-PCG。
- no-TAPA single SpMV 的 host-PCG 兼容路径也仍可用，主要用于复用 `cuper_packed_spmv_kernel` 做对照。
- TAPA Jacobi iteration 是普通 Jacobi 主线，不计算 PCG 的 `alpha/beta`，也不等同于默认
  Jacobi-PCG 预条件路线。
- legacy packed16hbm 版已从本同步目录移出，只在 `bitstream_archive/legacy-packed16hbm/README.md` 留文字记录；二进制文件在 U55C 服务器上保留。

对比时至少记录：

```text
dataset
n / nnz
tau
max_iters
iterations
status
residual_abs / residual_rel
plan / setup / kernel / spmv 时间
```
