# 变更说明

## RTL

- 保持 `CuperSpmvChisel8` kernel 名、AXI-Lite register map、host argument 顺序、
  13 路 `m_axi_*` 端口和 U55C HBM mapping 不变。
- 将 entry-probe FSM 替换为 HBM drain-probe FSM：
  读取完整 ptr table，读取完整 X packet 序列，按 `ptr[channel]` drain
  `Matrix_data_0..7`。
- 每个 AXI master 仍使用单 ID、单 outstanding 事务；本版目标是确认地址、长度、
  bank mapping 和 read-response 链路，不追求吞吐。
- `Y_out` 仍只写 `Y_out[0]=0`，用于保持 scalar writer 返回链路。
- `Status` 和 `Metrics` 从 16 word 扩到 64 word，记录 expected/read 计数、
  matrix done mask、R/B response error mask、每路 matrix beat 读数和 first/last
  low64 摘要。

## Host

- `host/cuper_spmv_chisel_xrt.cpp` 的模式名更新为 `cuper-spmv-chisel8-drain-probe`。
- host 在 matrix planning 阶段计算：
  - `ptr_words_expected = 8 * (Batch_num + 2)`；
  - `x_packets_expected = ceil(Column_num / 16)`；
  - `matrix_len_per_hbm[0..7]`。
- 回读后打印 `[probe-check]`、`[matrix-beats-expected]`、`[matrix-beats-read]` 和
  first/last low64 摘要。
- no-check 模式也会在 drain 计数或 response mask 不符时返回 `rc=2`；
  `CHECK_Y=1` 仍保留 SpMV 校验，预期返回 `rc=3`。

## 未做内容

- 未接入 `StripCoreLane` / `StripAccumLane` full SpMV datapath。
- 未插入 scoreboard。
- 已生成新的 drain-probe `.xclbin` 并同步到 `395bitstream/`。本机没有 U55C/XRT
  device；服务器侧用户结果显示 no-check 全 `thermal2` sweep 已通过，
  `--check-y` 抽样按预期失败。
- 未更新正式 `source.diff`：本版虽已通过 drain-probe 上板 no-check，但不计算 SpMV，
  没有 SpMV 正确性或性能收益结论。
