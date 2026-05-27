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
- Follow `docs/codex/testing.md` for demo-vs-standard dynamic comparison and required datasets.
- For bitstream/build/TAPA/report/version-record work, read the matching `docs/codex/workflows/*.md` file before editing.
- Keep version records in `docs/bitstream_summaries/<version>/`.
- For code-changing demo candidates, maintain `README.md`, `changes.md`, `testing.md`, `source.diff`, and, when useful, `code_reading_guide.md`.
- Store synchronized candidate bitstreams in `395bitstream/` with a `-demo` suffix until the user explicitly approves promotion.
- Do not replace standard bitstreams without archiving the old standard and updating `395bitstream/README.md`.

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
