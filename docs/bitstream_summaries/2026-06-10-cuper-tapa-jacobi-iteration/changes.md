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
- 已把 split-bank 版本同步到 Jacobi demo 槽：
  `395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin`。当前同步文件的 kernel
  是 `CuperJacobiMmapProbeOnly`，不是完整 `CuperJacobiIteration` graph；它用于验证
  mmap/ABI/runtime 边界。

## 当前没有做

- mmap-only micro top 已生成 timing-clean `.xclbin`，且 split-bank 同步 demo 已完成
  native XRT 上板 smoke。`ROW_NUM=16` / `ROW_NUM=1024` 均为 `rc=0`、
  `wait_state=COMPLETED`，wait 前 sample sync 已读到 `Status/Metrics/Debug`
  probe magic。
- 完整 `CuperJacobiIteration` 仍没有 timing-clean、可正常返回的硬件 bitstream；上一版
  entry mmap probe demo routed timing 未收敛，WNS `-2.350 ns`。
- 没有把 HBM 使用压回 16 个通道。
- 没有把 Jacobi 变成 PCG 预条件子。
- 没有生成正式 `source.diff`；当前版本还没有硬件 demo-only 性能确认。

## 当前风险

- Jacobi 收敛性取决于矩阵性质；software smoke 只证明当前 kernel 数据通路和 CPU
  reference 对齐，不代表所有矩阵都适合 Jacobi。
- 当前 HBM ABI 使用矩阵 0..15 之外的 `B/Diag_inv/X/Status/Metrics/Debug` 通道；
  如果后续要追求只用 16 个 HBM，需要重做数据供给策略。
- `thermal2_n262144` 的当前记录来自早期 software run，已经证明功能方向，但还没有用
  当前 root target 补跑。
- 当前同步的 2026-06-13 demo 是 mmap-only micro probe，不是完整 Jacobi graph，不能
  作为 Jacobi 算法功能或性能结论。native XRT runner 已证明 Status/Metrics/Debug BO
  sync 和 kernel launch 边界可用；下一步回到完整 graph 的 `Finish()` 收尾问题。
