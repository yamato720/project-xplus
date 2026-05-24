# Cuper Include

这里放 `Cuper` 独立子项目自己的公共头文件。

建议后续放置：

- 数据结构定义
- 公共常量和配置
- host/kernel 共享接口
- 平台参数
- 与上层工程类型之间的适配头

放置原则：

- 先保证 `Cuper` 自己在本目录下自洽编译
- 如与上层 `Project-XPlus/include/` 有重合，先做薄适配
- 暂时不急着回并到上层公共 include
