# 代码阅读指南

## 入口

- `Makefile`：新增 `CUPER_SPMV_CHISEL8_*` 变量、host build、RTL generate、XO
  package、xclbin link、tmux build 和 run target。
- `chisel/cuper-spmv8/src/main/scala/cuper/spmv/Generate.scala`：
  `GenerateCuperSpmvChisel8` 生成 `CuperSpmvChisel8.sv`。
- `chisel/cuper-spmv8/src/main/scala/cuper/spmv/CuperSpmvChisel8.scala`：
  当前顶层 RTL kernel。

## AXI 接口

`AxiPorts.scala` 定义两类 Bundle：

- `AxiMasterPort(dataBits)`：Vitis RTL kernel 使用的 AXI4 master 信号形状。
- `AxiLiteSlavePort`：AXI-Lite control 寄存器接口。

`CuperSpmvChisel8` 暴露 13 个 AXI master：

```text
m_axi_SpElement_list_ptr: 32-bit
m_axi_Matrix_data_0..7: 512-bit
m_axi_X: 512-bit
m_axi_Y_out: 32-bit
m_axi_Status: 32-bit
m_axi_Metrics: 64-bit
```

这些端口通过 `cfg/connectivity_cuper_spmv_chisel8_u55c.cfg` 分别接到 HBM[0..10]、
HBM[30]、HBM[31]。RTL 内没有 HBM crossbar 或单 PC 转发。

## AXI-Lite ABI

kernel XML 和 Chisel 寄存器表一致：

```text
0x10 SpElement_list_ptr
0x1c Matrix_data_0
0x28 Matrix_data_1
0x34 Matrix_data_2
0x40 Matrix_data_3
0x4c Matrix_data_4
0x58 Matrix_data_5
0x64 Matrix_data_6
0x70 Matrix_data_7
0x7c X
0x88 Y_out
0x94 Status
0xa0 Metrics
0xac Batch_num
0xb4 Matrix_len
0xbc Row_num
0xc4 Column_num
0xcc Iteration_num
```

Vitis host 侧仍按 kernel argument 顺序调用，不直接写这些 offset。

## Probe FSM

当前 `CuperSpmvChisel8` 的 `state` 只完成一条保守、单 outstanding、单 beat 链路：

```text
idle
readPtrAddr/readPtrData
readMatrixAddr/readMatrixData, channel 0..7
readXAddr/readXData
writeYAddr/writeYData/writeYResp
writeStatusAddr/writeStatusData/writeStatusResp, word 0..15
writeMetricsAddr/writeMetricsData/writeMetricsResp, word 0..15
idle + doneSticky
```

后续 full SpMV 可以把这段 FSM 后面的 probe 行为替换成：

```text
ptr drain -> X loader -> Matrix_data_i streams -> StripCoreLane -> StripAccumLane -> Y_out writer
```

## Host

`host/cuper_spmv_chisel_xrt.cpp` 做三件事：

- 复用 `Cuper_common.h` 的 ownerbank8/lane-static real 打包路径准备 ptr 和
  `Matrix_data_0..7`。
- 用 native XRT `xrt::kernel` 调用 `CuperSpmvChisel8`。
- 打印 raw `Status` / `Metrics`，默认 `--skip-y-check`。

当前 host 的正确用法是 entry-probe smoke，不是性能测试：

```bash
make run-cuper-spmv-chisel8-xrt TARGET=hw \
  BITFILE=395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-entryprobe-demo.xclbin \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16
```
