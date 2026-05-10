# Project-XPlus Data

`Project-XPlus` 默认使用本目录下的生成数据集。

当前生成器：

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
