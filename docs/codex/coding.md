# Project-XPlus Codex 工作纪律

这份文件是新对话里的入口页。它只保留高优先级纪律和文档导航；具体流程拆到
`docs/codex/workflows/`，避免所有规则挤在一个 Markdown 里。

## 0. 开始前先做

1. 先读本文件，再读 `395bitstream/README.md`。
2. 先跑 `git status --short`，确认当前工作树里已有改动，不要覆盖用户改动。
3. 再跑 `tmux ls 2>/dev/null || true`。如果硬件构建已经进入 `vpl` / `impl` /
   routing，源码改动只影响下一次构建，回复里必须说明。
4. 只读型窄问题可以只读相关章节；涉及代码、构建、bitstream、报告或版本记录时，
   必须按下面文档地图补读对应 workflow。

## 1. 文档地图

| 任务 | 先读 |
| --- | --- |
| bitstream 同步、demo 晋级、旧版归档 | `docs/codex/workflows/bitstreams.md` |
| build 目录、tmux、Vitis/Vivado 构建 | `docs/codex/workflows/builds.md` |
| TAPA/Cuper 源码组织、stream/HBM 注释、SpMV 优化目标 | `docs/codex/workflows/tapa_sources.md` |
| HTML、analysis、XO/Vitis 报告和报告口径 | `docs/codex/workflows/reports.md` |
| `docs/bitstream_summaries/<版本>/`、`source.diff`、阅读指南 | `docs/codex/workflows/version_records.md` |
| demo-only 动态测试、数据集、失败边界、标准基线复用 | `docs/codex/testing.md` |
| 四条实现线的长期说明 | `docs/design/implementation_versions_zh.md` |

涉及 bitstream、构建、运行脚本时，也要查：

```text
Makefile
scripts/launcher.py
395bitstream/README.md
bitstream_archive/README.md
```

## 2. 四条主线

Project-XPlus 当前只把下面四条作为主要模式。兼容/旧实验路径可以保留，但不能
混成主线命名。

| 主线简称 | 含义 | 典型 bitstream 名 |
| --- | --- | --- |
| `cuper-tapa-pcg` | TAPA Cuper SpMV + FPGA 内 PCG，全流程 kernel | `cuper-tapa-pcg-fpga-u55c-YYYYMMDD.xclbin` |
| `cuper-tapa-spmv` | TAPA Cuper single SpMV，PCG 不在 FPGA 内全流程 | `cuper-tapa-spmv-u55c-YYYYMMDD.xclbin` |
| `cuper-notapa-pcg` | no-TAPA HLS Cuper/PCG control kernel，全流程 kernel | `cuper-notapa-pcg-fpga-u55c-YYYYMMDD.xclbin` |
| `cuper-notapa-spmv` | no-TAPA single SpMV kernel | `cuper-notapa-spmv-u55c-YYYYMMDD.xclbin` |

注意：

- `pcg-fpga` 表示 PCG 主循环、dot、alpha/beta、向量更新和收敛判断在 FPGA kernel 内。
- `spmv` 表示只测/只构造 SpMV kernel，host 可以控制 PCG 或只跑 single SpMV。
- `host-PCG`、旧 CSR 多 kernel、packed16hbm legacy 等都属于兼容或历史路线，不加入四条主线命名。
- `395bitstream/` 的成品槽位是四个标准 bitstream 加两个当前 demo 候选；
  当前两个 demo 槽位分别服务 `cuper-tapa-spmv` 和 `cuper-tapa-pcg`。新 demo
  允许覆盖同主线旧 demo 文件，但不能覆盖四条标准文件。

## 3. 高优先级规则

1. 新 bitstream 在用户明确认可前只能作为 demo，文件名必须带 `-demo` 后缀。
2. demo 不允许直接覆盖四条主线标准 bitstream；晋级前必须按
   `workflows/bitstreams.md` 归档旧版。
3. 允许用最新 demo 覆盖 `395bitstream/` 中同主线旧 demo 候选槽；覆盖后必须更新
   `395bitstream/README.md` 和对应版本记录，说明旧 demo 结论已变成历史记录。
   不同主线 demo 可以同时保留，当前上限是两个 demo 槽位。
4. 硬件构建不要使用裸 `build/` 混放；构建目录使用当前主线或 bitstream 名加
   `-build` 后缀。
5. 每次生成 `TARGET=hw` bitstream 前，必须先做同一 top/ABI 的软件级验证：
   优先跑 `sw_emu` 或 TAPA software simulation；如果工具链限制导致不能跑，
   至少跑对应 host/local smoke，并在版本记录或回复里写明原因。
6. 长时间硬件构建用 tmux，并保留结束后的 shell，方便回看日志。启动 tmux 后
   不能立刻离开：至少守到 XO 生成并完成必要 patch，且 Vitis link 已进入
   `vpl` / synthesis / implementation 早期阶段；若本轮目标只是 XO，则守到 XO
   文件存在、patch 通过、`make -q` 认为 XO target up-to-date。
7. 当前优化目标已从 single SpMV 本体切到 full-PCG controller/dot/update 路径。
   2026-05-29 one-shot `CuperPcgSpmv(...)` demo 已能跑完整 `thermal2`，共同成功点
   接近满血 `Cuper(...)`，后续主要作为 single-SpMV 回归基线和边界检查使用；
   不要再把 `Pcg_Single*` controller/command/stop/writer-done 壳作为 single
   SpMV 优化目标。full-PCG 性能优化只在 `CuperPcg(...)` 路径处理，优先看
   `detail/pcg_controller.hpp` 的 `dot_p_ap`、`update_xr`、`update_p`、
   `P_spmv` / `AP_spmv` 消费、HBM 访问模式、stage timer 和 service drain/stop
   开销。若要证明性能提升，必须跑 full-PCG 软件或硬件验证；不能只凭 single
   SpMV demo 结论判断。
8. single SpMV baseline/回归记录维护在
   `docs/bitstream_summaries/2026-05-28-cuper-tapa-spmv-single-optimization/`；
   full-PCG controller/dot/update 优化记录维护在
   `docs/bitstream_summaries/2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/`。
9. 迭代优化时先测试、后写正式 `source.diff`。每轮 demo 必须更新
   `README.md`、`changes.md`、`testing.md` 和 HTML 结论；但只有 demo-only
   上板测试确认核心性能提升，或用户明确要求保留功能边界修复补丁时，才更新版本目录
   的正式 `source.diff`。性能退步、测试失败或只是跑通更大规模时，不覆盖上一份
   已验证有效的 `source.diff`。
10. 中文源码和文档优先写中文注释；英文只用于代码符号、命令和固定术语。

## 4. 常用验证

Host smoke：

```bash
make cuper-tapa-pcg-fpga-host
make run-cuper-pcg-tapa-fpga DATASET=data/generated/cgsolver/n512 MAX_ITERS=1 DIFF_TOL=1e-3
```

提交或结束代码/文档改动前：

```bash
git diff --check
git status --short
```

硬件测试和 demo 对比按 `docs/codex/testing.md` 执行。新 demo 默认只跑 demo
本身，四大标准版本默认复用既有记录；不要只说“理论上可以”。

## 5. 提交与推送纪律

1. 只提交本次任务相关文件，不顺手提交无关脏改动。
2. 替换 bitstream 的提交必须包含：
   - 新 `395bitstream/*.xclbin`
   - 新 `395bitstream/*.xclbin.info`
   - 更新后的 `395bitstream/README.md`
   - 旧版归档 README 和需要同步的归档文件
3. 大文件 push 出现 GitHub 50 MB warning 时要在回复里说明。不要假装没看见。
4. 用户明确要求 push 时再 push；否则保留本地提交或本地改动并说明状态。

## 6. 回复纪律

1. 用户问“现在是什么阶段”，直接说当前 tmux/log 阶段、已用时间、可能剩余压力点。
2. 用户问“能不能过某规模”，必须区分编码/位宽硬边界、stream/死锁、host/runtime
   返回问题、routing/timing 问题。
3. 用户问“这是不是顶层/并行/流水”，用 `CuperPcg` 的 task graph 解释，不要泛泛讲 FPGA。
4. 回答里给出具体文件路径和命令，避免只给概念。
