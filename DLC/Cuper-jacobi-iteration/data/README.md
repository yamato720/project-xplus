# Cuper Jacobi Iteration Data

这里放 `Cuper-jacobi-iteration` 独立实验目录自己的数据集、测试输入或生成结果。

当前本机已有：

- `matrices/cant.mtx`：Matrix Market 格式大矩阵，`62451 x 62451`。

`matrices/*.mtx` 属于本地大样本，默认被根 `.gitignore` 排除，不随源码提交。
一键 regression 传 `ALLOW_MISSING=1` 时，如果发现本地样本不存在，会在摘要里标成
`SKIP`；正式测试记录仍以实际机器上的数据路径为准。

host 也支持直接读取 Project-XPlus 根目录里的 CSR 数据目录，例如：

```bash
cd DLC/Cuper-jacobi-iteration
MAX_ITERS=1 make run-sw MATRIX=../../data/suitesparse/Schmid/csr/thermal2_n262144
```

CSR 目录需要包含：

- `row_ptr.txt`
- `col_idx.txt`
- `values.txt`
- 可选 `b.txt`

如果存在 `b.txt`，Jacobi demo 使用它作为右端项；否则 `.mtx` 路径默认构造
`b = A * ones` 作为自检 RHS。
