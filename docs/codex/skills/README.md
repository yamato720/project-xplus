# Codex Skills

这里存放 Project-XPlus 可同步到其它机器的 Codex skill 副本。真正启用时，要把
对应 skill 目录复制到目标机器的 `$CODEX_HOME/skills/` 下。

默认 `$CODEX_HOME` 通常是：

```text
~/.codex
```

## project-xplus

用途：让新 Codex 会话在处理 Project-XPlus / Cuper / TAPA / U55C / bitstream
相关任务时，先读仓库内纪律文档。

安装到当前机器：

```bash
mkdir -p ~/.codex/skills
cp -r Project-XPlus/docs/codex/skills/project-xplus ~/.codex/skills/
```

在服务器上安装：

```bash
cd /path/to/project-x
mkdir -p ~/.codex/skills
cp -r Project-XPlus/docs/codex/skills/project-xplus ~/.codex/skills/
```

如果服务器的 Codex 使用不同 `CODEX_HOME`，改成：

```bash
mkdir -p "$CODEX_HOME/skills"
cp -r Project-XPlus/docs/codex/skills/project-xplus "$CODEX_HOME/skills/"
```

## 怎么触发

新对话里提到 `Project-XPlus`、`Project-X`、`Project-XS`、`Cuper`、`TAPA`、
`U55C`、`bitstream`、`395bitstream`、`docs/bitstream_summaries` 等任务时，
Codex 会按 skill 描述加载它。

也可以显式说：

```text
使用 project-xplus skill
```

skill 本体只做入口，不替代仓库文档。事实源仍然是：

```text
Project-XPlus/docs/codex/coding.md
Project-XPlus/docs/codex/testing.md
Project-XPlus/395bitstream/README.md
```

`395bitstream/` 当前允许两个 demo 槽位并存：`cuper-tapa-spmv` single SpMV
候选和 `cuper-tapa-pcg` full-PCG 候选。新 demo 只覆盖同主线旧 demo，四个标准
bitstream 仍需用户明确确认后才能归档替换。

当前 skill 目标边界是：single SpMV demo 不再承载 PCG service/control 优化。
`CuperPcgSpmv(...)` 保留历史 kernel 名和 demo 构建入口，但内部应走和
`Cuper(...)` 一样的 one-shot Cuper SpMV task graph。PCG 的控制和 service 优化
只在 full `CuperPcg(...)` 路径处理；若要证明某个 SpMV 改动进入 PCG，必须修改
`pcg_spmv_service.hpp` / `pcg_controller.hpp` 等 full-PCG 实际使用路径，并补跑
full-PCG 验证。围绕该目标的连续修改记录统一维护在：

```text
Project-XPlus/docs/bitstream_summaries/2026-05-28-cuper-tapa-spmv-single-optimization/
```

其中 `coding.md` 是入口页，细分流程放在：

```text
Project-XPlus/docs/codex/workflows/
```
