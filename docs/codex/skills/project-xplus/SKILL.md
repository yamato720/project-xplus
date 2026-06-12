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
  demo-only by default; do not rerun the standard bitstreams unless the user explicitly
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
- Store synchronized candidate bitstreams in `395bitstream/` with a `-demo` suffix until the user explicitly approves promotion or asks to archive them. Project-XPlus now has five Cuper mainlines. `395bitstream/` may keep up to three demo slots: one `cuper-tapa-spmv` single-SpMV candidate, one `cuper-tapa-pcg` full-PCG candidate, and one `cuper-tapa-jacobi` Jacobi-iteration candidate. New demos overwrite only the same-mainline demo slot; archived demos move to `bitstream_archive/`.
- Cuper Jacobi candidates use names like `cuper-tapa-jacobi-u55c-YYYYMMDD-demo.xclbin` and must not be confused with Jacobi-preconditioned PCG. There is no Jacobi standard xclbin yet.
- Do not replace standard bitstreams without archiving the old standard and updating `395bitstream/README.md`.
- Before starting any `TARGET=hw` bitstream build, run the matching software-level
  validation first (`sw_emu`, TAPA software simulation, or a documented host/local smoke
  fallback). After launching a tmux hardware build, watch until the safe checkpoint in
  `docs/codex/workflows/builds.md`: XO generated and patched, then Vitis link has entered
  VPL/synthesis/implementation for full xclbin builds; for XO-only tasks, watch until the
  XO target is up-to-date.

## Current Goal

- Current measured boundary: the one-shot `CuperPcgSpmv(...)` single-SpMV demo now returns
  through full `thermal2` and is close to full/native `Cuper(...)` on shared successful points.
  Treat it as the single-SpMV regression baseline and boundary check, not the primary
  optimization target.
- `DLC/Cuper-jacobi-iteration` is the fifth Cuper mainline (`cuper-tapa-jacobi`), not
  Jacobi-preconditioned PCG. Its top is `CuperJacobiIteration(...)`: host splits `A=D+R`,
  the vector loader feeds `-x_old`, the Cuper service produces `-R*x_old`, and the update
  stage writes `x_next=(b+(-R*x_old))*diag_inv` back into the single `X` buffer. It is wired
  into the root `Makefile` through `cuper-jacobi-*` targets and currently reports
  `[jacobi-stage-cycles]` / `[jacobi-stage-ms]` timing debug from `Metrics[4..7]`. Current
  records are software/TAPA simulation plus one tail-drain-fix deadlock-debug xclbin artifact:
  `395bitstream/cuper-tapa-jacobi-u55c-20260612-demo.xclbin` (UUID
  `401e53eb-a68f-55fb-78f8-5553f14edcd2`, SHA256
  `46272395b4f4cef1a977767225080dfe2194fed3cf55baccbb5e4eec68e82e2f`). That artifact is not
  timing-clean (routed WNS -2.842 ns, TNS -74910.742 ns) and has not been board-tested, so
  there is still no Jacobi standard xclbin.
- The active optimization target has moved to full `CuperPcg(...)` PCG control and vector
  update paths. Prioritize `detail/pcg_controller.hpp`, `dot_p_ap`, `update_xr`,
  `update_p`, `P_spmv` / `AP_spmv` consumption, controller HBM access patterns, stage
  timers, and service drain/stop overhead. The 2026-05-29 data shows raw SpMV/AP receive is
  no longer the dominant 1iter cost.
- Keep `CuperPcgSpmv(...)` as a Cuper-compatible one-shot graph. Do not reintroduce
  `Pcg_Single*` command/stop/writer-done control shells into the single-SpMV demo.
- To claim a full-PCG performance improvement, modify the full `CuperPcg(...)` path and run
  full-PCG software or hardware validation. A single-SpMV demo result alone is only a SpMV
  regression/boundary result.
- For ongoing notes, keep single-SpMV baseline records in:

```text
docs/bitstream_summaries/2026-05-28-cuper-tapa-spmv-single-optimization/
```

- Keep full-PCG controller/update optimization records in:

```text
docs/bitstream_summaries/2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/
```

- Keep Cuper Jacobi iteration records in:

```text
docs/bitstream_summaries/2026-06-10-cuper-tapa-jacobi-iteration/
DLC/Cuper-jacobi-iteration/docs/testing.md
```

## Common Commands

Host smoke:

```bash
make cuper-tapa-pcg-fpga-host
make run-cuper-pcg-tapa-fpga DATASET=data/generated/cgsolver/n512 MAX_ITERS=1 DIFF_TOL=1e-3
```

Cuper Jacobi smoke:

```bash
make cuper-jacobi-build-host
MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=DLC/Cuper-jacobi-iteration/data/matrices/cant.mtx
MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n65536
make cuper-jacobi-regression-sw MODE=quick
```

Hardware builds should run in tmux and keep the shell open after completion. Use existing Makefile tmux targets when available.

Before finalizing code/docs:

```bash
git diff --check
git status --short
```
