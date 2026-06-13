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
- `Finish` 不返回且根因待定时的下一版 debug 改进方案：`deadlock_debug_improvement_plan.md`
- 2026-06-12 tail-drain demo 仍 `Finish` 不返回后的候选原因、细粒度监测点，以及
  2026-06-13 pre-Finish/empty-R demo 的后续验证入口：
  `finish_nonreturn_monitoring_points.md`
- 2026-06-13 entry mmap probe 上板仍失败后的 debug 结论和下一版修改建议：
  `entry_mmap_probe_failure_analysis.md`
