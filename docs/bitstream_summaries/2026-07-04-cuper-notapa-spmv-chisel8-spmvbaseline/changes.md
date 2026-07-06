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

## 2026-07-04 fmul latency / FP 输出观测更新

- 记录服务器侧反馈：`logs/spmv_chisel8_correctness_debug_hw_20260704_192807/`
  未同步到本地，但用户提供的结论表明 ptr/X/matrix decode 和 accumulator accepts
  都是活的；旧 `nonzero_products` 只证明 fmul 输入非零，不证明 fmul 输出非零。
- 修正 fmul valid/data 对齐：
  - `StripCoreLane.fmulLatency` 从 8 改为 7；
  - `HlsFmul32.NUM_STAGE` 从 8 改为 7；
  - 保持 wrapper/module 名
    `CuperSpmvOnly_CoreStrip_fmul_32ns_32ns_32_8_max_dsp_1` 不变；
  - 原因是硬件 wrapper 是 1 拍输入寄存器 + Vivado `floating_point c_latency=6`，
    当前 Verilator path 之前用 `NUM_STAGE=8` 会自遮蔽这一拍偏差。
- fadd latency 保持 12，不改 `HlsFadd32` / `StripAccumLane` 的 12 拍对齐。
- 不扩大 `Status`/`Metrics` buffer，使用末尾保留槽位追加诊断：
  - `Status[56]`：`core_nonzero_out`，在真实 fmul 输出被 accumulator 接收且非零时计数；
  - `Status[57]`：`fadd_nonzero_out`，在 fadd 有效输出写回 partial SRAM 前非零时计数；
  - `Status[58]`：`partial_read_nonzero`，在最终 partial SRAM 读出、进入 tagged 输出前非零时计数；
  - `Status[59..61]`：三段首个非零 sample bits；
  - `Metrics[62]`：`{fadd_nonzero_out[31:0], core_nonzero_out[31:0]}`；
  - `Metrics[63]`：`{partial_read_nonzero[31:0], 32'h0}`。
- Host 新增 `[debug-fp]` 行，打印三段 counters 和首个 sample 的 float 值；no-check
  校验仍只看 magic/count/done/error mask，不把这些 debug counters 作为通过条件。
- `CuperSpmvChisel8` AXI top smoke 已更新 tiny 非零 case 的期望，要求
  `core_nonzero_out`、`fadd_nonzero_out` 和 `partial_read_nonzero` 都非零。

## 2026-07-05 slim/no-debug 同步更新

- 新增/使用 `CUPER_SPMV_CHISEL8_SLIM_DEBUG=1` 生成路径，把重 debug fanout 从同步
  xclbin 中隔离出去，降低时序和布线压力。
- 保持 `CuperSpmvChisel8` kernel 名、AXI-Lite register map、host argument 顺序、
  13 路 `m_axi_*` 端口和 U55C HBM mapping 不变。
- 保持 fmul 7 拍 valid/data 对齐和 fadd 12 拍对齐修复；只关闭重 debug counters 的
  组合/寄存 fanout。
- `Status[40..61]` / `Metrics[47..63]` 槽位 ABI 仍保留，但 slim/no-debug xclbin 中
  debug counters 预期为 0；no-check 判定仍只依赖 magic/count/done/error mask。
- 本地 AXI top smoke 在 slim 模式下验证 tiny case 仍写出 `Y_out[0]=2`，同时
  `valid_slots/nonzero_products/core_nonzero_out/fadd_nonzero_out/partial_read_nonzero`
  保持 0。

## 2026-07-05 owner-step8 phase-1 更新

- 记录 slim/no-debug 同步版的服务器侧 board truth：`CHECK_Y=1` 已通过所有 listed
  `thermal2*` 数据集，但完整 `thermal2` 为 `459.425 ms`，远慢于 strip8 的
  `2.71420 ms`。因此下一步目标从 correctness 转为 ABI-preserving 性能恢复。
- 保持 `CuperSpmvChisel8` kernel 名、AXI-Lite register map、host argument 顺序、
  13 路 `m_axi_*` 端口、U55C HBM mapping、debug slots ABI 和 scalar `Y_out` writer
  不变。
- `CuperSpmvOnly_ChiselDataPath8` 的 matrix issue 从 serial source/owner 改为
  owner-step8：
  - 每个 source 最多预取 1 个 pending matrix beat；
  - 同一个 owner slot 跨最多 8 个 pending source 同周期 issue；
  - 每个 source 使用 1 个 X read port；
  - active source 的对应 Core lane 必须全部 ready 才推进该 owner step，避免同
    group/ping-pong 的 fmul/fadd RAW 顺序被打乱。
- 为避免重新生成旧版 64-read-port RTL，X cache 改为 8 份单读/单写
  `SyncReadMem`。`loadXWrite` 仍每个 `float_v16` packet 分 16 拍写入，但把同一个
  X word 同步写入所有 8 份副本。
- 保留 `StripCoreLane` 和 `StripAccumLane`，不引入旧 scoreboard 分支，也不改变 fmul
  7 拍和 fadd 12 拍对齐。
- Datapath smoke 新增两类覆盖：
  - `matrix-heavy-owner-step`：128 beat/source、低输出量，要求总周期低于旧 serial
    issue 的 read/wait/send 下界；
  - `owner-step-raw-hazard`：同 source/owner lane 重复写同一 row/group，验证 owner
    step stall 仍保持 fadd correctness。
- Makefile 的 Chisel8 Verilator 规则现在会在
  `CUPER_SPMV_CHISEL8_SLIM_DEBUG=1` 时同时传递 RTL `-D` 和 C++ harness
  `-CFLAGS -D...`，避免 slim RTL 被 full-debug harness 误判 debug counters。

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
- 服务器侧 `CHECK_Y=1` 后续显示该版 correctness 仍失败；旧 `477.6 ms` 与 `Y` 错误结论只
  对应旧 UUID `c36bff4e-...`，不能套用到后续 `0f31be8c-...` 或 `765e33c9-...` demo。
- fmul latency / FP-counter 版已完成完整 Vitis hw link，并已再次覆盖同步到同一个
  demo 文件：
  `395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-spmvbaseline-demo.xclbin`。
- Build log：`logs/cuper_spmv_chisel8_hw_20260704_200820.log`。
- UUID：`765e33c9-f3e4-5a25-55ca-ff9bc3a1ddad`。
- SHA256：`550ed459faa550fa5f18947e7c2c5c0bf6624f0f78745540195d0c11b41626d3`。
- DATA/KERNEL/HBM clock：`85 / 500 / 345 MHz`。
- Routed timing 严重不是 150 MHz clean：WNS `-5.008 ns`，TNS `-35127.766 ns`，
  setup failing endpoints `41704`，hold WHS `0.008 ns`。
- 该版本只作为 correctness-debug 候选，等待服务器侧 `CHECK_Y=1`；由于频率大幅
  降到 85 MHz，不作为性能结果，也不晋级标准 bitstream。
- slim/no-debug 版已完成完整 Vitis hw link，并已覆盖同步到同一个 demo 文件：
  `395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-spmvbaseline-demo.xclbin`。
- Build log：`logs/cuper_spmv_chisel8_slimdebug_hw_20260705_165202.log`。
- UUID：`495e02a6-2d7b-8c84-fa0d-e7bfedc10f87`。
- SHA256：`adb1c8630a4edde50560baf60826d86988b5e25e73d1c093da05ea4cc8653946`。
- DATA/KERNEL/HBM clock：`120 / 500 / 450 MHz`。
- Routed timing 仍不是 150 MHz clean：WNS `-1.644 ns`，TNS `-6319.366 ns`，
  setup failing endpoints `15852`，hold WHS `0.009 ns`。
- 服务器侧 `CHECK_Y=1` 已通过所有 listed `thermal2*` 数据集；完整 `thermal2`
  为 `459.425 ms`，相比 strip8 `2.71420 ms` 明显慢，因此该版本作为 correctness
  基线，不作为性能候选，也不晋级标准 bitstream。
- owner-step8 phase-1 候选已通过本地前置验证、完成完整 Vitis hw link，并覆盖同步到
  同一个 demo 槽：
  `395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-spmvbaseline-demo.xclbin`。
- Build log：`logs/cuper_spmv_chisel8_ownerstep8_hw_retry_20260705_235929.log`。
- UUID：`09ac7fd6-26a1-7d3b-ac94-c6ea4cdbb8ea`。
- SHA256：`0cd940e760afe59f2969a3b9de541d6d819a73b8b0173461d6b651443c576745`。
- INFO SHA256：`c393270710938f9571e691e3a57ff1513477fb91cb43384b7d73b1a55f369a80`。
- DATA/KERNEL/HBM clock：`138 / 500 / 450 MHz`。
- Routed timing 仍不是 150 MHz clean：WNS `-0.539 ns`，TNS `-718.710 ns`，
  setup failing endpoints `4761`，hold WHS `0.009 ns`。
- Vitis elapsed：`2h 10m 17s`。
- 当前生成 RTL 保持小体量：
  `verilog/tapa/CuperSpmvOnly_ChiselDataPath8.v` 为 `4768` 行 / `211969` bytes，
  `verilog/chisel/CuperSpmvChisel8.sv` 为 `8064` 行 / `341915` bytes；没有回到旧
  64-read-port / 131 万行 Verilog 爆内存边界。
- 服务器侧 `CHECK_Y=1` 与性能 sweep 待跑；该版本暂不晋级标准 bitstream。

## 未做内容

- 未插入 scoreboard。
- 未修改 host ABI 或 HBM mapping。
- 未更新正式 `source.diff`：owner-step8 phase-1 尚未完成 demo-only 上板 correctness
  与性能验证。
