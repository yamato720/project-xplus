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
只有在用户明确确认满意后才会归档旧版并晋级替换。当前 `395bitstream/` 保留四个
已有标准 bitstream、一个 single SpMV demo 槽、一个 full-PCG demo 槽和一个
Jacobi demo 槽；`cuper-tapa-jacobi` 还没有标准 bitstream。

如果某个文件带 `legacy`，说明它不是当前五条主线的首选版本，只作为历史对照保留。

## 当前文件

| 文件 | 主线 | PCG 主循环 | SpMV 实现 | 状态 |
| --- | --- | --- | --- | --- |
| `cuper-tapa-spmv-u55c-20260522.xclbin` | TAPA Cuper / single SpMV | host 或不跑 PCG | `DLC/Cuper/kernels/Cuper.cpp` / `Cuper` | 已有旧可用 bitstream |
| `cuper-tapa-pcg-fpga-u55c-20260525.xclbin` | TAPA Cuper / FPGA-PCG | FPGA kernel | `DLC/Cuper/kernels/Cuper.cpp` / `CuperPcg` | 2026-05-26 20:31 timed-debug 版，全流程 FPGA PCG |
| `cuper-notapa-spmv-u55c-20260524.xclbin` | no-TAPA Cuper / single SpMV | host 或不跑 PCG | `kernels/cuper_pcg_control_kernel.cpp` / `cuper_packed_spmv_kernel` | 2026-05-24 新生成 |
| `cuper-notapa-pcg-fpga-u55c-20260522.xclbin` | no-TAPA Cuper / FPGA-PCG | FPGA kernel | `kernels/cuper_pcg_control_kernel.cpp` / `cuper_pcg_control_kernel` | 当前 no-TAPA FPGA-PCG 对照版 |
| 暂无标准文件 | TAPA Cuper / Jacobi iteration | FPGA kernel | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperJacobiIteration` | 第五主线已接入源码和软件测试，当前只有 demo 候选 |
| `cuper-tapa-spmv-u55c-20260528-demo.xclbin` | TAPA Cuper / single SpMV demo | host 或不跑 PCG | `DLC/Cuper/kernels/Cuper.cpp` / `CuperPcgSpmv` | demo 候选，未晋级标准 |
| `cuper-tapa-pcg-fpga-u55c-20260531-demo.xclbin` | TAPA Cuper / FPGA-PCG demo | FPGA kernel | `DLC/Cuper/kernels/Cuper.cpp` / `CuperPcg` | packed timing demo 候选，未晋级标准 |
| `cuper-tapa-jacobi-u55c-20260614-demo.xclbin` | TAPA Cuper / Jacobi iteration demo | FPGA kernel | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` / `CuperJacobiIteration` | full graph light-trace debug demo，已生成并同步，150 MHz timing-clean，待上板 smoke，未晋级标准 |

TAPA Cuper / Jacobi iteration 当前主线记录：

```text
DLC/Cuper-jacobi-iteration/
```

这条主线的顶层是 `CuperJacobiIteration`，普通 Jacobi 迭代公式为
`x_next=D^{-1}(b-Rx_old)`，不是 Jacobi 预条件子 PCG。当前代码已经接入根
`Makefile` 的 `cuper-jacobi-*` 目标，并完成 software/TAPA simulation smoke：
`cant.mtx` `MAX_ITERS=2`、`thermal2_n65536` `MAX_ITERS=1` 均为当前
deadlock-debug 单 `X` ABI 通过，`thermal2_n262144` 早期 software run 通过但还需用
当前 root target 补跑。
当前已有一个 demo 候选进入 Jacobi demo 槽，但还不是标准 bitstream。版本记录见
`docs/bitstream_summaries/2026-06-10-cuper-tapa-jacobi-iteration/`。

TAPA Cuper / Jacobi iteration 当前 demo 候选文件：

```text
cuper-tapa-jacobi-u55c-20260614-demo.xclbin
```

这版是 `CuperJacobiIteration` full graph light-trace 硬件 debug demo，接入完整 Jacobi
dataflow、Cuper SpMV service 和 Jacobi update，并通过 `JACOBI_TRACE_LIGHT=1` 增加
16 路关键进度 trace，额外覆盖 matrix loader0 首拍和 8 路 `pair_compute[0..7]`。它覆盖同主线 Jacobi demo
槽，但不替换任何标准文件；当前
`cuper-tapa-jacobi` 仍然没有标准 bitstream。demo xclbin UUID 为
`3fc9b8f4-901b-008f-8bc9-26ea3bf6f0c1`，SHA256 为
`4d1fb090afebcf75d8087156665d969f02105813f984935feb8818c31afc38ab`。
最终 xclbin info 中 DATA clock 为 150 MHz，KERNEL clock 为 500 MHz，
HBM clock 为 450 MHz。构建目录为 `cuper-jacobi-iteration-build/`，构建日志为
`cuper-jacobi-iteration-build/logs/build_hw_tmux.log`。

当前 light-trace ABI 把 `SpElement_list_ptr` 和 `Matrix_data_0` 映射到 HBM[0]，
`Matrix_data_1..15` 映射到 HBM[1..15]，`B` 在 HBM[20]，`Diag_inv` 在 HBM[21]，
`X` 在 HBM[22]，`Status` 在 HBM[24]，`Metrics` 在 HBM[25]，`Debug` 在 HBM[26]。
VPL implementation 和 `.xclbin` 封装都已完成，`Run completed`，v++ link 总耗时
`3h 52m 13s`。routed timing 已收敛：WNS `0.003 ns`，TNS `0.000 ns`，
setup failing endpoints `0`，hold worst slack `0.009 ns`。这版尚未完成上板 smoke。

它覆盖的上一版同名 `20260614` 15 路 light-trace full graph demo UUID 为
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
