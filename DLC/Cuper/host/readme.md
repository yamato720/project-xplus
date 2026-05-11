# Cuper Host

这里放 `Cuper` 独立子项目自己的 host 侧代码。

建议后续放置：

- `main.cpp`
- `xrt_host.cpp`
- 参数解析
- 数据加载
- 调试脚本对应的 host glue code
- 与上层工程共享类型之间的薄适配层

当前原则：

- 先保证 `Cuper` 自己能独立运行
- 不和上层 `Project-XPlus/host/` 混写
- 等接口稳定后，再决定哪些部分值得抽回公共层
