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

## 2026-06-23 RTL issue scoreboard debug artifact

测试对象：

```text
395bitstream/cuper-tapa-spmv-u55c-20260623-scoreboard16-demo.xclbin
kernel: CuperSpmvServiceOnly
macro: JACOBI_SPMV_LANE_STATIC_REAL=1,
       JACOBI_SPMV_OOO_ACCUMULATE=1,
       JACOBI_SPMV_OOO_SCOREBOARD_RTL=1,
       JACOBI_SPMV_SCOREBOARD_DEBUG=1
```

构建前软件验证：

```bash
JACOBI_TOP=CuperSpmvServiceOnly \
JACOBI_SPMV_ONLY=1 \
JACOBI_SPMV_OOO_SCOREBOARD_RTL=1 \
JACOBI_SPMV_SCOREBOARD_DEBUG=1 \
make -C DLC/Cuper-jacobi-iteration build-host

JACOBI_TOP=CuperSpmvServiceOnly \
JACOBI_SPMV_ONLY=1 \
JACOBI_SPMV_OOO_SCOREBOARD_RTL=1 \
JACOBI_SPMV_SCOREBOARD_DEBUG=1 \
make -C DLC/Cuper-jacobi-iteration run-sw \
  MATRIX=/home/pyx/project-x/Project-XPlus/data/suitesparse/Schmid/csr/thermal2_n1024
```

软件验证结果：

```text
thermal2_n16: Error Num=0
thermal2_n1024: Error Num=0
thermal2_n1024 debug counts: core=6362, issue=6362, acc=6362
```

构建命令口径：

```bash
export BUILD_DIR=/home/pyx/project-x/Project-XPlus/cuper-tapa-spmv-scoreboard-debug-build
export JACOBI_TOP=CuperSpmvServiceOnly
export JACOBI_SPMV_ONLY=1
export JACOBI_SPMV_OOO_SCOREBOARD_RTL=1
export JACOBI_SPMV_SCOREBOARD_DEBUG=1
export JACOBI_TAPA_ENABLE_SYNTH_UTIL=0
export CLOCK_PERIOD=4.0
export JACOBI_KERNEL_FREQUENCY=150
./scripts/build_host.sh
./scripts/build_xo_u55c.sh
./scripts/link_xclbin_u55c.sh
```

构建目录和日志：

```text
cuper-tapa-spmv-scoreboard-debug-build/
cuper-tapa-spmv-scoreboard-debug-build/logs/build_hw_tmux.log
```

关键构建输出：

```text
TAPA post-synthesis resource reports: disabled
generated the v++ xo file at .../CuperSpmvServiceOnly.xo
Run vpl: FINISHED. Run Status: impl Complete!
Created .../cuper-tapa-spmv-scoreboard-debug-build/CuperSpmvServiceOnly.xclbin
Total elapsed time: 4h 26m 42s
tmux run finished with exit code: 0
```

构建结果：

| 版本 | UUID | SHA256 | DATA | KERNEL | HBM | WNS | TNS | setup failing endpoints |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| scoreboard16 debug | `8ab0b8d1-2889-e7b2-38c0-4026db80679e` | `bbe249cfc1d974e8111584d1ef76fc64b5b084247e4cffef7453d1524cc9d8fd` | 39 MHz | 500 MHz | 450 MHz | -18.950 ns | -32059.350 ns | 28858 |

当前状态：

- xclbin 和 `.info` 已同步到 `395bitstream/`；
- VPL `impl Complete`，POST-VPL `0 errors`，但 timing constraints not met；
- Vitis 自动把 DATA clock 降到 `39 MHz`，所以该版不用于性能判断；
- 服务器侧上板 sweep 尚未执行；
- 本轮只是同步功能/监测边界 artifact，不更新正式 `source.diff`。

## 2026-06-27 8-HBM SpMV-only artifacts

测试对象：

```text
395bitstream/cuper-tapa-spmv-u55c-20260627-original8-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260627-strip8-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-demo.xclbin
kernel: CuperSpmvServiceOnly
```

软件验证状态：

```text
original8: JACOBI_HBM_CHANNELS=8, Error Num=0
strip8: JACOBI_HBM_CHANNELS=8, JACOBI_SPMV_STRIP_PADDING=1, Error Num=0
lanereal8-scoreboard: JACOBI_HBM_CHANNELS=8, JACOBI_SPMV_LANE_STATIC_REAL=1,
                      JACOBI_SPMV_OOO_ACCUMULATE=1,
                      JACOBI_SPMV_OOO_SCOREBOARD_RTL=1, Error Num=0
```

构建命令口径：

```bash
export JACOBI_TOP=CuperSpmvServiceOnly
export JACOBI_SPMV_ONLY=1
export JACOBI_HBM_CHANNELS=8
export CLOCK_PERIOD=4.0
export JACOBI_KERNEL_FREQUENCY=150

# original8
export BUILD_DIR=$PWD/cuper-jacobi-spmv-original8-hw-150m-build
./scripts/build_host.sh
./scripts/build_xo_u55c.sh
./scripts/link_xclbin_u55c.sh

# strip8
export BUILD_DIR=$PWD/cuper-jacobi-spmv-strip8-hw-150m-build
export JACOBI_SPMV_STRIP_PADDING=1
./scripts/build_host.sh
./scripts/build_xo_u55c.sh
./scripts/link_xclbin_u55c.sh

# lanereal8-scoreboard
export BUILD_DIR=$PWD/cuper-jacobi-spmv-lanereal8-scoreboard-hw-150m-build
export JACOBI_SPMV_STRIP_PADDING=0
export JACOBI_SPMV_LANE_STATIC_REAL=1
export JACOBI_SPMV_OOO_ACCUMULATE=1
export JACOBI_SPMV_OOO_SCOREBOARD_RTL=1
./scripts/build_host.sh
./scripts/build_xo_u55c.sh
./scripts/link_xclbin_u55c.sh
```

三轮按 tmux 延迟队列启动，间隔约 2.5 小时。tmux 结束状态均为：

```text
tmux run finished with exit code: 0
```

构建目录和日志：

```text
cuper-jacobi-spmv-original8-hw-150m-build/
cuper-jacobi-spmv-original8-hw-150m-build/logs/build_hw_tmux.log
cuper-jacobi-spmv-strip8-hw-150m-build/
cuper-jacobi-spmv-strip8-hw-150m-build/logs/build_hw_tmux.log
cuper-jacobi-spmv-lanereal8-scoreboard-hw-150m-build/
cuper-jacobi-spmv-lanereal8-scoreboard-hw-150m-build/logs/build_hw_tmux.log
```

关键构建输出：

```text
original8:
  Created .../cuper-jacobi-spmv-original8-hw-150m-build/CuperSpmvServiceOnly.xclbin
  Total elapsed time: 2h 18m 21s
  Completed: 2026-06-26 22:43:39 CST

strip8:
  Created .../cuper-jacobi-spmv-strip8-hw-150m-build/CuperSpmvServiceOnly.xclbin
  Total elapsed time: 2h 35m 56s
  Completed: 2026-06-27 01:23:21 CST

lanereal8-scoreboard:
  Replaced generated scoreboard wrapper with custom RTL
  Copied scoreboard primitive
  Created .../cuper-jacobi-spmv-lanereal8-scoreboard-hw-150m-build/CuperSpmvServiceOnly.xclbin
  Total elapsed time: 2h 52m 37s
  Completed: 2026-06-27 04:08:28 CST
```

构建结果：

| 版本 | UUID | SHA256 | DATA | KERNEL | HBM | WNS | TNS | setup failing endpoints |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| original8 | `c4fbfa69-5f20-f3d8-3e7f-c514119625e6` | `a32eeece95bf9664cb9afc4dbea20cdb46b47bfb58b5bb9f6f01e726e648c90f` | 150 MHz | 500 MHz | 450 MHz | 0.003 ns | 0.000 ns | 0 |
| strip8 | `14f04872-7324-2970-12bd-135e3e8eb55b` | `d615702025dc041cd61cd858d2219660230e66f346fd16719e4a34758385aad3` | 150 MHz | 500 MHz | 450 MHz | 0.003 ns | 0.000 ns | 0 |
| lanereal8-scoreboard | `9f6a0133-2169-15f9-d6ec-752ecd8eaca0` | `0268f72b8438b84a55448bd02eb9a485010e745a55fcbf65234dfd43ca473453` | 150 MHz | 500 MHz | 450 MHz | 0.003 ns | 0.000 ns | 0 |

当前状态：

- 三份 xclbin 和 `.info` 已同步到 `395bitstream/`；
- 三版均 VPL `impl Complete`，POST-VPL `0 errors`，routed timing clean；
- 服务器侧上板 sweep 已执行到 scoreboard 失败边界；
- 本轮没有性能提升候选，scoreboard 分支还有 timeout，仍不更新正式 `source.diff`。

服务器侧上板结果：

| 数据集 | original8 FPGA ms | strip8 FPGA ms | lanereal8-scoreboard |
| --- | ---: | ---: | --- |
| `thermal2_n16` | 0.074269 | 0.079358 | 0.080481 PASS |
| `thermal2_n1024` | 0.077615 | 0.083637 | timeout 300s |
| `thermal2_n4096` | 0.089127 | 0.087664 | 未继续 |
| `thermal2_n16384` | 0.109745 | 0.124493 | 未继续 |
| `thermal2_n65536` | 0.216074 | 0.222817 | 未继续 |
| `thermal2_n131072` | 0.345646 | 0.365984 | 未继续 |
| `thermal2_n262144` | 0.632533 | 0.622525 | 未继续 |
| `thermal2` | 2.74379 | 2.71420 | 未继续 |

结论：

- original8 和 strip8 都能跑完整 `thermal2`，说明 8-HBM SpMV-only connectivity、
  host packing 和基础 Cuper dataflow 可用；
- strip8 相对 original8 只在 `thermal2_n4096`、`thermal2_n262144` 和完整
  `thermal2` 略快，整体收益很小，且完整点仍慢于既有 16-HBM strip 记录；
- lanereal8-scoreboard 在 `thermal2_n16` 通过，但 `thermal2_n1024` 300s timeout。
  这不是 xclbin 加载或 8-HBM bank 映射问题，更像 RTL issue scoreboard / scheduled
  accumulator 之间的 valid-ready、padding beat、done token 或 scoreboard ageing
  语义没有在真实数据上闭合；
- 不建议继续跑 lanereal8-scoreboard 更大数据集，下一步应先回到 Verilator/xsim
  用 `thermal2_n1024` 复现 owner scoreboard 输出和 scheduled accumulator drain。

## 2026-06-27：lanereal8 scoreboard headreg 修正版

针对上面的 `thermal2_n1024` timeout，本轮把
`CuperSpmvOnly_RtlOwnerScoreboardOoo` 改为每 lane 缓存 owner FIFO head，再把缓存 head
交给 `CuperSpmvOnly_RtlIssueScoreboard8`。旧版在 downstream full 或 RAW hazard bubble
时直接用组合 `s_empty_n/s_dout` 当 head，容易出现重复 read、head 丢失或 pop 同拍采到
变化后 `s_dout` 的进度问题。

本地 RTL/模型测试：

```text
verilator --lint-only -Wall -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL -Wno-PINCONNECTEMPTY -Wno-UNDRIVEN \
  --top-module CuperSpmvOnly_RtlOwnerScoreboardOoo \
  verilog/tapa/CuperSpmvOnly_RtlIssueScoreboard8.v \
  verilog/tapa/CuperSpmvOnly_RtlOwnerScoreboardOoo.v

make -C verilog tapa-owner-scoreboard-sim
make -C verilog tapa-vector-scoreboard-sim
make -C verilog tapa-scoreboard-dataset-cpp-sim
```

结果：

```text
PASS: owner scoreboard wrapper cycles=16 nonpad=9
PASS: vector issue scoreboard8 cycles=32 depth=4
tapa-scoreboard-dataset-cpp-sim: thermal2_n1024 owner 0 通过，done tokens=8，
input/issued counts match
```

新构建产物：

| 版本 | UUID | SHA256 | DATA | KERNEL | HBM | WNS | TNS | setup failing endpoints |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| lanereal8-scoreboard-headreg | `39eb30df-2178-51aa-3333-f1cbb9a0e389` | `1ac7664203e0eb3b6bd8bd35a932ecd8a1afdfb81d34dedf6cc26f98663870e1` | 150 MHz | 500 MHz | 450 MHz | 0.003 ns | 0.000 ns | 0 |

同步文件：

```text
395bitstream/cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-headreg-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-headreg-demo.xclbin.info
```

构建状态：

- `v++ link` 正常完成，POST-VPL `0 errors`；
- `CuperSpmvServiceOnly.xclbin` 写出大小约 57 MB；
- 总 elapsed `2h 59m 18s`；
- routed timing clean：WNS `0.003 ns`、TNS `0.000 ns`、setup failing endpoints `0`。

服务器侧后续复测确认该 headreg artifact 仍未解决旧 `thermal2_n1024` timeout；
下一版已转向 done-drain 语义。该版作为失败边界保留，不更新正式 `source.diff`。

## 2026-06-28：lanereal8 scoreboard done-drain 修正版

head-register 修正后，下一版继续保留 wrapper 缓存 head，不回退；本轮只改 issue
primitive 的 done/drain 协议。`CuperSpmvOnly_RtlIssueScoreboard8` 新增 per-lane
scoreboard empty 检查，普通数据 token 仍按 `{lane, addr, ping/pong}` RAW hazard
判断，done token 只有在该 lane scoreboard window 全空时才能 issue。blocked done
期间继续输出 all-padding beat，推动 scoreboard shift，直到该 lane drain 完成。

同步改动：

- `DLC/Cuper-jacobi-iteration/kernels/detail/cuper_spmv_service_only_top_graphs.hpp`
  的 C++ 等价占位调度器同步 done-drain 语义；
- `tb_tapa_vector_issue_scoreboard8.sv` 覆盖 real update 后同 lane done 必须先输出
  padding beats 再 issue；
- `tb_tapa_owner_scoreboard_ooo.sv` 覆盖 wrapper + primitive 联合场景，验证 cached
  done head 不 reread、不 reissue 已完成 lane，并在 drain 后最终发出 lane 0 done。

本地 RTL/模型测试：

```text
git diff --check

verilator --lint-only -Wno-fatal -Wno-WIDTH -Wno-DECLFILENAME \
  -Iverilog/tapa \
  --top-module CuperSpmvOnly_RtlOwnerScoreboardOoo \
  verilog/tapa/CuperSpmvOnly_RtlIssueScoreboard8.v \
  verilog/tapa/CuperSpmvOnly_RtlOwnerScoreboardOoo.v

make -C verilog tapa-vector-scoreboard-sim
make -C verilog tapa-owner-scoreboard-sim
make -C verilog tapa-scoreboard-dataset-cpp-sim
```

结果：

```text
PASS: vector issue scoreboard8 cycles=37 depth=4
PASS: owner scoreboard wrapper cycles=20 nonpad=9
tapa-scoreboard-dataset-cpp-sim: thermal2_n1024 owner 0 通过，
input_tokens=331, real_tokens=323, done_tokens=8,
issue_complete_cycle=359, done_issues=8, lane input/issued counts match
```

新同步 artifact：

```text
395bitstream/cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-donedrain-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-donedrain-demo.xclbin.info
```

版本信息：

```text
UUID: e90660d3-9efa-9066-7517-4547fc21097f
SHA256: 94d60c0d10dfd6e5384ec0e305e6836a4531ea82d74a24375d054c5546e1d7ad
DATA/KERNEL/HBM clock: 150 / 500 / 450 MHz
Build dir: cuper-jacobi-spmv-lanereal8-scoreboard-donedrain8-hw-150m-build/
Build log: cuper-jacobi-spmv-lanereal8-scoreboard-donedrain8-hw-150m-build/logs/build_hw_tmux.live.log
v++ link elapsed: 3h 15m 0s
POST-VPL: 0 errors
Routed timing: WNS 0.003 ns, TNS 0.000 ns, setup failing endpoints 0
```

服务器侧 demo-only 上板结果：

| 数据集 | 结果 | FPGA 时间 | 备注 |
| --- | --- | ---: | --- |
| `thermal2_n16` | PASS | 0.081832 ms | `Error Num=0` |
| `thermal2_n1024` | timeout | 无 | 300s timeout，`rc=124` |

运行 manifest：

```text
395bitstream/manifest.txt
base_log=logs/spmv_8hbm_donedrain_hw_20260628_144700
date=2026-06-28 14:47:00 CST
bitfile=395bitstream/cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-donedrain-demo.xclbin
uuid=e90660d3-9efa-9066-7517-4547fc21097f
sha256=94d60c0d10dfd6e5384ec0e305e6836a4531ea82d74a24375d054c5546e1d7ad
```

`thermal2_n1024` 的 debug 与前两版一致：停在 `after ReadFromDevice before Finish`；
三次 pre-finish 采样里 `Status/Metrics` 仍是初始化哨兵，`progress_magic valid=0`，
`Y[0..15]` 全 0。结论：done-drain 没有解决 8-HBM scoreboard 分支的 n1024 卡死。
这轮仍不更新正式 `source.diff`。下一步若继续该分支，应单独构建
`JACOBI_SPMV_SCOREBOARD_DEBUG=1` 版本，用 core/issue/acc lane counters 定位卡在
core、RTL issue、scheduled accumulator 还是 scatter writer。

## 2026-07-01：8-HBM owner-bank RTL artifact

测试对象：

```text
395bitstream/cuper-tapa-spmv-u55c-20260701-ownerbank8-demo.xclbin
kernel: CuperSpmvServiceOnly
UUID: e48ae29f-9a2f-372b-b3d3-1f811339dd27
SHA256: 6496bc8ee81ba63e23a6d104a62fdbf5e0bbc6e753b4b7bd5508ec86ddc46264
DATA/KERNEL/HBM clock: 150 / 500 / 450 MHz
HBM mapping: Matrix_data_0..7 -> HBM[0..7], SpElement_list_ptr -> HBM[8],
             X -> HBM[9], Y_out -> HBM[10], Status -> HBM[30], Metrics -> HBM[31]
Timing: WNS 0.003 ns, TNS 0.000 ns, setup failing endpoints 0
```

构建命令口径：

```bash
export JOBS=32
export JACOBI_TOP=CuperSpmvServiceOnly
export JACOBI_SPMV_ONLY=1
export JACOBI_HBM_CHANNELS=8
export JACOBI_SPMV_STRIP_PADDING=1
export JACOBI_SPMV_LANE_STATIC_REAL=1
export JACOBI_SPMV_OOO_ACCUMULATE=1
export JACOBI_SPMV_OOO_ACCUMULATE_RTL=1
export CLOCK_PERIOD=4.0
export JACOBI_KERNEL_FREQUENCY=150
export BUILD_DIR=$PWD/cuper-tapa-spmv-ownerbank8-hw-150m-build
./scripts/build_host.sh
./scripts/build_xo_u55c.sh
./scripts/link_xclbin_u55c.sh
```

构建结果：

```text
build_dir: cuper-tapa-spmv-ownerbank8-hw-150m-build/
log: cuper-tapa-spmv-ownerbank8-hw-150m-build/logs/build_hw_tmux.log
xclbin: cuper-tapa-spmv-ownerbank8-hw-150m-build/CuperSpmvServiceOnly.xclbin
Run vpl: FINISHED. Run Status: impl Complete!
Created .../cuper-tapa-spmv-ownerbank8-hw-150m-build/CuperSpmvServiceOnly.xclbin
Total elapsed time: 2h 40m 37s
tmux run finished with exit code: 0
```

本地验证命令：

```bash
make -C verilog tapa-bank-lint
make -C verilog tapa-bank-sim
make -C verilog tapa-splitter-bank-generated-scatter-cpp-sim
make -C verilog tapa-ownerbank8-dataset-cpp-sim
git diff --check
```

8-HBM dataset C++/Verilator 覆盖：

```text
thermal2_n16
thermal2_n16 + fifo-depth=4 + write-response-delay=7 + output stall
thermal2_n1024
thermal2_n1024 + fifo-depth=16 + write-response-delay=11 + output stall
```

本轮新增的关键检查：

- 8 个 generated `SourceLaneSplitterOoo` 全部参与；
- 8 个 `RtlOwnerBankAccumulatorOoo` owner bank 全部参与；
- generated `TaggedScatterWriterOoo<8>` 参与，而不是只测模型 scatter；
- bank 输出先和按输入 lane token 累加的 expected pair 做逐 pair 校验；
- scatter 写回端模拟 write response delay 和 output full，覆盖 writer backpressure；
- `thermal2_n16` 和 `thermal2_n1024` 都有正常压力和 backpressure 压力两组。

当前状态：

- xclbin 和 `.info` 已同步到 `395bitstream/`；
- 本机 lint/sim 和 routed timing 均通过；
- 服务器侧上板已确认 `thermal2_n16` PASS；
- `thermal2_n1024` 300s timeout，timeout 前采样里 `Status/Metrics` 仍是初始化哨兵，
  `progress_magic valid=0`，`Y[0..15]` 全 0；
- ownerbank8 保留为失败边界，未确认板上性能提升前不更新正式 `source.diff`，
  也不刷新 HTML 性能曲线。

## 2026-07-01：ownerbank8-lighttrace 本地验证

调试构建宏：

```bash
export JACOBI_TOP=CuperSpmvServiceOnly
export JACOBI_SPMV_ONLY=1
export JACOBI_HBM_CHANNELS=8
export JACOBI_SPMV_LANE_STATIC_REAL=1
export JACOBI_SPMV_OOO_ACCUMULATE=1
export JACOBI_SPMV_OOO_ACCUMULATE_RTL=1
export JACOBI_SPMV_OWNERBANK_LIGHTTRACE=1
export CLOCK_PERIOD=4.0
export JACOBI_KERNEL_FREQUENCY=150
export BUILD_DIR=$PWD/cuper-tapa-spmv-ownerbank8-lighttrace-build
```

已完成的本地验证：

```bash
make -C verilog tapa-bank-lint
make -C verilog tapa-bank-sim
make -C verilog tapa-splitter-bank-generated-scatter-cpp-sim
make -C verilog tapa-ownerbank8-dataset-cpp-sim
JACOBI_TOP=CuperSpmvServiceOnly \
  JACOBI_SPMV_ONLY=1 \
  JACOBI_HBM_CHANNELS=8 \
  JACOBI_SPMV_LANE_STATIC_REAL=1 \
  JACOBI_SPMV_OOO_ACCUMULATE_RTL=1 \
  JACOBI_SPMV_OWNERBANK_LIGHTTRACE=1 \
  CUPER_JACOBI_BUILD_DIR=$PWD/cuper-tapa-spmv-ownerbank8-lighttrace-build \
  make cuper-jacobi-build-host
JACOBI_TOP=CuperSpmvServiceOnly \
  JACOBI_SPMV_ONLY=1 \
  JACOBI_HBM_CHANNELS=8 \
  JACOBI_SPMV_LANE_STATIC_REAL=1 \
  JACOBI_SPMV_OOO_ACCUMULATE_RTL=1 \
  JACOBI_SPMV_OWNERBANK_LIGHTTRACE=1 \
  BUILD_DIR=$PWD/cuper-tapa-spmv-ownerbank8-lighttrace-build \
  make -C DLC/Cuper-jacobi-iteration run-sw \
  MATRIX=$PWD/data/suitesparse/Schmid/csr/thermal2_n16
JACOBI_TOP=CuperSpmvServiceOnly \
  JACOBI_SPMV_ONLY=1 \
  JACOBI_HBM_CHANNELS=8 \
  JACOBI_SPMV_LANE_STATIC_REAL=1 \
  JACOBI_SPMV_OOO_ACCUMULATE_RTL=1 \
  JACOBI_SPMV_OWNERBANK_LIGHTTRACE=1 \
  BUILD_DIR=$PWD/cuper-tapa-spmv-ownerbank8-lighttrace-build \
  make -C DLC/Cuper-jacobi-iteration run-sw \
  MATRIX=$PWD/data/suitesparse/Schmid/csr/thermal2_n1024
JACOBI_TOP=CuperSpmvServiceOnly \
  JACOBI_SPMV_ONLY=1 \
  JACOBI_HBM_CHANNELS=8 \
  JACOBI_SPMV_LANE_STATIC_REAL=1 \
  JACOBI_SPMV_OOO_ACCUMULATE_RTL=1 \
  JACOBI_SPMV_OWNERBANK_LIGHTTRACE=1 \
  JOBS=1 \
  BUILD_DIR=$PWD/cuper-tapa-spmv-ownerbank8-lighttrace-build \
  make -C DLC/Cuper-jacobi-iteration build-xo
```

结果：

- `tapa-bank-lint` PASS，保留既有 `SHORTREAL` / `DECLFILENAME` warning；
- `tapa-bank-sim` PASS，owner-bank RTL accumulator 输出正确；
- `tapa-splitter-bank-generated-scatter-cpp-sim` PASS；
- `tapa-ownerbank8-dataset-cpp-sim` PASS，覆盖 `thermal2_n16` / `thermal2_n1024`、
  output backpressure 和 write response delay；
- lighttrace host build PASS，`CMakeLists.txt` 已确认 host 编译路径带上
  `JACOBI_SPMV_OWNERBANK_LIGHTTRACE=1`；
- lighttrace TAPA software simulation PASS：`thermal2_n16` 和 `thermal2_n1024`
  均 `Correctness Verification: Passed`、`Error Num=0`；
- lighttrace XO smoke PASS，生成
  `cuper-tapa-spmv-ownerbank8-lighttrace-build/CuperSpmvServiceOnly.xo`，TAPA analyze、
  HLS synth、custom RTL wrapper/fadd IP hotpatch 和 TAPA pack 均完成。

XO 构建注意事项：

- 首次不限制并行度的 `build-xo` 在 `CuperSpmvOnly_OwnerBankOutputLightTraceTap`
  HLS worker 内触发 Vitis HLS 2022.2 `Abnormal program termination (6)`，栈位于
  dispatch service 初始化，没有 C++ 语法诊断；
- 复跑 `JOBS=1` 后同一份源码完成 HLS 和 pack；
- `scripts/build_xo_u55c.sh` 已对 `JACOBI_SPMV_OWNERBANK_LIGHTTRACE=1` 自动强制
  `JOBS=1`，避免后续硬件构建复现该并发工具问题。

硬件构建：

```bash
tmux new-session -d -s cuper_ownerbank8_lighttrace_hw \
  /home/pyx/project-x/Project-XPlus/cuper-tapa-spmv-ownerbank8-lighttrace-build/run_lighttrace_hw_tmux.sh
```

结果：

```text
Build dir: cuper-tapa-spmv-ownerbank8-lighttrace-build/
Main log: cuper-tapa-spmv-ownerbank8-lighttrace-build/logs/ownerbank8_lighttrace_hw_tmux.log
Memory log: cuper-tapa-spmv-ownerbank8-lighttrace-build/logs/ownerbank8_lighttrace_memory.log
xclbin: cuper-tapa-spmv-ownerbank8-lighttrace-build/CuperSpmvServiceOnly.xclbin
UUID: b0ded244-a8a1-dc8f-3cbf-a62cdf2a9867
SHA256: 5b4bd6b4439ba651df67a1bd384d1149d29d7d8d361be98186fbdf9866c6b227
DATA/KERNEL/HBM clock: 150 / 500 / 450 MHz
Routed timing: WNS 0.003 ns, TNS 0.000 ns, setup failing endpoints 0
v++ total elapsed: 2h 33m 11s
tmux run finished with exit code: 0
```

内存监控：

- `.memory_abort` 不存在；
- swap 使用量一直为 0；
- 最大 RSS 约 17 GB，低于脚本阈值 65 GB；
- 构建结束时系统可用内存约 76 GiB。

同步文件：

```text
395bitstream/cuper-tapa-spmv-u55c-20260701-ownerbank8-lighttrace-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260701-ownerbank8-lighttrace-demo.xclbin.info
```

硬件只跑 `thermal2_n16` 和 `thermal2_n1024`。验收标准不是性能提升，而是在
pre-Finish 采样里至少看到一个明确阶段 counter，并定位最后推进到 entry/mmap、
loader/core、owner-bank 或 scatter writer 哪一段。服务器侧结果为：

| 数据集 | 结果 | 关键现象 |
| --- | --- | --- |
| `thermal2_n16` | PASS | `stage=final(15)` |
| `thermal2_n1024` | timeout | 300s timeout，`Status/Metrics[8..15]` 仍是哨兵，`Y[0..15]=0` |

full lighttrace 在 n1024 上仍没有暴露 progress writer 状态。下一步先做 entry-probe
隔离 kernel entry / Status mmap / HBM first-read，不先做全流程 Chisel。

## 2026-07-02：ownerbank8-entryprobe 本地源码验证

本轮按用户要求不生成新的 XO/xclbin/bitstream，只实现并验证同 ABI 的
`ownerbank8-entryprobe` 源码路径。

构建/运行宏：

```bash
JACOBI_TOP=CuperSpmvServiceOnly
JACOBI_SPMV_ONLY=1
JACOBI_HBM_CHANNELS=8
JACOBI_SPMV_ENTRY_PROBE=1
CUPER_JACOBI_BUILD_DIR=$PWD/cuper-tapa-spmv-ownerbank8-entryprobe-smoke-build
SPMV_PREFINISH_POLL_COUNT=1
```

host build：

```bash
JACOBI_TOP=CuperSpmvServiceOnly \
JACOBI_SPMV_ONLY=1 \
JACOBI_HBM_CHANNELS=8 \
JACOBI_SPMV_ENTRY_PROBE=1 \
CUPER_JACOBI_BUILD_DIR=$PWD/cuper-tapa-spmv-ownerbank8-entryprobe-smoke-build \
make cuper-jacobi-build-host
```

结果：PASS。CMake 输出确认：

```text
Cuper SpMV-only lane-static real packing is ENABLED
Cuper SpMV-only ownerbank8 entry probe is ENABLED
```

software smoke：

```bash
JACOBI_TOP=CuperSpmvServiceOnly \
JACOBI_SPMV_ONLY=1 \
JACOBI_HBM_CHANNELS=8 \
JACOBI_SPMV_ENTRY_PROBE=1 \
CUPER_JACOBI_BUILD_DIR=$PWD/cuper-tapa-spmv-ownerbank8-entryprobe-smoke-build \
SPMV_PREFINISH_POLL_COUNT=1 \
make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n16

JACOBI_TOP=CuperSpmvServiceOnly \
JACOBI_SPMV_ONLY=1 \
JACOBI_HBM_CHANNELS=8 \
JACOBI_SPMV_ENTRY_PROBE=1 \
CUPER_JACOBI_BUILD_DIR=$PWD/cuper-tapa-spmv-ownerbank8-entryprobe-smoke-build \
SPMV_PREFINISH_POLL_COUNT=1 \
make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024
```

关键结果：

| 数据集 | 结果 | Status/Metrics probe |
| --- | --- | --- |
| `thermal2_n16` | PASS | `magic=1162891842`, `stage=entryprobe_final(63)`, `events=12`, `last_value=10` |
| `thermal2_n1024` | PASS | `magic=1162891842`, `stage=entryprobe_final(63)`, `events=12`, `last_value=10` |

`1162891842` 是 `0x45505242` (`EPRB`)。两个 smoke 都正常写回
`Status/Metrics[8..15]`，且 host 打印：

```text
[spmv-only-entry-probe] skip Y correctness: probe top does not run SpMV datapath
```

这是预期行为；entry-probe 不计算 `Y=A*X`，所以 `Y[0..15]=0` 不作为错误。

本轮非 bitstream 检查：

```bash
make -C verilog tapa-bank-lint
make -C verilog tapa-bank-sim
git diff --check
```

结果：

- `tapa-bank-lint` PASS，保留既有 `SHORTREAL` / `DECLFILENAME` warning；
- `tapa-bank-sim` PASS，输出 `PASS: TAPA owner-bank RTL accumulator cycles=65 real_events=40 outputs=16`；
- `git diff --check` PASS。

本轮没有生成 bitstream，也没有更新正式 `source.diff`。

## 2026-07-03：ownerbank8-entryprobe tmux 硬件构建

用户要求补起硬件构建后，先启动第一次 tmux 构建：

```bash
JACOBI_TOP=CuperSpmvServiceOnly \
JACOBI_SPMV_ONLY=1 \
JACOBI_HBM_CHANNELS=8 \
JACOBI_SPMV_ENTRY_PROBE=1 \
CLOCK_PERIOD=4.0 \
JACOBI_KERNEL_FREQUENCY=150 \
CUPER_JACOBI_BUILD_DIR=$PWD/cuper-tapa-spmv-ownerbank8-entryprobe-build \
make cuper-jacobi-hw-tmux
```

失败结果：

```text
Build dir: cuper-tapa-spmv-ownerbank8-entryprobe-build/
Main log: cuper-tapa-spmv-ownerbank8-entryprobe-build/logs/build_hw_tmux.log
Stage: TAPA pack
Root cause: entry-probe did not use Y_out, so HLS optimized away m_axi_Y_out.
```

该失败说明 first-read/status probe 分支虽然保持了 host 参数列表，但没有实际触碰
`Y_out`，导致 IP packaging 时找不到原 ABI 的 `m_axi_Y_out` 接口。

修复后先复跑软件 smoke：

```bash
JACOBI_TOP=CuperSpmvServiceOnly \
JACOBI_SPMV_ONLY=1 \
JACOBI_HBM_CHANNELS=8 \
JACOBI_SPMV_ENTRY_PROBE=1 \
CUPER_JACOBI_BUILD_DIR=$PWD/cuper-tapa-spmv-ownerbank8-entryprobe-smoke-build \
make cuper-jacobi-build-host

JACOBI_TOP=CuperSpmvServiceOnly \
JACOBI_SPMV_ONLY=1 \
JACOBI_HBM_CHANNELS=8 \
JACOBI_SPMV_ENTRY_PROBE=1 \
CUPER_JACOBI_BUILD_DIR=$PWD/cuper-tapa-spmv-ownerbank8-entryprobe-smoke-build \
SPMV_PREFINISH_POLL_COUNT=1 \
make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n16

JACOBI_TOP=CuperSpmvServiceOnly \
JACOBI_SPMV_ONLY=1 \
JACOBI_HBM_CHANNELS=8 \
JACOBI_SPMV_ENTRY_PROBE=1 \
CUPER_JACOBI_BUILD_DIR=$PWD/cuper-tapa-spmv-ownerbank8-entryprobe-smoke-build \
SPMV_PREFINISH_POLL_COUNT=1 \
make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024
```

结果：

| 数据集 | 结果 | Status/Metrics probe |
| --- | --- | --- |
| `thermal2_n16` | PASS | `magic=1162891842`, `stage=entryprobe_final(63)`, `events=12`, `last_value=10` |
| `thermal2_n1024` | PASS | `magic=1162891842`, `stage=entryprobe_final(63)`, `events=12`, `last_value=10` |

修复版第二次 tmux 构建：

```bash
JACOBI_TOP=CuperSpmvServiceOnly \
JACOBI_SPMV_ONLY=1 \
JACOBI_HBM_CHANNELS=8 \
JACOBI_SPMV_ENTRY_PROBE=1 \
CLOCK_PERIOD=4.0 \
JACOBI_KERNEL_FREQUENCY=150 \
CUPER_JACOBI_BUILD_DIR=$PWD/cuper-tapa-spmv-ownerbank8-entryprobe-yout-build \
make cuper-jacobi-hw-tmux
```

当前检查点：

```text
Build dir: cuper-tapa-spmv-ownerbank8-entryprobe-yout-build/
Main log: cuper-tapa-spmv-ownerbank8-entryprobe-yout-build/logs/build_hw_tmux.log
XO: cuper-tapa-spmv-ownerbank8-entryprobe-yout-build/CuperSpmvServiceOnly.xo
TAPA pack: PASS
Vitis system_link/cfgen/cf2bd: PASS
VPL: started at 2026-07-03 13:30:29 CST
```

关键日志确认：

```text
generated the v++ xo file at .../CuperSpmvServiceOnly.xo
cfgen ... -sp CuperSpmvServiceOnly_1.Y_out:HBM[10]
Run run_link: Step vpl: Started
```

最终构建结果：

```text
Run vpl: FINISHED. Run Status: impl Complete!
Run completed
Total elapsed time: 1h 18m 3s
```

同步文件：

```text
395bitstream/cuper-tapa-spmv-u55c-20260703-ownerbank8-entryprobe-yout-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260703-ownerbank8-entryprobe-yout-demo.xclbin.info
```

同步后校验：

```text
UUID: e0dbc189-228b-3519-0d68-dd541d6bc70a
xclbin SHA256: f443c729851e7b64e1b87eb59ce613a79de44aacb8d7072b5068a7bb0e4b8d0e
info SHA256: 2c70ba0517b4f73160fd1f0fae4743ffded87ed61e31ff88bb4132c53d6cec1e
DATA/KERNEL/HBM clock: 150 / 500 / 450 MHz
Routed timing: WNS 0.003 ns, TNS 0.000 ns, setup failing endpoints 0
```

HBM 映射：

```text
Matrix_data_0..7 -> HBM[0..7]
SpElement_list_ptr -> HBM[8]
X -> HBM[9]
Y_out -> HBM[10]
Status -> HBM[30]
Metrics -> HBM[31]
```

这轮 probe 仍是 debug 边界验证，尚未跑服务器侧 `thermal2_n16` /
`thermal2_n1024` 上板测试，不更新正式 `source.diff`。
