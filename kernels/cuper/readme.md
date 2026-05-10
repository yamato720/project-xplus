# cuper kernel migration area

这个目录预留给 `cuper` 迁入后的 kernel 相关代码。

建议放这里的内容：

- HLS 顶层 kernel
- kernel 内部 helper
- 临时保留的旧项目 kernel 原型
- 为移植验证准备的最小 kernel demo

当前阶段不要求结构很完整，重点是：

- 先把要迁移的 kernel 独立落下来
- 尽量保持和原项目接口一致，方便逐个替换
- 等 kernel 形态稳定后，再决定是否并回 `kernels/` 主路径

如果某个 kernel 只是占位版本，也可以先保留最小可编译骨架。
