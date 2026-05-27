# 报告生成与口径纪律

1. 面向同步/对比的 HTML 报告放 `395bitstream/`。
2. bitstream 版本级详细总结放 `docs/bitstream_summaries/<版本目录>/`。
   一个版本一个子文件夹，至少包含：
   - `README.md`：测试摘要、关键结论、日志路径、是否建议晋级；
   - `changes.md`：这一版相对上一标准版改了什么、预期收益、实际结果。
   - `testing.md`：测试命令、数据集、关键输出、失败边界、待补项；
   - `code_reading_guide.md`：版本相关代码阅读指南，复杂版本或用户要求时补；
   - `source.diff`：只在板上测试确认性能提升，或用户明确要求保留某个功能边界
     修复补丁后提供/更新。不要因为每轮 demo 都刷新正式 diff，也不要把大段源码
     直接贴进 Markdown。
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
   若本轮 demo 测试失败或性能退步，只更新 HTML/Markdown 测试结论，不覆盖上一份
   已验证有效的 `source.diff`。

## HTML 视图口径

`395bitstream/cuper_spmv_u55c_compare_20260524.html` 是同步给人看的总览页。
更新它时优先保证“当前 demo 诊断”和“历史/标准对比”分清楚，不要为了把所有数据放进
同一张图而混掉测试口径。

1. 新 demo 上板后，HTML 必须有单独的 demo-only 测试块，写清楚：
   - demo `.xclbin` 路径、UUID、SHA256；
   - 日志目录；
   - 本轮是否只跑 demo；若有误跑标准版，明确说明误跑日志不作为本轮结论；
   - 本轮运行类型：single SpMV、full-PCG init-only、full-PCG 1iter 或其它；
   - full-PCG demo 写 init-only / 1iter 的返回状态和关键时间；single SpMV demo
     写 `spmv_avg`、diff、rc、timeout 边界，并明确“本轮未跑 PCG，无 init/1iter
     过程”。
2. `TAPA PCG 分段时间` 和 `Init 与 1iter 差值` 是当前诊断视图。测试新
   TAPA full-PCG demo 后，这两块必须刷新为本轮 demo-only 实测数据，不能继续展示旧
   TAPA full-PCG 标准版数据。
   如果本轮只跑 single SpMV demo，不得把 SpMV 数据填进这两块；PCG 相关图表和
   数据保持上一次 full-PCG 测试状态不动，并在标题、图注或表格说明里明确标注：
   “本轮为 single SpMV 测试，未跑 PCG，无 init/1iter 过程；本块保留上一轮
   full-PCG 数据”。
3. 历史趋势图如果仍使用 2026-05-24/26 的标准/基线记录，图注必须明确写：
   “不包含当前 demo-only 新测数据”。不要把当前 demo 曲线塞进历史标准趋势图里，
   除非图名和图注已经改成 demo 对比口径。
4. 标准/上一 demo/本 demo 对比必须单独成块。需要两组视图：
   - `SpMV / AP 路径对比`：折线图加表格；
   - `一次迭代对比`：折线图加表格。
5. `SpMV / AP 路径对比` 的标准基准是 standalone TAPA Cuper SpMV 标准曲线：
   `395bitstream/cuper-tapa-spmv-u55c-20260522.xclbin` 的 `spmv_avg`。
   不要用 TAPA full-PCG 标准版里的内嵌 `iter_spmv` 当 SpMV 标准基准。
   若当前 demo 是从 `CuperPcg` 抠出来的 `cuper-tapa-spmv` 单 SpMV demo，应把它
   标成 `PCG SpMV 抽出版` 或等价名称，直接在这一组里和满血 standalone TAPA
   Cuper SpMV 对比 `spmv_avg`、成功/timeout 边界和 diff。
   single SpMV demo 的新增数据只进入这一组和 demo-only 块；不要同时改写
   `TAPA PCG 分段时间`、`Init 与 1iter 差值` 或 `一次迭代对比` 的数值。
6. `一次迭代对比` 的标准基准才是 TAPA full-PCG 标准版：
   `395bitstream/cuper-tapa-pcg-fpga-u55c-20260525.xclbin` 的
   `1iter kernel_reported`。
   PCG 抽出版 single SpMV demo 不进入这一组，只有回填到 full-PCG 并上板测试后，
   才加入一次迭代对比。若 HTML 在 single SpMV 测试后仍展示这一组，必须保留旧数据
   并标注“本轮未跑 PCG，无一次迭代新数据”。
7. 分段名必须按当前 kernel 语义标注。若某版把第二轮 SpMV 的 AP 后处理拆开，
   raw `iter_spmv` 只覆盖 packed AP 接收/缓存，则 HTML 里应标为 `iter recv`，
   并把 `iter recv + dot_p_ap` 合成 `AP path` 后再用于 SpMV/AP 路径对比。
   不要把 raw `iter_spmv` 单独解释成完整 SpMV 性能。
8. 表格和图必须同时表达失败边界：
   - 对共同成功的数据点画折线；
   - 对 timeout/failed 数据点在表格里写 `timeout` 或 `failed`，不要用 0、
     空白或连线外推；
   - 完整 `thermal2` 如果只有本 demo 返回，表格保留这一行，图中只画本 demo 点。
9. 每个对比结论必须分开写“能跑到多大”和“性能是否进步”。能跑完整
   `thermal2` 是功能边界进步；若共同数据点 1iter 变慢，就必须明确写性能退步，
   不能用“跑通最大规模”替代性能结论。
10. 图表中的数字必须能回溯到 raw log、当前 HTML 总表或版本目录 `testing.md`。
    若手工合成指标，例如 `AP path = iter recv + dot_p_ap`，表头和正文必须写明公式。
