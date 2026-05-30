# 2026-05-27 Cuper TAPA full-PCG packed feed/AP demo 总结

## 版本信息

- 主线：`cuper-tapa-pcg`
- 状态：2026-05-31 已用 II=1 controller 实验构建覆盖 `395bitstream/`
  的 full-PCG demo 槽；新 demo 已完成 `hw` bitstream 构建，尚未做 demo-only
  上板测试，未替换当前标准版
- 持续目标：优化 full `CuperPcg(...)` 的 controller/dot/update 路径，降低
  `1iter kernel_reported` 和 `controller_total`
- 当前 demo 方向：single SpMV one-shot demo 已接近满血 `Cuper(...)` 并能跑完整
  `thermal2`，后续作为回归基线；主优化转向 full-PCG 中的 `dot_p_ap`、
  `update_xr`、`update_p`、`P_spmv` / `AP_spmv` 消费、HBM 访问和 service
  drain/stop 开销。
- 记录策略：该目录是当前目标的唯一持续记录目录；后续围绕此目标的源码改动、
  demo bitstream 和测试结论继续更新这里，不再每版新建目录。正式 `source.diff`
  只在测试确认性能提升，或用户明确要求保留功能边界修复补丁后更新
- 对应标准版：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260525.xclbin`
- 当前 demo：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin`
- 当前 demo UUID：`0170fa86-6e62-cfc9-aa66-2d330dd72cf2`
- 当前 demo SHA256：`ec3a98b09d662611ce50c4c484cb6b55ad2e7dbcd712a0b6d7833b38e4579fc8`
- 当前 demo DATA/KERNEL/HBM：223/500/444 MHz
- 当前构建目录：`cuper-tapa-pcg-ii1-build/`
- 当前 tmux 会话：`project-xplus-cuper-tapa-pcg-ii1`
- 当前构建日志：`logs/cuper_tapa_pcg_ii1_hw_20260530_200825.log`
- 上一个已测试 demo UUID：`086a3345-ddf0-ffdd-b260-16ca5fa5223a`
- 历史 packed feed/AP demo：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin`
  / UUID `cc61e044-06f7-4726-8f18-773ac52ab1b2`

## 历史阶段

本目录同时保留旧 receive-path demo 的历史记录：

- `receive_path_demo.md`
- `receive_path_changes.md`
- `receive_path_source.diff`

这些历史文件对应旧 demo UUID `9474ef8e-571b-ae13-f898-890e3af8ae5e`，不再对应
后续任何同名 `cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin`。

2026-05-27 packed feed/AP demo 的板上测试记录仍保留在本文档和 `testing.md`；
它对应 UUID `cc61e044-06f7-4726-8f18-773ac52ab1b2`。当前 2026-05-29 full-PCG
demo 测试时覆盖过 `395bitstream/` 的 `cuper-tapa-pcg` demo 槽，并已完成
demo-only 上板测试。2026-05-31 新 II=1 controller 实验构建再次覆盖同名 demo
槽，因此 2026-05-29 的测试结论只作为历史记录保留，不再对应当前同步目录里的
同名 `.xclbin` 文件。

## 这一版做了什么

这一版不是单纯调频率，而是改 `CuperPcg` full-PCG 内部 SpMV 的周边数据路径：

1. 新增 `X_spmv` / `P_spmv` 两个 packed `float_v16` HBM 向量入口。
2. `Pcg_Vector_Loader` 不再等待 controller 从 `double X/P` 逐元素打包，而是直接
   从 `X_spmv` 或 `P_spmv` 顺序读包。
3. 新增 `AP_spmv` packed `float_v16` HBM 缓冲。
4. `iter_spmv` 收到 Cuper 的 `A*p` 输出后直接写 `AP_spmv[packet]`，避免先拆成
   16 个 double 写入旧 `AP`。
5. `dot_p_ap` 和 `update_xr` 改为按 `AP_spmv` 包读取，再在 lane 内转 double。
6. host 默认 ABI 更新到 `AP_spmv/X_spmv/P_spmv`，同时保留 `--legacy-abi`，方便
   用旧 host 路径跑当前标准 bitstream。

## 预期收益

历史 packed feed/AP 目标收益曾首先看 `iter_spmv`，其次才看
`controller_total` 和 `kernel_reported`。2026-05-29 single SpMV demo 已显示
SpMV 本体接近满血 Cuper，因此当前收益应优先体现在 full-PCG 的
`controller_total`、`dot_p_ap`、`update_xr`、`update_p` 和
`1iter kernel_reported`。

如果板上实测有效，合理表现应是：

- `1iter kernel_reported` 下降；
- `controller_total` 下降；
- `dot_p_ap`、`update_xr`、`update_p` 至少一个大头阶段明显下降；
- `AP path = iter recv + dot_p_ap` 不应恶化；
- 完整 `thermal2` 仍能返回，数值 diff 仍通过。

## 当前验证结论

已完成：

- host 编译通过；
- `n512 MAX_ITERS=1` TAPA 软件仿真通过；
- `thermal2_n1024 MAX_ITERS=1` TAPA 软件仿真通过；
- `hw_emu` 目标的 TAPA/HLS/XO 生成通过；
- `hw` bitstream 生成成功，`v++` 总耗时 4h39m35s；
- 2026-05-27 新 xclbin 和 `.xclbin.info` 当时已覆盖 `395bitstream/`
  full-PCG demo 槽位；
- 本轮 demo-only 测试显示性能退步；按当前版本管理纪律，不应因为这轮 demo
  自动刷新“性能有效补丁”的正式 `source.diff`。若保留相关源码 diff，只能标为
  功能边界修复候选，不能标为性能提升补丁。
- `code_reading_guide.md` 已记录本版代码阅读顺序和关键数据流。
- 2026-05-28 已按用户要求完成 demo-only 上板测试：
  `thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、
  `thermal2_n262144`、完整 `thermal2` 的 init-only 和 1iter 全部返回。
- 完整 `thermal2` 边界已变化：init-only 和 1iter 都从启动前 `ctrl=0x4`
  运行到完成后 `ctrl=0xe`，不再复现旧标准记录里的 `ctrl=0x0` 未完成。
- 2026-05-29 已用当前源码重新生成 full-PCG `CuperPcg` demo bitstream，测试时放入
  第二个 demo 槽：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin`；
  该旧 UUID 文件已完成 demo-only 上板测试。2026-05-31 该同步槽被 II=1 实验
  构建覆盖，旧测试结论只保留为历史记录。
- 2026-05-29 当前 full-PCG demo 的 `thermal2_n16`、`thermal2_n65536`、
  `thermal2_n131072`、`thermal2_n262144` 和完整 `thermal2` 的 init-only
  与 1iter 全部返回，direct ctrl 均为 `0x4 -> 0xe`，数值校验通过。
- 2026-05-31 II=1 controller 实验 `hw` bitstream 构建成功，并覆盖
  `395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin`。该文件当前 UUID
  为 `0170fa86-6e62-cfc9-aa66-2d330dd72cf2`，尚未做 demo-only 上板测试。

尚未完成：

- 未按本轮要求重跑四个标准 bitstream；旧标准数据只复用既有 HTML/Markdown 记录。
- 未证明当前 2026-05-31 II=1 full-PCG demo 是性能提升版；需要先做 demo-only
  init-only / 1iter 上板测试，再决定是否更新 HTML 当前诊断表和正式 `source.diff`。

## 是否建议晋级

暂不建议直接晋级为标准版。

理由：

- 正面：当前 II=1 实验能完成完整 `hw` route，最终 routed timing met；
  DATA/HBM clock 提升到 `223/444 MHz`。
- 风险：XO/HLS 报告显示多个被强制 `II=1` 的 controller loop 实际未达到 II=1，
  `update_xr_lanes` 和 `update_p_lanes` 仍分别是实际 II 18 / 24；是否改善板上
  `1iter` 只能看 demo-only 实测。
- 结论：它目前只是“可上板验证的 full-PCG controller II 实验候选”，不是标准
  替换候选。

下一步不要继续把主要精力放在 single SpMV 本体上；它作为回归基线保留。full-PCG
优化应直接面向 `detail/pcg_controller.hpp` 及相关 service/timer 路径，先拆
`dot_p_ap`、`update_xr`、`update_p` 的 HBM 读写和 lane 内 FP64 计算，再看
controller/service 收尾同步。更新 HTML 时，`TAPA PCG 分段时间` 和
`Init 与 1iter 差值` 必须展示当前 full-PCG demo-only 数据；single SpMV demo 只进入
SpMV/demo-only 和 SpMV 对比区域。
