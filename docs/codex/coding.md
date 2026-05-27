# Project-XPlus Codex 工作纪律

这份文件给新对话里的 Codex 先读。目标是避免把 Project-XPlus 的几条
Cuper / PCG 路线、bitstream、build 目录和报告搞混。

## 0. 开始前先做

1. 先读本文件，再读 `395bitstream/README.md`。
2. 先跑 `git status --short`，确认当前工作树里已有改动，不要覆盖用户改动。
3. 涉及 bitstream、构建、运行脚本时，再查：
   - `Makefile`
   - `scripts/launcher.py`
   - `395bitstream/README.md`
   - `bitstream_archive/README.md`
4. 如果有 tmux 正在跑硬件构建，先确认阶段。源码改动不会影响已经进入
   Vitis/Vivado link/impl 的那次构建，但会影响下一次构建。必须在回复里说明。

## 1. 四条主线

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

## 2. bitstream 同步目录

`395bitstream/` 永远放当前要同步到服务器和用于对比的最新版本。

要求：

1. 四条主线各自保留一个当前首选 `.xclbin`。
2. 文件名保持稳定、简洁，不往文件名里塞 `debug`、`fix`、频率、UUID 等额外信息。
3. 版本差异写进 `395bitstream/README.md` 和 `.xclbin.info`，不要靠改文件名表达。
4. 对比报告 HTML 也放在 `395bitstream/`，文件名要能看出测试对象和日期。
5. 替换 `.xclbin` 时必须同步替换对应 `.xclbin.info`。

### demo bitstream 规则

新生成的 bitstream 在用户明确认可前，一律只是候选版，不允许直接替换四条主线
的标准文件。

1. 候选 bitstream 必须仍归入四条基础版本之一：
   - `cuper-tapa-pcg`
   - `cuper-tapa-spmv`
   - `cuper-notapa-pcg`
   - `cuper-notapa-spmv`
2. 候选 bitstream 放进 `395bitstream/` 时，文件名必须在 `.xclbin` 前加入
   `-demo` 后缀，例如：

```text
cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin
cuper-notapa-spmv-u55c-20260527-demo.xclbin
```

3. 候选版必须同时放入对应 `.xclbin.info`：

```text
cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin.info
```

4. demo 文件可以在 `395bitstream/` 中短期存在，用于服务器同步和对比；但
   `395bitstream/README.md` 必须明确它是 demo，不是当前标准。
5. demo 的测试数据必须和同主线当前标准 bitstream 动态对比。旧版跨主线基线
   默认复用当前 HTML 和 `docs/bitstream_summaries/` 的记录，除非用户要求、
   标准 bitstream/host 变化、或 demo 结果与旧记录矛盾。
6. 只有当用户明确表示“满意/替换/设为标准”后，才允许按归档流程把当前标准
   归档，并把 demo 晋级为稳定标准文件名。
7. demo 晋级后，标准文件名继续保持四条主线的简洁命名，不保留 `demo`、
   `debug`、`fix` 等额外后缀；差异写入 README 和 `.info`。
8. demo 的数据和结论必须进入 `395bitstream/` 下的 HTML 报告，不能只写在
   `logs/` 目录。
9. 每个 demo 或标准替换版本都要在 `docs/bitstream_summaries/<版本目录>/`
   留一份 Markdown 详细总结，并包含一个说明“这一版改了什么”的文档。

替换后至少记录：

```text
生成日期/时间
主线
构建目的或关键改动
DATA/KERNEL/HBM clock
UUID
SHA256
已知测试状态
旧版归档路径
```

## 3. bitstream 替换与归档流程

替换 `395bitstream/` 里的任何标准 `.xclbin` 前，必须先归档旧版。demo 版
只能在用户确认满意后晋级为标准版；测试未完成或用户未确认时，不能覆盖标准
文件。

标准流程：

1. 确认新构建成功，日志里应有 `impl Complete` 或等价成功信息。
2. 先以 `-demo.xclbin` 形式放入 `395bitstream/`，同步 `.xclbin.info`。
3. 按 `docs/codex/testing.md` 与同主线当前标准 bitstream 做动态对比。
4. 用户明确确认 demo 结果满意后，才进入标准替换流程。
5. 对新 `.xclbin` 生成或复制 `.xclbin.info`。
6. 计算新旧文件 `sha256sum`，必要时记录 UUID。
7. 在 `bitstream_archive/` 下新建目录：

```text
bitstream_archive/YYYY-MM-DD-<主线>-<简短原因>/
```

8. 把即将被替换的旧 `.xclbin` 和 `.xclbin.info` 放入该归档目录。
9. 在归档目录写中文 `README.md`，说明：
   - 归档对象
   - 被什么版本替换
   - 旧版频率/UUID/SHA256
   - 旧版已知表现或失败原因
   - 相关日志/报告路径
10. 再覆盖 `395bitstream/` 里的当前标准文件。
11. 更新 `395bitstream/README.md`。

如果 `.gitignore` 导致 `bitstream_archive/*.xclbin` 被忽略，不能悄悄漏掉：

- 若用户要求同步归档二进制，用 `git add -f` 加入归档 `.xclbin`。
- 若决定只本地保留二进制，必须在 README 和回复里明确说明。

## 4. 构建目录纪律

硬件构建不要使用裸 `build/` 混放。

规则：

1. 构建目录必须使用当前主线或当前 bitstream 名加 `-build` 后缀。
2. 典型形式：

```text
cuper-tapa-spmv-build/
cuper-notapa-spmv-build/
cuper-notapa-fpga-pcg-build/
cuper-tapa-pcg-fpga-u55c-YYYYMMDD-build/
```

3. 所有 build 目录必须被 `.gitignore` 忽略。
4. 如果新增 build 目录，要确认 `*-build/` 或显式条目已经覆盖。
5. 不要把临时 build 产物、`.xo`、`.link_summary`、`.compile_summary`、Vivado 中间目录提交进仓库。
6. 只把需要同步的 `.xclbin` 放到 `395bitstream/`，只把需要归档的文件放到 `bitstream_archive/`。

## 5. 构建与 tmux 纪律

1. 长时间硬件构建用 tmux，不在普通前台会话里跑。
2. tmux 命名要能看出主线，例如：

```text
project-xplus-cuper-tapa-pcg-hw
project-xplus-cuper-notapa-spmv-hw
```

3. tmux 命令结束后保留 shell，不要自动退出，方便回看日志。
4. 构建日志写入 `logs/`，文件名包含主线和时间戳。
5. 不要随意杀正在跑的实现。需要停止时先确认阶段、日志和是否有并行构建。
6. 如果构建进入 `vpl` / `impl` / routing，后续源码注释或重构不会影响这次结果；新源码只影响下一次构建。

## 6. TAPA 源码组织纪律

1. `DLC/Cuper/kernels/Cuper.cpp` 里有两个 TAPA top：
   - `Cuper(...)`：TAPA single SpMV
   - `CuperPcg(...)`：TAPA Cuper + FPGA 内 PCG
2. `CuperPcg` 是 `tapa compile -t CuperPcg` 的顶层。
3. 若拆文件，优先拆成被 `Cuper.cpp` include 的 `.hpp` / `.inc` 片段，让 TAPA 仍看到一个 translation unit。
4. 不要直接拆成多个 `.cpp`，除非同步修改 Makefile/TAPA compile 命令并验证。
5. TAPA task graph 里的 stream、core 链、HBM channel 映射要加中文注释；尤其是：
   - `PE_Param[0..16]`
   - `Vector_X_Stream[0..16]`
   - `Matrix_A_Stream[0..15]`
   - `Matrix_Mult_Vector_Stream[0..15]`
   - `Destroy_*` 链尾消费
6. 对 Cuper 内部 row 编码必须说明它不是原始全局 row，避免误判 `65535` 边界。

## 7. 测试纪律

代码改动后按风险选择测试，不要只说“理论上可以”。

建议顺序：

1. 纯软件/本地小数据 smoke：

```bash
make run-cuper-pcg-tapa-fpga DATASET=data/generated/cgsolver/n512 MAX_ITERS=...
```

2. `thermal2_n1024` 低迭代检查数值和返回路径。
3. 新 bitstream 上板后先跑 `MAX_ITERS=0` 或 `MAX_ITERS=1`，区分 init SpMV、迭代 SpMV、PCG 更新阶段问题。
4. 大矩阵问题按规模递增：

```text
thermal2_n65536
thermal2_n131072
thermal2_n262144
完整 thermal2
```

5. 每次硬件实测至少记录：

```text
dataset
n / nnz
tau
max_iters
iterations
status
residual_abs / residual_rel
kernel time
host wall time
bitstream path / SHA256 / UUID
```

## 8. 报告纪律

1. 面向同步/对比的 HTML 报告放 `395bitstream/`。
2. bitstream 版本级详细总结放 `docs/bitstream_summaries/<版本目录>/`。
   一个版本一个子文件夹，至少包含：
   - `README.md`：测试摘要、关键结论、日志路径、是否建议晋级；
   - `changes.md`：这一版相对上一标准版改了什么、预期收益、实际结果。
   - `source.diff`：推荐放。记录这一版相对上一标准源码的可逆补丁，
     不要把大段源码直接贴进 Markdown。
3. demo 的数据必须同时写进 HTML 报告和对应 Markdown 总结。HTML 给同步查看，
   Markdown 给详细追踪。
4. 设计解释、失败分析、实现版本说明放 `docs/design/`。
5. 旧版构建尝试、失败原因、routing 信息要写清楚日志路径，不要只贴最后一屏错误。
6. 频率、资源、时序结论要注明来自哪个报告：
   - `.xclbin.info`
   - timing summary
   - implementation report
   - TAPA HLS report
   - Vitis system estimate
7. `sw_emu` / HLS 资源估算不能当作 routed bitstream 的最终资源。
8. 如果版本总结目录里有 `source.diff`，回退说明使用
   `git apply --unidiff-zero -R docs/bitstream_summaries/<版本目录>/source.diff`；
   复现说明使用
   `git apply --unidiff-zero docs/bitstream_summaries/<版本目录>/source.diff`。

## 9. 提交与推送纪律

1. 提交前跑：

```bash
git status --short
git diff --check
```

2. 只提交本次任务相关文件，不顺手提交无关脏改动。
3. 替换 bitstream 的提交必须包含：
   - 新 `395bitstream/*.xclbin`
   - 新 `395bitstream/*.xclbin.info`
   - 更新后的 `395bitstream/README.md`
   - 旧版归档 README 和需要同步的归档文件
4. 大文件 push 出现 GitHub 50 MB warning 时要在回复里说明。不要假装没看见。
5. 用户明确要求 push 时再 push；否则保留本地提交或本地改动并说明状态。

## 10. 回复纪律

1. 用户问“现在是什么阶段”，直接说当前 tmux/log 阶段、已用时间、可能剩余压力点。
2. 用户问“能不能过某规模”，必须区分：
   - 编码/位宽硬边界
   - stream/死锁问题
   - host/runtime 返回问题
   - routing/timing 问题
3. 用户问“这是不是顶层/并行/流水”，用 CuperPcg 的 task graph 解释，不要泛泛讲 FPGA。
4. 回答里给出具体文件路径和命令，避免只给概念。
5. 对中文源码和文档，优先写中文注释；英文只用于代码符号、命令和固定术语。
