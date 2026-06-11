# Cuper Jacobi Iteration Docs

这里放 `Cuper-jacobi-iteration` 独立实验目录自己的设计说明与迁移记录。

建议至少保留：

- `CuperJacobiIteration(...)` HLS 顶层接口约定
- kernel 迁移状态
- 与上层 `Project-XPlus` 的差异说明
- 后续是否回并主线的判断记录
- Cuper SpMV 数学到算法映射：`cuper_spmv_math_mapping.md`
- service 化 SpMV 边界说明：`spmv_service.md`
- Jacobi 迭代算法说明：`jacobi_iteration.md`
- Jacobi kernel 实现方案：`jacobi_implementation_plan.md`
- 当前测试流程和已记录数据：`testing.md`
- 上板 `Finish` 挂住的排障和修改建议：`hardware_finish_hang_fix_suggestions.md`
