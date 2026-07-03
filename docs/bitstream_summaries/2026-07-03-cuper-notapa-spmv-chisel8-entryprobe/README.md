# CuperSpmvChisel8 Entry-Probe

## 状态

这版是 `cuper-notapa-spmv` 主线下的独立 Chisel RTL kernel bring-up artifact：

```text
kernel: CuperSpmvChisel8
bitfile: 395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-entryprobe-demo.xclbin
build dir: cuper-spmv-chisel8-build/
build log: logs/cuper_spmv_chisel8_hw_20260703_183507.log
```

它不是 `CuperSpmvServiceOnly` 的 TAPA graph，也不是 full SpMV。当前只做
entry-probe：读取 `ptr[0]`、`Matrix_data_0..7[0]` 和 `X[0]`，写 `Y_out[0]`、
`Status[0..15]`、`Metrics[0..15]`。目的就是确认 Chisel RTL top 能作为 Vitis
RTL kernel 接入 XRT、AXI-Lite、HBM 和 mmap 结果返回链路。

## 同步信息

```text
UUID: 3fbc0fc1-7776-ec77-1418-1674584aff18
SHA256: 30bd05a8066640fc2a407b04ff2c7a6602a0ac1fc3f0480c3269d9310ca654e0
DATA/KERNEL/HBM clock: 150 / 500 / 450 MHz
Routed timing: WNS 0.003 ns, TNS 0.000 ns, setup failing endpoints 0
Vitis result: Run completed, VPL impl Complete
```

## HBM ABI

HBM mapping 沿用 ownerbank8：

```text
Matrix_data_0..7 -> HBM[0..7]
SpElement_list_ptr -> HBM[8]
X -> HBM[9]
Y_out -> HBM[10]
Status -> HBM[30]
Metrics -> HBM[31]
```

每个 HBM-facing 参数都是独立 `m_axi_*` master port；不是一个单 AXI master 在 RTL
内部转发到多个 HBM bank。

## 建议

保留为 demo/debug artifact，不晋级标准 bitstream。下一步先上板跑
`thermal2_n16` entry-probe，确认 `Status`/`Metrics` 中的 magic、first-read 和
`Y_out[0]` 写回后，再进入 HBM drain-probe。

## 2026-07-03 本轮复核

本轮仓库内没有找到 entry-probe 上板通过日志。尝试用当前本机直接跑
`395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-entryprobe-demo.xclbin`
时，XRT 在设备打开阶段返回 `No such device with index '0'`，因此本轮不能把
`thermal2` 全套 no-check `rc=0` 或 `CHECK_Y=1 rc=3` 写成已复核事实。

源码后续已推进到下一阶段 HBM drain-probe，并已单独生成/同步
`395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-drainprobe-demo.xclbin`。
这不改变本文件记录的 entry-probe xclbin 内容；entry-probe 只作为历史 first-read
artifact 保留。
