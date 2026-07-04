# CuperSpmvChisel8 Full SpMV Baseline

## 状态

这版是 `cuper-notapa-spmv` 主线下的独立 Chisel RTL kernel full SpMV baseline
源码候选：

```text
kernel: CuperSpmvChisel8
source: chisel/cuper-spmv8/src/main/scala/cuper/spmv/CuperSpmvChisel8.scala
generated RTL: verilog/chisel/CuperSpmvChisel8.sv
build dir: cuper-spmv-chisel8-build/
current hw log: logs/cuper_spmv_chisel8_hw_20260704_014807.log
demo xclbin: 395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-spmvbaseline-demo.xclbin
```

它保持 drain-probe 版 kernel 名、host ABI、AXI-Lite offsets、13 路 `m_axi_*`
端口和 HBM mapping 不变，内部从 drain FSM 改成：

```text
ptr/X/matrix AXI loaders
  -> internal stream FIFO
  -> CuperSpmvOnly_ChiselDataPath8
  -> tagged output FIFO
  -> scalar Y_out writer
  -> Status/Metrics writer
```

本版不做 scoreboard、不做 full-PCG、不晋级标准 bitstream；仍作为
`cuper-notapa-spmv` demo 候选。

## 当前构建

已完成本地 RTL 生成、Verilator lint、host 编译、XO 打包和完整 hw link。第一轮
完整 hw link 在 Vitis `vpl` block-level synthesis 后触发极端内存使用：

```text
log: logs/cuper_spmv_chisel8_hw_20260704_003315.log
build dir: cuper-spmv-chisel8-build/
safe checkpoint: system_link/cf2sw completed, vpl started at 2026-07-04 00:33:51 +0800
failure boundary: Vivado OOC synth completed RTL modules, then repeated 2.684GB tcmalloc allocations
```

随后源码改成 low-memory serial issue baseline：`CuperSpmvOnly_ChiselDataPath8`
仍保留 8 路 HBM ABI 和 8x8 Core/Accumulator lanes，但 X buffer 从多端口
`Reg(Vec(8192))` 改为单读/单写 `SyncReadMem`，X packet 分 16 拍写入，matrix beat
按 source/owner slot 串行读 X 并送入 Core。生成 RTL 从约 131 万行 / 43MB 降到
8195 行 / 353KB；Verilator lint 内存从 GB 级降到约 91MB。

第二轮 low-memory 完整 hw link 已生成 xclbin 并同步到 `395bitstream/`：

```text
log: logs/cuper_spmv_chisel8_hw_20260704_014807.log
result: Vitis link Run completed, VPL impl Complete
demo: 395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-spmvbaseline-demo.xclbin
UUID: c36bff4e-7efc-805f-b6a0-ccfd1677cda0
SHA256: 5da1df03f85077185ad1ab787e95e3f71cd064308b739ddfd4f711e209fd9907
DATA/KERNEL/HBM clock: 119 / 500 / 450 MHz
routed timing: WNS -1.719 ns, TNS -3059.849 ns, setup failing endpoints 10198, hold WHS 0.009 ns
```

这版可作为 demo 上板，但不是 timing-clean：link 请求 DATA 150 MHz，最终 xclbin
info 记录 DATA clock 为 119 MHz。上板 correctness 通过前，不晋级标准 bitstream，
也不更新正式 `source.diff`。

## 验收目标

已同步 demo 文件：

```text
395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-spmvbaseline-demo.xclbin
```

上板验收：

- no-check：magic、ptr/X/matrix/tagged/Y-write counts 全部匹配，done mask `0xff`，
  R/B error mask 为 0。
- `--check-y`：`thermal2_n16`、`n1024`、`n4096`、`n16384`、`n65536`、
  `n131072`、`n262144` 和完整 `thermal2` 目标全部 `rc=0`。
