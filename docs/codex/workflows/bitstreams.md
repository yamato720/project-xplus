# Bitstream 同步与归档纪律

## 1. bitstream 同步目录

`395bitstream/` 永远放当前要同步到服务器和用于对比的最新版本。

要求：

1. 四条主线各自保留一个当前首选 `.xclbin`。
2. 文件名保持稳定、简洁，不往文件名里塞 `debug`、`fix`、频率、UUID 等额外信息。
3. 版本差异写进 `395bitstream/README.md` 和 `.xclbin.info`，不要靠改文件名表达。
4. 对比报告 HTML 也放在 `395bitstream/`，文件名要能看出测试对象和日期。
5. 替换 `.xclbin` 时必须同步替换对应 `.xclbin.info`。

## 2. demo bitstream 规则

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
   留一份 Markdown 详细总结，并包含：
   - `README.md`：版本摘要和是否建议晋级；
   - `changes.md`：说明“这一版改了什么”；
   - `testing.md`：测试命令、关键输出和失败边界；
   - `code_reading_guide.md`：版本相关代码阅读指南，复杂版本或用户要求时补；
   - `source.diff`：这一版源码/脚本改动的可逆补丁。

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
