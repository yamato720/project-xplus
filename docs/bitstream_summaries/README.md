# Bitstream 版本总结

这个目录专门存放 bitstream 版本级 Markdown 总结。面向同步查看的总报告仍放在
`395bitstream/`，这里保留更详细的版本追踪记录。

## 目录规则

一个版本一个子文件夹，命名建议：

```text
YYYY-MM-DD-<主线>-<简短说明>/
```

例如：

```text
2026-05-27-cuper-tapa-pcg-demo/
```

## 已有版本记录

- `2026-05-27-cuper-tapa-pcg-demo/`：已生成 bitstream 的 TAPA full-PCG
  receive-path demo 对比。
- `2026-05-27-cuper-tapa-pcg-packed-feed-ap-demo/`：当前 packed
  `X_spmv/P_spmv/AP_spmv` 优化候选，硬件 bitstream 构建中。

每个版本目录至少包含：

- `README.md`：测试摘要、关键结论、日志路径、是否建议晋级。
- `changes.md`：这一版相对上一标准版改了什么、预期收益、实际结果。
- `testing.md`：测试命令、数据集、关键输出、失败边界、待补项目。
- `code_reading_guide.md`：版本相关代码阅读指南。只在该版源码较复杂或用户
  要求研究代码时补充，避免把版本特有解释散落到全局设计文档。
- `source.diff`：必须提供。记录这一版相对上一标准源码的可逆补丁，
  用于以后复现或回退源码改动，避免在 Markdown 里粘贴大段源码。

## diff 文件规则

`source.diff` 只记录源码和脚本改动，不记录 `.xclbin`、build 产物、日志等大文件。
生成方式示例：

```bash
git diff <base-commit>..<version-commit> -- path/to/source > source.diff
```

如果这一版已经是一个独立提交，也可以使用 0-context patch，避免补丁文件里的
空白上下文行触发 `git diff --check`：

```bash
git show --format= --no-ext-diff --unified=0 <commit> -- path/to/source > source.diff
```

回退方式：

```bash
git apply --unidiff-zero -R docs/bitstream_summaries/<version>/source.diff
```

复现方式：

```bash
git apply --unidiff-zero docs/bitstream_summaries/<version>/source.diff
```

如果补丁已经提交到 Git，优先记录 commit id；`source.diff` 用作离线可读和可逆备份。

## 测试复用规则

旧基线默认复用 `395bitstream/cuper_spmv_u55c_compare_20260524.html` 和本目录中
已经记录的数据。只有用户要求、标准 bitstream/host 变化、或 demo 结果与旧记录
矛盾时，才重跑旧基线。

demo 的结果必须同时写入：

- `395bitstream/` 下的 HTML 报告；
- 本目录对应版本子文件夹的 Markdown 总结，详细测试过程写入该目录
  `testing.md`，不要只在最终回复或日志里保留。
