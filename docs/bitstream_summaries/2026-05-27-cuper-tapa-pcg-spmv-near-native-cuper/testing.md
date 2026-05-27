# 测试记录

## 当前状态

记录时间：2026-05-27

本版已完成 `hw` bitstream 构建，并已覆盖 `395bitstream/` 当前 demo 槽位：

```bash
395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin
395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin.info
```

当前 demo 文件会被后续新 demo 优先覆盖；旧 receive-path demo 的测试结论只作为
历史记录保留，不再对应当前这个 `.xclbin` 文件。

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
- 性能不满足当前“内嵌 SpMV 接近 standalone TAPA Cuper”的目标：
  `thermal2_n262144` 的 1iter `kernel_reported=416.6492 ms`，完整
  `thermal2` 为 `1887.4481 ms`，主要开销在 `update_xr`、`update_p` 和
  `dot_p_ap`，不是 `iter_spmv` 本身。
- 暂不建议按性能目标晋级为标准版；可以把它作为 full-size 功能边界修复候选记录。
- 下一步测试目标改为 PCG 抽出版 `cuper-tapa-spmv` 单 SpMV demo：只跑该 demo 的
  single SpMV 数据集，和满血 TAPA Cuper SpMV 标准记录静态对比 `spmv_avg`、
  timeout 边界和 diff；确认有效后再回填 full-PCG。
- 下一次更新 HTML 时，single SpMV 结果只进入 SpMV/demo-only 区域；PCG 分段、
  `Init 与 1iter 差值` 和一次迭代区域保留本轮 full-PCG 数据，并标注“本轮未跑
  PCG，无 init/1iter 过程/无一次迭代新数据”。
