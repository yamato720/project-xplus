# 测试记录

## 本地构建

已完成：

```bash
make cuper-spmv-chisel8-generate
verilator --lint-only ...
make cuper-spmv-chisel8-xrt-host
make build-cuper-spmv-chisel8-xo
make cuper-spmv-chisel8-hw-tmux
```

硬件构建结果：

```text
log: logs/cuper_spmv_chisel8_hw_20260703_183507.log
xclbin: cuper-spmv-chisel8-build/hw/CuperSpmvChisel8.xclbin
result: Run completed, VPL impl Complete
elapsed: 1h 22m 23s
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

同步校验：

```bash
sha256sum 395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-entryprobe-demo.xclbin
```

输出：

```text
30bd05a8066640fc2a407b04ff2c7a6602a0ac1fc3f0480c3269d9310ca654e0
```

## 已确认的 xclbin 元信息

```text
kernel: CuperSpmvChisel8
UUID: 3fbc0fc1-7776-ec77-1418-1674584aff18
DATA clock: 150 MHz
KERNEL clock: 500 MHz
HBM clock: 450 MHz
```

Connectivity：

```text
Matrix_data_0..7 -> HBM[0..7]
SpElement_list_ptr -> HBM[8]
X -> HBM[9]
Y_out -> HBM[10]
Status -> HBM[30]
Metrics -> HBM[31]
```

## 未完成

尚未上板运行。推荐第一条命令：

```bash
make run-cuper-spmv-chisel8-xrt TARGET=hw \
  BITFILE=395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-entryprobe-demo.xclbin \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16
```

预期只检查 entry-probe：

- `Status[0] == 1`
- `Status[1] == 0x43535056`
- `Metrics[0] == 0x4353504d56384348`
- raw `Status` / `Metrics` 中能看到 row/column/batch/matrix_len 和 first-read 摘要
- `Y_out[0]` 被写回

这版默认不加 `CHECK_Y=1`。如果强制 Y correctness，会失败或无意义，因为 RTL 还不计算
SpMV。

## source.diff

未生成/更新正式 `source.diff`。原因：本轮只是同步 entry-probe xclbin，尚未完成板上
demo-only 测试，也没有 SpMV 性能或正确性结论。
