# Cuper Kernels

这里放 `Cuper` 独立子项目自己的 HLS kernel 代码。

建议后续放置：

- HLS 顶层 kernel
- kernel 内部 helper
- kernel 私有数据通路模块
- 为迁移验证准备的最小 kernel demo

当前原则：

- 先保证 `Cuper` 的 kernel 入口和上层工程彻底解耦
- 尽量保留原项目接口语义，方便逐个对齐
- 等形态稳定后，再判断是否需要回并到上层主线
