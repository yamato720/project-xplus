# 变更说明

## RTL

- 保持 `CuperSpmvChisel8` kernel 名、AXI-Lite register map、host argument 顺序、
  13 路 `m_axi_*` 端口和 U55C HBM mapping 不变。
- 用 full SpMV baseline 替换 drain-probe FSM：
  - `ptr[0..7]` 读取每路 matrix beat 数；
  - `ptr[8..]` boundary-major table 经内部 FIFO 送入 PE 参数流；
  - X loader 读取 `ceil(Column_num/16)` 个 512-bit packets；
  - 8 路 Matrix loader 各自按 `ptr[channel]` 顺序读取 HBM；
  - 复用 `CuperSpmvOnly_ChiselDataPath8`、`StripCoreLane` 和 `StripAccumLane`；
  - tagged 输出按 `(packet << 4) + (pair << 1) + {0,1}` 写回 scalar `Y_out`。
- `Status[31]` 从 drain magic `0x44525042` 改为 SpMV baseline magic `0x53504d56`。
- 新增 tagged pair、scalar write、datapath done、writer done 和 writer response
  计数到 `Status[32..39]` / `Metrics[43..46]`。
- 修复 8 路 matrix loader 同周期完成时 `matrixDoneMask` 多次赋值互相覆盖的问题：
  现在先汇总本周期完成 bit，再一次性 OR 回 mask。
- 降低 Chisel datapath 的综合内存压力：
  - 原实现用 `Reg(Vec(8192, UInt(32.W)))` 做 X cache，并在同周期提供 16 写和
    64 个组合读端口，导致生成 RTL 超过 131 万行，Vivado OOC synth 后出现连续
    2.684GB 大块内存申请；
  - 现在 X cache 改为单读/单写 `SyncReadMem`；
  - `loadX` 将每个 `float_v16` packet 分 16 拍写入 SRAM；
  - `consumeBatch` 每次读取一路 matrix beat，再按 8 个 owner slot 串行读 X 并送入
    对应 Core lane；
  - 8-HBM stream ABI、8x8 Core/Accumulator bank 结构、tagged Y 输出格式不变。
- standalone XO packaging 现在会随 kernel 打包已有 fmul/fadd wrapper RTL，并在
  Vivado package project 中生成对应 floating_point IP。

## Host

- `host/cuper_spmv_chisel_xrt.cpp` 模式名改为
  `cuper-spmv-chisel8-spmv-baseline`。
- no-check 保留 magic、ptr/X/matrix、done mask、R/B error mask 校验，并新增
  tagged/scalar writer 计数和 datapath/writer done 校验。
- `--check-y` 仍返回 `rc=3` 表示数值校验失败；对本版 baseline，目标是上板通过。

## 构建结果

- low-memory serial issue 版已完成完整 Vitis hw link 并同步 demo：
  `395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-spmvbaseline-demo.xclbin`。
- Build log：`logs/cuper_spmv_chisel8_hw_20260704_014807.log`。
- UUID：`c36bff4e-7efc-805f-b6a0-ccfd1677cda0`。
- SHA256：`5da1df03f85077185ad1ab787e95e3f71cd064308b739ddfd4f711e209fd9907`。
- DATA/KERNEL/HBM clock：`119 / 500 / 450 MHz`。
- Routed timing 不是 150 MHz clean：WNS `-1.719 ns`，TNS `-3059.849 ns`，
  setup failing endpoints `10198`，hold WHS `0.009 ns`。该 demo 用于 correctness
  上板验证，不作为性能结论。

## 未做内容

- 未插入 scoreboard。
- 未修改 host ABI 或 HBM mapping。
- 未更新正式 `source.diff`：low-memory 版尚未完成 demo-only 上板 correctness，且
  当前 xclbin 不是 150 MHz timing-clean。
