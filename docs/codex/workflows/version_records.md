# 版本记录与阅读辅助纪律

当一次改动已经进入“可能生成 demo bitstream 或影响主线行为”的程度，不能只把
源码改完就结束。必须同步做版本级记录，让新对话、服务器侧 AI 和未来回退都能
知道这一版到底是什么。

## 1. 版本目录

每个候选版或标准替换版都放到：

```text
docs/bitstream_summaries/YYYY-MM-DD-<主线>-<简短说明>/
```

当前正在推进的 single TAPA SpMV 性能目标例外：后续连续改动统一写入下面这个
目标目录，避免每次 demo 都开一个新目录导致记录分散：

```text
docs/bitstream_summaries/2026-05-28-cuper-tapa-spmv-single-optimization/
```

这个目标的定义是：优化 `Cuper(...)` + `detail/cuper_spmv_tasks.hpp` 这条
standalone/native TAPA Cuper single-SpMV 路线。它不负责 full-PCG 的 controller、
FP64 dot/update 或 `CuperPcg` service-path 指标。`README.md`、`changes.md`、
`testing.md` 记录当前最新 demo/尝试状态；`source.diff` 不是“每次尝试的草稿
补丁”，只代表经过板上测试后确认有性能提升、值得继续保留或晋级的最新有效补丁。

目录内至少维护：

```text
README.md
changes.md
testing.md
```

`source.diff` 只有在满足本文件第 3 节条件时才更新。若最新 demo 只是功能边界修复、
测试失败、或性能退步，保留测试记录和 HTML 结论即可，不要覆盖上一份有效
`source.diff`。

复杂代码改动、用户明确要研究代码、或接口/数据流容易混淆时，再加：

```text
code_reading_guide.md
```

`code_reading_guide.md` 放在版本目录里，不默认放 `docs/design/`。只有当内容已经
抽象成长期通用设计说明，才另写或同步到 `docs/design/`。

## 2. 每个文件写什么

- `README.md`：版本状态、主线、构建目录、bitstream/demo 名、是否建议晋级。
- `changes.md`：这一版改了什么、为什么改、预期收益、已知代价。
- `testing.md`：已经跑过的命令、关键输出、日志路径、失败边界、下一步测试计划。
- `code_reading_guide.md`：按文件顺序解释怎么读这版代码，重点写 ABI、HBM 映射、
  task graph、controller 阶段、metrics 口径。
- `source.diff`：测试确认有性能提升后才写入/更新的可逆补丁；它是“有效候选补丁”，
  不是每轮探索的自动快照。

不要把大段源码复制进 Markdown；解释写在 Markdown，真实改动以 `source.diff`
为准。若最新 demo 没有更新 `source.diff`，必须在 `testing.md` 或 `changes.md`
中写明原因，例如“性能退步，source.diff 未更新”。

## 3. source.diff 生成纪律

先测试，后写 diff。对于迭代优化版本，禁止在未完成板上测试前因为“做了一版 demo”
就刷新正式 `source.diff`。

允许生成/更新 `source.diff` 的条件：

1. 已完成对应 demo-only 上板测试，并把结果写入 HTML 和 `testing.md`。
2. 对当前优化目标的核心性能指标有提升：
   - 当前单 SpMV demo 阶段，对比 PCG 抽出版 `cuper-tapa-spmv` demo 的
     `spmv_avg`、成功/timeout 边界和数值误差，标准基准使用
     `docs/codex/workflows/reports.md` 定义的 standalone TAPA Cuper SpMV；
   - 回填 full-PCG 后，1iter 对比使用 TAPA full-PCG 标准版和上一 demo 的同口径
     记录；
   - 不能只因为“能跑更大规模”就更新性能优化 `source.diff`。
3. 没有引入不可接受的功能退化、数值错误或新的失败边界。
4. 用户明确要求保留某个功能修复补丁时，可以例外更新，但必须在 `changes.md`
   里写清楚这是“功能边界修复补丁”，不是性能提升补丁。

不满足上述条件时：

- 不更新正式 `source.diff`；
- 在 `testing.md` 写清楚测试结果、日志路径、退步或失败原因；
- 在 HTML 里把它标成失败/退步/功能边界候选；
- 若需要临时保存工作树改动，用普通 git 工作流或单独命名的临时文件，不要覆盖
  目标目录的正式 `source.diff`。

生成 `source.diff` 时只包含本版相关源码、host、cfg、Makefile 和纪律文档。不要把
build 产物、日志、`.xclbin` 或版本目录自身递归塞进去。

示例：

```bash
git diff --no-ext-diff --unified=0 -- \
  DLC/Cuper/include/Cuper.h \
  DLC/Cuper/kernels/detail/cuper_top_graphs.hpp \
  DLC/Cuper/kernels/detail/pcg_common.hpp \
  DLC/Cuper/kernels/detail/pcg_controller.hpp \
  DLC/Cuper/kernels/detail/pcg_spmv_service.hpp \
  Makefile \
  cfg/connectivity_cuper_tapa_pcg_u55c.cfg \
  host/cuper_tapa_pcg_fpga_main.cpp \
  docs/codex/coding.md \
  docs/codex/testing.md \
  docs/codex/workflows/bitstreams.md \
  docs/codex/workflows/builds.md \
  docs/codex/workflows/reports.md \
  docs/codex/workflows/tapa_sources.md \
  docs/codex/workflows/version_records.md \
  docs/codex/skills/README.md \
  docs/codex/skills/project-xplus/SKILL.md \
  docs/bitstream_summaries/README.md \
  > docs/bitstream_summaries/<版本目录>/source.diff
```

如果新增了别的相关源码，要加入路径列表。生成后跑：

```bash
git diff --check
```

## 4. 阅读辅助写法

阅读辅助优先服务“以后怎么接着改”，不是写科普文章。至少回答：

1. 这版新增/改变了哪些 ABI、BO、HBM bank 或 register offset。
2. 从 host 到 kernel 的数据如何流动。
3. TAPA task graph 里哪些 stream 是串接链，哪些是真并行数组。
4. controller 每个 PCG 阶段读写哪些 HBM/stream。
5. 哪些 metrics 可以信，哪些当前只是粗略拆分。
6. 旧标准版和新 demo 的兼容开关是什么，例如 `LEGACY_ABI=1`。

## 5. 构建并行状态

如果 tmux 里已经有硬件构建进入 `vpl` / `impl` / routing，后续源码注释、
阅读指南和按本文件纪律允许的 `source.diff` 更新不会影响那一次构建。回复里必须说明
“这些改动只影响下一次构建”，并记录当前日志阶段。
