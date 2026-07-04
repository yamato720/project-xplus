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

low-memory 版 Verilator 摘要：

```text
Built from 0.410 MB sources in 16 modules
Walltime 3.354 s
allocated 90.934 MB
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
make cuper-spmv-chisel8-hw-tmux
```

第二轮 low-memory full hw link 结果：通过，生成并同步 demo：

```text
log: logs/cuper_spmv_chisel8_hw_20260704_014807.log
build dir: cuper-spmv-chisel8-build/
xclbin: cuper-spmv-chisel8-build/hw/CuperSpmvChisel8.xclbin
synced demo: 395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-spmvbaseline-demo.xclbin
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

## 失败边界

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

该失败发生在 low-memory 改造之前。当前 low-memory 版已经重新完成 generation、
Verilator lint、XO packaging 和完整 hw link。

## 未执行

- 没有 `sw_emu`：`CuperSpmvChisel8` 是 RTL kernel path，当前 Makefile 没有 sw_emu
  model；本轮用 Chisel generation、Verilator lint、host build、XO packaging 和 hw link
  early checkpoint 作为前置验证。
- 未上板：本机没有 U55C/XRT device；需要服务器侧 demo-only correctness sweep。

## source.diff

未生成/更新正式 `source.diff`。原因：full SpMV baseline 已完成 xclbin 构建和同步，
但尚未完成 demo-only 上板 correctness；且当前 xclbin 不是 150 MHz timing-clean。
