# 395bitstream 对比说明

这个目录放 U55C 上需要保留/对比的 Project-XPlus Cuper xclbin。文件名按四条主线统一：

```text
cuper-{tapa|notapa}-{spmv|pcg-fpga}-u55c-YYYYMMDD.xclbin
```

同步目录常态保留六个成品槽位：四个标准 bitstream，加两个带 `-demo` 后缀的当前
候选 bitstream。两个 demo 槽位目前分别用于 `cuper-tapa-spmv` single SpMV 候选
和 `cuper-tapa-pcg` full-PCG 候选；新 demo 进入同一主线槽位时优先覆盖旧 demo
文件。四个标准版只有在用户明确确认满意后才会归档旧版并晋级替换。

如果某个文件带 `legacy`，说明它不是当前四条主线的首选版本，只作为历史对照保留。

## 当前文件

| 文件 | 主线 | PCG 主循环 | SpMV 实现 | 状态 |
| --- | --- | --- | --- | --- |
| `cuper-tapa-spmv-u55c-20260522.xclbin` | TAPA Cuper / single SpMV | host 或不跑 PCG | `DLC/Cuper/kernels/Cuper.cpp` / `Cuper` | 已有旧可用 bitstream |
| `cuper-tapa-spmv-u55c-20260528-demo.xclbin` | TAPA Cuper / single SpMV | 不跑 PCG | `DLC/Cuper/kernels/Cuper.cpp` / `CuperPcgSpmv` | 当前 single-SpMV demo 槽：Cuper-compatible one-shot 图，2026-05-29 demo-only 上板通过到完整 `thermal2` |
| `cuper-tapa-pcg-fpga-u55c-20260525.xclbin` | TAPA Cuper / FPGA-PCG | FPGA kernel | `DLC/Cuper/kernels/Cuper.cpp` / `CuperPcg` | 2026-05-26 20:31 timed-debug 版，全流程 FPGA PCG |
| `cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin` | TAPA Cuper / FPGA-PCG | FPGA kernel | `DLC/Cuper/kernels/Cuper.cpp` / `CuperPcg` | 当前 full-PCG demo 槽：2026-05-29 demo-only init/1iter 上板通过到完整 `thermal2` |
| `cuper-notapa-spmv-u55c-20260524.xclbin` | no-TAPA Cuper / single SpMV | host 或不跑 PCG | `kernels/cuper_pcg_control_kernel.cpp` / `cuper_packed_spmv_kernel` | 2026-05-24 新生成 |
| `cuper-notapa-pcg-fpga-u55c-20260522.xclbin` | no-TAPA Cuper / FPGA-PCG | FPGA kernel | `kernels/cuper_pcg_control_kernel.cpp` / `cuper_pcg_control_kernel` | 当前 no-TAPA FPGA-PCG 对照版 |

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

这版是 `CuperPcgSpmv`，当前文件已在 2026-05-29 被新构建覆盖。
它属于 `cuper-tapa-spmv` demo 候选，不替换当前标准
`cuper-tapa-spmv-u55c-20260522.xclbin`。demo xclbin UUID 为
`c95c1dfc-20ca-9152-279e-bafdf35fdc3d`，SHA256 为
`19d227179db7f22adfd12e78da119a99d102c59ebe25df686a652c6715ea95f2`。
最终 xclbin info 中 DATA clock 为 147 MHz，KERNEL clock 为 500 MHz，
HBM clock 为 418 MHz。构建日志为
`logs/cuper_tapa_pcg_spmv_hw_parallel_20260528_222446.log`，版本记录见
`docs/bitstream_summaries/2026-05-28-cuper-tapa-spmv-single-optimization/`。

当前 single-SpMV demo 只保留历史 `CuperPcgSpmv` kernel 名和 demo/host 入口，
内部回到和满血 `Cuper(...)` 一致的 one-shot Cuper SpMV task graph，不再复用
`pcg_spmv_service.hpp` 的 command/stop/service 控制壳。它用于和
`cuper-tapa-spmv-u55c-20260522.xclbin` 标准曲线比较 `spmv_avg`、timeout 边界
和 diff。2026-05-28 上板 timeout 结论只对应旧 service 抽出版
`08f1f2dc-8c44-007f-a0a5-4dce1236ddd9`，不再对应当前这个 demo 文件。

2026-05-29 已按 demo-only 口径上板测试当前 UUID
`c95c1dfc-20ca-9152-279e-bafdf35fdc3d`，日志在
`logs/codex_two_demo_test_20260529_1300/`。本轮没有重跑四个标准 bitstream。
`thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、`thermal2_n262144`
和完整 `thermal2` 均返回且 diff 通过；完整 `thermal2` 的
`spmv_avg=1.781541 ms`，`gflops=9.632462`。共同成功点上它比 standalone TAPA
Cuper SpMV 标准略慢约 2.7% 到 8.1%，但成功边界从标准旧记录的
`thermal2_n131072` 扩到完整 `thermal2`。该 demo 仍未晋级为标准版。

TAPA Cuper / FPGA-PCG 当前 demo 候选文件：

```text
cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin
```

这版是 `CuperPcg`，和上面的 single-SpMV demo 使用同批源码构建。它不替换当前标准
`cuper-tapa-pcg-fpga-u55c-20260525.xclbin`。demo xclbin UUID 为
`086a3345-ddf0-ffdd-b260-16ca5fa5223a`，SHA256 为
`83baded1910ecb2c9e662f9ff6920fd8a55dbd2898ae69629c862714e17cf7f1`。
最终 xclbin info 中 DATA clock 为 210 MHz，KERNEL clock 为 500 MHz，
HBM clock 为 408 MHz。构建日志为
`logs/cuper_tapa_pcg_hw_parallel_20260528_222446.log`，版本记录见
`docs/bitstream_summaries/2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/`。

2026-05-29 已按 demo-only 口径上板测试，日志在
`logs/codex_two_demo_test_20260529_1300/`。本轮没有重跑四个标准 bitstream。
`thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、`thermal2_n262144`
和完整 `thermal2` 的 init-only 与 `MAX_ITERS=1` 均返回，direct ctrl 均为
`0x4 -> 0xe`，数值校验通过。完整 `thermal2` 上 init-only
`kernel_reported=421.301805 ms`，1iter `kernel_reported=1959.134367 ms`。
共同成功点上 1iter 仍明显慢于当前 TAPA full-PCG 标准版和上一 demo，因此该 demo
不建议按性能目标晋级为标准版。

被移出旧 single demo 槽的 full-PCG packed feed/AP demo 已本地归档到：

```text
bitstream_archive/2026-05-28-tapa-pcg-packed-ap-demo-before-spmv-demo/
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

TAPA Cuper-compatible single SpMV demo（kernel 名仍为 `CuperPcgSpmv`）：

```bash
make cuper-tapa-pcg-host
make run-cuper-tapa-pcg-spmv \
  TARGET=hw \
  DATASET=/path/to/dataset \
  BITFILE=395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin \
  SPMV_REPEATS=3 \
  DIFF_TOL=1e-1
```

TAPA Cuper / FPGA-PCG demo：

```bash
make cuper-tapa-pcg-fpga-host
make run-cuper-pcg-tapa-fpga \
  TARGET=hw \
  DATASET=/path/to/dataset \
  BITFILE=395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin \
  TAU=1e-8 \
  MAX_ITERS=1000
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
- TAPA single SpMV 的旧兼容 host-PCG 路径仍可用，但不算当前四条主线里的 FPGA-PCG。
- no-TAPA single SpMV 的 host-PCG 兼容路径也仍可用，主要用于复用 `cuper_packed_spmv_kernel` 做对照。
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
