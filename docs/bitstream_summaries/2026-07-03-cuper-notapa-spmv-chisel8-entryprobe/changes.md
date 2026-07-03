# 变更说明

## 新增内容

- 新增独立 Chisel top `CuperSpmvChisel8`，作为 Vitis RTL kernel 暴露 AXI-Lite
  control 和 13 个 AXI master ports。
- 新增 AXI 基础 Bundle：`AxiMasterPort`、`AxiLiteSlavePort`。
- 新增 `GenerateCuperSpmvChisel8` elaboration 入口，生成
  `verilog/chisel/CuperSpmvChisel8.sv`。
- 新增 RTL kernel packaging/link 脚本、kernel XML 和 U55C connectivity cfg。
- 新增 native XRT host `cuper_spmv_chisel_xrt`，复用现有 Cuper ownerbank8
  数据打包逻辑，默认跳过 Y correctness。
- 新增 Makefile targets：
  `cuper-spmv-chisel8-generate`、`cuper-spmv-chisel8-xrt-host`、
  `build-cuper-spmv-chisel8-xo`、`build-cuper-spmv-chisel8-hw`、
  `cuper-spmv-chisel8-hw-tmux`、`run-cuper-spmv-chisel8-xrt`。

## 当前 RTL 行为

当前 FSM 是 entry-probe，不计算 SpMV：

1. 通过 AXI-Lite 接收 13 个 pointer/scalar args。
2. 读 `SpElement_list_ptr[0]`。
3. 依次读 `Matrix_data_0..7[0]`。
4. 读 `X[0]`。
5. 写 `Y_out[0]=0`。
6. 写 raw `Status[0..15]` 和 raw `Metrics[0..15]`。

`Status[1]` 写 magic `0x43535056`，`Metrics[0]` 写 magic
`0x4353504d56384348`。host 按 raw `uint32_t[64]` / `uint64_t[64]` 打印，避免在
Chisel 中做 int-to-double 转换。

## 未做内容

- 未接入 `StripCoreLane -> StripAccumLane` full datapath。
- 未实现完整 ptr table drain、X packet drain 或 matrix stream drain。
- 未插入 scoreboard。
- 未上板验证 `Status`/`Metrics`，也未做 Y correctness 或性能结论。
- 未更新正式 `source.diff`：这版只是 entry-probe bring-up，未完成 demo-only
  上板测试，也没有性能提升结论。
