# 2026-05-27 Cuper TAPA full-PCG packed feed/AP demo 总结

## 版本信息

- 主线：`cuper-tapa-pcg`
- 状态：demo bitstream 已生成并放入 `395bitstream/`，尚未替换当前标准版
- 持续目标：把 `CuperPcg` 内嵌 SpMV 性能优化到接近 standalone/native
  TAPA Cuper SpMV
- 当前 demo 方向：先做 `cuper-tapa-spmv` 单 SpMV demo，把 `CuperPcg` 里的 PCG
  服务化 SpMV 抠出来单独测试；它确认有效后再回填 full-PCG。代码里同时保留两套
  SpMV：满血 `Cuper(...)` / `cuper_spmv_tasks.hpp` 作为标准基准，和
  `CuperPcg(...)` / `pcg_spmv_service.hpp` 中为了 PCG 重复触发与 TAPA 编译约束
  调整过的服务化 SpMV。
- 记录策略：该目录是当前目标的唯一持续记录目录；后续围绕此目标的源码改动、
  demo bitstream 和测试结论继续更新这里，不再每版新建目录。正式 `source.diff`
  只在测试确认性能提升，或用户明确要求保留功能边界修复补丁后更新
- 对应标准版：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260525.xclbin`
- 当前 demo 命名：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin`
- 当前 demo UUID：`086a3345-ddf0-ffdd-b260-16ca5fa5223a`
- 当前 demo SHA256：`83baded1910ecb2c9e662f9ff6920fd8a55dbd2898ae69629c862714e17cf7f1`
- 当前 demo DATA/KERNEL/HBM：210/500/408 MHz
- 当前构建目录：`cuper-tapa-pcg-fpga-u55c-20260525-build/`
- 当前 tmux 会话：`project-xplus-cuper-tapa-pcg-hw`
- 当前构建日志：`logs/cuper_tapa_pcg_hw_parallel_20260528_222446.log`
- 历史 packed feed/AP demo：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin`
  / UUID `cc61e044-06f7-4726-8f18-773ac52ab1b2`

## 历史阶段

本目录同时保留旧 receive-path demo 的历史记录：

- `receive_path_demo.md`
- `receive_path_changes.md`
- `receive_path_source.diff`

这些历史文件对应旧 demo UUID `9474ef8e-571b-ae13-f898-890e3af8ae5e`，不再对应
当前 `395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin`。

2026-05-27 packed feed/AP demo 的板上测试记录仍保留在本文档和 `testing.md`；
它对应 UUID `cc61e044-06f7-4726-8f18-773ac52ab1b2`。当前 2026-05-29 full-PCG
demo 已覆盖 `395bitstream/` 的 `cuper-tapa-pcg` demo 槽，尚未上板测试。

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

目标收益应该首先体现在 `iter_spmv`，其次才可能反映到 `controller_total` 和
`kernel_reported`。这版不预期直接解决完整 `thermal2` 的 `ctrl=0x0` 边界问题。

如果板上实测有效，合理表现应是：

- `iter_spmv` 明显下降；
- `init_spmv` 有小幅下降或基本持平；
- full-PCG 1iter 总时间下降幅度小于 `iter_spmv`，因为 FP64 dot/update 仍在
  controller 路径里；
- `cuper-tapa-spmv` standalone 仍应作为 SpMV 性能上限。

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
- 2026-05-29 已用当前源码重新生成 full-PCG `CuperPcg` demo bitstream，并放入
  第二个 demo 槽：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin`。
  该文件只确认构建成功，尚未上板测试。

尚未完成：

- 未按本轮要求重跑四个标准 bitstream；旧标准数据只复用既有 HTML/Markdown 记录。
- 2026-05-29 full-PCG demo 尚未做 demo-only 上板测试。

## 是否建议晋级

暂不建议直接晋级为标准版。

理由：

- 当前 2026-05-29 full-PCG demo 只确认 bitstream 生成成功，尚未做 demo-only
  上板测试，不能作为标准替换依据。
- 正面：完整 `thermal2` init-only / 1iter 都能完成，说明这版改变了旧标准的
  full-size 失败边界。
- 风险：性能目标是让 `CuperPcg` 内嵌 SpMV 靠近 standalone TAPA Cuper，但本版
  在大规模 1iter 上 controller/update 代价很高；`thermal2_n262144` 的
  `kernel_reported=416.649 ms`，明显慢于既有标准记录中的约 `188.820 ms`。
- 结论：它更像“full-size 功能边界修复候选”，还不是当前 SpMV 性能优化目标的
  标准替换候选。

下一步不要继续直接做 full-PCG 性能 demo；应先把当前 PCG 服务化 SpMV 路径抽成
`cuper-tapa-spmv` 单 SpMV demo，和满血 TAPA Cuper SpMV 标准曲线对比
`spmv_avg`、成功/timeout 边界和数值误差。只有单 SpMV 路径确认有效后，再回填
full-PCG 并重新看 `init_spmv`、`iter_spmv`、`controller_total` 和
`kernel_reported`。

更新 HTML 时，single SpMV demo 的新增数据只写入 SpMV/demo-only 区域；PCG 分段、
`Init 与 1iter 差值` 和一次迭代区域保留当前 full-PCG 数据，但必须标注“本轮未跑
PCG，无 init/1iter 过程/无一次迭代新数据”。
