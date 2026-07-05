# 测试记录

## 已完成

```bash
make cuper-spmv-chisel8-generate
```

结果：通过，生成 `verilog/chisel/CuperSpmvChisel8.sv`。

记录：

```text
wide issue / pre-lowmem: 约 131 万行，约 43MB，生成耗时 234s
low-memory serial issue: 8195 行，353196 bytes，生成耗时 20s
correctness-debug current: datapath RTL 5303 行 / 241612 bytes，top RTL 8667 行 / 375690 bytes
```

```bash
make cuper-spmv-chisel8-datapath-generate
```

结果：通过，生成/复用 `verilog/tapa/CuperSpmvOnly_ChiselDataPath8.v`。

```bash
make cuper-spmv-chisel8-datapath-smoke
```

结果：通过。

关键输出：

```text
PASS basic-two-beat: 64 tagged outputs in 1573 cycles
PASS raw-reuse-padding: 64 tagged outputs in 1209 cycles
PASS multi-group-empty-source: 136 tagged outputs in 1275 cycles
PASS all-padding: 16 tagged outputs in 1160 cycles
PASS cross-8192-column-batch: 64 tagged outputs in 8832 cycles
CuperSpmvOnly_ChiselDataPath8 smoke PASS: 344 tagged outputs across 5 cases
```

```bash
make cuper-spmv-chisel8-axi-top-smoke
```

结果：通过。

关键输出：

```text
CuperSpmvChisel8 AXI top smoke PASS y0=2 valid_slots=1 nonzero_products=1 core_nonzero_out=1 fadd_nonzero_out=1 partial_read_nonzero=1 nonzero_y_writes=1
```

```bash
verilator --lint-only --timing -Wno-fatal -Wno-WIDTH -Wno-DECLFILENAME \
  -Wno-SHORTREAL -DVERILATOR=1 -Iverilog/tapa \
  --top-module CuperSpmvChisel8 \
  verilog/tapa/CuperSpmvOnly_CoreStrip_fmul_32ns_32ns_32_8_max_dsp_1.v \
  verilog/tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1.v \
  verilog/chisel/CuperSpmvChisel8.sv
```

结果：通过。唯一诊断是生成端口名 `interrupt` 触发 Verilator `SYMRSVDWORD` warning。

当前 correctness-debug 版 Verilator 摘要：

```text
Built from 0.431 MB sources in 16 modules
Walltime 1.331 s
allocated 90.492 MB
```

```bash
make cuper-spmv-chisel8-xrt-host
```

结果：通过。编译 warning 来自既有 HLS/TAPA headers。

```bash
make build-cuper-spmv-chisel8-xo
```

结果：通过，生成：

```text
cuper-spmv-chisel8-build/hw/CuperSpmvChisel8.xo
```

XO packaging 已包含 fmul/fadd wrapper RTL 和对应 Vivado floating_point IP。

```bash
make -q cuper-spmv-chisel8-build/hw/CuperSpmvChisel8.xo TARGET=hw \
  BUILD_DIR=/home/pyx/project-x/Project-XPlus/cuper-spmv-chisel8-build
```

结果：返回 `0`，Makefile 认为 XO target up-to-date。

```bash
make cuper-spmv-chisel8-hw-tmux
```

第二轮 low-memory full hw link 结果：通过，生成旧版 demo：

```text
log: logs/cuper_spmv_chisel8_hw_20260704_014807.log
build dir: cuper-spmv-chisel8-build/
xclbin: cuper-spmv-chisel8-build/hw/CuperSpmvChisel8.xclbin
previous synced demo: 395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-spmvbaseline-demo.xclbin
UUID: c36bff4e-7efc-805f-b6a0-ccfd1677cda0
SHA256: 5da1df03f85077185ad1ab787e95e3f71cd064308b739ddfd4f711e209fd9907
DATA/KERNEL/HBM clock: 119 / 500 / 450 MHz
Vitis elapsed: 2h 20m 50s
```

关键日志：

```text
Run vpl: FINISHED. Run Status: impl Complete!
Run completed.
The compiler selected the following frequencies ... hbm_aclk = 450, KERNEL = 500, DATA = 119
```

Timing 状态：不是 timing-clean。150 MHz DATA 约束未收敛，Vitis 生成了降频 xclbin：

```text
Timing constraints are not met.
WNS -1.719 ns
TNS -3059.849 ns
TNS failing endpoints 10198
WHS 0.009 ns
```

```bash
make cuper-spmv-chisel8-hw-tmux
```

第三轮 correctness-debug full hw link 结果：通过，已覆盖同步同一个 demo 文件：

```text
log: logs/cuper_spmv_chisel8_correctness_debug_hw_20260704_143204.log
build dir: cuper-spmv-chisel8-build/
xclbin: cuper-spmv-chisel8-build/hw/CuperSpmvChisel8.xclbin
synced demo: 395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-spmvbaseline-demo.xclbin
UUID: 0f31be8c-e77e-4e25-d85a-1498693befbb
SHA256: 1ea8f0051cad2c3a81ab50f9e66a0d8fa982a55310e82aa054bd753bb658ab8e
DATA/KERNEL/HBM clock: 139 / 500 / 450 MHz
Vitis elapsed: 2h 23m 29s
```

关键日志：

```text
Run vpl: FINISHED. Run Status: impl Complete!
Run completed.
The compiler selected the following frequencies ... hbm_aclk = 450, KERNEL = 500, DATA = 139
```

Timing 状态：仍不是 timing-clean。150 MHz DATA 约束未收敛，Vitis 生成了 139 MHz
DATA xclbin：

```text
Timing constraints are not met.
WNS -0.478 ns
TNS -173.688 ns
TNS failing endpoints 1448
WHS 0.001 ns
```

## 失败边界

服务器侧已同步 spmvbaseline 旧 demo（UUID `c36bff4e-7efc-805f-b6a0-ccfd1677cda0`）
的上板结果：

```text
bitfile: 395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-spmvbaseline-demo.xclbin
no-check: 完整 thermal2 可返回，用户提供完整点耗时 477.6 ms
CHECK_Y=1: 失败，Y mostly zeros/错误
结论: correctness failed；477.6 ms 不是有效 SpMV 性能成绩
```

本地仓库没有该服务器侧 sweep 的完整逐规模 timing 表或原始日志，因此本记录只登记
用户提供的完整点和 correctness 结论，不补造缺失数据。

上一轮已同步的新 demo（UUID `0f31be8c-e77e-4e25-d85a-1498693befbb`）服务器侧
`CHECK_Y=1` 仍失败。用户提供的反馈目录为
`logs/spmv_chisel8_correctness_debug_hw_20260704_192807/`，该目录当前未同步到本地仓库。
反馈结论是 ptr/X/matrix decode 和 accumulator accepts 都是活的；但旧
`nonzero_products` 只证明 fmul 输入非零，不证明 fmul 输出非零。因此 full-debug 同步版
修正 fmul tag/valid 对齐并追加 FP/partial 输出 counters。旧 `477.6 ms` 和更早
`Y` 错误结论不能套用到当前 UUID `495e02a6-...` slim/no-debug demo。

## 2026-07-04 fmul latency / FP counter 本地验证

```bash
make cuper-spmv-chisel8-xrt-host
```

结果：通过。编译 warning 仍来自既有 HLS/TAPA headers。

```bash
make cuper-spmv-chisel8-generate
make cuper-spmv-chisel8-datapath-generate
```

结果：通过，重新生成：

```text
verilog/chisel/CuperSpmvChisel8.sv
verilog/tapa/CuperSpmvOnly_ChiselDataPath8.v
```

```bash
make cuper-spmv-chisel8-datapath-smoke
```

结果：通过。

关键输出：

```text
PASS basic-two-beat: 64 tagged outputs in 1573 cycles
PASS raw-reuse-padding: 64 tagged outputs in 1207 cycles
PASS multi-group-empty-source: 136 tagged outputs in 1275 cycles
PASS all-padding: 16 tagged outputs in 1160 cycles
PASS cross-8192-column-batch: 64 tagged outputs in 8831 cycles
CuperSpmvOnly_ChiselDataPath8 smoke PASS: 344 tagged outputs across 5 cases
```

```bash
make cuper-spmv-chisel8-axi-top-smoke
```

结果：通过。

关键输出：

```text
CuperSpmvChisel8 AXI top smoke PASS y0=2 valid_slots=1 nonzero_products=1 core_nonzero_out=1 fadd_nonzero_out=1 partial_read_nonzero=1 nonzero_y_writes=1
```

```bash
verilator --lint-only --timing -Wno-fatal -Wno-WIDTH -Wno-DECLFILENAME \
  -Wno-SHORTREAL -DVERILATOR=1 -Iverilog/tapa \
  --top-module CuperSpmvChisel8 \
  verilog/tapa/CuperSpmvOnly_CoreStrip_fmul_32ns_32ns_32_8_max_dsp_1.v \
  verilog/tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1.v \
  verilog/chisel/CuperSpmvChisel8.sv
```

结果：通过。唯一诊断仍是生成端口名 `interrupt` 触发 Verilator `SYMRSVDWORD` warning。

```bash
make build-cuper-spmv-chisel8-xo
```

结果：通过，生成：

```text
cuper-spmv-chisel8-build/hw/CuperSpmvChisel8.xo
```

```bash
make build-cuper-spmv-chisel8-hw
```

结果：通过，已覆盖同步同一个 demo 文件：

```text
log: logs/cuper_spmv_chisel8_hw_20260704_200820.log
build dir: cuper-spmv-chisel8-build/
xclbin: cuper-spmv-chisel8-build/hw/CuperSpmvChisel8.xclbin
synced demo: 395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-spmvbaseline-demo.xclbin
UUID: 765e33c9-f3e4-5a25-55ca-ff9bc3a1ddad
SHA256: 550ed459faa550fa5f18947e7c2c5c0bf6624f0f78745540195d0c11b41626d3
DATA/KERNEL/HBM clock: 85 / 500 / 345 MHz
Vitis elapsed: 21h 59m 45s
```

关键日志：

```text
Run vpl: FINISHED. Run Status: impl Complete!
Run completed.
The compiler selected the following frequencies ... hbm_aclk = 345, KERNEL = 500, DATA = 85
```

Timing 状态：严重不是 timing-clean。150 MHz DATA 约束未收敛，Vitis 生成了 85 MHz
DATA xclbin：

```text
Timing constraints are not met.
WNS -5.008 ns
TNS -35127.766 ns
TNS failing endpoints 41704
WHS 0.008 ns
```

## 2026-07-05 slim/no-debug 本地验证与 hw link

```bash
CUPER_SPMV_CHISEL8_SLIM_DEBUG=1 make cuper-spmv-chisel8-datapath-smoke
```

结果：通过。该 smoke 复用 datapath packed cases，确认 slim mode 不改变功能路径。

```bash
CUPER_SPMV_CHISEL8_SLIM_DEBUG=1 make cuper-spmv-chisel8-axi-top-smoke
```

结果：通过。

关键输出：

```text
CuperSpmvChisel8 AXI top smoke PASS y0=2 slim_debug=1 valid_slots=0 nonzero_products=0 core_nonzero_out=0 fadd_nonzero_out=0 partial_read_nonzero=0 nonzero_y_writes=1
```

```bash
CUPER_SPMV_CHISEL8_SLIM_DEBUG=1 verilator --lint-only --timing -Wno-fatal -Wno-WIDTH \
  -Wno-DECLFILENAME -Wno-SHORTREAL -DVERILATOR=1 -Iverilog/tapa \
  --top-module CuperSpmvChisel8 \
  verilog/tapa/CuperSpmvOnly_CoreStrip_fmul_32ns_32ns_32_8_max_dsp_1.v \
  verilog/tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1.v \
  verilog/chisel/CuperSpmvChisel8.sv
```

结果：通过。唯一诊断仍是生成端口名 `interrupt` 触发 Verilator `SYMRSVDWORD`
warning。

```bash
make cuper-spmv-chisel8-hw-tmux
```

slim/no-debug full hw link 结果：通过，已覆盖同步同一个 demo 文件：

```text
log: logs/cuper_spmv_chisel8_slimdebug_hw_20260705_165202.log
build dir: cuper-spmv-chisel8-slimdebug-build/
xclbin: cuper-spmv-chisel8-slimdebug-build/hw/CuperSpmvChisel8.xclbin
synced demo: 395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-spmvbaseline-demo.xclbin
UUID: 495e02a6-2d7b-8c84-fa0d-e7bfedc10f87
SHA256: adb1c8630a4edde50560baf60826d86988b5e25e73d1c093da05ea4cc8653946
DATA/KERNEL/HBM clock: 120 / 500 / 450 MHz
Vitis elapsed: 2h 46m 0s
```

关键日志：

```text
Run vpl: FINISHED. Run Status: impl Complete!
Run completed.
The compiler selected the following frequencies ... hbm_aclk = 450, KERNEL = 500, DATA = 120
```

Timing 状态：仍不是 timing-clean。150 MHz DATA 约束未收敛，Vitis 生成了 120 MHz
DATA xclbin：

```text
Timing constraints are not met.
WNS -1.644 ns
TNS -6319.366 ns
TNS failing endpoints 15852
WHS 0.009 ns
```

```bash
make cuper-spmv-chisel8-hw-tmux
```

第一轮 full hw link 状态：

```text
log: logs/cuper_spmv_chisel8_hw_20260704_003315.log
safe checkpoint: system_link/cf2sw completed, vpl started
failure: Vivado OOC synth 完成模块综合后，连续触发 2.684GB tcmalloc large alloc，随后 interrupt
```

关键日志：

```text
tcmalloc: large alloc 2684035072 bytes
INFO: [Common 17-41] Interrupt caught. Command should exit soon.
```

该失败发生在 low-memory 改造之前。当前 correctness-debug 版已经重新完成
generation、Verilator lint、XO packaging 和完整 hw link。

## 未执行

- 没有 `sw_emu`：`CuperSpmvChisel8` 是 RTL kernel path，当前 Makefile 没有 sw_emu
  model；本轮用 Chisel generation、datapath packed smoke、AXI top smoke、
  Verilator lint、host build 和 XO packaging 作为前置验证。
- 当前 slim/no-debug correctness demo 未上板：本机没有 U55C/XRT device；需要服务器侧 demo-only
  `CHECK_Y=1` correctness sweep。

## source.diff

未生成/更新正式 `source.diff`。原因：当前 slim/no-debug demo 尚未完成
demo-only `CHECK_Y=1` 上板验证。
