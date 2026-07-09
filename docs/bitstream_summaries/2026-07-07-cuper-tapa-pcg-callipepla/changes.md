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
- 2026-07-09 晚间同步版进一步在 `cmd_drain` controller 路径拆出细粒度 checkpoint：
  `Status[52]` 可停在 `10/11` total stage begin、`20/21` init_spmv stage begin、
  `30/31` ptr command、`40/41` matrix command fanout、`50/51` SpMV vector command、
  `60/61` init-spmv vector fake command、`70/71` fake ack read、`80/81` init_spmv
  stage end、`90/91` init_zp command、`100/101` init_zp result read，`110+` 覆盖
  stop/finalization。`Status[58]/Status[59]` 现在作为 `detail0/detail1`。
- `loader_drain` 模式逐档恢复真实 ptr/vector/matrix loader，但 core/acc/checker/sort
  仍保持 drain/stop 替身，用于定位卡死是否来自 HBM read/loader 层。
- Probe 状态约定：`Status[50]=0x43505242`，`Status[51]=mode_id`，其中
  `1/2/3` 对应 `entry/cmd_drain/loader_drain`；`Status[52..63]` 记录 probe stage、
  SpMV command 轮数、matrix command 计数、vector command/ack 计数和基础规模参数。
