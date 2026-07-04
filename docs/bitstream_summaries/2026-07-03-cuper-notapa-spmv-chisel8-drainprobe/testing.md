# 测试记录

## 已完成

```bash
make cuper-spmv-chisel8-generate
```

结果：通过，重新生成：

```text
verilog/chisel/CuperSpmvChisel8.sv
```

```bash
verilator --lint-only -Wall \
  -Wno-DECLFILENAME -Wno-UNUSED -Wno-WIDTHEXPAND -Wno-WIDTHTRUNC \
  -Wno-SYMRSVDWORD \
  verilog/chisel/CuperSpmvChisel8.sv
```

结果：通过。`-Wno-SYMRSVDWORD` 只用于 Vitis RTL kernel 固定端口名 `interrupt`。

```bash
make cuper-spmv-chisel8-xrt-host
```

结果：通过。编译中仍有来自既有 HLS/TAPA header 的 warning，不是本轮 host 源码错误。

```bash
make build-cuper-spmv-chisel8-xo
```

结果：通过，生成：

```text
cuper-spmv-chisel8-build/hw/CuperSpmvChisel8.xo
```

Vivado/IP packaging 识别到 13 路 `m_axi_*`、`s_axi_control`、`ap_clk`、
`ap_rst_n` 和 `interrupt`。日志中仍有既有 SystemVerilog top packaging 支持提示和
AXI FREQ_HZ warning；entry-probe 版同一路径也有这些 warning，不影响 XO 生成。

```bash
make cuper-spmv-chisel8-hw-tmux
```

完整 xclbin 构建结果：通过。

```text
tmux session: project-xplus-cuper-spmv-chisel8-drainprobe-hw
log: logs/cuper_spmv_chisel8_drainprobe_hw_20260703_213712.log
build dir: cuper-spmv-chisel8-build/
xclbin: cuper-spmv-chisel8-build/hw/CuperSpmvChisel8.xclbin
result: Run completed, VPL impl Complete
elapsed: 1h 56m 41s
DATA/KERNEL/HBM clock: 150 / 500 / 450 MHz
```

Routed timing：

```text
report: cuper-spmv-chisel8-build/hw/reports/link/imp/impl_1_hw_bb_locked_timing_summary_routed.rpt
WNS: 0.003 ns
TNS: 0.000 ns
setup failing endpoints: 0
hold WHS: 0.009 ns
```

同步到 `395bitstream/`：

```text
bitfile: 395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-drainprobe-demo.xclbin
info: 395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-drainprobe-demo.xclbin.info
UUID: 3ea13c75-0ba3-5dbe-0d58-4778e489313b
SHA256: 3783f92f2dd0000d009ea5ff98e7be61157fbeb948bfd846ee975bbdc191e80f
```

## 本机不能执行的上板步骤

本机尝试运行旧 entry-probe xclbin 时，XRT 报：

```text
No such device with index '0'
```

因此本轮暂不能在本机执行 drain-probe 上板 smoke。新的 drain-probe xclbin 已经生成
并同步，需要在有 U55C/XRT device 的服务器侧跑 no-check sweep。

## 服务器侧上板结果

用户给出的 drain-probe 上板结果已经覆盖 full `thermal2` 套件：

| 数据集范围 | 模式 | 结果 |
| --- | --- | --- |
| `thermal2_n16` 到完整 `thermal2` | no-check | 全部 `rc=0` |
| `thermal2_n16` 到完整 `thermal2` | no-check status/metrics | ptr/X/matrix 计数匹配，`Status[11]=0xff`，`Status[12]=0`，`Status[13]=0` |
| 抽样点 | `--check-y` | `rc=3`，预期失败，因为 drain-probe 只写 `Y_out[0]=0` |

结论：AXI-Lite、ptr/X/8 路 Matrix HBM 读链路、Status/Metrics 写回链路和 HBM mapping
已经通过 drain-probe 板测；下一步可以进入 full SpMV baseline。

## 原待上板命令

已测试 bitfile：

```text
395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-drainprobe-demo.xclbin
```

上板验收命令建议：

```bash
for d in thermal2_n16 thermal2_n1024 thermal2_n4096 thermal2_n16384 thermal2_n65536 thermal2_n131072 thermal2_n262144 thermal2; do
  timeout 180s make run-cuper-spmv-chisel8-xrt TARGET=hw \
    BITFILE=395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-drainprobe-demo.xclbin \
    DATASET="data/suitesparse/Schmid/csr/$d"
done
```

预期：

- no-check 全部 `rc=0`；
- `Status[1] == 0x43535056`；
- `Metrics[0] == 0x4353504d56384348`；
- `Status[11] == 0xff`；
- `Status[12] == 0` 且 `Status[13] == 0`；
- `Status[7] == Status[8] == 8 * (Batch_num + 2)`；
- `Status[9] == Status[10] == ceil(Column_num / 16)`；
- `Metrics[8..15]` 等于 host 打印的 `[matrix-beats-expected]`。

`CHECK_Y=1` 仍预期失败并返回 `rc=3`，因为 drain-probe 不计算 SpMV。

## source.diff

未生成/更新正式 `source.diff`。原因：本轮只是 drain-probe demo 候选，虽已完成构建、
同步和 no-check 上板，但不计算 SpMV，没有 SpMV 正确性或性能提升结论。
