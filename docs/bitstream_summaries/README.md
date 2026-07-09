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

- `2026-07-07-cuper-tapa-pcg-callipepla/`：隔离的 `cuper-tapa-pcg`
  Callipepla-style full-PCG 实验。源码在 `DLC/Cuper-callipepla-pcg/`，顶层为
  `CuperPcgCallipepla`，矩阵仍用 Cuper `SpElement_list_ptr + Matrix_data[0..15]`，
  默认启用 strip16 去 HBM padding 和 accumulator window=10。2026-07-08 低频
  full graph demo 已生成但最小上板 timeout；trace-light 版 routing verification
  失败；当前同步槽为 2026-07-09 `CUPER_CALLIPEPLA_PROBE_MODE=entry` debug artifact
  `395bitstream/cuper-tapa-pcg-fpga-u55c-20260709-demo.xclbin`，UUID
  `7ab50484-4649-ffd5-dd5c-0925c61a9504`，DATA/KERNEL/HBM 为 `100/500/450 MHz`，
  routed timing clean。它只验证 entry、AXI-Lite offsets、Status/Metrics mmap 和
  HBM mapping，不代表完整 PCG/SpMV 功能或性能。
- `2026-07-04-cuper-notapa-spmv-chisel8-spmvbaseline/`：独立 no-TAPA Chisel RTL
  kernel `CuperSpmvChisel8` 的 full SpMV baseline 和 correctness-debug 记录。已同步
  `395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-spmvbaseline-demo.xclbin`，
  当前 UUID 为 `09ac7fd6-26a1-7d3b-ac94-c6ea4cdbb8ea`，DATA/KERNEL/HBM 为
  `138/500/450 MHz`。当前同步版是在已通过 correctness 的 slim/no-debug 基线上做
  owner-step8 phase-1：保持 ABI/HBM mapping/AXI-Lite offsets/13 路 `m_axi_*` 端口和
  scalar `Y_out` writer 不变，用 `CUPER_SPMV_CHISEL8_SLIM_DEBUG=1` 隔离重 debug fanout，
  并把 datapath issue 改为同 owner slot 跨最多 8 source 发射。完整 hw link 已完成，
  150 MHz timing 仍未收敛；服务器侧 `CHECK_Y=1` 和性能 sweep 待跑，不晋级标准
  bitstream。上一 UUID `495e02a6-...` 已通过 listed `thermal2*` correctness，但完整
  `thermal2` 为 `459.425 ms`，远慢于 strip8 `2.71420 ms`；再上一 UUID
  `765e33c9-...` 是 full-debug FP/partial counter 版，因 debug fanout 频率降到
  `85/500/345 MHz`；
  再上一 UUID `0f31be8c-...` 的服务器侧反馈表明
  ptr/X/matrix decode 和 accumulator accepts 活着，但旧 `nonzero_products` 不能证明
  fmul 输出非零；更早 UUID `c36bff4e-...` no-check 可跑到完整 `thermal2`，但
  `CHECK_Y=1` 失败、`Y` mostly zeros/错误，用户提供的 `477.6 ms` 不作为有效 SpMV
  性能成绩。
- `2026-07-03-cuper-notapa-spmv-chisel8-drainprobe/`：独立 no-TAPA Chisel RTL
  kernel `CuperSpmvChisel8` 的 HBM drain-probe demo。该版保持 entry-probe 的
  ABI/HBM mapping 不变，完整读取 ptr table、X packets 和 8 路 Matrix_data beats，
  只写 drain 计数和摘要，不计算 SpMV；已生成 timing-clean xclbin 并同步到
  `395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-drainprobe-demo.xclbin`，
  等待有 U55C/XRT device 的服务器侧上板。
- `2026-07-03-cuper-notapa-spmv-chisel8-entryprobe/`：独立 no-TAPA Chisel RTL
  kernel `CuperSpmvChisel8` 的 entry-probe 记录。该版只验证 AXI-Lite、13 路
  AXI master、ownerbank8 HBM mapping、`Status`/`Metrics` 和 scalar `Y_out[0]`
  ABI；已经生成 timing-clean xclbin 并同步到 `395bitstream/`，但未上板、不是完整
  SpMV bitstream。
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
- `2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/`：当前 full-PCG
  controller/update 持续记录目录。
  早期目标是把 `CuperPcg` 内嵌 SpMV 性能优化到接近 standalone/native TAPA
  Cuper SpMV；2026-05-29 之后 single SpMV one-shot demo 已转为回归基线，当前
  主目标是 full `CuperPcg(...)` 的 `iter_spmv_recv_dot`、`update_x`、
  `update_rz_reduce`、`update_p`、service drain/stop 和 controller HBM 访问。
  后续围绕 full-PCG controller/update 的源码改动说明、demo bitstream 和测试结论
  继续写入该目录，不再为每个小 demo 新建目录；正式 `source.diff` 只有在测试确认
  性能提升，或用户明确要求保留功能边界修复补丁后才更新。

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
