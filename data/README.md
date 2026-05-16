# Project-XPlus Data

`Project-XPlus` 默认使用本目录下的数据集。

本仓库只提交轻量 metadata 和下载/转换脚本。SuiteSparse 原始归档、MatrixMarket 文件和转换后的 CSR 文本数据不会提交到 Git。

## 生成的小型数据

当前本地生成器：

```text
scripts/generate_cg_dataset.py
```

默认输出目录：

```text
data/generated/cgsolver/n512
```

常用命令：

```bash
make generate
python3 scripts/generate_cg_dataset.py --size 1024 --output-dir data/generated/cgsolver/n1024
```

## SuiteSparse 数据

下载并转换当前硬件 smoke test 使用的 `thermal2_n1024`：

```bash
make download-suitesparse-data
```

下载并转换全部已登记数据：

```bash
make download-suitesparse-data DATASETS=all
```

从完整 `thermal2` 生成任意尺寸的裁剪数据：

```bash
make download-suitesparse-data DATASETS=thermal2_n2048
make download-suitesparse-data DATASETS=thermal2_n65536
```

也可以在交互菜单里选择：

```bash
make launcher
# 然后选 d. 数据集下载/生成
```

`thermal2_n<N>` 会取完整 `thermal2` 的前 `N x N` 主子矩阵，输出到：

```text
data/suitesparse/Schmid/csr/thermal2_n<N>
```

`N` 最大不能超过完整矩阵维度 `1,228,045`。当前硬件 bitstream 仍受 `include/cg_common.hpp` 里的 `kMaxN = 1024` 限制；超过 1024 的数据可以生成，但不能直接跑当前硬件。

查看可选数据集：

```bash
make list-suitesparse-data
```

来源、尺寸和 checksum 见：

```text
data/suitesparse/SOURCES.md
```
