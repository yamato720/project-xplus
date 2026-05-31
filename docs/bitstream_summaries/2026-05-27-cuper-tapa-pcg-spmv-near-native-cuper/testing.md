# 测试记录

## 当前状态

记录时间：2026-05-27，更新：2026-05-31

当前 full-PCG demo 已完成 `hw` bitstream 构建，并放入 `395bitstream/`
full-PCG demo 槽位：

```bash
395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin
395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin.info
```

当前 demo 信息：

| 项目 | 数值 |
| --- | --- |
| UUID | `1d536c39-f561-340b-7efc-ac2c8440543d` |
| SHA256 | `bc58605b36c98b29d84ce14939b95f8fc6b84bb7a505007fda95458545a349b8` |
| DATA clock | 211 MHz |
| KERNEL clock | 500 MHz |
| HBM clock | 450 MHz |
| 构建目录 | `cuper-tapa-pcg-controller-split-build/` |
| 构建日志 | `logs/cuper_tapa_pcg_controller_split_hw_20260531_020548.log` |
| 构建耗时 | 4h 31m 0s |

关键构建输出：

```text
Run vpl: FINISHED. Run Status: impl Complete!
Created .../cuper-tapa-pcg-controller-split-build/hw/CuperPcg.xclbin
Total elapsed time: 4h 31m 0s
build finished with exit code: 0
```

当前 2026-05-31 controller-split 实验 demo 尚未完成 demo-only 上板测试。旧
II=1 controller UUID `0170fa86-6e62-cfc9-aa66-2d330dd72cf2` 和 2026-05-29 旧
UUID `086a3345-ddf0-ffdd-b260-16ca5fa5223a` 的测试数据只作为历史记录保留，不能
套用到当前同名 `.xclbin`。

历史 2026-05-27 packed feed/AP demo 曾完成 `hw` bitstream 构建并覆盖旧 demo
槽位：

```bash
395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin
395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin.info
```

该历史 demo 文件已被 2026-05-29 demo 替换；旧 receive-path demo 和 2026-05-27
packed feed/AP demo 的测试结论只作为历史记录保留，不再对应当前这个 `.xclbin` 文件。

## 2026-05-31 controller-split 实验构建

软件级验证：

```bash
make cuper-tapa-pcg-fpga-host
make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=1 DIFF_TOL=1e-3
```

结果：通过。关键结果：

| 指标 | 数值 |
| --- | ---: |
| status | converged |
| iter | 1 |
| max_abs_diff | 1.086781531434e-08 |
| max_rel_diff | 9.286405581786e-09 |

XO / HLS 构建：

```bash
make _build-cuper-tapa-pcg TARGET=hw \
  BUILD_DIR=/home/pyx/project-x/Project-XPlus/cuper-tapa-pcg-controller-split-build
```

本轮在完整 `hw` 构建前已生成并 patch 通过 XO：

```text
cuper-tapa-pcg-controller-split-build/hw/CuperPcg.xo
XO SHA256: e8760fbaf97ed27372f6cfec6e1fb48bcc57221e80caced2f34043fb6b65ea89
```

HLS 关键变化：

| loop | achieved II / latency |
| --- | ---: |
| `iter_dot_p_ap_lanes` | II=5，latency 97 |
| `update_xr_compute_lanes` | II=1，latency 39 |
| `update_xr_store_lanes` | II=1，latency 22 |
| `update_p_compute_lanes` | II=1，latency 41 |
| `update_p_store_lanes` | II=1，latency 22 |
| `update_z_reduce` | II=5，latency 40000018 |

完整 bitstream 构建：

```bash
tmux new-session -d -s project-xplus-cuper-tapa-pcg-controller-split-hw ...
make _build-cuper-tapa-pcg TARGET=hw \
  BUILD_DIR=/home/pyx/project-x/Project-XPlus/cuper-tapa-pcg-controller-split-build
```

构建结果：成功。

关键输出：

```text
Run vpl: FINISHED. Run Status: impl Complete!
INFO: [v++ 60-586] Created /home/pyx/project-x/Project-XPlus/cuper-tapa-pcg-controller-split-build/hw/CuperPcg.xclbin
INFO: [v++ 60-791] Total elapsed time: 4h 31m 0s
build finished with exit code: 0
```

同步到 `395bitstream/` 的 demo 信息：

| 项目 | 数值 |
| --- | --- |
| demo xclbin | `395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin` |
| demo info | `395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin.info` |
| UUID | `1d536c39-f561-340b-7efc-ac2c8440543d` |
| SHA256 | `bc58605b36c98b29d84ce14939b95f8fc6b84bb7a505007fda95458545a349b8` |
| DATA clock | 211 MHz |
| KERNEL clock | 500 MHz |
| HBM clock | 450 MHz |

待测试：

- 该 UUID 尚未完成 demo-only 上板测试；
- 因 `dot_p_ap` 已合入 `iter_spmv_stream`，HTML 中需要把 raw `iter_spmv` 标成
  `iter recv + dot` 或等价新语义，不能直接沿用旧 `dot_p_ap` stage 口径；
- 当前仍不更新正式 `source.diff`，待 demo-only 上板测试确认性能后再决定。

## 2026-05-31 II=1 controller 实验构建

源码验证：

```bash
cmake --build DLC/Cuper/build --target cuper_host -j "$(nproc)"
```

结果：通过。只有 TAPA packed attribute 相关 warning。

XO 级 TAPA compile：

```bash
rm -rf /tmp/cuper_tapa_pcg_ii1_test
mkdir -p /tmp/cuper_tapa_pcg_ii1_test
cd DLC/Cuper
source scripts/env_u55c.sh
tapa -w /tmp/cuper_tapa_pcg_ii1_test/tapa_CuperPcg compile \
  -f kernels/Cuper.cpp -t CuperPcg \
  -p "$DEVICE" --clock-period 3.3 \
  -j "${JOBS:-$(nproc)}" --enable-synth-util \
  -c "-I$PWD/include" \
  -o /tmp/cuper_tapa_pcg_ii1_test/CuperPcg_ii1.xo
```

结果：通过。

HLS 实际 II：

| loop | target II | achieved II |
| --- | ---: | ---: |
| `init_r_lanes` | 1 | 1 |
| `init_zp_lanes` | 1 | 5 |
| `dot_p_ap_lanes` | 1 | 5 |
| `update_xr_lanes` | 1 | 18 |
| `update_z_reduce` | 1 | 5 |
| `update_p_lanes` | 1 | 24 |

说明：`II=1` pragma 不是全部实现为 II=1。`update_xr_lanes` 和 `update_p_lanes`
仍是主要调度压力点。XO 报告中 `Pcg_Controller` 有时序压力；最终完整 `hw`
实现仍通过 routed timing。

完整 bitstream 构建：

```bash
make CUPER_TAPA_FPGA_PCG_BUILD_DIR=/home/pyx/project-x/Project-XPlus/cuper-tapa-pcg-ii1-build \
  build-cuper-tapa-pcg-hw
```

tmux 会话：

```text
project-xplus-cuper-tapa-pcg-ii1
```

构建结果：成功。

关键输出：

```text
Run vpl: FINISHED. Run Status: impl Complete!
INFO: [v++ 60-586] Created /home/pyx/project-x/Project-XPlus/cuper-tapa-pcg-ii1-build/hw/CuperPcg.xclbin
INFO: [v++ 60-791] Total elapsed time: 4h 36m 24s
build finished with exit code: 0
```

同步到 `395bitstream/` 的 demo 信息：

| 项目 | 数值 |
| --- | --- |
| demo xclbin | `395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin` |
| demo info | `395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin.info` |
| UUID | `0170fa86-6e62-cfc9-aa66-2d330dd72cf2` |
| SHA256 | `ec3a98b09d662611ce50c4c484cb6b55ad2e7dbcd712a0b6d7833b38e4579fc8` |
| DATA clock | 223 MHz |
| KERNEL clock | 500 MHz |
| HBM clock | 444 MHz |

本轮上板测试见下一节。虽然 `1iter kernel_reported` 相比 2026-05-29 旧 UUID
有改善，但共同成功点仍慢于当前标准版和上一 demo，因此暂不更新正式
`source.diff`。

## 2026-05-31 II=1 controller demo-only 上板测试

日志目录：

```text
logs/codex_ii1_demo_test_20260531_011314/
```

测试对象：

```text
395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin
```

环境：

```text
git: 5b7a028 Archive TAPA demo bitstream metadata
XRT: 2.15.225
BDF: 0000:01:00.1
UUID: 0170fa86-6e62-cfc9-aa66-2d330dd72cf2
SHA256: ec3a98b09d662611ce50c4c484cb6b55ad2e7dbcd712a0b6d7833b38e4579fc8
DATA/KERNEL/HBM: 223/500/444 MHz
```

本轮没有重跑四个标准 bitstream；标准数据复用当前 HTML 和历史测试记录。

运行口径：

```bash
timeout 240s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/<dataset> \
  BITFILE=395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin \
  TAU=1e100 MAX_ITERS=1 DIFF_TOL=1e-1

timeout 240s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/<dataset> \
  BITFILE=395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin \
  MAX_ITERS=1 DIFF_TOL=1e-4
```

退出状态：

| 模式 | 数据集 | rc | direct ctrl | status | max_abs_diff | max_rel_diff |
| --- | --- | ---: | --- | --- | ---: | ---: |
| init | `thermal2_n16` | 0 | `0x4 -> 0xe` | converged | 0 | 0 |
| init | `thermal2_n65536` | 0 | `0x4 -> 0xe` | converged | 0 | 0 |
| init | `thermal2_n131072` | 0 | `0x4 -> 0xe` | converged | 0 | 0 |
| init | `thermal2_n262144` | 0 | `0x4 -> 0xe` | converged | 0 | 0 |
| init | `thermal2` | 0 | `0x4 -> 0xe` | converged | 0 | 0 |
| 1iter | `thermal2_n16` | 0 | `0x4 -> 0xe` | converged | 1.0868e-08 | 9.2864e-09 |
| 1iter | `thermal2_n65536` | 0 | `0x4 -> 0xe` | max_iter | 7.5004e-10 | 7.2993e-10 |
| 1iter | `thermal2_n131072` | 0 | `0x4 -> 0xe` | max_iter | 2.3337e-10 | 1.8330e-10 |
| 1iter | `thermal2_n262144` | 0 | `0x4 -> 0xe` | max_iter | 5.3981e-10 | 4.0347e-10 |
| 1iter | `thermal2` | 0 | `0x4 -> 0xe` | max_iter | 1.1717e-09 | 1.0914e-09 |

关键计时，单位 ms：

| 数据集 | init kernel | init ctrl | init SpMV | 1iter kernel | 1iter ctrl | iter SpMV | dot_p_ap | update_xr | update_p |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `thermal2_n16` | 13.9018 | 0.0063 | 0.0019 | 10.6769 | 0.0261 | 0.0008 | 0.0022 | 0.0085 | 0.0072 |
| `thermal2_n65536` | 28.7686 | 18.0269 | 6.6797 | 104.3695 | 92.5708 | 0.2044 | 9.4554 | 33.5769 | 29.8331 |
| `thermal2_n131072` | 46.6432 | 36.0601 | 13.3574 | 197.8439 | 185.1159 | 0.4080 | 18.9116 | 67.1272 | 59.6626 |
| `thermal2_n262144` | 83.4064 | 72.1119 | 26.7298 | 385.1288 | 370.2404 | 0.8173 | 37.8233 | 134.2822 | 119.3272 |
| `thermal2` | 353.0281 | 337.8235 | 125.2876 | 1767.8254 | 1734.2845 | 3.8549 | 177.1899 | 628.7404 | 559.0005 |

本轮结论：

- 当前 II=1 demo 能跑完整 `thermal2` 的 init-only 和 1iter，direct ctrl 均为
  `0x4 -> 0xe`，数值校验通过。
- 相比 2026-05-29 旧 UUID，`thermal2_n262144` 1iter 从 `426.3557 ms`
  降到 `385.1288 ms`，完整 `thermal2` 1iter 从 `1960.0357 ms`
  降到 `1767.8254 ms`。
- 相比当前 TAPA full-PCG 标准版共同成功点仍明显偏慢：
  `thermal2_n262144` 标准版为 `188.8202 ms`，当前 II=1 demo 为
  `385.1288 ms`。
- 大规模主开销仍在 PCG 非 SpMV 路径。完整 `thermal2` 1iter 中
  `update_xr=628.7404 ms`、`update_p=559.0005 ms`、`dot_p_ap=177.1899 ms`、
  `init_zp=212.6581 ms`，raw SpMV 本身为 `129.1594 ms`。
- 暂不建议晋级为标准版，也不更新正式 `source.diff`。

## 已跑命令

### host 编译

```bash
make cuper-tapa-pcg-fpga-host
```

结果：通过。只有第三方头文件和 HLS/TAPA 相关 warning。

### 软件仿真：generated n512

```bash
make run-cuper-pcg-tapa-fpga \
  DATASET=data/generated/cgsolver/n512 \
  MAX_ITERS=1 DIFF_TOL=1e-3
```

结果：

| 指标 | 数值 |
| --- | ---: |
| status | max_iter |
| iter | 1 |
| residual_abs | 9.699123342347e+00 |
| max_abs_diff | 1.845319074767e-07 |
| max_rel_diff | 3.783121170261e-05 |
| kernel_reported | 29.53334 ms |
| init_spmv work ticks | 13,400 |
| iter_spmv work ticks | 179 |

结论：数值通过，packed 输入和 packed AP 没有破坏基本 PCG 结果。

### 软件仿真：thermal2_n1024

```bash
make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 \
  MAX_ITERS=1 DIFF_TOL=1e-3
```

结果：

| 指标 | 数值 |
| --- | ---: |
| status | max_iter |
| iter | 1 |
| residual_abs | 9.826252042344e+00 |
| max_abs_diff | 9.278180446159e-10 |
| max_rel_diff | 7.931379237982e-10 |
| kernel_reported | 45.546318 ms |
| init_spmv work ticks | 14,777 |
| iter_spmv work ticks | 1,746 |

结论：`thermal2_n1024` 软件仿真通过，数值误差很小。

### XO/HLS 打包

```bash
make "$(pwd)/cuper-tapa-pcg-packed-ap-build/hw_emu/CuperPcg.xo" \
  TARGET=hw_emu \
  BUILD_DIR="$(pwd)/cuper-tapa-pcg-packed-ap-build" \
  JOBS=8
```

结果：通过。

产物：

```text
cuper-tapa-pcg-packed-ap-build/hw_emu/CuperPcg.xo
cuper-tapa-pcg-packed-ap-build/hw_emu/tapa_CuperPcg/report.json
```

TAPA patch 日志：

```text
patched .../CuperPcg.xo: initialized 64 FSM state regs, top defaults added 1, workdir_patched=True
```

## 硬件构建记录

启动命令：

```bash
make build-cuper-tapa-pcg TARGET=hw \
  CUPER_TAPA_FPGA_PCG_BUILD_DIR=/home/pyx/project-x/Project-XPlus/cuper-tapa-pcg-packed-ap-build \
  JOBS=8
```

tmux 包装命令已让 shell 在结束后保留，方便回看日志。

当前日志位置：

```text
logs/cuper_tapa_pcg_packed_ap_hw_20260527_191340.log
```

构建结果：成功。

关键输出：

```text
INFO: [v++ 60-586] Created /home/pyx/project-x/Project-XPlus/cuper-tapa-pcg-packed-ap-build/hw/CuperPcg.xclbin
INFO: [v++ 60-791] Total elapsed time: 4h 39m 35s
build finished with exit code: 0
```

同步到 `395bitstream/` 的 demo 信息：

| 项目 | 数值 |
| --- | --- |
| demo xclbin | `395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin` |
| demo info | `395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin.info` |
| UUID | `cc61e044-06f7-4726-8f18-773ac52ab1b2` |
| SHA256 | `add20fec0352d83c2b8cc8161d78d7f989124e11278ca553fe94a8cd231309bb` |
| DATA clock | 216 MHz |
| KERNEL clock | 500 MHz |
| HBM clock | 450 MHz |
| kernel signature | 新 ABI，含 `AP_spmv` / `X_spmv` / `P_spmv` |

2026-05-28 已按用户要求补跑 demo-only 上板测试；本轮不再补跑四个标准版。

## 板上 demo-only 测试

测试时间：2026-05-28 00:51 CST

日志目录：

```text
logs/codex_demo_only_test_20260528_005152/
```

测试对象：

```text
395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin
```

环境：

```text
git: a1dcd85 Set CuperPcg SpMV optimization goal
XRT: 2.15.225
BDF: 0000:01:00.1
UUID: cc61e044-06f7-4726-8f18-773ac52ab1b2
SHA256: add20fec0352d83c2b8cc8161d78d7f989124e11278ca553fe94a8cd231309bb
DATA/KERNEL/HBM: 216/500/450 MHz
```

说明：本轮开始时曾按文档误跑了部分标准版用例，日志在
`logs/codex_bitstream_test_20260528_004547/` 和
`logs/codex_bitstream_test_20260528_004939_isolated/`。按用户后续要求，这些
标准版日志不作为本轮结论，只保留为旁路记录。

### 运行命令口径

init-only：

```bash
timeout 180s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/<dataset> \
  BITFILE=395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin \
  TAU=1e100 MAX_ITERS=1 DIFF_TOL=1e-1
```

1iter：

```bash
timeout 180s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/<dataset> \
  BITFILE=395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin \
  MAX_ITERS=1 DIFF_TOL=1e-4
```

完整 `thermal2` 前执行了 `xbutil reset --device 0000:01:00.1 --force --batch`，
避免旧失败状态污染后续 case。

### 退出状态

| 模式 | 数据集 | rc | direct ctrl | status | max_abs_diff | max_rel_diff |
| --- | --- | ---: | --- | --- | ---: | ---: |
| init | `thermal2_n16` | 0 | `0x4 -> 0xe` | converged | 0 | 0 |
| init | `thermal2_n65536` | 0 | `0x4 -> 0xe` | converged | 0 | 0 |
| init | `thermal2_n131072` | 0 | `0x4 -> 0xe` | converged | 0 | 0 |
| init | `thermal2_n262144` | 0 | `0x4 -> 0xe` | converged | 0 | 0 |
| init | `thermal2` | 0 | `0x4 -> 0xe` | converged | 0 | 0 |
| 1iter | `thermal2_n16` | 0 | `0x4 -> 0xe` | converged | 1.0868e-08 | 9.2864e-09 |
| 1iter | `thermal2_n65536` | 0 | `0x4 -> 0xe` | max_iter | 7.5004e-10 | 7.2993e-10 |
| 1iter | `thermal2_n131072` | 0 | `0x4 -> 0xe` | max_iter | 2.3337e-10 | 1.8330e-10 |
| 1iter | `thermal2_n262144` | 0 | `0x4 -> 0xe` | max_iter | 5.3981e-10 | 4.0347e-10 |
| 1iter | `thermal2` | 0 | `0x4 -> 0xe` | max_iter | 1.1717e-09 | 1.0914e-09 |

### 关键计时

单位：ms。

| 数据集 | init kernel | init ctrl | init SpMV | 1iter kernel | 1iter ctrl | iter SpMV | dot_p_ap | update_xr | update_p |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `thermal2_n16` | 14.7369 | 0.0044 | 0.0020 | 11.1677 | 0.0192 | 0.0007 | 0.0017 | 0.0062 | 0.0051 |
| `thermal2_n65536` | 32.3798 | 15.1425 | 6.6416 | 112.3167 | 71.9071 | 0.1465 | 6.8470 | 27.2012 | 21.4884 |
| `thermal2_n131072` | 53.5494 | 30.2821 | 13.2807 | 213.5826 | 143.8117 | 0.2924 | 13.6951 | 54.4024 | 42.9742 |
| `thermal2_n262144` | 97.0455 | 60.5679 | 26.5658 | 416.6492 | 287.6244 | 0.5862 | 27.3892 | 108.8046 | 85.9482 |
| `thermal2` | 414.5353 | 283.7481 | 124.4632 | 1887.4481 | 1329.0066 | 2.7665 | 132.7367 | 486.8336 | 402.6350 |

### 本轮结论

- 当前 demo 能跑完整 `thermal2` 的 init-only 和 1iter，旧记录中的
  `ctrl=0x0` 边界在本轮 demo-only 测试中没有复现。
- 数值校验通过；1iter 的最大误差远低于 `DIFF_TOL=1e-4`。
- 性能不满足当前 full-PCG controller/dot/update 优化目标：
  `thermal2_n262144` 的 1iter `kernel_reported=416.6492 ms`，完整
  `thermal2` 为 `1887.4481 ms`，主要开销在 `update_xr`、`update_p` 和
  `dot_p_ap`，不是 `iter_spmv` 本身。
- 暂不建议按性能目标晋级为标准版；可以把它作为 full-size 功能边界修复候选记录。
- 后续 single SpMV demo 只作为回归基线；主要优化目标转向 full `CuperPcg(...)`
  的 `dot_p_ap`、`update_xr`、`update_p`、controller HBM 访问和 service
  drain/stop 开销。
- 更新 HTML 时，PCG 分段、`Init 与 1iter 差值` 和一次迭代区域必须展示当前
  full-PCG demo 数据；single SpMV 结果只进入 SpMV/demo-only 和 SpMV 对比区域。

## 2026-05-29 当前 full-PCG demo-only 上板测试

日志目录：

```text
logs/codex_two_demo_test_20260529_1300/
```

测试对象：

```text
395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin
kernel: CuperPcg
UUID: 086a3345-ddf0-ffdd-b260-16ca5fa5223a
SHA256: 83baded1910ecb2c9e662f9ff6920fd8a55dbd2898ae69629c862714e17cf7f1
DATA/KERNEL/HBM clock: 210 / 500 / 408 MHz
```

说明：上面的 `395bitstream/` 是本轮上板测试时的同步路径。2026-05-31 该同名
demo 槽已被 II=1 controller 实验构建覆盖；这里的结果只对应旧 UUID
`086a3345-ddf0-ffdd-b260-16ca5fa5223a`。

本轮按 demo-only 口径只跑当前 full-PCG demo，不重跑四个标准 bitstream。先跑
`thermal2_n16` 低规格 smoke；低规格通过后继续跑规定数据集。

init-only 代理：

```bash
timeout 180s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/<dataset> \
  BITFILE=395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin \
  TAU=1e100 MAX_ITERS=1 DIFF_TOL=1e-1
```

1iter：

```bash
timeout 180s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/<dataset> \
  BITFILE=395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin \
  MAX_ITERS=1 DIFF_TOL=1e-4
```

### 退出状态

| 模式 | 数据集 | rc | direct ctrl | status | max_abs_diff | max_rel_diff |
| --- | --- | ---: | --- | --- | ---: | ---: |
| init | `thermal2_n16` | 0 | `0x4 -> 0xe` | converged | 0 | 0 |
| init | `thermal2_n65536` | 0 | `0x4 -> 0xe` | converged | 0 | 0 |
| init | `thermal2_n131072` | 0 | `0x4 -> 0xe` | converged | 0 | 0 |
| init | `thermal2_n262144` | 0 | `0x4 -> 0xe` | converged | 0 | 0 |
| init | `thermal2` | 0 | `0x4 -> 0xe` | converged | 0 | 0 |
| 1iter | `thermal2_n16` | 0 | `0x4 -> 0xe` | converged | 1.0868e-08 | 9.2864e-09 |
| 1iter | `thermal2_n65536` | 0 | `0x4 -> 0xe` | max_iter | 7.5004e-10 | 7.2993e-10 |
| 1iter | `thermal2_n131072` | 0 | `0x4 -> 0xe` | max_iter | 2.3337e-10 | 1.8330e-10 |
| 1iter | `thermal2_n262144` | 0 | `0x4 -> 0xe` | max_iter | 5.3981e-10 | 4.0347e-10 |
| 1iter | `thermal2` | 0 | `0x4 -> 0xe` | max_iter | 1.1717e-09 | 1.0914e-09 |

### 关键计时

单位：ms。`AP path = iter_spmv + dot_p_ap`，用于观察第二次 `A*p` 接收/累加路径；
它不是完整一次迭代时间。

| 数据集 | init kernel | init ctrl | init SpMV | 1iter kernel | 1iter ctrl | iter recv | dot_p_ap | AP path | update_xr | update_p |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `thermal2_n16` | 10.7189 | 0.0049 | 0.0023 | 10.9542 | 0.0192 | 0.0007 | 0.0016 | 0.0022 | 0.0063 | 0.0053 |
| `thermal2_n65536` | 35.2486 | 15.2043 | 6.6708 | 114.0532 | 72.0756 | 0.1540 | 6.9254 | 7.0794 | 26.5686 | 22.1407 |
| `thermal2_n131072` | 53.8559 | 30.4084 | 13.3415 | 218.2640 | 144.1505 | 0.3075 | 13.8509 | 14.1584 | 53.1371 | 44.2820 |
| `thermal2_n262144` | 98.2320 | 60.8188 | 26.6850 | 426.7009 | 288.2930 | 0.6165 | 27.6965 | 28.3131 | 106.2738 | 88.5623 |
| `thermal2` | 421.3018 | 284.9298 | 125.0278 | 1959.1344 | 1350.5749 | 2.9066 | 129.7450 | 132.6516 | 497.8559 | 414.8799 |

### 本轮结论

- 当前 full-PCG demo 的低规格 `thermal2_n16` init/1iter 均通过，因此继续跑完规定
  demo-only sweep；
- 当前 demo 的 init-only 和 1iter 均能跑完整 `thermal2`，标准版旧记录中的完整
  规模 `ctrl=0x0` 失败边界没有复现；
- 数值校验通过，1iter 最大误差远低于 `DIFF_TOL=1e-4`；
- 性能仍不满足当前 SpMV 优化目标：`thermal2_n262144` 的 1iter
  `kernel_reported=426.7009 ms`，约为当前标准版 `188.8202 ms` 的 `2.26x`，
  也比上一 2026-05-27 full-PCG demo 的 `416.6492 ms` 略慢；
- 完整 `thermal2` 的 1iter 为 `1959.1344 ms`，比上一 demo 的
  `1887.4481 ms` 略慢，但两者都能返回完整规模；
- 本轮只更新 README/testing/HTML 测试记录，不更新正式 `source.diff`。
