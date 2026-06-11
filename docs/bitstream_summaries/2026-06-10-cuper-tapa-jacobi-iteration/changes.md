# 变更记录

## 目标

把 `DLC/Cuper` 同级的 Jacobi iteration demo 登记为第五条 Cuper 主线
`cuper-tapa-jacobi`，并保留当前 software/TAPA simulation 的测试口径。

## 已完成

- 新增独立目录 `DLC/Cuper-jacobi-iteration/`，避免混入既有 `DLC/Cuper` 的
  SpMV/PCG 主线。
- 顶层命名为 `CuperJacobiIteration`，不再沿用 PCG 命名。
- host 侧把矩阵拆成 `A = D + R`，并生成 `Diag_inv`。
- kernel 侧复用 Cuper single SpMV service；`Jacobi_Vector_Loader` 读取 `X0/X1`
  时取负，让 SpMV service 输出 `-R*x_old`。
- 新增 `Jacobi_Update_Service`，消费 `-R*x_old`，读取 `B/Diag_inv/x_old`，
  写回 `x_next`。
- `X0/X1` 做双缓冲，`Status[1]` 返回最终结果所在 buffer。
- 新增 Jacobi stage timing，host 输出 `[jacobi-timing-work]`、
  `[jacobi-stage-cycles]` 和 `[jacobi-stage-ms]`。
- 根 `Makefile` 接入 `cuper-jacobi-*` 转发 target。
- 文档口径从四条 Cuper 主线扩展为五条，并给 `cuper-tapa-jacobi` 保留独立 demo 槽。
- 生成第一版硬件 demo artifact：
  `395bitstream/cuper-tapa-jacobi-u55c-20260611-demo.xclbin`。
- 修正 Jacobi standalone link 脚本：`v++ --link` 子进程临时不继承 `XILINX_XRT`，
  避免最后包装阶段调用本地 XRT 2.18 的 `xclbinutil` 并缺 Boost 1.83。

## 当前没有做

- 没有上板测试。
- 没有得到 timing-clean bitstream；当前 routed timing 未收敛，WNS `-1.203 ns`。
- 没有把 HBM 使用压回 16 个通道。
- 没有把 Jacobi 变成 PCG 预条件子。
- 没有生成正式 `source.diff`；当前版本还没有硬件 demo-only 性能确认。

## 当前风险

- Jacobi 收敛性取决于矩阵性质；software smoke 只证明当前 kernel 数据通路和 CPU
  reference 对齐，不代表所有矩阵都适合 Jacobi。
- 当前 HBM ABI 使用矩阵 0..15 之外的 `B/Diag_inv/X0/X1/Status/Metrics` 通道；
  如果后续要追求只用 16 个 HBM，需要重做数据供给策略。
- `thermal2_n262144` 的当前记录来自早期 software run，已经证明功能方向，但还没有用
  最新 timed/root target 补跑。
- 当前 demo bitstream 没有过 timing，不能作为稳定性能结论。
