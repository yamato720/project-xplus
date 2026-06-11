# Cuper Jacobi Iteration Host

这里放 `Cuper-jacobi-iteration` 独立子项目自己的 host 侧代码。

当前 `main.cpp` 负责：

- 读取 Matrix Market `.mtx` 文件。
- 读取 Project-XPlus CSR 目录：`row_ptr.txt`、`col_idx.txt`、`values.txt`、可选 `b.txt`。
- 复用 Cuper host 侧 slice/SpElement/HBM 打包流程。
- 生成 `R=A-D`、`Diag_inv`、`X0/X1`，并把 Jacobi 向量打包成 `float_v16`。
- 运行 CPU Jacobi reference 并校验 `CuperJacobiIteration(...)` 输出。

`.mtx` 输入没有单独 RHS 文件时，host 默认构造：

$$
b = A \cdot \mathbf{1}
$$

CSR 目录存在 `b.txt` 时，host 使用数据集提供的 RHS。

本目录暂时不和上层 `Project-XPlus/host/` 混写；等接口稳定后，再决定哪些部分值得抽回公共层。
