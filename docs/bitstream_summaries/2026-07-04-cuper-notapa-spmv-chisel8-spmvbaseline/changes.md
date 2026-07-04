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

## 2026-07-04 correctness-first debug 更新

- 修正 Chisel accumulator latency：
  - `HlsFadd32` 的 `NUM_STAGE` 从 13 改为 12；
  - `StripAccumLane.faddLatency` 从 13 改为 12；
  - 该值对齐已有 TAPA/RTL owner-lane accumulator wrapper 的
    `FADD_PIPE_LATENCY=12`，避免 SyncReadMem accumulator read/write 与 fadd 返回
    对不齐。
- 增加 datapath 可观测性，不改变既有 `Status[0..39]` / `Metrics[0..46]`：
  - `Status[40]` valid slots；
  - `Status[41]` nonzero X reads；
  - `Status[42]` nonzero products；
  - `Status[43]` accumulator accepts；
  - `Status[44]` nonzero tagged writes；
  - `Status[45]` nonzero scalar Y writes；
  - `Status[46..47]` first nonzero scalar Y address/data；
  - `Status[48..55]` datapath/writer first nonzero tagged packet/pair/ping/pong。
  - `Metrics[47..61]` 记录对应 64-bit counters、first tagged/Y sample、raw stall 和
    writer backpressure。
- 新增 `CuperSpmvOnly_ChiselDataPath8` local packed smoke：
  - 使用 8-HBM lane-static real/batch packing；
  - 修正 PE boundary stream 为 `boundary0 + each batch end`；
  - 覆盖 single batch、multi-owner/pair、padding/reuse 和跨 8192-column multi-batch。
- 新增 `CuperSpmvChisel8` AXI top smoke：
  - 用同步 AXI single-beat memory model 驱动 ptr/X/matrix 读和 Y/Status/Metrics 写；
  - 覆盖 tiny matrix 从 AXI loaders 到 scalar `Y_out` 写回；
  - 验证新增 debug counters 非零。

## Host

- `host/cuper_spmv_chisel_xrt.cpp` 模式名改为
  `cuper-spmv-chisel8-spmv-baseline`。
- no-check 保留 magic、ptr/X/matrix、done mask、R/B error mask 校验，并新增
  tagged/scalar writer 计数和 datapath/writer done 校验。
- `--check-y` 失败时打印首批 mismatch、最大 diff 位置、debug datapath/tagged/Y
  摘要和首批非零 Y；no-check 仍只依赖 magic/count/done/error mask。
- `--check-y` 仍返回 `rc=3` 表示数值校验失败；对下一版 demo，目标是上板通过。

## 构建结果

- low-memory serial issue 旧版已完成完整 Vitis hw link，并曾同步 demo：
  `395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-spmvbaseline-demo.xclbin`。
- Build log：`logs/cuper_spmv_chisel8_hw_20260704_014807.log`。
- UUID：`c36bff4e-7efc-805f-b6a0-ccfd1677cda0`。
- SHA256：`5da1df03f85077185ad1ab787e95e3f71cd064308b739ddfd4f711e209fd9907`。
- DATA/KERNEL/HBM clock：`119 / 500 / 450 MHz`。
- Routed timing 不是 150 MHz clean：WNS `-1.719 ns`，TNS `-3059.849 ns`，
  setup failing endpoints `10198`，hold WHS `0.009 ns`。该 demo 用于 correctness
  上板验证，不作为性能结论。
- 服务器侧 no-check 完整 `thermal2` 已能返回，用户提供完整点耗时 `477.6 ms`；
  但 `CHECK_Y=1` 失败，`Y` mostly zeros/错误。该结果不是有效 SpMV 性能成绩。
- correctness-debug 新版已完成本地前置验证、XO packaging 和完整 Vitis hw link，并已
  覆盖同步到同一个 demo 文件：
  `395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-spmvbaseline-demo.xclbin`。
- Build log：`logs/cuper_spmv_chisel8_correctness_debug_hw_20260704_143204.log`。
- UUID：`0f31be8c-e77e-4e25-d85a-1498693befbb`。
- SHA256：`1ea8f0051cad2c3a81ab50f9e66a0d8fa982a55310e82aa054bd753bb658ab8e`。
- DATA/KERNEL/HBM clock：`139 / 500 / 450 MHz`。
- Routed timing 仍不是 150 MHz clean：WNS `-0.478 ns`，TNS `-173.688 ns`，
  setup failing endpoints `1448`，hold WHS `0.001 ns`。
- 服务器侧 `CHECK_Y=1` correctness sweep 尚未执行；旧 `477.6 ms` 与 `Y` 错误结论只
  对应旧 UUID `c36bff4e-...`，不能套用到当前 `0f31be8c-...` demo。

## 未做内容

- 未插入 scoreboard。
- 未修改 host ABI 或 HBM mapping。
- 未更新正式 `source.diff`：当前 correctness-debug demo 尚未完成 demo-only 上板
  `CHECK_Y=1`。
