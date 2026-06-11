# Bitstream 同步与归档纪律

## 1. bitstream 同步目录

`395bitstream/` 放当前要同步到服务器和用于对比的最新标准版本；需要测试新候选时，
可以临时放 demo。同步目录的上限是“五个标准成品 + 三个当前 demo 候选槽”：

- 五个标准成品分别对应五条主线；
- 三个 demo 槽位用 `-demo` 后缀标识，目前分别服务 `cuper-tapa-spmv`、
  `cuper-tapa-pcg` 和 `cuper-tapa-jacobi`；
- 新生成的 demo 进入 `395bitstream/` 时，优先覆盖同主线旧 demo 候选槽，不新增一串
  历史 demo 文件；不同主线 demo 可以同时保留，上限是三个。用户要求归档后，demo
  可以移入 `bitstream_archive/`，同步目录回到只保留五个标准成品。

要求：

1. 五条主线各自保留一个当前首选标准 `.xclbin`；尚未生成标准 bitstream 的主线要在
   `395bitstream/README.md` 写清楚“暂无标准 bitstream”，不能用旧文件占位。
2. 文件名保持稳定、简洁，不往文件名里塞 `debug`、`fix`、频率、UUID 等额外信息。
3. 版本差异写进 `395bitstream/README.md` 和 `.xclbin.info`，不要靠改文件名表达。
4. 对比报告 HTML 也放在 `395bitstream/`，文件名要能看出测试对象和日期。
5. 替换 `.xclbin` 时必须同步替换对应 `.xclbin.info`。
6. 覆盖 demo 候选槽不需要归档旧 demo 二进制，但必须在 `395bitstream/README.md`
   和对应 `docs/bitstream_summaries/` 中说明当前 demo 文件已经变更；旧 demo 的
   测试结论只能继续作为历史记录，不能再套到当前 demo 文件上。
7. 用户要求归档当前 demo 时，把 demo `.xclbin` 和 `.xclbin.info` 移入
   `bitstream_archive/YYYY-MM-DD-<主线或目标>-<简短原因>/`，更新归档 README、
   `395bitstream/README.md` 和对应版本记录。归档后的 `.xclbin` 继续受
   `.gitignore` 保护，默认不进 Git。

## 2. demo bitstream 规则

新生成的 bitstream 在用户明确认可前，一律只是候选版，不允许直接替换五条主线
的标准文件。

1. 候选 bitstream 必须仍归入五条基础版本之一：
   - `cuper-tapa-pcg`
   - `cuper-tapa-spmv`
   - `cuper-tapa-jacobi`
   - `cuper-notapa-pcg`
   - `cuper-notapa-spmv`
2. 候选 bitstream 放进 `395bitstream/` 时，文件名必须在 `.xclbin` 前加入
   `-demo` 后缀，例如：

```text
cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin
cuper-notapa-spmv-u55c-20260527-demo.xclbin
cuper-tapa-jacobi-u55c-20260610-demo.xclbin
```

3. 候选版必须同时放入对应 `.xclbin.info`：

```text
cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin.info
```

4. demo 文件可以在 `395bitstream/` 中短期存在，用于服务器同步和对比；但
   `395bitstream/README.md` 必须明确它是 demo，不是当前标准。
5. `395bitstream/` 最多保留三个当前 demo 候选槽。新 demo 可以直接覆盖同主线旧
   demo 文件和 `.xclbin.info`，但不能覆盖五条主线标准文件；不同主线 demo
   可以共存，默认是 `cuper-tapa-spmv` 一个、`cuper-tapa-pcg` 一个、
   `cuper-tapa-jacobi` 一个。归档后同步目录
   可以没有 demo 文件。
6. demo 上板测试默认只跑新 demo 本身；同主线当前标准 bitstream 和其它标准
   bitstream 默认复用当前 HTML、`docs/bitstream_summaries/` 和已归档日志，不要
   自动重跑全部标准版本。只有用户要求、标准 bitstream/host 变化、demo 结果与旧记录
   矛盾，或旧记录缺少同口径失败边界时，才重跑标准/旧基线。
7. 只有当用户明确表示“满意/替换/设为标准”后，才允许按归档流程把当前标准
   归档，并把 demo 晋级为稳定标准文件名。
8. demo 晋级后，标准文件名继续保持五条主线的简洁命名，不保留 `demo`、
   `debug`、`fix` 等额外后缀；差异写入 README 和 `.info`。
9. demo 的数据和结论必须进入 `395bitstream/` 下的 HTML 报告，不能只写在
   `logs/` 目录。
   HTML 视图口径按 `docs/codex/workflows/reports.md` 执行。
   对 TAPA full-PCG demo，HTML 的当前诊断段落（例如 `TAPA PCG 分段时间`、
   `Init 与 1iter 差值`）必须随本轮 demo-only 实测刷新；标准/上一 demo/本 demo
   的历史对比应放在独立对比块里。
   其中 SpMV 对比组的标准基准是 standalone TAPA Cuper SpMV 标准曲线
   `cuper-tapa-spmv-u55c-20260522.xclbin`；一次迭代对比组才使用 TAPA full-PCG
   标准版的 `1iter kernel_reported`。
   对 `CuperPcgSpmv` 这类 `cuper-tapa-spmv` 单 SpMV demo，按
   `cuper-tapa-spmv` 主线 demo 同步；HTML 中要写清楚它到底是历史 PCG service
   抽出版，还是当前 Cuper-compatible one-shot 图，并只进入 SpMV 对比组，不进入
   一次迭代对比组。PCG 诊断和一次迭代区域保留旧数据，但必须标注“本轮未跑 PCG，
   无 init/1iter 过程/无一次迭代新数据”。
10. 每个 demo 或标准替换版本都要在 `docs/bitstream_summaries/<版本目录>/`
   留一份 Markdown 详细总结，并包含：
   - `README.md`：版本摘要和是否建议晋级；
   - `changes.md`：说明“这一版改了什么”；
   - `testing.md`：测试命令、关键输出和失败边界；
   - `code_reading_guide.md`：版本相关代码阅读指南，复杂版本或用户要求时补；
   - `source.diff`：只在 demo-only 测试确认性能提升，或用户明确要求保留某个
     功能边界修复补丁后更新。测试失败或性能退步时，不覆盖上一份已验证有效的
     `source.diff`。

## 3. 替换后至少记录

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

## 4. 标准 bitstream 替换与归档流程

替换 `395bitstream/` 里的任何标准 `.xclbin` 前，必须先归档旧版。demo 版
只能在用户确认满意后晋级为标准版；测试未完成或用户未确认时，不能覆盖标准
文件。

标准流程：

1. 确认新构建成功，日志里应有 `impl Complete` 或等价成功信息。
2. 先以 `-demo.xclbin` 形式放入 `395bitstream/`，同步 `.xclbin.info`。
3. 按 `docs/codex/testing.md` 先做 demo-only 上板测试，并用已有标准/基线记录做
   静态对照；不要自动重跑全部标准版本。
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
