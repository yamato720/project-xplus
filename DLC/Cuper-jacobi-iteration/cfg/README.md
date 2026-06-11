# Cuper Jacobi Iteration Config

这里放 `Cuper-jacobi-iteration` 子项目自己的平台与 connectivity 配置。

当前 `connectivity.cfg` 只服务 `CuperJacobiIteration`：

- `SpElement_list_ptr` 映射到 HBM[0]。
- 16 路 `Matrix_data_0..15` 映射到 HBM[0..15]，当前保存的是 `R=A-D` 的 packed 数据。
- `B` 映射到 HBM[20]。
- `Diag_inv` 映射到 HBM[21]。
- `X0` / `X1` 映射到 HBM[22] / HBM[23]。
- `Status` 和 `Metrics` 映射到 HBM[24]；`Metrics` 当前是 `double` 调试数组，
  保存 diff、包数和 stage cycle。

这只是第一版 demo 的保守 bank 分配。后续如果上板性能不理想，需要结合 HBM
访问热点重新安排 `B/Diag_inv/X0/X1` 和矩阵 bank 的位置。
