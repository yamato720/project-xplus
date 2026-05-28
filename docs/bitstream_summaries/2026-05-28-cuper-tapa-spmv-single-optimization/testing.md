# 测试记录

## 当前状态

记录时间：2026-05-28

本目录是 single TAPA SpMV 优化的新目标记录。本轮按用户要求，先不做优化，
先生成一个当前 TAPA-PCG service SpMV 抽出版 demo bitstream。

当前构建已完成并已同步到 demo 槽：

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

本轮 PCG service SpMV 抽出版使用新的 host 入口：

```bash
make run-cuper-tapa-pcg-spmv TARGET=hw \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  BITFILE=395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin \
  SPMV_REPEATS=3 DIFF_TOL=1e-1
```

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
