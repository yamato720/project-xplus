# changes

## 新增隔离子项目

- 新增 `DLC/Cuper-callipepla-pcg/`，包含独立 host、kernel、connectivity 和 U55C
  build/link/run 脚本。
- 根 `Makefile` 新增 `cuper-tapa-pcg-callipepla-*` targets，默认使用独立 build
  dir，不覆盖旧 `DLC/Cuper` 或 `DLC/Cuper-jacobi-iteration` 构建产物。
- 新环境变量采用 PCG-facing 命名：
  `CUPER_CALLIPEPLA_HBM_CHANNELS`、`CUPER_CALLIPEPLA_SPMV_STRIP_PADDING`、
  `CUPER_CALLIPEPLA_SPMV_ACC_WINDOW`。脚本内部映射到沿用的 `JACOBI_*` 编译宏。

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
