# 变更记录

## 目标

把 `DLC/Cuper` 同级的 Jacobi iteration demo 登记为第五条 Cuper 主线
`cuper-tapa-jacobi`，并保留当前 software/TAPA simulation 的测试口径。

## 已完成

- 新增独立目录 `DLC/Cuper-jacobi-iteration/`，避免混入既有 `DLC/Cuper` 的
  SpMV/PCG 主线。
- 顶层命名为 `CuperJacobiIteration`，不再沿用 PCG 命名。
- host 侧把矩阵拆成 `A = D + R`，并生成 `Diag_inv`。
- kernel 侧复用 Cuper single SpMV service；`Jacobi_Vector_Loader` 读取单个 `X`
  buffer 时取负，让 SpMV service 输出 `-R*x_old`。
- update path 消费 `-R*x_old`，读取 `B/Diag_inv`，写回
  `x_next=(b+(-R*x_old))*diag_inv`。
- 写回端完成整轮后才反馈下一轮 token，因此当前实现取消 `X0/X1` 双缓冲，改成单个
  `X` buffer 原地更新；`Status[1]` 固定为 `0`。
- 新增 Jacobi stage timing，host 输出 `[jacobi-timing-work]`、
  `[jacobi-stage-cycles]` 和 `[jacobi-stage-ms]`。
- 新增 deadlock debug ABI：`JACOBI_DEADLOCK_DEBUG=1` 时启用 `Debug` buffer，
  用于记录轮次、pack writer、HBM writer 等阶段的进度/等待位置。
- 修复 `SpmvService_DestroyFloatV16` 的链尾退出竞态：旧逻辑看到尾端 X stream
  暂时为空就可能先吃 stop 退出，导致后续 Core15 转发的残余 X 包无人消费。现在按
  `ceil(Column_num / 16) * Max_iters` 精确 drain 完链尾 X 包后才允许 stop 结束。
- 根 `Makefile` 接入 `cuper-jacobi-*` 转发 target。
- 文档口径从四条 Cuper 主线扩展为五条，并给 `cuper-tapa-jacobi` 保留独立 demo 槽。
- 生成并同步 deadlock-debug 硬件 demo artifact：
  `395bitstream/cuper-tapa-jacobi-u55c-20260611-demo.xclbin`。
- 2026-06-12 重新生成包含链尾 drain 修复的 deadlock-debug 硬件 bitstream：
  `cuper-tapa-jacobi-u55c-20260612-tail-drain-debug-build/CuperJacobiIteration.xclbin`。
- 已把 2026-06-12 tail-drain 修复版同步进 Jacobi demo 槽：
  `395bitstream/cuper-tapa-jacobi-u55c-20260612-demo.xclbin`。
- 2026-06-12 继续排查 tail-drain 修复版上板后 `Finish` 不返回问题，按
  `finish_nonreturn_monitoring_points.md` 的优先级去掉最可疑的隐式退出点：
  `Jacobi_UpdatePairCompute[0..7]` 不再使用 `tapa::detach` 无限循环，改为由
  `JacobiFrame` 显式驱动，收到 stop frame 后 return。源码、debug ABI XO 和完整
  xclbin 构建均已验证。
- 2026-06-13 已生成并同步 finite-pair debug 硬件 demo artifact：
  `395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin`。这版包含 tail-drain
  修复和 finite pair compute stop-frame 修复，覆盖同主线 2026-06-12 tail-drain-only
  demo 槽。
- 2026-06-13 复测上一版同名 finite-pair demo 后，`thermal2_n16 MAX_ITERS=1`
  仍卡在 `tapa::invoke -> Finish()`；probe 显示 CU 已 `IDLE`、firewall GOOD，
  同时存在 `[CuperJacobiIter]` D 状态线程。为继续定位，host 改为拆开
  `WriteToDevice/Exec/ReadFromDevice/Finish`，在 `Finish()` 前先打印
  Status/Metrics/Debug 快照。
- 修复 `Batch_num==0` 空 R 路径：`Jacobi_Vector_Loader` 在空 R 时不再读取或写出
  X 包，`SpmvService_DestroyFloatV16` 在空 R 时期望链尾 X 包数为 0，避免
  `thermal2_n16` 这种 diagonal-only case 在 Vector_X drain 协议上留下不闭合包。
- 2026-06-13 已生成并同步 pre-Finish/empty-R debug 硬件 demo artifact：
  `395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin`。这版覆盖上一版同名
  finite-pair demo 槽。
- 2026-06-13 服务器侧复测当前 pre-Finish/empty-R demo 后，`thermal2_n16` 和
  `thermal2_n1024` 的 `MAX_ITERS=1` 均在 120s timeout，host 仍停在
  `[tapa-invoke] after ReadFromDevice before Finish`。这次 `ReadFromDevice()` 前置
  dump 生效，但 Status/Metrics/Debug 全 0；probe 期间 CU 为 `IDLE`，firewall
  `GOOD`。这说明问题比后端 update/drain 更靠前，优先看 kernel 入口 task、HBM[24]
  mmap 写回路径和 TAPA/FRT `Finish()` 清理。
- 当前源码已加入 debug-only 入口 mmap probe：`Jacobi_DebugMonitor` 在入口阻塞写
  `Debug[0]` 和 `Debug[48..51]` 并等待 write response；`Jacobi_RoundDispatcher`
  在入口写 `Status[8..11]`、`Metrics[8..11]`。host 会在 pre-Finish 和正常返回后
  打印这些槽位。`thermal2_n16` / `thermal2_n1024` 的 debug ABI software/TAPA
  simulation 均已通过，并重新生成 `cuper-jacobi-iteration-build/CuperJacobiIteration.xo`。
- 2026-06-13 已生成并同步 entry mmap probe debug 硬件 demo artifact：
  `395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin`。这版覆盖上一版同名
  pre-Finish/empty-R demo 槽，但仍未晋级标准。
- 2026-06-13 服务器侧复测当前 entry mmap probe demo 后，`thermal2_n16` 与
  `thermal2_n1024` 的 `MAX_ITERS=1` 均为 120s timeout，host 仍停在
  `[tapa-invoke] after ReadFromDevice before Finish`，Status[8..11]、
  Metrics[8..11]、Debug[48..51] 入口 probe 全 0。当前证据不能直接定性为 PL
  dataflow 死锁，下一步先拆 host/runtime/m_axi 写回边界。
- 新增 debug-only `CuperJacobiMmapProbeOnly` micro top：只写 Status/Metrics/Debug
  固定槽位并等待 write response 后返回，不接入完整 Jacobi dataflow。
- 新增 native XRT debug runner `cuper_jacobi_mmap_probe_xrt`，使用 sentinel 初始化
  Status/Metrics/Debug，并在 wait 前后主动 sync BO 打印快照。
- 新增 same-bank 与 split-bank micro probe 构建入口：
  `cuper-jacobi-link-mmap-probe-xclbin` 和
  `cuper-jacobi-link-mmap-probe-xclbin-split`。
- 2026-06-13 已在 tmux 中生成 mmap-only micro probe same-bank 与 split-bank 两版
  xclbin。两版 routed timing 均收敛，WNS `0.003 ns`，TNS `0.000 ns`。
- 当时已把 split-bank 版本同步到 Jacobi demo 槽：
  `395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin`。该历史文件的 kernel
  是 `CuperJacobiMmapProbeOnly`，不是完整 `CuperJacobiIteration` graph；它用于验证
  mmap/ABI/runtime 边界，现已被后续 full graph demo 覆盖。
- 2026-06-14 已把默认完整 graph 构建切回 no-debug ABI，生成并同步新的
  `CuperJacobiIteration` full graph demo：
  `395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin`。这版覆盖上一条同名
  mmap-only micro probe demo，保留 probe 结果作为历史边界记录。
- 2026-06-14 新增 `JACOBI_TRACE_ISOTOPE=1` trace/debug ABI。它把完整 graph 的关键
  task 作为 source 编号记录到 Debug BO：dispatcher、ptr/vector loader、16 路
  matrix loader、16 路 accumulator、frame/coeff loader、8 路 pair compute、
  pack writer 和 X HBM writer。业务 task 只用 `try_write` 非阻塞发事件，
  `Jacobi_DebugMonitor` 单独写 `Debug[0..255]`，host 在 `Finish()` 前先
  `ReadFromDevice()` 并打印 Debug 快照，因此 full graph 卡在 `Finish()` 时也能拿到
  最后事件。
- trace 版硬件 connectivity 把 `Status/Metrics/Debug` 分到 `HBM[24]/HBM[25]/HBM[26]`，
  沿用 mmap-only split-bank probe 已验证过的 mmap 写回边界。当时 trace host 在
  `Exec` 后等待 `JACOBI_PREFINISH_SAMPLE_DELAY_MS=250` 毫秒再做 pre-Finish 采样；
  可设为 0 关闭。
- 已验证 `JACOBI_TRACE_ISOTOPE=1` 的 host 构建和 software/TAPA simulation：
  `thermal2_n16 MAX_ITERS=1`、`thermal2_n1024 MAX_ITERS=1` 均 `Error Num=0`，
  Debug[48..51] 可读，47 个 source 槽位在正常返回时均可见。
- 2026-06-14 full isotope 硬件构建在 `Jacobi_DebugMonitor` HLS/resource synthesis
  阶段暴露过高开销：47 路 `JacobiDebugEvent` stream 会形成大量 FIFO/peek 端口，
  单个 `vitis_hls` 进程 RSS 一度接近 70GB，未到 XO 安全点即手动停止。
- 新增 `JACOBI_TRACE_LIGHT=1` 轻量 trace ABI。light 版仍保留 Debug BO 和
  pre-Finish 快照，但只连接 7 路关键 source：dispatcher、ptr loader、vector loader、
  frame fork、coeff loader、pack writer、X HBM writer。matrix loader 16 路、
  accumulator 16 路和 pair compute 8 路只在 `JACOBI_TRACE_ISOTOPE=1` 或
  `JACOBI_DEADLOCK_DEBUG=1` 的 full trace 中接入 DebugMonitor。
- light trace 构建脚本已接入 CMake、子目录 Makefile、根 Makefile、
  `build_xo_u55c.sh`、`link_xclbin_u55c.sh` 和 `launcher.py`，并沿用 split-bank
  debug connectivity：`Status/Metrics/Debug` 分别接 HBM[24]/HBM[25]/HBM[26]。
- 已验证 `JACOBI_TRACE_LIGHT=1` 的 host 构建和 software/TAPA simulation：
  `thermal2_n16 MAX_ITERS=1`、`thermal2_n1024 MAX_ITERS=1` 均 `Error Num=0`，
  Debug[48..51]=`1245921841,7,1,8192`，7 个关键 source 槽位在正常返回时可见。
- 2026-06-14 已生成并同步 light-trace full graph 硬件 demo artifact：
  `395bitstream/cuper-tapa-jacobi-u55c-20260614-demo.xclbin`。这版覆盖上一条
  `20260613` no-debug full graph demo 槽，但仍未晋级标准。
- 服务器侧复测 `20260614-demo` 后，`thermal2_n16` 和 `thermal2_n1024` 的
  `MAX_ITERS=1` 已返回通过，`thermal2_n65536` 仍卡在 `Finish()`。为定位这个规模相关
  问题，当时源码把后续 light trace 从 7 路扩到 15 路，新增 8 路
  `pair_compute[0..7]`；host 在 trace ABI 下默认做 60 次 pre-Finish 周期同步，每次
  输出简短 Debug summary，每 10 次输出完整 source 表，并在看到 `Status[0]` 被覆盖时
  提前进入 `Finish()`。该结果只对应旧 7 路 UUID。
- 2026-06-15 已用 `JACOBI_TRACE_ISOTOPE=0 JACOBI_TRACE_LIGHT=1 FORCE=1 make
  cuper-jacobi-hw-tmux` 生成并同步 15 路 light-trace full graph demo：
  `395bitstream/cuper-tapa-jacobi-u55c-20260614-demo.xclbin`。新 UUID 为
  `ef3b1102-90ec-551a-d1e9-55fb6c023da5`，SHA256 为
  `ba3db5ae3cc0e2720425097eec7110cd59bcc0b2b4a62608204046e0c5c7feb2`。它覆盖同名
  7 路 light-trace demo 槽，但仍不是标准 bitstream，尚未上板 smoke。
- 2026-06-15 随后按同一 light-trace ABI 重新构建 timing-clean 版本，默认把 TAPA
  `CLOCK_PERIOD` 调到 `4.0`，link 侧把 DATA clock 请求降到 `150 MHz`。新的
  `20260614-demo` UUID 为 `3fc9b8f4-901b-008f-8bc9-26ea3bf6f0c1`，SHA256 为
  `4d1fb090afebcf75d8087156665d969f02105813f984935feb8818c31afc38ab`。routed timing
  已收敛：WNS `0.003 ns`，TNS `0.000 ns`，setup failing endpoints `0`。它覆盖
  上一条 164 MHz timing-fail demo 槽，但仍不是标准 bitstream，尚未上板 smoke。
- 2026-06-15 根据前一版 timing-clean 但 `thermal2_n16 MAX_ITERS=1` 仍卡 `Finish()`
  的结果，删除旧 `RoundToken`/`FeedbackToken` 自循环和 `UpdateFrameFork` 控制路径，
  改成 `Jacobi_MasterController` 显式推进每轮 command/ack：controller 每轮发
  matrix loader、SpMV compute、update command，等待 `Jacobi_XHbmWriter` 的
  `JacobiUpdateDone` 后进入下一轮，最后统一广播 stop 并写 Status/Metrics。
- 已验证 master-controller 版的 host 构建和 software/TAPA simulation：trace ABI
  `thermal2_n16`、`thermal2_n1024`、`thermal2_n65536` 的 `MAX_ITERS=1` 均
  `Error Num=0`；no-trace `thermal2_n16 MAX_ITERS=1` 也 `Error Num=0`。
- 2026-06-15 已用 `JACOBI_TRACE_LIGHT=1 FORCE=1 make cuper-jacobi-hw-tmux` 生成并
  同步 master-controller light-trace full graph demo：
  `395bitstream/cuper-tapa-jacobi-u55c-20260615-demo.xclbin`。UUID 为
  `c37ecdbf-92ab-5d06-11bd-e2f9edc7f720`，SHA256 为
  `78c4ffdb9268aa5c1635bf2eefeed3b828e8a26e60ab3ccb8d795c9484d975a7`。DATA/KERNEL/HBM
  clock 为 `150/500/450 MHz`，routed timing 已收敛：WNS `0.003 ns`，TNS
  `0.000 ns`，setup failing endpoints `0`，hold worst slack `0.009 ns`。v++ link
  总耗时 `3h 27m 40s`。它覆盖上一条 `20260614-demo` timing-clean 旧控制流 demo 槽，
  但仍不是标准 bitstream。
- 2026-06-15 已完成该 master-controller demo 的 demo-only 上板测试，日志在
  `logs/jacobi_full_graph_hw_20260615_223100_master_controller/`。`MAX_ITERS=1`
  从 `thermal2_n16` 到完整 `thermal2` 全部返回并校验通过；完整固定轮数按 CPU
  reference 到 `tau=1e-5` 的轮数设置 `MAX_ITERS`，`thermal2_n1024` /
  `thermal2_n65536` / `thermal2_n131072` / `thermal2_n262144` / 完整 `thermal2`
  分别跑 `451/743/842/900/24409` 轮并通过。完整 `thermal2` 的 CPU reference
  时间为 `173375 ms`，FPGA kernel 时间为 `113035 ms`。

## 当前没有做

- mmap-only micro top 已生成 timing-clean `.xclbin`，且当时的 split-bank 同步 demo 已完成
  native XRT 上板 smoke。`ROW_NUM=16` / `ROW_NUM=1024` 均为 `rc=0`、
  `wait_state=COMPLETED`，wait 前 sample sync 已读到 `Status/Metrics/Debug`
  probe magic。
- 当前完整 `CuperJacobiIteration` master-controller light-trace full graph 已生成
  150 MHz timing-clean 硬件 bitstream，并已通过 demo-only 单轮和完整固定轮数上板测试。
  但它仍是 debug demo，不是标准 bitstream。
- 没有把 HBM 使用压回 16 个通道。
- 没有把 Jacobi 变成 PCG 预条件子。
- 没有生成正式 `source.diff`；当前版本证明 Jacobi full graph 功能边界已打通，但还不是
  标准/性能优化晋级候选。
- 没有把 isotope trace 版当作性能优化或标准候选；它是定位 `Finish()` 卡住点的
  debug build 边界。

## 当前风险

- Jacobi 收敛性取决于矩阵性质；software smoke 只证明当前 kernel 数据通路和 CPU
  reference 对齐，不代表所有矩阵都适合 Jacobi。
- 当前 HBM ABI 使用矩阵 0..15 之外的 `B/Diag_inv/X/Status/Metrics/Debug` 通道；
  如果后续要追求只用 16 个 HBM，需要重做数据供给策略。
- `thermal2_n262144` 的当前记录来自早期 software run，已经证明功能方向，但还没有用
  当前 root target 补跑。
- 当前同步的 2026-06-15 demo 是完整 Jacobi graph master-controller light-trace debug
  版，timing 已收敛且已板测通过。它仍然是固定轮数实现，`Status=1` 表示到达
  `MAX_ITERS`，不是硬件内部 early-exit；如果要作为长期标准，需要补真实收敛判断或
  明确固定轮数 ABI，并减少 debug ABI/HBM 额外通道依赖。
- 2026-06-13 mmap-only probe 通过后，完整 graph debug 改成默认非阻塞：
  `JACOBI_DEADLOCK_DEBUG=1` 只启用 Debug buffer/event stream，不再入口阻塞写
  Debug/Status/Metrics probe；旧入口阻塞 probe 需要额外设置
  `JACOBI_BLOCKING_ENTRY_PROBE=1`。
- host 对 Status/Metrics/Debug 使用 sentinel 初始化并改为 `read_write_mmap`，让
  pre-Finish dump 能区分“kernel 没覆盖 BO”和“写回已经穿透但值异常”。
- 已验证 no-debug 与 nonblocking-debug 的 `thermal2_n16 MAX_ITERS=1` software/TAPA
  simulation 均通过，`Error Num=0`；nonblocking-debug 路径无
  `Debug_Event_Stream` leftover 警告。这条是 2026-06-13 的边界记录；当前硬件 debug
  demo 已改用 `JACOBI_TRACE_LIGHT=1` ABI。
- full isotope 47 路 trace 不适合直接作为默认硬件 xclbin 构建；需要硬件 debug 时优先
  使用 `JACOBI_TRACE_LIGHT=1`，只有在必须细分到 matrix/accumulator/pair compute 时再
  开 full trace。
