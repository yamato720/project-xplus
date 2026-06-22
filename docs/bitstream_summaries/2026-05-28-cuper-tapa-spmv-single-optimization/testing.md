# 测试记录

## 当前状态

记录时间：2026-05-28，更新：2026-05-29

本目录是 single TAPA SpMV 与 full-PCG service/control 拆分边界的新目标记录。
历史上本轮先生成过一个 TAPA-PCG service SpMV 抽出版 demo bitstream；该 bitstream
在最小上板 smoke 中 timeout。当前源码已经切回 Cuper-compatible one-shot
`CuperPcgSpmv(...)`，并已在 2026-05-29 生成新的 one-shot demo bitstream。该
demo 文件已完成 demo-only 上板测试，single SpMV 可返回到完整 `thermal2`；测试后
已归档到 `bitstream_archive/2026-05-29-tapa-pcg-spmv-demo-candidates/`。

one-shot demo 测试时同步到：

```text
session: project-xplus-cuper-tapa-pcg-spmv-hw
log: logs/cuper_tapa_pcg_spmv_hw_parallel_20260528_222446.log
build_dir: cuper-tapa-spmv-u55c-20260528-demo-build/
xclbin: cuper-tapa-spmv-u55c-20260528-demo-build/hw/CuperPcgSpmv.xclbin
demo: 395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin
```

2026-05-29 归档后当前保存位置为：

```text
bitstream_archive/2026-05-29-tapa-pcg-spmv-demo-candidates/cuper-tapa-spmv-u55c-20260528-demo.xclbin
bitstream_archive/2026-05-29-tapa-pcg-spmv-demo-candidates/cuper-tapa-spmv-u55c-20260528-demo.xclbin.info
```

硬件构建结果：

```text
Run vpl: FINISHED. Run Status: impl Complete!
Created .../cuper-tapa-spmv-u55c-20260528-demo-build/hw/CuperPcgSpmv.xclbin
Total elapsed time: 7h 29m 0s
```

当前已归档 bitstream 信息：

```text
kernel: CuperPcgSpmv
UUID: c95c1dfc-20ca-9152-279e-bafdf35fdc3d
SHA256: 19d227179db7f22adfd12e78da119a99d102c59ebe25df686a652c6715ea95f2
DATA/KERNEL/HBM clock: 147 / 500 / 418 MHz
```

## 2026-06-18 compact16 SpMV-only demo

测试对象：

```text
395bitstream/cuper-tapa-spmv-u55c-20260618-compact16-demo.xclbin
kernel: CuperSpmvServiceOnly
UUID: 7f1e6302-e2a1-05e5-ab24-42a81b9f1488
SHA256: 2ec7758129ea44dfadd617b97587030de27a0f20d44b56f4bb727749768186b6
DATA/KERNEL/HBM clock: 200 / 500 / 448 MHz
HBM mapping: Matrix_data_0..15 -> HBM[0..15], SpElement_list_ptr -> HBM[16],
             X -> HBM[17], Y_out -> HBM[18], Status -> HBM[30], Metrics -> HBM[31]
Timing: HBM WNS -0.006 ns, TNS -0.007 ns, setup failing endpoints 2
```

构建：

```text
session: cuper_jacobi_iteration_hw_build
build_dir: cuper-jacobi-spmv-compact16-build/
log: cuper-jacobi-spmv-compact16-build/logs/build_hw_tmux.log
xclbin: cuper-jacobi-spmv-compact16-build/CuperSpmvServiceOnly.xclbin
```

关键构建结果：

```text
Run vpl: FINISHED. Run Status: impl Complete!
Created .../cuper-jacobi-spmv-compact16-build/CuperSpmvServiceOnly.xclbin
Total elapsed time: 3h 55m 24s
```

软件仿真命令口径：

```bash
JACOBI_TOP=CuperSpmvServiceOnly \
JACOBI_SPMV_ONLY=1 \
JACOBI_HBM_CHANNELS=16 \
JACOBI_SPMV_COMPACT_PE=1 \
  make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/<dataset>
```

本机 software simulation 已通过：

| 数据集 | 结果 | 原读 beats | compact 后 beats | 节省 |
| --- | --- | ---: | ---: | ---: |
| `thermal2_n1024` | `Correctness Verification: Passed`, `Error Num=0` | 2,624 | 2,208 | 15.85% |
| `thermal2_n65536` | `Correctness Verification: Passed`, `Error Num=0` | 68,464 | 60,824 | 11.16% |
| `thermal2` | `Correctness Verification: Passed`, `Error Num=0` | 1,373,424 | 1,187,402 | 13.54% |

服务器侧上板测试命令口径：

```bash
make cuper-jacobi-build-host \
  CUPER_JACOBI_BUILD_DIR=$PWD/cuper-jacobi-spmv-compact16-build \
  JACOBI_TOP=CuperSpmvServiceOnly \
  JACOBI_SPMV_ONLY=1 \
  JACOBI_HBM_CHANNELS=16 \
  JACOBI_SPMV_COMPACT_PE=1

timeout 240s env \
  XILINX_XRT=/opt/xilinx/xrt \
  BITFILE=$PWD/395bitstream/cuper-tapa-spmv-u55c-20260618-compact16-demo.xclbin \
  JACOBI_SPMV_ONLY=1 \
  DIFF_TOL=1e-1 \
  SPMV_REPEATS=1 \
  LD_LIBRARY_PATH=/home/pyx/.tapa/usr/lib:/opt/xilinx/xrt/lib:$LD_LIBRARY_PATH \
  ./cuper-jacobi-spmv-compact16-build/cuper_jacobi_host \
  $PWD/data/suitesparse/Schmid/csr/<dataset>
```

第一次直接运行时缺少 `XILINX_XRT`，host 在加载 XRT 前 abort：

```text
what():  XILINX_XRT not set
```

补上 `XILINX_XRT=/opt/xilinx/xrt` 后，`thermal2_n16` smoke 通过。完整 sweep 日志：

```text
logs/spmv_compact16_hw_sweep_20260618_174007/
```

上板结果：

| 数据集 | rc | spmv ms | GFLOP/s | original beats | compact beats | 节省 | 状态 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `thermal2_n16` | 0 | 0.088892 | 0.000360 | 176 | 16 | 90.91% | `Status=1`, `Error Num=0` |
| `thermal2_n1024` | 0 | 0.129026 | 0.0986 | 2,624 | 2,208 | 15.85% | `Status=1`, `Error Num=0` |
| `thermal2_n4096` | 0 | 0.193093 | 0.2714 | 4,704 | 4,266 | 9.31% | `Status=1`, `Error Num=0` |
| `thermal2_n16384` | 0 | 0.457307 | 0.4719 | 16,960 | 15,060 | 11.20% | `Status=1`, `Error Num=0` |
| `thermal2_n65536` | 0 | 1.639790 | 0.5330 | 68,464 | 60,824 | 11.16% | `Status=1`, `Error Num=0` |
| `thermal2_n131072` | 0 | 3.176400 | 0.5453 | 137,152 | 120,824 | 11.91% | `Status=1`, `Error Num=0` |
| `thermal2_n262144` | 0 | 6.322140 | 0.5533 | 280,848 | 243,145 | 13.42% | `Status=1`, `Error Num=0` |
| `thermal2` | 0 | 30.280400 | 0.5667 | 1,373,424 | 1,187,402 | 13.54% | `Status=1`, `Error Num=0` |

结论：

- 功能边界：compact16 上板可跑通完整 `thermal2`，校验全通过。
- 性能：不建议晋级。完整 `thermal2` 上 compact16 为 `30.2804 ms`，只有 strip16
  `1.29158 ms` 的 `0.043x`，也只有 one-shot `1.781541 ms` 的 `0.059x`。
- 原因判断：读 beat 节省没有转化为加速，当前 compact accumulator 的 512-bit
  slot 串行解码/分发和 lane tag 回填开销大概率压过了 HBM 节省；该协议也还没有消除
  PE 内部 `reorder_holes`。
- 本轮只更新 HTML 和 Markdown 测试结论，不写入正式 `source.diff`。

## 2026-06-18 reorder-free pack profile

compact16 的硬件后端已经证明动态 lane tag 写回代价过高，因此本轮继续用纯 C
`pack_profile` 评估下一版 SpMV v2 协议。如果未来直接去掉 reorder holes，关键是看
固定 lane accumulator 能否在读取量上接近真实 nonzero 下限。

构建命令：

```bash
make cuper-jacobi-pack-profile
```

完整 A 的 SpMV-only 口径使用 `DROP_DIAG=0`：

```bash
make cuper-jacobi-run-pack-profile \
  MATRIX=data/suitesparse/Schmid/csr/thermal2 \
  HBM=16 \
  DROP_DIAG=0 \
  TOP_BATCHES=3
```

Jacobi R 口径仍默认 drop diagonal：

```bash
make cuper-jacobi-run-pack-profile \
  MATRIX=data/suitesparse/Schmid/csr/thermal2 \
  HBM=16 \
  TOP_BATCHES=3
```

完整 A 口径关键结果：

| 数据集 | HBM | 当前密度 | strip密度 | compact-sched密度 | lane-static最终密度 | real-compact下限密度 | strip节省 | compact-sched节省 | lane-static节省 | real-compact节省 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `thermal2_n1024` | 16 | 30.31% | 32.98% | 36.02% | 90.06% | 99.28% | 8.12% | 15.85% | 66.35% | 69.47% |
| `thermal2_n1024` | 24 | 20.33% | 22.47% | 24.84% | 80.25% | 98.79% | 9.53% | 18.15% | 74.67% | 79.42% |
| `thermal2_n1024` | 32 | 15.34% | 17.01% | 19.08% | 86.35% | 98.67% | 9.80% | 19.60% | 82.23% | 84.45% |
| `thermal2_n65536` | 16 | 79.79% | 83.71% | 89.88% | 98.92% | 99.99% | 4.69% | 11.23% | 19.34% | 20.20% |
| `thermal2_n65536` | 24 | 69.20% | 75.39% | 84.50% | 98.82% | 99.98% | 8.21% | 18.10% | 29.97% | 30.79% |
| `thermal2_n65536` | 32 | 67.87% | 74.02% | 80.42% | 98.68% | 99.97% | 8.31% | 15.61% | 31.22% | 32.11% |
| `thermal2_n262144` | 16 | 77.83% | 82.54% | 89.99% | 99.43% | 100.00% | 5.70% | 13.51% | 21.72% | 22.16% |
| `thermal2_n262144` | 24 | 69.31% | 75.94% | 85.09% | 99.29% | 99.99% | 8.74% | 18.54% | 30.20% | 30.69% |
| `thermal2_n262144` | 32 | 65.06% | 71.90% | 81.26% | 99.26% | 100.00% | 9.51% | 19.93% | 34.45% | 34.94% |
| `thermal2` | 16 | 78.09% | 83.00% | 90.40% | 99.83% | 100.00% | 5.91% | 13.62% | 21.78% | 21.91% |
| `thermal2` | 24 | 70.55% | 77.04% | 85.95% | 99.79% | 100.00% | 8.43% | 17.92% | 29.30% | 29.45% |
| `thermal2` | 32 | 65.30% | 72.60% | 82.30% | 99.77% | 100.00% | 10.05% | 20.66% | 34.55% | 34.70% |

结论：

- `lane-static real/stream` 已经非常接近 `real compact512/stream` 理论下限；
- 后续硬件 v2 应优先保持固定 lane accumulator，避免 compact16 的动态 lane 写回；
- 真正需要重写的是 host packer、每 lane/每 HBM 的长度协议和 matrix loader/core
  的消费边界，而不是继续修 compact16 accumulator。

## 2026-06-18 lanereal16 SpMV-only demo

测试对象：

```text
395bitstream/cuper-tapa-spmv-u55c-20260618-lanereal16-demo.xclbin
kernel: CuperSpmvServiceOnly
UUID: 98358acf-f40e-4f2f-b77f-4a25c24f4473
SHA256: c8ef2426248a1acd4d02a75da39d72439c1cabdd12450428cfa83ce0baf1b49d
DATA/KERNEL/HBM clock: 197 / 500 / 450 MHz
HBM mapping: Matrix_data_0..15 -> HBM[0..15], SpElement_list_ptr -> HBM[16],
             X -> HBM[17], Y_out -> HBM[18], Status -> HBM[30], Metrics -> HBM[31]
Timing: WNS -0.073 ns, TNS -4.957 ns, setup failing endpoints 215
```

构建：

```text
session: cuper_jacobi_iteration_hw_build
build_dir: cuper-jacobi-spmv-lanereal16-build/
log: cuper-jacobi-spmv-lanereal16-build/logs/build_hw_tmux.log
xclbin: cuper-jacobi-spmv-lanereal16-build/CuperSpmvServiceOnly.xclbin
```

关键构建结果：

```text
Run vpl: FINISHED. Run Status: impl Complete!
Created .../cuper-jacobi-spmv-lanereal16-build/CuperSpmvServiceOnly.xclbin
Total elapsed time: 4h 13m 41s
```

软件仿真命令口径：

```bash
JACOBI_TOP=CuperSpmvServiceOnly \
JACOBI_SPMV_ONLY=1 \
JACOBI_HBM_CHANNELS=16 \
JACOBI_SPMV_LANE_STATIC_REAL=1 \
SPMV_REPEATS=1 \
  make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/<dataset>
```

本机 software simulation 已通过：

| 数据集 | 结果 | 原读 beats | lanereal 后 beats | 节省 |
| --- | --- | ---: | ---: | ---: |
| `thermal2_n1024` | `Correctness Verification: Passed`, `Error Num=0` | 2,624 | 883 | 66.35% |
| `thermal2_n65536` | `Correctness Verification: Passed`, `Error Num=0` | 68,464 | 57,472 | 16.0552% |

HLS 关键对比：

| 路径 | build | `Accumulator_Pipeline_cuper_acc_accumulate` II |
| --- | --- | ---: |
| strip16 | `cuper-jacobi-spmv-strip16-build/` | 2 |
| lanereal16 | `cuper-jacobi-spmv-lanereal16-build/` | 5 |

服务器侧命令口径：

```bash
make cuper-jacobi-build-host \
  CUPER_JACOBI_BUILD_DIR=$PWD/cuper-jacobi-spmv-lanereal16-build \
  JACOBI_TOP=CuperSpmvServiceOnly \
  JACOBI_SPMV_ONLY=1 \
  JACOBI_HBM_CHANNELS=16 \
  JACOBI_SPMV_LANE_STATIC_REAL=1

timeout 240s env \
  XILINX_XRT=/opt/xilinx/xrt \
  BITFILE=$PWD/395bitstream/cuper-tapa-spmv-u55c-20260618-lanereal16-demo.xclbin \
  JACOBI_SPMV_ONLY=1 \
  DIFF_TOL=1e-1 \
  SPMV_REPEATS=1 \
  LD_LIBRARY_PATH=/home/pyx/.tapa/usr/lib:/opt/xilinx/xrt/lib:$LD_LIBRARY_PATH \
  ./cuper-jacobi-spmv-lanereal16-build/cuper_jacobi_host \
  $PWD/data/suitesparse/Schmid/csr/<dataset>
```

服务器侧 smoke 日志：

```text
logs/spmv_lanereal16_hw_20260618_233409/
```

服务器侧完整 sweep 日志：

```text
logs/spmv_lanereal16_hw_sweep_20260618_233431/
```

上板结果：

| 数据集 | rc | spmv ms | GFLOP/s | original beats | lane-static beats | 节省 | 状态 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `thermal2_n16` | 0 | 0.073251 | 0.000437 | 176 | 16 | 90.91% | `Status=1`, `Error Num=0` |
| `thermal2_n1024` | 0 | 0.089773 | 0.1417 | 2,624 | 883 | 66.35% | `Status=1`, `Error Num=0` |
| `thermal2_n4096` | 0 | 0.087198 | 0.6010 | 4,704 | 3,455 | 26.55% | `Status=1`, `Error Num=0` |
| `thermal2_n16384` | 0 | 0.101947 | 2.1169 | 16,960 | 14,062 | 17.09% | `Status=1`, `Error Num=0` |
| `thermal2_n65536` | 0 | 0.186370 | 4.6896 | 68,464 | 57,472 | 16.06% | `Status=1`, `Error Num=0` |
| `thermal2_n131072` | 0 | 0.319455 | 5.4221 | 137,152 | 114,874 | 16.24% | `Status=1`, `Error Num=0` |
| `thermal2_n262144` | 0 | 0.546613 | 6.3993 | 280,848 | 234,721 | 16.42% | `Status=1`, `Error Num=0` |
| `thermal2` | 0 | 2.355660 | 7.2848 | 1,373,424 | 1,151,370 | 16.17% | `Status=1`, `Error Num=0` |

结论：

- 功能边界：lanereal16 上板可跑通完整 `thermal2`，8 个点校验全通过。
- 报告口径：HTML 主报告中用 lanereal16 直接替代前一条异常慢的 compact16 曲线；
  compact16 只作为历史退步记录保留。
- 性能：不建议晋级。完整 `thermal2` 上 lanereal16 为 `2.35566 ms`，慢于 strip16
  `1.29158 ms`，速度比约 `0.55x`；也慢于 one-shot demo `1.781541 ms`。
- 原因判断：读包节省在大规模约 `16%`，但 accumulator II 从 strip16 的 `2` 退到
  `5`，后端吞吐抵消了前端读量收益。
- 本轮只更新 HTML 和 Markdown 测试结论，不写入正式 `source.diff`。

## 2026-06-20 RTL owner-bank OOO accumulator SpMV-only demo

测试对象：

```text
395bitstream/cuper-tapa-spmv-u55c-20260620-ooobank16-demo.xclbin
kernel: CuperSpmvServiceOnly
UUID: 58158740-a7ef-a803-0da5-1fd8b3206253
SHA256: 5e3f2e863cba4efca519ce18f9e9e735f05084af1242cd493e079c1844f777b3
DATA/KERNEL/HBM clock: 150 / 500 / 445 MHz
HBM mapping: Matrix_data_0..15 -> HBM[0..15], SpElement_list_ptr -> HBM[16],
             X -> HBM[17], Y_out -> HBM[18], Status -> HBM[30], Metrics -> HBM[31]
Timing: WNS -0.024 ns, TNS -0.086 ns, setup failing endpoints 8
```

构建：

```text
session: cuper_spmv_ooobank16_lat12_hw_build
build_dir: cuper-tapa-spmv-ooobank16-lat12-hw-150m-20260621-build/
log: cuper-tapa-spmv-ooobank16-lat12-hw-150m-20260621-build/logs/build_hw_tmux.log
xclbin: cuper-tapa-spmv-ooobank16-lat12-hw-150m-20260621-build/CuperSpmvServiceOnly.xclbin
```

关键构建结果：

```text
Run vpl: FINISHED. Run Status: impl Complete!
Created .../cuper-tapa-spmv-ooobank16-lat12-hw-150m-20260621-build/CuperSpmvServiceOnly.xclbin
Total elapsed time: 3h 59m 3s
```

资源摘要：

| 资源 | 使用量 | 占比 |
| --- | ---: | ---: |
| CLB LUTs | 336,729 | 25.83% |
| CLB | 83,386 | 51.17% |
| CLB Registers | 523,457 | 20.08% |
| Block RAM Tile | 1,320 | 65.48% |
| URAM | 512 | 53.33% |
| DSPs | 695 | 7.70% |
| Total SLLs | 24,473 | - |

本机 RTL / Vivado xsim 仿真：

```bash
make -C verilog tapa-bank-lint BUILD_DIR=build_bank_fp_fix
make -C verilog tapa-bank-sim BUILD_DIR=build_bank_fp_fix
make -C verilog tapa-lint BUILD_DIR=build_lane_fp_fix
make -C verilog tapa-sim BUILD_DIR=build_lane_fp_fix

make -C verilog tapa-backend-dataset-xsim \
  BUILD_DIR=build_backend_xsim_thermal2_n65536_lat12 \
  CSR_MATRIX=../data/suitesparse/Schmid/csr/thermal2_n65536
```

结果：

```text
PASS: TAPA owner-bank RTL accumulator cycles=69 real_events=40 outputs=16 cycles_per_event=1.725 cycles_per_output=4.312
PASS: TAPA owner-lane RTL accumulator cycles=71
PASS: backend dataset ROWS=4096 cycles=6735 consumed=9386 produced=2048 writes=4096 reads=2048
PASS: backend dataset ROWS=16384 cycles=27222 consumed=51656 produced=8192 writes=16384 reads=8192
PASS: backend dataset ROWS=65536 cycles=109582 consumed=291319 produced=32768 writes=65536 reads=32768
```

`thermal2_n16` 和 `thermal2_n1024` 此前也已通过同一路径。`thermal2_n131072` 当前不是
RTL 计算失败，而是 testbench 写死 `MAX_ROWS=65536`：

```text
Fatal: FAIL: ROWS=131072 exceeds MAX_ROWS=65536
```

本机 TAPA software smoke：

```bash
JACOBI_TOP=CuperSpmvServiceOnly \
JACOBI_SPMV_ONLY=1 \
JACOBI_HBM_CHANNELS=16 \
JACOBI_SPMV_OOO_ACCUMULATE_RTL=1 \
  make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n16

JACOBI_TOP=CuperSpmvServiceOnly \
JACOBI_SPMV_ONLY=1 \
JACOBI_HBM_CHANNELS=16 \
JACOBI_SPMV_OOO_ACCUMULATE_RTL=1 \
  make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024
```

结果：

```text
Correctness Verification: Passed
Error Num=0
```

旧版上板失败记录：

```text
old UUID: 22b0a282-c282-cfaf-e45a-f8bebf4cc644
old SHA256: a5ab4ba8a601bb12c3b737e318da28c29a3e4bdd2c037a9e670ac31a5a9f51b4
thermal2_n16: Finish 正常返回，Status=1，时间 0.104123 ms，但 16 个输出全是 0，Error Num=16/16
thermal2_n1024: 240s timeout，停在 [tapa-invoke] after ReadFromDevice before Finish
```

本次 `5815...` 同步版包含两层修复：

- bank wrapper 统计 `expected_outputs` / `output_count`，不再早于 tagged output 完全吐出就 `ap_done`；
- lane wrapper 显式接收 `Owner_id` 和 `Pair_lane`，不再从首 token 推断 tag。
- lane accumulator 的 floating_point pipeline 对齐修为 12 cycle，匹配 Vivado/xsim
  实测 IP 行为，避免把加法结果写错 ping/pong bank。

服务器侧建议测试口径：

```bash
make cuper-jacobi-build-host \
  CUPER_JACOBI_BUILD_DIR=$PWD/cuper-tapa-spmv-ooobank16-lat12-hw-150m-20260621-build \
  JACOBI_TOP=CuperSpmvServiceOnly \
  JACOBI_SPMV_ONLY=1 \
  JACOBI_HBM_CHANNELS=16 \
  JACOBI_SPMV_LANE_STATIC_REAL=1 \
  JACOBI_SPMV_OOO_ACCUMULATE=1 \
  JACOBI_SPMV_OOO_ACCUMULATE_RTL=1

timeout 240s env \
  XILINX_XRT=/opt/xilinx/xrt \
  BITFILE=$PWD/395bitstream/cuper-tapa-spmv-u55c-20260620-ooobank16-demo.xclbin \
  JACOBI_SPMV_ONLY=1 \
  DIFF_TOL=1e-1 \
  SPMV_REPEATS=1 \
  LD_LIBRARY_PATH=/home/pyx/.tapa/usr/lib:/opt/xilinx/xrt/lib:$LD_LIBRARY_PATH \
  ./cuper-tapa-spmv-ooobank16-lat12-hw-150m-20260621-build/cuper_jacobi_host \
  $PWD/data/suitesparse/Schmid/csr/<dataset>
```

当前结论：

- 这版已经完成 xclbin 生成并覆盖同步到 `395bitstream/` 同名 SpMV demo 槽；
- 当前修复版存在轻微 routed setup violation，WNS `-0.024 ns`；
- 服务器上板 sweep 尚未执行，当前不进入 HTML 性能曲线；
- 未确认性能提升前，不更新正式 `source.diff`。

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
- 当时没有启动新硬件构建，也没有生成新的 one-shot demo xclbin；后续已在
  2026-05-29 完成 one-shot demo 硬件构建并覆盖 `395bitstream/` single-SpMV
  demo 槽；
- 测试时的 `395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin` 是
  UUID `c95c1dfc-20ca-9152-279e-bafdf35fdc3d` 的 one-shot demo，不再是历史
  service 抽出版；测试完成后该 demo 已归档。

## 2026-05-29 one-shot demo-only 上板测试

日志目录：

```text
logs/codex_two_demo_test_20260529_1300/
```

测试对象：

```text
395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin
kernel: CuperPcgSpmv
UUID: c95c1dfc-20ca-9152-279e-bafdf35fdc3d
SHA256: 19d227179db7f22adfd12e78da119a99d102c59ebe25df686a652c6715ea95f2
DATA/KERNEL/HBM clock: 147 / 500 / 418 MHz
```

说明：上面的 `395bitstream/` 是本轮上板测试时的同步路径。测试完成后，该 demo
已归档到
`bitstream_archive/2026-05-29-tapa-pcg-spmv-demo-candidates/`。

本轮按 demo-only 口径只跑当前 `Cuper-compatible one-shot` single SpMV demo，
不重跑四个标准 bitstream。本轮未跑 PCG，无 init/1iter 过程；PCG 相关数据保留
在 full-PCG demo 记录中。

运行命令：

```bash
timeout 180s make run-cuper-tapa-pcg-spmv TARGET=hw \
  DATASET=data/suitesparse/Schmid/csr/<dataset> \
  BITFILE=395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin \
  SPMV_REPEATS=3 DIFF_TOL=1e-1
```

### 退出状态与计时

| 数据集 | rc | spmv_avg ms | GFLOP/s | max_abs_diff | max_rel_diff | 状态 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `thermal2_n16` | 0 | 0.068825 | 0.000465 | 3.7558e-07 | 7.6333e-08 | ok |
| `thermal2_n65536` | 0 | 0.149121 | 5.8610 | 2.3596e-06 | 8.2651e-04 | ok |
| `thermal2_n131072` | 0 | 0.235847 | 7.3443 | 3.2888e-06 | 1.3183e-03 | ok |
| `thermal2_n262144` | 0 | 0.425394 | 8.2229 | 3.2376e-06 | 3.1482e-03 | ok |
| `thermal2` | 0 | 1.781541 | 9.6325 | 1.6333e-06 | 4.4911e-05 | ok |

### 和 standalone TAPA Cuper SpMV 标准记录对比

标准基线复用 `395bitstream/cuper-tapa-spmv-u55c-20260522.xclbin` 的既有
HTML/Markdown 记录，本轮没有重跑标准版。

| 数据集 | TAPA 标准 spmv_avg ms | 本 demo spmv_avg ms | 本 demo / 标准 | 备注 |
| --- | ---: | ---: | ---: | --- |
| `thermal2_n16` | 0.0670 | 0.068825 | 1.03x | 共同成功点，demo 略慢 |
| `thermal2_n65536` | 0.1379 | 0.149121 | 1.08x | 共同成功点，demo 略慢 |
| `thermal2_n131072` | 0.2197 | 0.235847 | 1.07x | 共同成功点，demo 略慢 |
| `thermal2_n262144` | timeout | 0.425394 | - | 标准旧记录 180s timeout，本 demo 返回 |
| `thermal2` | timeout | 1.781541 | - | 标准旧记录 180s timeout，本 demo 返回 |

结论：

- 低规格 `thermal2_n16` 已通过，因此按纪律继续跑完了规定 demo-only sweep；
- 当前 one-shot demo 的成功边界从标准旧记录的 `thermal2_n131072` 扩到完整
  `thermal2`，这是功能边界改善；
- 共同成功点上，本 demo 比 standalone TAPA Cuper SpMV 标准略慢约 2.7% 到 8.1%，
  不能声明为单点性能提升；
- 数值校验全部通过，`max_rel_diff` 均低于 `DIFF_TOL=1e-1`；
- 本轮只更新 README/testing/HTML 测试记录，不更新正式 `source.diff`。

## 2026-06-22 strip16 window 300 MHz no-progress artifacts

测试对象：

```text
395bitstream/cuper-tapa-spmv-u55c-20260622-strip16-window14-300m-noprogress-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260622-strip16-window16-300m-noprogress-demo.xclbin
kernel: CuperSpmvServiceOnly
macro: JACOBI_SPMV_STRIP_PADDING=1, JACOBI_SPMV_ACC_WINDOW={14|16}
```

构建结果：

| 版本 | UUID | SHA256 | requested DATA | achieved DATA | KERNEL | HBM | WNS | TNS | setup failing endpoints |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| window14 300m no-progress | `f13edc28-7dd1-d7aa-ab66-e51c52cf31b7` | `10c7c11c2b461b8c4b376f6eb3cceeee754722ab883d32d9b6cb89b6289733d5` | 300 MHz | 202.5 MHz | 500 MHz | 450 MHz | -1.604 ns | -31426.979 ns | 74375 |
| window16 300m no-progress | `525491e1-725b-4c9c-df09-cf555a26ba37` | `5de4d204bf28528693d13c69b5873f19877120e69d511c2ab0b771a4152298f1` | 300 MHz | 192.1 MHz | 500 MHz | 447 MHz | -1.870 ns | -32768.406 ns | 69785 |

构建目录和日志：

```text
cuper-jacobi-spmv-strip16-window14-300m-noprogress-build/
cuper-jacobi-spmv-strip16-window14-300m-noprogress-build/logs/build_hw_tmux.log
cuper-jacobi-spmv-strip16-window16-300m-noprogress-build/
cuper-jacobi-spmv-strip16-window16-300m-noprogress-build/logs/build_hw_tmux.log
```

关键结论：

- 两版均 `impl Complete` 并生成 xclbin；
- routed timing 明确未满足 300 MHz 目标，Vitis 对 DATA clock 做了 auto-frequency scaling；
- 这轮还没有服务器侧上板功能/性能数据；
- 当前仅同步到 `395bitstream/` 供服务器侧风险测试，不建议晋级标准；
- 本轮不更新正式 `source.diff`。

## 2026-06-22 RTL owner-bank heartbeat-clean artifact

测试对象：

```text
395bitstream/cuper-tapa-spmv-u55c-20260622-ooobank16-heartbeat-clean-demo.xclbin
kernel: CuperSpmvServiceOnly
macro: JACOBI_SPMV_LANE_STATIC_REAL=1,
       JACOBI_SPMV_OOO_ACCUMULATE=1,
       JACOBI_SPMV_OOO_ACCUMULATE_RTL=1
```

构建结果：

| 版本 | UUID | SHA256 | DATA | KERNEL | HBM | WNS | TNS | setup failing endpoints |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| ooobank16 heartbeat-clean | `a87434fe-0abf-57ff-a272-98407dfdf44d` | `cbb2caab1406c79cf01826639168fe0f08611ceae261d4f078bfc08a27a875f2` | 150 MHz | 500 MHz | 450 MHz | 0.003 ns | 0.000 ns | 0 |

构建目录和日志：

```text
cuper-tapa-spmv-ooobank16-heartbeat-clean-hw-150m-20260622-build/
cuper-tapa-spmv-ooobank16-heartbeat-clean-hw-150m-20260622-build/logs/build_hw_tmux.log
```

关键构建输出：

```text
Run vpl: FINISHED. Run Status: impl Complete!
Created .../cuper-tapa-spmv-ooobank16-heartbeat-clean-hw-150m-20260622-build/CuperSpmvServiceOnly.xclbin
Total elapsed time: 4h 30m 55s
```

当前状态：

- xclbin 和 `.info` 已同步到 `395bitstream/`；
- routed timing clean，POST-VPL `0 errors`；
- 服务器侧上板 sweep 尚未执行；
- 本轮只是同步待测 artifact，不更新正式 `source.diff`。
