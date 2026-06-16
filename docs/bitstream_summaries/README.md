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
2026-05-28-cuper-tapa-spmv-single-optimization/
```

## 已有版本记录

- `2026-06-10-cuper-tapa-jacobi-iteration/`：第五条 Cuper 主线
  `cuper-tapa-jacobi` 的起始记录。
  目标是用 `DLC/Cuper-jacobi-iteration` 复用 TAPA Cuper SpMV service，在 FPGA kernel
  内执行普通 Jacobi iteration `x_next=D^{-1}(b-Rx_old)`。当前
  `cuper-tapa-jacobi-u55c-20260615-demo.xclbin` 已完成 master-controller
  full graph demo-only 上板，单轮和完整固定轮数均通过；`20260616-demo` 是
  `JACOBI_WIDE_HBM=1` 的 24 路 Matrix_data 实验 artifact，build 已完成但
  routed timing 未收敛，尚未上板。Jacobi 主线还没有标准 bitstream。
- `2026-05-28-cuper-tapa-spmv-single-optimization/`：当前持续目标目录。
  目标是优化 `Cuper(...)` + `detail/cuper_spmv_tasks.hpp` 这条
  standalone/native TAPA Cuper single-SpMV 路线。后续 single TAPA SpMV 的源码
  改动说明、demo bitstream 和测试结论都继续写入该目录。2026-05-29 one-shot
  demo 已从 `395bitstream/` 移入
  `bitstream_archive/2026-05-29-tapa-pcg-spmv-demo-candidates/`。
- `2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/`：历史 full-PCG
  embedded-SpMV 目标目录。
  目标是把 `CuperPcg` 内嵌 SpMV 性能优化到接近 standalone/native TAPA
  Cuper SpMV。2026-05-29 full-PCG demo 用于确认 `CuperPcg(...)` 路径仍可生成
  routed bitstream，并已完成 demo-only 上板测试；该 demo 已从 `395bitstream/`
  移入 `bitstream_archive/2026-05-29-tapa-pcg-spmv-demo-candidates/`，未替换标准版。
  后续围绕 full-PCG 内嵌 SpMV / service/control 的源码改动说明、demo bitstream
  和测试结论继续写入该目录，不再为每个小 demo 新建目录；正式 `source.diff`
  只有在测试确认性能提升，或用户明确要求保留功能边界修复补丁后才更新。

该目标目录内同时保留历史阶段：

- `receive_path_demo.md` / `receive_path_changes.md` / `receive_path_source.diff`：
  旧 receive-path demo 的历史记录；
- `README.md` / `changes.md` / `testing.md`：
  当前最新 demo/尝试状态；
- `source.diff`：最新经过测试确认有效的候选补丁，不是每轮 demo 的自动快照。

每个版本目录至少包含：

- `README.md`：测试摘要、关键结论、日志路径、是否建议晋级。
- `changes.md`：这一版相对上一标准版改了什么、预期收益、实际结果。
- `testing.md`：测试命令、数据集、关键输出、失败边界、待补项目。
- `code_reading_guide.md`：版本相关代码阅读指南。只在该版源码较复杂或用户
  要求研究代码时补充，避免把版本特有解释散落到全局设计文档。
- `source.diff`：测试确认性能提升后才提供/更新的可逆补丁，用于以后复现或回退
  已验证有效的源码改动，避免在 Markdown 里粘贴大段源码。若只是测试失败、
  性能退步或未确认收益的探索版，只记录 Markdown/HTML 结论，不覆盖现有
  `source.diff`。

## diff 文件规则

先测试，后写 diff。`source.diff` 只记录源码和脚本改动，不记录 `.xclbin`、
build 产物、日志等大文件。生成或覆盖正式 `source.diff` 前，必须已经在板上完成
对应 demo-only 测试，并确认核心性能指标比标准/上一 demo 的同口径记录更好；仅仅
“能跑更大规模”不等价于性能提升。

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
