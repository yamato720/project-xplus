---
name: project-xplus
description: Use when working in the Project-XPlus / Project-X / Project-XS codebase, especially Cuper, TAPA, U55C, Vitis/Vivado, bitstream generation, tmux hardware builds, 395bitstream synchronization, demo bitstream testing, docs/bitstream_summaries version records, source.diff, testing.md, or code_reading_guide.md.
metadata:
  short-description: Project-XPlus Cuper/TAPA/U55C workflow
---

# Project-XPlus

This skill is only an entry point. The repository documents are the source of truth.

## First Steps

1. Find the repo root. Prefer the current working directory if it contains `Project-XPlus`; otherwise check:

```bash
/home/pyx/ProjectFS/Project-X/Project-XPlus
/home/pyx/project-x/Project-XPlus
```

2. For narrow read-only questions, load only the relevant repository docs/sections. Before changing build scripts, kernels, bitstreams, reports, or version docs, read these files:

```text
Project-XPlus/docs/codex/coding.md
Project-XPlus/docs/codex/testing.md
Project-XPlus/395bitstream/README.md
```

3. If the task touches implementation naming, also read:

```text
Project-XPlus/docs/design/implementation_versions_zh.md
```

4. Run:

```bash
git status --short
tmux ls 2>/dev/null || true
```

If a hardware build is already in `vpl`, `impl`, or routing, say that source edits affect only the next build.

## Workflow Rules

- Follow `docs/codex/coding.md` as the entry point. It links to detailed workflow docs under `docs/codex/workflows/`.
- Follow `docs/codex/testing.md` for demo testing and required datasets. New demo testing is
  demo-only by default; do not rerun the four standard bitstreams unless the user explicitly
  asks, standard bitstream/host changed, or old records are insufficient to interpret a mismatch.
- When updating the HTML report for a TAPA full-PCG demo, keep current diagnostic sections such
  as `TAPA PCG 分段时间` and `Init 与 1iter 差值` on the latest demo-only measurements.
  Put standard/previous-demo/current-demo comparisons in a separate comparison block.
  In that comparison block, use standalone TAPA Cuper SpMV as the SpMV standard baseline;
  use TAPA full-PCG standard only for the 1iter comparison.
  If a demo changes stage semantics, label raw counters by their real meaning, e.g. `iter recv`,
  and compare derived metrics such as `AP path = iter recv + dot_p_ap` only when the formula is
  written in the HTML. See `docs/codex/workflows/reports.md` before editing HTML views.
- For a single-SpMV demo, record new data only in the SpMV/demo-only sections. Leave PCG
  diagnostic and 1iter data unchanged, but label those sections as not run in this round
  (`本轮未跑 PCG，无 init/1iter 过程`).
- For bitstream/build/TAPA/report/version-record work, read the matching `docs/codex/workflows/*.md` file before editing.
- Keep version records in `docs/bitstream_summaries/<version>/`.
- For code-changing demo candidates, maintain `README.md`, `changes.md`, `testing.md`, and, when useful, `code_reading_guide.md`. Update official `source.diff` only after demo-only board testing confirms a performance improvement, or when the user explicitly asks to preserve a functional-boundary fix; do not overwrite the last effective `source.diff` for failed or slower demos.
- Store synchronized candidate bitstreams in `395bitstream/` with a `-demo` suffix until the user explicitly approves promotion.
- Do not replace standard bitstreams without archiving the old standard and updating `395bitstream/README.md`.

## Current Goal

- Current optimization target: make `CuperPcg` embedded SpMV approach standalone/native
  TAPA Cuper SpMV performance. The current demo path should be a `cuper-tapa-spmv`
  single-SpMV bitstream extracted from the `CuperPcg` SpMV service path, so it can be
  tested in isolation and then reused/replaced inside full-PCG after it improves.
- Keep two SpMV forms distinct: full standalone `Cuper(...)` in `cuper_spmv_tasks.hpp`
  is the baseline; the PCG-adjusted service path in `pcg_spmv_service.hpp` is the
  optimization/demo target.
- For this target, keep ongoing notes and source diffs in:

```text
docs/bitstream_summaries/2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/
```

- Update that target directory for incremental SpMV optimization work instead of creating a new bitstream summary directory every time, unless the user explicitly asks for a new independent version record.

## Common Commands

Host smoke:

```bash
make cuper-tapa-pcg-fpga-host
make run-cuper-pcg-tapa-fpga DATASET=data/generated/cgsolver/n512 MAX_ITERS=1 DIFF_TOL=1e-3
```

Hardware builds should run in tmux and keep the shell open after completion. Use existing Makefile tmux targets when available.

Before finalizing code/docs:

```bash
git diff --check
git status --short
```
