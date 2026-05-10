# cuper host migration area

这个目录预留给 `cuper` 迁入后的 host 侧代码。

建议放这里的内容：

- 运行入口
- 数据加载和参数解析
- 调试脚本对应的 host glue code
- 与现有 `Project-XPlus` host 路径之间的兼容层

迁移原则先保持简单：

- 先把 `cuper` 的 host 代码独立放在这里
- 先不要强行和当前 `main.cpp / xrt_host.cpp` 混写
- 等接口稳定后，再决定哪些部分抽回公共 host 层

如果后续只是临时验证某个移植组件，也可以直接在这里放最小可运行入口，不必一开始就整理成最终结构。
