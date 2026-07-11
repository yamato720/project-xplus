# changes

## 新增隔离子项目

- 新增 `DLC/Cuper-callipepla-pcg/`，包含独立 host、kernel、connectivity 和 U55C
  build/link/run 脚本。
- 根 `Makefile` 新增 `cuper-tapa-pcg-callipepla-*` targets，默认使用独立 build
  dir，不覆盖旧 `DLC/Cuper` 或 `DLC/Cuper-jacobi-iteration` 构建产物。
- 新环境变量采用 PCG-facing 命名：
  `CUPER_CALLIPEPLA_HBM_CHANNELS`、`CUPER_CALLIPEPLA_SPMV_STRIP_PADDING`、
  `CUPER_CALLIPEPLA_SPMV_ACC_WINDOW`。脚本内部映射到沿用的 `JACOBI_*` 编译宏。
- 2026-07-08 追加 `CUPER_CALLIPEPLA_TRACE_LIGHT=1` 编译开关。trace-light 版不新增
  HBM 端口，不改变 `CuperPcgCallipepla` ABI，只把早期进度写入既有 `Status`
  BO 的扩展槽 `Status[16..63]`。
- 2026-07-09 追加 `CUPER_CALLIPEPLA_PROBE_MODE=entry|cmd_drain|loader_drain`
  编译开关，并与 `CUPER_CALLIPEPLA_TRACE_LIGHT` 互斥。probe 版保持 kernel 名、
  host 参数顺序、AXI-Lite offsets 和 HBM mapping 不变，用于先定位 entry/Status
  mmap/controller fanout/loader HBM read 边界。
- `loader_drain` 额外支持 `CUPER_CALLIPEPLA_LOADER_DRAIN_LEVEL=1|2|3`：
  level 1 恢复真实 ptr loader 并 drain `PE_Param`；level 2 再恢复真实 vector loader
  并 drain `Vector_X`；level 3 只让 matrix loader ch0/ch15 读取 HBM，其它 matrix
  command 仍 drain。

## Kernel ABI 和 HBM 映射

- 顶层 kernel 为 `CuperPcgCallipepla`。
- `Matrix_data_0..15` 固定映射到 HBM[0..15]。
- `SpElement_list_ptr` 映射到 HBM[16]。
- `X_0/X_1` 映射到 HBM[17]/HBM[18]，`P_0/P_1` 映射到 HBM[19]/HBM[20]，
  `AP` 映射到 HBM[21]，`R_0/R_1` 映射到 HBM[22]/HBM[23]。
- `M_inv`、`Residuals`、`Status`、`Metrics` 分别映射到 HBM[24]、HBM[25]、
  HBM[30]、HBM[31]。
- 为兼容当前 TAPA front-end，顶层 C++ 签名使用显式 `X_0/X_1`、`P_0/P_1`、
  `R_0/R_1` 端口；这对应计划里的 `X[2]`、`P[2]`、`R[2]` 物理 bank ABI。

## Strip16 SpMV service

- 默认启用 16 路 HBM、strip padding、accumulator window=10。
- Host 在 strip 模式下把 `SpElement_list_ptr` BO 打包为：
  前 16 个 word 是每路 `Matrix_len_per_hbm`，后续是 boundary-major 的 per-HBM
  batch boundary。
- Kernel 新增 service-mode strip task：
  `SpmvService_StripPtrLoader`、`SpmvService_MatrixLoaderStrip`、
  `SpmvService_CoreStrip`。
- 非 strip service path 保留，可通过 `CUPER_CALLIPEPLA_SPMV_STRIP_PADDING=0`
  显式回退。

## PCG task graph

- 新增 controller、stage timer、vector loader、SpMV output checker/sort、
  vector phase worker 等 Callipepla-style 分段文件。
- 初始化轮按 `rp=-1` 语义执行：host 初始化 `X_0=x0`、`P_0=x0`、`R_0=b`；
  kernel 先算 `AP=A*x0`，再得到 `R=b-AP`、`Z=M_inv*R`、`P=Z`。
- 新 ABI 没有独立 `Z` port；`apply_m_inv` 阶段临时把 `Z` 写入下一 `P` bank，
  `update_p` 阶段再覆盖为新的 `P`。
- SpMV 边界仍是 FP32 packed `float_v16`；PCG 状态和 dot/update 保持 FP64
  `double_v8`。

## 2026-07-10 command/result 顺序修复

- `pcg_callipepla_controller.hpp` 不再对配对的 vector command/result 使用分离的
  blocking `write()` / `read()`。新的事务循环包含 `send-command`、`wait-result`、
  `done` 三态，只在 `try_write()` 成功后递增 command counter 并进入等待态，只在
  `try_read()` 成功后递增 result counter 并结束事务。
- 状态循环显式设置 `loop_flatten off` 和 `pipeline off`，避免 HLS 把 result read
  提前到 command write 之前。相同事务模式覆盖 `InitSpmv`、`InitZp`、`IterDot`、
  `UpdateX`、`UpdateR`、`ApplyMInvDot`、`UpdateP`。
- `cmd_drain` checkpoint 保持原编号：`61/91` 只在 command 真正接受后写入，
  `70/100` 表示已进入 result 等待态，`71/101` 只在 result 真正读到后写入；
  `20..51` 和 `110+` 语义不变。
- 单向 stop command 仍使用 blocking write；它不等待 result，不属于本次配对事务。
- 顶层签名、参数顺序、AXI-Lite offsets、HBM mapping、probe mode 名称和真实
  Tau/dimension 判断均未改变。
- 修复后的 thin-status `cmd_drain` 曾生成 100 MHz timing-clean xclbin；该历史 artifact
  已由下面的独立握手 monitor 版覆盖，不再对应同步槽中的当前文件。

## 2026-07-10 独立握手 monitor

- 顺序修复版上板稳定显示 `Status[52]=60`，但生成 RTL 中事务内部的 `61/70/71`
  Status 写已被 HLS 合并或消除。因此 `60` 只能证明 controller 到达首条 vector
  transaction，不能证明 `try_write()` 没有接受 command。
- `cmd_drain` 新增 controller/ack 两条窄 `PcgCallipeplaProbeEvent` stream。controller
  直接采样 `Vector_Command_out.full()`，分别记录 command attempt、full、accepted、
  wait-result 和 result-received；fake-ack 分别记录 command-received、result-sent 和
  stop。
- 新增 `PcgCallipepla_Probe_HandshakeMonitor`，它是 `cmd_drain` 下唯一的 Status mmap
  writer。controller 不再直接写细粒度 Status，因此握手事件不会再被 controller 的
  AXI 写调度合并掉。monitor 每 `2^22` 个循环更新 heartbeat，并在 controller/ack
  都完成后写 `Status[52]=99` 和标准完成状态。
- `Status[50..63]` 改为 handshake 语义：magic、mode、last event、monitor heartbeat、
  command attempts/full/accepted、ack command/result、controller result、当前 phase、
  controller state、ack heartbeat 和 packed flags/drop counters。host 对 mode 2 解码
  这些名字；entry/loader mode 保持旧输出标签。
- `cmd_drain` fake-ack 删除按 iteration 做的 FP64 division，改为固定安全结果：
  InitZp 返回 `rz=rr=1`，IterDot 返回 `p_ap=1`，ApplyMInvDot 返回 `rz=rr=0.5`。
  生成 HLS 报告显示该 task 非流水、0 DSP、无 divider。
- 顶层 kernel 名、参数顺序、AXI-Lite offsets、HBM mapping、probe mode 名称和真实
  full graph controller 事务均未改变；本轮仍是定位 artifact，不更新正式
  `source.diff`。
- 独立 monitor 版已完成 `impl Complete` 并覆盖
  `395bitstream/cuper-tapa-pcg-fpga-u55c-20260710-demo.xclbin`。最终 DATA/KERNEL/HBM
  clock 为 `138/500/450 MHz`；请求 500 MHz DATA 下 routed timing 未收敛，WNS
  `-5.200 ns`、TNS `-26783.914 ns`，因此该文件只作为待上板的 debug artifact。

## 2026-07-11 loader-drain level 1 monitor

- mode 3 复用 mode 2 的 controller/fake-ack 事件握手；controller 在
  `loader_drain` 下也不再持有 Status mmap。新增
  `PcgCallipepla_Probe_LoaderLevel1Monitor`，它是 mode 3 唯一 Status writer。
- 真实 `SpmvService_StripPtrLoader` 增加启动长度读取、command 接收、boundary 读取
  进度、每轮完成和 stop 事件。进度事件全部使用 `try_write()`，最终 stop 事件使用
  blocking `write()`；ptr command 计数包含 stop，HBM word 计数包含启动时的 16 路
  matrix length。
- `PcgCallipepla_Probe_PEParamDrain` 真实消费 `Batch/Row/Column` 和
  `(Batch_num+1)*16` 个 boundary word，每轮报告 round done，stop 事件可靠送达。
- 16 路 matrix drain 继续消费各自 command 和 `Matrix_Len_Stream`，但 level 1 的
  `Matrix_data` read/write request enable 在生成 RTL 中全部 tied low；vector loader、
  SpMV core、accumulator、checker 和 sort tree 仍未恢复。
- mode 3 的 `Status[50..63]` 固定为：magic/mode、controller/ack last event、monitor
  heartbeat、六个 command/result counter、loader last event、ptr command + PE round
  packed counter、ptr HBM word count，以及 full/write/controller/ack/ptr/PE done flags 和
  controller/ack/loader 三类 event-drop counter。mode 2 布局不变。
- mode 3 controller/ack 事件 FIFO 单独扩到 128/64，避免软件仿真中短消息 burst 丢失
  stop-accepted 进度事件；mode 2 仍保持原 32/16 深度。
- Host 新增 mode 3 字段解码；顶层 kernel 名、参数顺序、AXI-Lite offsets、HBM mapping、
  Tau/dimension 判断和 probe mode 名称均不变。
- 100 MHz link 已完成并同步为
  `395bitstream/cuper-tapa-pcg-fpga-u55c-20260711-demo.xclbin`。UUID 为
  `fdbc2e10-20ea-8e78-6b3c-72a01803cde1`，DATA/KERNEL/HBM 为
  `100/500/450 MHz`；routed WNS `0.002 ns`、TNS `0`、WHS `0.009 ns`、THS `0`。
  同主线上一 `20260710-demo` 文件已从同步目录删除，历史握手板测记录保留。
- 该版本仍是 fake-ack debug artifact，等待服务器侧 loader level-1 demo-only 上板；
  不替换标准 bitstream，不更新正式 `source.diff`。

## Host

- 默认加载 Project-XPlus CSR dataset，并直接打包原始矩阵 `A`，不拆 `A=D+R`。
- Host 构造 Jacobi inverse，遇到非有限 inverse 直接报错。
- 支持 `--bitstream`、`--tau`、`--max-iters`、`--diff-tol`、
  `--kernel-timeout-sec`、`--live-status-poll-sec`。
- XRT 路径使用 `xrt::ip` 原生寄存器启动，支持 timeout 前同步
  `Status/Metrics/Residuals`。
- live poll 继续输出 `Status[8..15]`。如果 `Status[50]` 出现 trace magic，
  host 会额外打印 `Status[16..63]`；timeout 前最后一次同步也输出完整 debug
  snapshot。

## Trace-light 调试版

- `Status[8]` 的第一次 live 更新已移到 controller 入口最前面，早于 stage timer
  事件和 SpMV/vector command。
- trace source 固定覆盖 controller、ptr loader、vector loader、matrix loader
  ch0/ch15、core0/core15、acc0/acc15、checker0/checker7、sort tree 和 vector
  phases。
- 业务 task 只用 `try_write` 发 debug event，trace monitor 是 `Status` mmap 的唯一
  trace 写入者，避免 debug stream 反压数据通路。
- `Status[16..31]` 记录各 source 最后事件，`Status[32..47]` 记录事件计数，
  `Status[48]` 是 heartbeat，`Status[49]` 是 drop/异常计数，`Status[50]` 是
  debug magic。

## Hollow-probe 调试版

- 新增 `pcg_callipepla_probe.hpp`，提供 entry writeback、mmap port touch、command
  drain、fake vector ack、PE/vector/matrix drain helper。
- `entry` 模式只保留顶层 mmap touch 和一个 writer task：写 `Status[0..15]`、
  `Status[50..63]`、`Metrics`、`Residuals` 后返回。同步的 2026-07-09 demo xclbin
  就是该模式。
- `cmd_drain` 模式保留真实 controller 和 stage timer；ptr/matrix/vector command
  consumers 全部替换为 drain/fake ack，用于验证 controller 是否能完成 init/stop
  流程。当前同步的 2026-07-09 demo xclbin 已切到该模式。
- 2026-07-10 thin-status 同步版在 `cmd_drain` controller 路径拆出细粒度 checkpoint；
  当前版改为入口只写一次 probe header/规模信息，运行中 checkpoint
  只写 `Status[52]`，正常完成时再统一写最终 `Status[50..63]` counters。这样可区分
  多槽 Status mmap 写回本身、Stage_Event/stage timer 和条件判断路径。
- `Status[52]` 可停在 `10/11` total stage begin、`12` 条件判断前、`13`
  `Row_num/Column_num/Max_iters` 判断后、`14` `Tau <= 0` / `Tau != Tau` 判断后、
  `15` breakdown 分支、`20/21` init_spmv stage begin、`30/31` ptr command、
  `40/41` matrix command fanout、`50/51` SpMV vector command、`60/61` init-spmv
  vector fake command、`70/71` fake ack read、`80/81` init_spmv stage end、
  `90/91` init_zp command、`100/101` init_zp result read，`110+` 覆盖 stop/finalization。
  timeout 判定以 `Status[52]` 为准；运行中 `detail0/detail1` 可能仍是 header
  初始化或上一次最终写回的 stale 值。
- `loader_drain` 模式逐档恢复真实 ptr/vector/matrix loader，但 core/acc/checker/sort
  仍保持 drain/stop 替身，用于定位卡死是否来自 HBM read/loader 层。
- Probe 状态约定：`Status[50]=0x43505242`，`Status[51]=mode_id`，其中
  `1/2/3` 对应 `entry/cmd_drain/loader_drain`；最终完成时 `Status[52..63]` 记录
  probe stage、SpMV command 轮数、matrix command 计数、vector command/ack 计数和
  基础规模参数。thin-status 的运行中快照只保证 `Status[52]` 是最新 checkpoint。
