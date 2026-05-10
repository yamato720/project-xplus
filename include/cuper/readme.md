# cuper include migration area

这个目录预留给 `cuper` 迁入后的公共头文件。

建议放这里的内容：

- 数据结构定义
- 公共常量和配置
- host/kernel 共享接口
- 兼容旧项目命名或类型的适配头

放置原则：

- 先保证 `cuper` 自己能在这里自洽编译
- 如果某些类型已经和 `Project-XPlus` 现有定义重合，先做薄适配，不急着合并
- 等迁移完成后，再决定是否并入 `include/` 根目录下的公共接口
