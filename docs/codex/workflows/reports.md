# 报告生成与口径纪律

1. 面向同步/对比的 HTML 报告放 `395bitstream/`。
2. bitstream 版本级详细总结放 `docs/bitstream_summaries/<版本目录>/`。
   一个版本一个子文件夹，至少包含：
   - `README.md`：测试摘要、关键结论、日志路径、是否建议晋级；
   - `changes.md`：这一版相对上一标准版改了什么、预期收益、实际结果。
   - `testing.md`：测试命令、数据集、关键输出、失败边界、待补项；
   - `code_reading_guide.md`：版本相关代码阅读指南，复杂版本或用户要求时补；
   - `source.diff`：必须提供。记录这一版相对上一标准源码的可逆补丁，
     不要把大段源码直接贴进 Markdown。
3. demo 的数据必须同时写进 HTML 报告和对应 Markdown 总结。HTML 给同步查看，
   Markdown 给详细追踪；测试过程的详细 md 版本写入对应版本目录的
   `testing.md`。
4. 设计解释、失败分析、实现版本说明放 `docs/design/`。
5. 旧版构建尝试、失败原因、routing 信息要写清楚日志路径，不要只贴最后一屏错误。
6. 频率、资源、时序结论要注明来自哪个报告：
   - `.xclbin.info`
   - timing summary
   - implementation report
   - TAPA HLS report
   - Vitis system estimate
7. `sw_emu` / HLS 资源估算不能当作 routed bitstream 的最终资源。
8. 如果版本总结目录里有 `source.diff`，回退说明使用
   `git apply --unidiff-zero -R docs/bitstream_summaries/<版本目录>/source.diff`；
   复现说明使用
   `git apply --unidiff-zero docs/bitstream_summaries/<版本目录>/source.diff`。
