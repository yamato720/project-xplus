# 测试记录

## 当前状态

记录时间：2026-05-28

本目录是 single TAPA SpMV 与 full-PCG service/control 拆分边界的新目标记录。
历史上本轮先生成过一个 TAPA-PCG service SpMV 抽出版 demo bitstream；该 bitstream
在最小上板 smoke 中 timeout。当前源码已经切回 Cuper-compatible one-shot
`CuperPcgSpmv(...)`，并已在 2026-05-29 生成新的 one-shot demo bitstream。当前
demo 文件尚未上板测试。

当前 one-shot demo 已同步到：

```text
session: project-xplus-cuper-tapa-pcg-spmv-hw
log: logs/cuper_tapa_pcg_spmv_hw_parallel_20260528_222446.log
build_dir: cuper-tapa-spmv-u55c-20260528-demo-build/
xclbin: cuper-tapa-spmv-u55c-20260528-demo-build/hw/CuperPcgSpmv.xclbin
demo: 395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin
```

硬件构建结果：

```text
Run vpl: FINISHED. Run Status: impl Complete!
Created .../cuper-tapa-spmv-u55c-20260528-demo-build/hw/CuperPcgSpmv.xclbin
Total elapsed time: 7h 29m 0s
```

当前 bitstream 信息：

```text
kernel: CuperPcgSpmv
UUID: c95c1dfc-20ca-9152-279e-bafdf35fdc3d
SHA256: 19d227179db7f22adfd12e78da119a99d102c59ebe25df686a652c6715ea95f2
DATA/KERNEL/HBM clock: 147 / 500 / 418 MHz
```

说明：旧 2026-05-28 service 抽出版的 timeout 结论只对应旧 UUID
`08f1f2dc-8c44-007f-a0a5-4dce1236ddd9`，不再对应当前同名 demo 文件。

历史 service 抽出版曾完成构建并同步到 demo 槽；该文件已经被当前 one-shot demo
覆盖，以下只作为旧 UUID 的历史记录：

```text
session: project-xplus-cuper-tapa-pcg-spmv-hw
log: logs/cuper_tapa_pcg_spmv_hw_20260528_023906.log
build_dir: cuper-tapa-spmv-u55c-20260528-demo-build/
xclbin: cuper-tapa-spmv-u55c-20260528-demo-build/hw/CuperPcgSpmv.xclbin
demo: 395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin
```

硬件构建结果：

```text
Run vpl: FINISHED. Run Status: impl Complete!
Created .../cuper-tapa-spmv-u55c-20260528-demo-build/hw/CuperPcgSpmv.xclbin
Total elapsed time: 3h 55m 36s
```

bitstream 信息：

```text
kernel: CuperPcgSpmv
UUID: 08f1f2dc-8c44-007f-a0a5-4dce1236ddd9
SHA256: 0be3ed806febc39ad488ed833c063390978bb2911d4fa298c2056ef2e5ce6356
DATA/KERNEL/HBM clock: 222 / 500 / 450 MHz
```

说明：路由 timing summary 中仍有平台级 `Timing constraints are not met` 段落，
但 Vitis/VPL 返回 `impl Complete` 并生成了 xclbin。上板测试时要把是否能稳定
加载、是否 timeout 和 CPU diff 一起记录。

## 2026-05-28 XO 阶段结果

TAPA/HLS 已成功生成 XO：

```text
cuper-tapa-spmv-u55c-20260528-demo-build/hw/CuperPcgSpmv.xo
```

日志关键行：

```text
generated the v++ xo file at .../CuperPcgSpmv.xo
```

随后 Makefile 失败在 `patch_tapa_xo_control_fsm.py`，原因是脚本原先硬编码查找：

```text
ip_repo/tapa_xrtl_CuperPcg_1_0/src/CuperPcg_fsm.v
```

而新 top 的实际路径是：

```text
ip_repo/tapa_xrtl_CuperPcgSpmv_1_0/src/CuperPcgSpmv_fsm.v
```

已修复脚本，使其自动从 XO 中查找 `ip_repo/tapa_xrtl_*/src/*_fsm.v`。现有 XO
已手动补丁成功：

```text
initialized 64 FSM state regs, top defaults added 1, workdir_patched=True
```

并执行：

```bash
make -q cuper-tapa-spmv-u55c-20260528-demo-build/hw/CuperPcgSpmv.xo \
  TARGET=hw \
  BUILD_DIR=/home/pyx/project-x/Project-XPlus/cuper-tapa-spmv-u55c-20260528-demo-build
```

返回码为 `0`，说明当前 XO target 已是 up-to-date。随后复用该 XO 进入 Vitis
link/implementation，并完成 xclbin 生成。

启动前已完成：

```bash
make -n build-cuper-tapa-pcg-spmv-hw
make cuper-tapa-pcg-host
git diff --check
```

结果：Makefile dry-run 指向新 build 目录；host 编译通过，仅有既有 HLS/TAPA
头文件警告；`git diff --check` 通过。

## 标准基线

当前标准版：

```text
395bitstream/cuper-tapa-spmv-u55c-20260522.xclbin
```

既有记录显示：

| 数据集 | 预期状态 |
| --- | --- |
| `thermal2_n16` | 返回 |
| `thermal2_n1024` | 返回 |
| `thermal2_n4096` | 返回 |
| `thermal2_n16384` | 返回 |
| `thermal2_n65536` | 返回 |
| `thermal2_n131072` | 返回 |
| `thermal2_n262144` | 旧记录中 180s timeout |
| `thermal2` | 旧记录中 180s timeout |

## demo 测试口径

single TAPA SpMV demo 使用 `cuper-tapa-spmv` 主线命名：

```text
395bitstream/cuper-tapa-spmv-u55c-YYYYMMDD-demo.xclbin
```

测试时只跑 single SpMV，不跑 PCG：

```bash
make cuper-tapa-pcg-host
timeout 180s make run-cuper-tapa-spmv TARGET=hw \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n131072 \
  BITFILE=395bitstream/cuper-tapa-spmv-u55c-YYYYMMDD-demo.xclbin \
  SPMV_REPEATS=3 DIFF_TOL=1e-1
```

历史 PCG service SpMV 抽出版和当前 one-shot `CuperPcgSpmv(...)` demo 都使用下面的
host 入口，区别在于 xclbin 内部 task graph 已经从 service 控制壳切回
Cuper-compatible one-shot 图：

```bash
make run-cuper-tapa-pcg-spmv TARGET=hw \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  BITFILE=395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin \
  SPMV_REPEATS=3 DIFF_TOL=1e-1
```

说明：满血 `Cuper(...)` 标准 bitstream 是基准；当前 `CuperPcgSpmv(...)` 只保留
历史 kernel 名和 host/demo 入口，内部走 Cuper-compatible one-shot 图。single
SpMV demo 的结果不能自动证明 full `CuperPcg(...)` 会同步提升；PCG 优化必须另跑
full `CuperPcg(...)` 软件仿真或上板 smoke。

完整 sweep 建议复用 `docs/codex/testing.md` 的数据集列表：

```text
thermal2_n16
thermal2_n1024
thermal2_n4096
thermal2_n16384
thermal2_n65536
thermal2_n131072
thermal2_n262144
thermal2
```

## 必须记录

- bitstream 路径、UUID、SHA256、DATA/HBM clock；
- 每个数据集的退出码、timeout、是否返回；
- `spmv_avg`、GFLOP/s、CPU diff；
- 和当前标准 bitstream / 既有 HTML 记录的差异；
- 是否扩大成功边界，是否引入数值错误；
- 是否建议作为 demo 保留或晋级。

## 2026-05-28 demo-only 上板 smoke

日志目录：

```text
logs/codex_spmv_demo_only_test_20260528_143556/
```

测试对象：

```text
395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin
kernel: CuperPcgSpmv
UUID: 08f1f2dc-8c44-007f-a0a5-4dce1236ddd9
SHA256: 0be3ed806febc39ad488ed833c063390978bb2911d4fa298c2056ef2e5ce6356
DATA/KERNEL/HBM clock: 222 / 500 / 450 MHz
```

本轮按 demo-only 口径只跑当前 `PCG SpMV 抽出版` single SpMV demo，不重跑
四个标准 bitstream。最低 smoke 命令：

```bash
timeout 180s make run-cuper-tapa-pcg-spmv TARGET=hw \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  BITFILE=395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin \
  SPMV_REPEATS=3 DIFF_TOL=1e-1
```

结果：

| 数据集 | 尝试 | 退出码 | 结果 | 日志 |
| --- | --- | --- | --- | --- |
| `thermal2_n16` | 第一次 | `124` | 180s timeout，停在 `after ReadFromDevice before Finish` | `tapa_pcg_spmv_demo_thermal2_n16.log` |
| `thermal2_n16` | retry | `124` | 180s timeout，停在 `after ReadFromDevice before Finish` | `tapa_pcg_spmv_demo_thermal2_n16_retry.log` |

第一次 timeout 后曾尝试直接运行 `xbutil reset`，但当前 shell PATH 中没有
`xbutil`，该次 reset 返回 `127`。第二次 timeout 后使用绝对路径执行：

```bash
/opt/xilinx/xrt/bin/xbutil reset --device 0000:01:00.1 --force --batch
```

结果为：

```text
Successfully reset Device[0000:01:00.1]
```

结论：该 demo 在最小 `thermal2_n16` 上两次未完成，没有 `spmv_avg`、GFLOP/s
或 CPU diff；因此停止 sweep，不跑 `thermal2_n65536`、`thermal2_n131072`、
`thermal2_n262144` 和完整 `thermal2`。本轮没有性能提升，正式 `source.diff`
不更新。

## 2026-05-28 finite-exit 修复版验证

修复意图：把 `CuperPcgSpmv` 单 SpMV demo 尾端从 stop-driven
checker/sort 改成固定输出数量自然返回，避免上一版在 `ReadFromDevice`
之后卡在 `Finish`。

源码检查：

```bash
git diff --check
```

结果：通过。

软件仿真：

```bash
timeout 180s make run-cuper-tapa-pcg-spmv \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  SPMV_REPEATS=1 DIFF_TOL=1e-1
```

关键输出：

```text
[xplus] dataset="data/suitesparse/Schmid/csr/thermal2_n16" mode=cuper-spmv-tapa spmv=tapa-cuper-pcg-service bitstream=<software-sim>
[done] mode=spmv_only spmv_calls=1 status=ok
[check] max_abs_diff=3.755767679081e-07 max_rel_diff=7.633263769275e-08 diff_tol=1.000000000000e-01
[timing-ms] plan=5.105500000000e-02 spmv_total=2.654200300000e+01 spmv_calls=1 spmv_avg=2.654200300000e+01 gflops=1.205636213665e-06
```

硬件构建：

```bash
make cuper-tapa-pcg-spmv-hw-tmux
```

构建状态：

```text
session: project-xplus-cuper-tapa-pcg-spmv-hw
log: logs/cuper_tapa_pcg_spmv_hw_20260528_161221.log
build_dir: cuper-tapa-spmv-u55c-20260528-demo-build/
xclbin: cuper-tapa-spmv-u55c-20260528-demo-build/hw/CuperPcgSpmv.xclbin
```

已过安全检查点：

```text
generated the v++ xo file at .../CuperPcgSpmv.xo
patched .../CuperPcgSpmv.xo: initialized 64 FSM state regs, top defaults added 1, workdir_patched=True
Run run_link: Step vpl: Started
```

待完成：

- 等 VPL/implementation 生成新的 `CuperPcgSpmv.xclbin`；
- 覆盖同步到 `395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin`
  和 `.info`；
- demo-only 跑 `thermal2_n16`，确认上一版 `Finish` timeout 是否解除；
- 若 `thermal2_n16` 返回并 diff 通过，再继续跑
  `thermal2_n65536`、`thermal2_n131072`、`thermal2_n262144` 和完整
  `thermal2`。

说明：当前只完成软件仿真和构建前半段，尚未得到板上性能或边界收益。因此正式
`source.diff` 继续不更新。

## 2026-05-28 service Iteration_num 清理后软件验证

改动意图：`CuperPcgSpmv` 单 SpMV demo 和 full `CuperPcg` 共享同一个
`pcg_spmv_service.hpp`。本轮把 service 内部重复次数去掉，统一为：

```text
一条 CuperSpmvCommand == 一次 SpMV
```

`CuperPcgSpmv(...)` 的 ABI 仍保留 `Iteration_num` 参数以兼容 host/脚本，但
内部忽略该参数。standalone `Cuper(...)` 的 `Iteration_num` benchmark 语义不变。

数据准备：

```bash
make download-suitesparse-data DATASETS="thermal2_n4096 thermal2_n16384 thermal2_n65536 thermal2_n131072 thermal2_n262144 thermal2"
```

结果：补齐了 HTML 报告使用的 `thermal2_n4096`、`thermal2_n16384`、
`thermal2_n65536`、`thermal2_n131072`、`thermal2_n262144` 和完整
`thermal2`。本轮软件仿真只挑 `thermal2_n16`、`thermal2_n1024`、
`thermal2_n4096` 三个点；更大的点留给后续上板测试。

构建与静态检查：

```bash
git diff --check
make cuper-tapa-pcg-host
make cuper-tapa-pcg-fpga-host
```

结果：均通过。host 构建只有既有 Xilinx/TAPA 头文件和项目旧代码警告。

### `CuperPcgSpmv` service single SpMV 软件仿真

```bash
timeout 180s make run-cuper-tapa-pcg-spmv \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  SPMV_REPEATS=1 DIFF_TOL=1e-1
```

```text
[done] mode=spmv_only spmv_calls=1 status=ok
[check] max_abs_diff=3.755767679081e-07 max_rel_diff=7.633263769275e-08 diff_tol=1.000000000000e-01
[timing-ms] plan=1.314650000000e-01 spmv_total=2.246773400000e+01 spmv_calls=1 spmv_avg=2.246773400000e+01 gflops=1.424264681076e-06
```

```bash
timeout 240s make run-cuper-tapa-pcg-spmv \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 \
  SPMV_REPEATS=1 DIFF_TOL=1e-1
```

```text
[done] mode=spmv_only spmv_calls=1 status=ok
[check] max_abs_diff=1.350584250659e-06 max_rel_diff=2.196535197242e-06 diff_tol=1.000000000000e-01
[timing-ms] plan=1.151553000000e+00 spmv_total=7.583226200000e+02 spmv_calls=1 spmv_avg=7.583226200000e+02 gflops=1.677913814571e-05
```

```bash
timeout 300s make run-cuper-tapa-pcg-spmv \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n4096 \
  SPMV_REPEATS=1 DIFF_TOL=1e-1
```

```text
[done] mode=spmv_only spmv_calls=1 status=ok
[check] max_abs_diff=2.336642056733e-06 max_rel_diff=1.017541631777e-04 diff_tol=1.000000000000e-01
[timing-ms] plan=2.468834000000e+00 spmv_total=1.242856405000e+03 spmv_calls=1 spmv_avg=1.242856405000e+03 gflops=4.216738135569e-05
```

### full `CuperPcg` FPGA-PCG 软件仿真

```bash
timeout 180s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=1 DIFF_TOL=1e-1
```

```text
[done] iter=1 residual_abs=9.757821925835e-08 status=converged
[check] cpu_residual_abs=0.000000000000e+00 cuper_tapa_pcg_residual_abs=9.757821925835e-08 max_abs_diff=1.086781531434e-08 max_rel_diff=9.286405581786e-09 diff_tol=1.000000000000e-01 rr=6.162139191957e-14
```

```bash
timeout 240s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 \
  MAX_ITERS=1 DIFF_TOL=1e-1
```

```text
[done] iter=1 residual_abs=9.826252042344e+00 status=max_iter
[check] cpu_residual_abs=9.826252034486e+00 cuper_tapa_pcg_residual_abs=9.826252042344e+00 max_abs_diff=9.278180446159e-10 max_rel_diff=7.931379237982e-10 diff_tol=1.000000000000e-01 rr=9.655522643280e+01
```

```bash
timeout 300s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n4096 \
  MAX_ITERS=1 DIFF_TOL=1e-1
```

```text
[done] iter=1 residual_abs=2.118937172915e+01 status=max_iter
[check] cpu_residual_abs=2.118937179818e+01 cuper_tapa_pcg_residual_abs=2.118937172915e+01 max_abs_diff=4.093525740601e-09 max_rel_diff=3.223721052065e-09 diff_tol=1.000000000000e-01 rr=4.489894744335e+02
```

说明：`n1024` 和 `n4096` 使用 `MAX_ITERS=1`，所以 `status=max_iter` 是预期
结果；这里验证的是 full `CuperPcg` 软件模型和 CPU 同口径 1 次迭代是否一致。

结论：

- service single SpMV 软件仿真在三个点均返回且 diff 通过；
- full `CuperPcg` 软件仿真在三个点均返回，和 CPU reference 对齐；
- 本轮没有启动新的硬件构建，也没有生成新 xclbin；
- 这只是 service 协议清理和软件正确性验证，不是性能提升记录，正式
  `source.diff` 继续不更新。

## 2026-05-28 command helper 统一后软件复测

改动意图：把 full `Pcg_Controller` 和 single `Pcg_SingleSpmv_Controller` 中
重复的 SpMV command/stop 广播逻辑收敛到 `pcg_common.hpp`，避免两条 service
路径后续漂移。

静态检查和 host 构建：

```bash
git diff --check
make cuper-tapa-pcg-host
make cuper-tapa-pcg-fpga-host
```

结果：均通过。host 构建只有既有 Xilinx/TAPA 头文件和旧代码 warning。

### `CuperPcgSpmv` service single SpMV

```bash
timeout 180s make run-cuper-tapa-pcg-spmv \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  SPMV_REPEATS=1 DIFF_TOL=1e-1
```

```text
[done] mode=spmv_only spmv_calls=1 status=ok
[check] max_abs_diff=3.755767679081e-07 max_rel_diff=7.633263769275e-08 diff_tol=1.000000000000e-01
[timing-ms] plan=5.362270000000e-01 spmv_total=5.902071300000e+01 spmv_calls=1 spmv_avg=5.902071300000e+01 gflops=5.421825385268e-07
```

```bash
timeout 240s make run-cuper-tapa-pcg-spmv \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 \
  SPMV_REPEATS=1 DIFF_TOL=1e-1
```

```text
[done] mode=spmv_only spmv_calls=1 status=ok
[check] max_abs_diff=1.350584250659e-06 max_rel_diff=2.196535197242e-06 diff_tol=1.000000000000e-01
[timing-ms] plan=1.604695000000e+00 spmv_total=7.579837140000e+02 spmv_calls=1 spmv_avg=7.579837140000e+02 gflops=1.678664035254e-05
```

### full `CuperPcg` FPGA-PCG software sim

```bash
timeout 180s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=1 DIFF_TOL=1e-1
```

```text
[done] iter=1 residual_abs=9.757821925835e-08 status=converged
[check] cpu_residual_abs=0.000000000000e+00 cuper_tapa_pcg_residual_abs=9.757821925835e-08 max_abs_diff=1.086781531434e-08 max_rel_diff=9.286405581786e-09 diff_tol=1.000000000000e-01 rr=6.162139191957e-14
```

```bash
timeout 240s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 \
  MAX_ITERS=1 DIFF_TOL=1e-1
```

```text
[done] iter=1 residual_abs=9.826252042344e+00 status=max_iter
[check] cpu_residual_abs=9.826252034486e+00 cuper_tapa_pcg_residual_abs=9.826252042344e+00 max_abs_diff=9.278180446159e-10 max_rel_diff=7.931379237982e-10 diff_tol=1.000000000000e-01 rr=9.655522643280e+01
```

结论：公共 command helper 没有破坏 service single SpMV 或 full `CuperPcg`
软件路径。`n1024` full-PCG 使用 `MAX_ITERS=1`，因此 `status=max_iter` 是预期。
本轮没有生成新 xclbin，也不更新正式 `source.diff`。

## 2026-05-28 共享包数 helper 后软件验证

本轮把 service SpMV 的包数/padding 公式收敛到 `pcg_common.hpp`，属于源码边界
整理。它应该保持行为不变，但影响 single demo 和 full `CuperPcg` 的共同计数口径。

静态检查和 host 构建：

```bash
git diff --check
make cuper-tapa-pcg-host
make cuper-tapa-pcg-fpga-host
```

结果：均通过。host 构建只有既有 Xilinx/TAPA 头文件和旧代码 warning。

### `CuperPcgSpmv` service single SpMV

```bash
timeout 180s make run-cuper-tapa-pcg-spmv \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  SPMV_REPEATS=1 DIFF_TOL=1e-1
```

```text
[done] mode=spmv_only spmv_calls=1 status=ok
[check] max_abs_diff=3.755767679081e-07 max_rel_diff=7.633263769275e-08 diff_tol=1.000000000000e-01
```

```bash
timeout 240s make run-cuper-tapa-pcg-spmv \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 \
  SPMV_REPEATS=1 DIFF_TOL=1e-1
```

```text
[done] mode=spmv_only spmv_calls=1 status=ok
[check] max_abs_diff=1.350584250659e-06 max_rel_diff=2.196535197242e-06 diff_tol=1.000000000000e-01
```

### full `CuperPcg` FPGA-PCG software sim

```bash
timeout 180s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=1 DIFF_TOL=1e-1
```

```text
[done] iter=1 residual_abs=9.757821925835e-08 status=converged
[check] cpu_residual_abs=0.000000000000e+00 cuper_tapa_pcg_residual_abs=9.757821925835e-08 max_abs_diff=1.086781531434e-08 max_rel_diff=9.286405581786e-09 diff_tol=1.000000000000e-01 rr=6.162139191957e-14
```

```bash
timeout 240s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 \
  MAX_ITERS=1 DIFF_TOL=1e-1
```

```text
[done] iter=1 residual_abs=9.826252042344e+00 status=max_iter
[check] cpu_residual_abs=9.826252034486e+00 cuper_tapa_pcg_residual_abs=9.826252042344e+00 max_abs_diff=9.278180446159e-10 max_rel_diff=7.931379237982e-10 diff_tol=1.000000000000e-01 rr=9.655522643280e+01
```

结论：共享包数 helper 保持 single service SpMV 和 full `CuperPcg` 软件行为正确。
`n1024` full-PCG 使用 `MAX_ITERS=1`，因此 `status=max_iter` 是预期。未上板前
仍不更新正式 `source.diff`。

## 2026-05-28 vector/checker/sort helper 共享后软件验证

改动意图：继续把 `pcg_spmv_service.hpp` 中真正影响 SpMV 数据通路的公共逻辑从
single demo 和 full-PCG wrapper 里抽出来，包括 packed vector 读取、checker
padding 转发和 sort-tree 打包。

中间失败点：

```bash
timeout 180s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=1 DIFF_TOL=1e-1
```

最初把 full `Pcg_Vector_Checker` 直接改成整轮 helper 后，这条命令 180s timeout：

```text
make: *** [Makefile:639: run-cuper-pcg-tapa-fpga] Terminated
```

判断：full-PCG checker 是常驻服务，不能只在两轮之间检查 stop。它可能在
controller 发 stop 前抢先进下一轮，然后等待不存在的新一轮 accumulator 输出。
随后把共享边界改成“单步转发 helper”，full checker 在等待输入时继续检查
`Stop_in`，single checker 仍按固定输出数量自然返回。

静态检查和强制 host 重编：

```bash
git diff --check
make -B cuper-tapa-pcg-host
make -B cuper-tapa-pcg-fpga-host
```

结果：均通过。host 构建只有既有 Xilinx/TAPA 头文件和旧代码 warning。

### `CuperPcgSpmv` service single SpMV

```bash
timeout 180s make run-cuper-tapa-pcg-spmv \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  SPMV_REPEATS=1 DIFF_TOL=1e-1
```

```text
[done] mode=spmv_only spmv_calls=1 status=ok
[check] max_abs_diff=3.755767679081e-07 max_rel_diff=7.633263769275e-08 diff_tol=1.000000000000e-01
[timing-ms] plan=4.051040000000e-01 spmv_total=3.806724300000e+01 spmv_calls=1 spmv_avg=3.806724300000e+01 gflops=8.406177458136e-07
```

```bash
timeout 240s make run-cuper-tapa-pcg-spmv \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 \
  SPMV_REPEATS=1 DIFF_TOL=1e-1
```

```text
[done] mode=spmv_only spmv_calls=1 status=ok
[check] max_abs_diff=1.350584250659e-06 max_rel_diff=2.196535197242e-06 diff_tol=1.000000000000e-01
[timing-ms] plan=7.498190000000e-01 spmv_total=7.886953400000e+01 spmv_calls=1 spmv_avg=7.886953400000e+01 gflops=1.613297220699e-04
```

### full `CuperPcg` FPGA-PCG software sim

```bash
timeout 180s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=1 DIFF_TOL=1e-1
```

```text
[done] iter=1 residual_abs=9.757821925835e-08 status=converged
[check] cpu_residual_abs=0.000000000000e+00 cuper_tapa_pcg_residual_abs=9.757821925835e-08 max_abs_diff=1.086781531434e-08 max_rel_diff=9.286405581786e-09 diff_tol=1.000000000000e-01 rr=6.162139191957e-14
```

```bash
timeout 240s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 \
  MAX_ITERS=1 DIFF_TOL=1e-1
```

```text
[done] iter=1 residual_abs=9.826252042344e+00 status=max_iter
[check] cpu_residual_abs=9.826252034486e+00 cuper_tapa_pcg_residual_abs=9.826252042344e+00 max_abs_diff=9.278180446159e-10 max_rel_diff=7.931379237982e-10 diff_tol=1.000000000000e-01 rr=9.655522643280e+01
```

结论：

- single service SpMV 的 n16/n1024 软件仿真仍返回且 diff 通过；
- full `CuperPcg` 的 n16/n1024 软件仿真也返回，修复了中间版本的 stop 等待超时；
- `n1024` full-PCG 使用 `MAX_ITERS=1`，因此 `status=max_iter` 是预期；
- 本轮没有生成新 xclbin，也不更新正式 `source.diff`。

## 2026-05-28 single SpMV 去控制壳后软件验证

改动意图：当前 `CuperPcgSpmv(...)` 不再使用 PCG service single-SpMV 控制壳，而是
保留历史 kernel 名和 host flag，内部改回 Cuper 风格 one-shot task graph。

源码残留检查：

```bash
rg -n "Pcg_Single|pcg_checker_forward_round|Writer_Done|tapa-cuper-pcg-service" \
  DLC/Cuper host cfg Makefile -S
```

结果：源码实现路径中没有 `Pcg_Single*`、`Writer_Done` 或旧
`tapa-cuper-pcg-service` 标签残留。`Vector_Destroy_Stop_Stream` 仍存在于 full
`CuperPcg(...)`，这是常驻 service 正常退出路径。

静态检查和强制 host 重编：

```bash
git diff --check
make -B cuper-tapa-pcg-host
make -B cuper-tapa-pcg-fpga-host
```

结果：均通过。host 构建只有既有 Xilinx/TAPA 头文件和旧代码 warning。

### `CuperPcgSpmv` Cuper-compatible one-shot

```bash
timeout 180s make run-cuper-tapa-pcg-spmv \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  SPMV_REPEATS=1 DIFF_TOL=1e-1
```

```text
[xplus] dataset="data/suitesparse/Schmid/csr/thermal2_n16" mode=cuper-spmv-tapa spmv=tapa-cuper-compat-demo bitstream=<software-sim>
[done] mode=spmv_only spmv_calls=1 status=ok
[check] max_abs_diff=3.755767679081e-07 max_rel_diff=7.633263769275e-08 diff_tol=1.000000000000e-01
[timing-ms] plan=1.413830000000e-01 spmv_total=4.523495000000e+01 spmv_calls=1 spmv_avg=4.523495000000e+01 gflops=7.074176051924e-07
```

```bash
timeout 240s make run-cuper-tapa-pcg-spmv \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 \
  SPMV_REPEATS=1 DIFF_TOL=1e-1
```

```text
[xplus] dataset="data/suitesparse/Schmid/csr/thermal2_n1024" mode=cuper-spmv-tapa spmv=tapa-cuper-compat-demo bitstream=<software-sim>
[done] mode=spmv_only spmv_calls=1 status=ok
[check] max_abs_diff=1.350584250659e-06 max_rel_diff=2.196535197242e-06 diff_tol=1.000000000000e-01
[timing-ms] plan=7.733940000000e-01 spmv_total=6.289381100000e+01 spmv_calls=1 spmv_avg=6.289381100000e+01 gflops=2.023092542444e-04
```

### full `CuperPcg` FPGA-PCG software sim

```bash
timeout 180s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  MAX_ITERS=1 DIFF_TOL=1e-1
```

```text
[done] iter=1 residual_abs=9.757821925835e-08 status=converged
[check] cpu_residual_abs=0.000000000000e+00 cuper_tapa_pcg_residual_abs=9.757821925835e-08 max_abs_diff=1.086781531434e-08 max_rel_diff=9.286405581786e-09 diff_tol=1.000000000000e-01 rr=6.162139191957e-14
```

备注：这条软件仿真仍出现 TAPA leftover warning：
`Vector_Y_Stream[13] destructed with leftovers`。当前结果返回且 diff 通过，但它是
后续 full-PCG service drain 需要继续观察的信号。

```bash
timeout 240s make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 \
  MAX_ITERS=1 DIFF_TOL=1e-1
```

```text
[done] iter=1 residual_abs=9.826252042344e+00 status=max_iter
[check] cpu_residual_abs=9.826252034486e+00 cuper_tapa_pcg_residual_abs=9.826252042344e+00 max_abs_diff=9.278180446159e-10 max_rel_diff=7.931379237982e-10 diff_tol=1.000000000000e-01 rr=9.655522643280e+01
```

结论：

- 当前 `CuperPcgSpmv` one-shot single SpMV 的 n16/n1024 软件仿真返回且 diff 通过；
- full `CuperPcg` n16/n1024 软件仿真也返回，说明去掉 single SpMV 控制壳没有破坏
  full-PCG 软件路径；
- 当前没有启动新硬件构建，也没有生成新的 one-shot demo xclbin；
- `395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin` 仍是历史 service 抽出版
  bitstream，不代表当前源码。
