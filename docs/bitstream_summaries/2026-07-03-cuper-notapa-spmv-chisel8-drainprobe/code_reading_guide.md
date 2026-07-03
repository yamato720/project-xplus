# 代码阅读指南

## 入口

- `chisel/cuper-spmv8/src/main/scala/cuper/spmv/CuperSpmvChisel8.scala`：
  独立 Vitis RTL kernel 顶层和 drain-probe FSM。
- `host/cuper_spmv_chisel_xrt.cpp`：native XRT host、ownerbank8 打包和 drain-probe
  摘要校验。
- `chisel/cuper-spmv8/packaging/CuperSpmvChisel8.kernel.xml`：kernel argument 顺序和
  AXI-Lite offset。
- `cfg/connectivity_cuper_spmv_chisel8_u55c.cfg`：13 路 `m_axi_*` 到 U55C HBM bank 的映射。

## ABI

ABI 仍是 entry-probe 版：

```text
0  SpElement_list_ptr -> HBM[8]
1  Matrix_data_0      -> HBM[0]
...
8  Matrix_data_7      -> HBM[7]
9  X                  -> HBM[9]
10 Y_out              -> HBM[10]
11 Status             -> HBM[30]
12 Metrics            -> HBM[31]
13 Batch_num
14 Matrix_len
15 Row_num
16 Column_num
17 Iteration_num
```

## FSM

`CuperSpmvChisel8` 的状态顺序是：

```text
idle
readPtrAddr/readPtrData
readXAddr/readXData
readMatrixAddr/readMatrixData
writeYAddr/writeYData/writeYResp
writeStatusAddr/writeStatusData/writeStatusResp
writeMetricsAddr/writeMetricsData/writeMetricsResp
idle + doneSticky
```

关键计数：

- `ptrWordsExpected = 8 * (Batch_num + 2)`；
- `xPacketsExpected = ceil(Column_num / 16)`；
- `matrixLenPerChannel[i] = ptr[i]`；
- `matrixBeatsRead[i]` 是实际从 `Matrix_data_i` 收到的 512-bit beat 数。

R response error mask 的 bit 分配：

```text
bit 0    SpElement_list_ptr
bit 1..8 Matrix_data_0..7
bit 9    X
```

B response error mask 当前能可靠反映 `Y_out` 和 `Status` 写入；`Metrics` 写入如果返回错误，
已经发生在 metrics 内容写出之后，只能从 host/XRT 层继续定位。

## Host 校验

host 不再只打印 raw hex。它会检查：

- `Status[1]` / `Metrics[0]` magic；
- ptr/X expected/read 是否相等；
- matrix done mask 是否为 `0xff`；
- R/B response error mask 是否为 0；
- `Metrics[8..15]` 是否等于 host 计算的每路 matrix beat 数。

这些检查通过时 no-check 返回 `rc=0`。`CHECK_Y=1` 会继续做 CPU `A*b` 校验，drain-probe
因为只写 `Y_out[0]=0`，预期返回 `rc=3`。
