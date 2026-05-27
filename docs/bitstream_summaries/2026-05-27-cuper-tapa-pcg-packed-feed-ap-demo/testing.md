# 测试记录

## 当前状态

记录时间：2026-05-27

本版还处于源码/XO 候选阶段，完整 `hw` bitstream 正在 tmux 中构建：

```bash
tmux attach -t project-xplus-cuper-tapa-pcg-packed-ap-hw
tail -f logs/cuper_tapa_pcg_packed_ap_hw_20260527_191340.log
```

截至写入本文件时，构建已进入 `vpl`，日志显示正在进行 block-level synthesis。

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

下一次更新本文件时，需要补充：

- 是否生成 `CuperPcg.xclbin`；
- `.xclbin.info` 中 UUID 和 DATA/HBM clock；
- `sha256sum`；
- 板上 demo vs 标准版动态对比表；
- 是否建议晋级。

## 板上测试计划

bitstream 成功后先作为 demo 跑：

```text
395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin
```

对照标准版：

```text
395bitstream/cuper-tapa-pcg-fpga-u55c-20260525.xclbin
```

最小动态对比：

| 数据集 | 模式 |
| --- | --- |
| `thermal2_n16` | init-only / 1iter |
| `thermal2_n65536` | init-only / 1iter |
| `thermal2_n131072` | init-only / 1iter |
| `thermal2_n262144` | init-only / 1iter |
| `thermal2` | init-only / 1iter |

重点记录：

- `init_spmv`
- `iter_spmv`
- `controller_total`
- `kernel_reported`
- direct-register `ctrl` 状态
- timeout 或 `ctrl=0x0` 边界是否变化
