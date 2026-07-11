# code reading guide

## 入口

- `DLC/Cuper-callipepla-pcg/kernels/Cuper.cpp` 只 include 顶层 task graph。
- `DLC/Cuper-callipepla-pcg/include/Cuper.h` 定义编译常量、vector typedef 和
  `CuperPcgCallipepla` 顶层声明，也定义 `CUPER_CALLIPEPLA_PROBE_MODE_ID` /
  `CUPER_CALLIPEPLA_PROBE_ENABLED`。
- `DLC/Cuper-callipepla-pcg/kernels/detail/pcg_callipepla_top_graphs.hpp` 是实际
  TAPA task graph。
- `DLC/Cuper-callipepla-pcg/kernels/detail/pcg_callipepla_probe.hpp` 是 2026-07-09
  hollow-probe helper，包含 entry writer、mmap touch、command drain、fake vector ack
  和 loader drain task。

## Host 到 Kernel 的数据

- `host/main.cpp::build_tapa_matrix()` 从 Project-XPlus CSR dataset 构造 Cuper
  `SpElement` 列表。
- strip 模式下，host 调用
  `Create_SpElement_list_for_all_channels_strip_hbm_padding()` 生成 16 路 dense
  `Matrix_data` 和 per-HBM boundary。
- `SpElement_list_ptr` BO 的前 16 个 `INDEX_TYPE` 是每路 matrix beat 总数；
  后续按 `boundary * 16 + channel` 排列每个 batch boundary。
- X/R/P/M_inv 都按 `double_v8` 打包；AP 是 SpMV 输出边界的 `float_v16` 缓冲。

## SpMV Service

- `spmv_service_tasks.hpp` 保留原非 strip service path。
- 默认 strip path 由 `SpmvService_StripPtrLoader`、`SpmvService_MatrixLoaderStrip`
  和 `SpmvService_CoreStrip` 组成。
- `SpmvService_StripPtrLoader` 每收到一条 run command，就把 16 路 matrix length
  发给 matrix loader，并把 per-HBM boundary group 串给 core chain。
- `SpmvService_CoreStrip` 的每一级 core 只取自己 channel 的 boundary，其余 boundary
  继续向后转发。
- Accumulator、checker、sort tree 仍复用 Cuper 原始输出规整逻辑。

## PCG 阶段

- `pcg_callipepla_controller.hpp` 是标量 controller：发 SpMV/vector command，
  计算 alpha/beta，维护 convergence/status/metrics。
- controller 中七条有返回值的 vector phase 统一经过
  `PcgCallipepla_ExchangeVectorCommand()`：state `0` 只尝试 `try_write(command)`，
  command 接受后 state 才变为 `1`；state `1` 只尝试 `try_read(result)`，读到后变为
  state `2`。该循环禁止 pipeline/flatten，读代码时不要再把 command 和 result 当成
  可被 HLS 同周期重排的两个独立 blocking 调用。
- `pcg_callipepla_vector_loader.hpp` 把当前 X 或 P bank 从 FP64 `double_v8`
  转成 SpMV service 需要的 FP32 `float_v16`。
- `pcg_callipepla_vector_phases.hpp` 执行 init residual、init z/p、pAp dot、
  update x、update r、apply m_inv、update p 等向量阶段。
- `pcg_callipepla_stage_timer.hpp` 记录阶段 cycle。
- `pcg_callipepla_spmv_output.hpp` 包含 tail drain、checker 和 sort tree。

## ABI 备注

计划里写作 `X[2]`、`P[2]`、`R[2]`。当前 TAPA top 使用显式 `X_0/X_1`、
`P_0/P_1`、`R_0/R_1` 端口，因为 `tapacc` 不接受在 `.invoke(...)` 参数里写
`X[0]` 这类 `mmaps` index 表达式。物理 bank ABI、argument order 和 HBM mapping
仍等价于两个 bank。

`CUPER_CALLIPEPLA_PROBE_MODE=entry|cmd_drain|loader_drain` 不改变顶层函数签名、
host argument order、AXI-Lite offsets 或 HBM mapping。probe 只在 top graph 内部
替换后级 task，因此同一个 host/XRT register 写入路径可以直接加载 probe xclbin。

三档 probe：

- `entry`：只 touch 所有 mmap 端口并写 Status/Metrics/Residuals 后返回。旧同步
  UUID `7ab50484-4649-ffd5-dd5c-0925c61a9504` 已通过完整 `thermal2` 入口/mmap
  上板验证。
- `cmd_drain`：保留 controller 和 stage timer，ptr/matrix/vector command consumer
  全部换成 drain/fake ack。最新源码不再让 controller 直接写 Status；controller 和
  fake-ack 分别向事件 stream 发握手事件，独立
  `PcgCallipepla_Probe_HandshakeMonitor` 是 mode 2 唯一 Status writer。controller 在
  每次 command attempt 前直接读取 `Vector_Command_out.full()`，因此 timeout 时可区分
  FIFO full、command 已接受、ack 未读、result 未写或 controller 未读。
- `loader_drain`：逐档恢复真实 ptr/vector/matrix loader；core/acc/checker/sort 仍不接。
  level 1 使用真实 `SpmvService_StripPtrLoader`、16 路 matrix-length fanout 和
  `PcgCallipepla_Probe_PEParamDrain`。16 路 matrix drain 只消费 command/length，
  `Matrix_data` request enable 为 0。controller、fake-ack、ptr loader 和 PE drain 分别
  发事件，`PcgCallipepla_Probe_LoaderLevel1Monitor` 是唯一 Status writer。

## Metrics

- `Status[0..7]` 是正式完成状态：status、iteration、final bank、HBM channel 数、
  vector packet count、matrix len。
- `Status[8..15]` 是 live progress snapshot。
- Probe magic 是 `Status[50]=0x43505242`；`Status[51]` 是 mode id，
  `1/2/3` 对应 `entry/cmd_drain/loader_drain`。mode 2 的握手布局为：`52` last event、
  `53` monitor heartbeat、`54` command attempts、`55` command full observations、
  `56` command accepted、`57` ack command received、`58` ack result sent、`59`
  controller result received、`60` current phase、`61` controller transaction state、
  `62` ack heartbeat、`63` flags/drop counters。正常完成时 `52=99`。
- mode 3 的 `Status[50..63]` 为：`50/51` magic/mode，`52` controller/ack last event，
  `53` monitor heartbeat，`54..59` command attempts/full/accepted、ack command/result、
  controller result，`60` loader last event，`61[15:0]` ptr commands（含 stop），
  `61[31:16]` PE rounds，`62` ptr HBM words，`63[5:0]` full/write/controller/ack/ptr/PE
  flags，`63[15:8]` controller drops，`63[23:16]` ack drops，`63[31:24]` loader drops。
  正常完成时 `52=99`，所有 done flag 置位。
- `Metrics[0..4]` 是最终 `rz/rr/p_ap/alpha/beta`。
- `Metrics[5..15]` 是 packet/work counters。
- `Metrics[16..31]` 是 stage cycle 和 vector work 拆分。
