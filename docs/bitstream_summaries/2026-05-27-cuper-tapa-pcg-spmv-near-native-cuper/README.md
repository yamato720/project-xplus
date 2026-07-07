# 2026-05-27 Cuper TAPA full-PCG controller/vector demo 总结

## 版本信息

- 主线：`cuper-tapa-pcg`
- 状态：2026-07-07 vector phase worker 拆分实验 demo 已完成软件级验证和
  `hw` bitstream 构建；尚未上板，未替换当前标准版
- 持续目标：优化 full `CuperPcg(...)` 的 controller/vector/update 路径，降低
  `1iter kernel_reported` 和 `controller_total`
- 当前源码方向：single SpMV one-shot demo 已接近满血 `Cuper(...)` 并能跑完整
  `thermal2`，后续作为回归基线；主优化转向 full-PCG 中的
  `iter_spmv_recv_dot`、`update_x`、`update_rz_reduce`、`update_p`、HBM 往返和
  service drain/stop 开销。本轮把这些大段 HBM 向量阶段从 `Pcg_Controller`
  下沉到常驻 `Pcg_Vector_Phases` worker，controller 只保留标量调度、alpha/beta、
  收敛判断和 metrics/status 写回。
- 记录策略：该目录是当前目标的唯一持续记录目录；后续围绕此目标的源码改动、
  demo bitstream 和测试结论继续更新这里，不再每版新建目录。正式 `source.diff`
  只在测试确认性能提升，或用户明确要求保留功能边界修复补丁后更新
- 对应标准版：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260525.xclbin`
- 当前 demo 文件：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260707-demo.xclbin`
- 当前 demo UUID：`1de9a25a-0257-8c9d-e39d-a470554d0f20`
- 当前 demo SHA256：`4b2ab1b8b10b27917947b044511da73812ddf688145719146780d21ad60baf25`
- 当前 demo `.info` SHA256：`fb4f0c8c09eb43c0738f420bc0c35a1c4f4a1f63b308ea6577b458b2ffbcb9a1`
- 当前 demo DATA/KERNEL/HBM：228/500/422 MHz
- 当前 routed timing：WNS `-1.043 ns`，TNS `-24489.869 ns`，setup failing
  endpoints `69563`；`impl Complete` 但 timing 未收敛
- 当前构建目录：`cuper-tapa-pcg-fpga-u55c-20260525-build/`
- 当前 tmux 会话：`project-xplus-cuper-tapa-pcg-hw`
- 当前构建日志：`logs/cuper_tapa_pcg_hw_20260707_131157.log`
- 当前上板测试日志：尚未上板
- 归档 controller-split demo：`bitstream_archive/2026-05-31-tapa-pcg-controller-split-demo/`
  / UUID `1d536c39-f561-340b-7efc-ac2c8440543d`
- 上一个 II=1 demo UUID：`0170fa86-6e62-cfc9-aa66-2d330dd72cf2`
- 历史 packed feed/AP demo：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin`
  / UUID `cc61e044-06f7-4726-8f18-773ac52ab1b2`

## 历史阶段

本目录同时保留旧 receive-path demo 的历史记录：

- `receive_path_demo.md`
- `receive_path_changes.md`
- `receive_path_source.diff`
- `slr_utilization_history.md` 记录 2026-05-24 到 2026-06-02 几代 Cuper/TAPA
  构建的 SLR 使用、SLL 连接和 route 失败归因。

这些历史文件对应旧 demo UUID `9474ef8e-571b-ae13-f898-890e3af8ae5e`，不再对应
后续任何同名 `cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin`。

2026-05-27 packed feed/AP demo 的板上测试记录仍保留在本文档和 `testing.md`；
它对应 UUID `cc61e044-06f7-4726-8f18-773ac52ab1b2`。当前 2026-05-29 full-PCG
demo 测试时覆盖过 `395bitstream/` 的 `cuper-tapa-pcg` demo 槽，并已完成
demo-only 上板测试。2026-05-31 的 II=1 controller 实验构建再次覆盖同名 demo
槽，随后 controller-split 实验构建又覆盖该槽，并已在 2026-05-31 归档到
`bitstream_archive/2026-05-31-tapa-pcg-controller-split-demo/`。之后同步目录里的
full-PCG demo 槽换成 `cuper-tapa-pcg-fpga-u55c-20260531-demo.xclbin` packed timing
实验；2026-07-07 又换成
`cuper-tapa-pcg-fpga-u55c-20260707-demo.xclbin` vector phase worker 实验。因此
2026-05-29、旧 II=1 demo、controller-split demo 与 2026-05-31 packed timing
demo 的测试结论都只作为历史记录保留，不再对应当前同步目录里的 full-PCG demo
文件。

## 这一版做了什么

这一版不是单纯调频率，而是改 `CuperPcg` full-PCG 内部 SpMV 的周边数据路径和
controller 主状态访问：

1. 新增 `X_spmv` / `P_spmv` 两个 packed `float_v16` HBM 向量入口。
2. `Pcg_Vector_Loader` 不再等待 controller 从 `double X/P` 逐元素打包，而是直接
   从 `X_spmv` 或 `P_spmv` 顺序读包。
3. 新增 `AP_spmv` packed `float_v16` HBM 缓冲。
4. `iter_spmv` 收到 Cuper 的 `A*p` 输出后直接写 `AP_spmv[packet]`，避免先拆成
   16 个 double 写入旧 `AP`。
5. 当前 packed timing 版把 `p^T AP` 合入 `iter_spmv_stream`，接收 `A*p` 时同步
   读 packed `P` 并做 FP64 累加；`AP_spmv` 仍供后续 residual update 读取。
6. `B/M_inv/X/R/Z/P` 主状态从标量 `double` mmap 改为 packed `double_v8` mmap，
   每个 HBM packet 一次承载 8 个 FP64 lane。
7. host 默认 ABI 更新到 `AP_spmv/X_spmv/P_spmv`，同时保留 `--legacy-abi`，方便
   用旧 host 路径跑当前标准 bitstream。

2026-07-05 源码轮次继续只改 full `CuperPcg(...)` controller/update 路径：

- 保持 `CuperPcg(...)` 顶层参数顺序、Cuper 数据格式、SpMV service 和
  `CuperPcgSpmv(...)` single-SpMV demo 不变；
- `iter_spmv_recv_dot` 仍接收 `A*p`、写 `AP_spmv`，并融合计算 `p^T AP`；
- 原迭代更新语义从 `update_xr + update_z_reduce` 改为
  `update_x + update_rz_reduce`；
- `update_x` 只读 `X/P` 并写回 `X = X + alpha * P`；
- `update_rz_reduce` 读 `R/AP_spmv/M_inv`，同时写回 `R`、写 `Z` 并累计
  `rz_new/rr_new`，避免先写 `R` 后再为 `Z/reduction` 重读一次 `R`；
- host 输出标签同步改为 `update_x` 和 `update_rz_reduce`，
  `pcg_vector_total` 仍表示 `init_zp + update_x + update_rz_reduce + update_p`。

2026-07-07 源码轮次继续保持顶层 ABI、connectivity、Cuper SpMV 数据格式、
`Cuper(...)` 和 `CuperPcgSpmv(...)` 不变，只拆 full-PCG 内部向量阶段：

- 新增 `detail/pcg_vector_phases.hpp`，定义 `PcgVectorCommand`、
  `PcgVectorResult` 和常驻 task `Pcg_Vector_Phases`。
- `Pcg_Controller` 不再直接持有 `B/M_inv/X/R/Z/P/AP_spmv/P_spmv` mmap，也不再直接
  消费 `Pcg_Spmv_Stream`；它通过 `Vector_Command_Stream` / `Vector_Result_Stream`
  调度向量 worker。
- `Pcg_Vector_Phases` 接管 `init_spmv`、`init_zp`、`iter_dot`、`update_x`、
  `update_rz_reduce` 和 `update_p` 的大段 HBM 向量访问；每个非 stop command
  返回一条 result，controller 用 result 作为 stage 完成边界。
- `AP_spmv` 和 `P_spmv` 仍保留为 HBM 断点；本轮不做 AP stream 旁路或跨迭代
  `P` stream 旁路。

## 预期收益

历史 packed feed/AP 目标收益曾首先看 `iter_spmv`，其次才看
`controller_total` 和 `kernel_reported`。2026-05-29 single SpMV demo 已显示
SpMV 本体接近满血 Cuper，因此当前收益应优先体现在 full-PCG 的
`controller_total`、`iter_spmv_recv_dot`、`init_zp`、`update_x`、
`update_rz_reduce`、`update_p` 和
`1iter kernel_reported`。

如果板上实测有效，合理表现应是：

- `1iter kernel_reported` 下降；
- `controller_total` 下降；
- `iter_spmv_recv_dot`、`update_x`、`update_rz_reduce`、`update_p` 至少一个大头阶段明显下降；
- `iter recv + dot` 不应恶化；
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
  为 `0170fa86-6e62-cfc9-aa66-2d330dd72cf2`。
- 2026-05-31 II=1 controller 实验 demo-only 上板测试完成：
  `thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、
  `thermal2_n262144` 和完整 `thermal2` 的 init-only 与 1iter 全部返回，
  direct ctrl 均为 `0x4 -> 0xe`，数值校验通过。完整 `thermal2` 的
  1iter `kernel_reported=1767.8254 ms`，比 2026-05-29 旧 UUID 的
  `1960.0357 ms` 改善；`thermal2_n262144` 的 1iter 为 `385.1288 ms`，
  仍明显慢于标准版 `188.8202 ms` 和上一 demo `182.5644 ms`。
- 2026-05-31 controller-split 实验 `hw` bitstream 构建成功，并覆盖
  `395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin`。该文件当前 UUID
  为 `1d536c39-f561-340b-7efc-ac2c8440543d`，SHA256 为
  `bc58605b36c98b29d84ce14939b95f8fc6b84bb7a505007fda95458545a349b8`。
- 2026-05-31 controller-split 实验 demo-only 上板测试完成：
  `thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、
  `thermal2_n262144` 和完整 `thermal2` 的 init-only 与 1iter 全部返回，
  direct ctrl 均为 `0x4 -> 0xe`，数值校验通过。完整 `thermal2` 的
  1iter `kernel_reported=954.0779 ms`，比上一 II=1 demo 的 `1767.8254 ms`
  明显改善；`thermal2_n262144` 的 1iter 为 `211.3790 ms`，仍略慢于标准版
  `188.8202 ms` 和上一 demo `182.5644 ms`。
- 2026-05-31 controller-split 实验 full-run 完整 PCG 补测完成到
  `thermal2_n262144`：`thermal2_n16` 到 `thermal2_n262144` 分别跑到
  `1/60/81/96/104/113/120` 次并收敛，`thermal2_n262144`
  `kernel_reported=15263.805830 ms`。完整 `thermal2` 禁用 host 60 秒超时后
  约 490 秒仍为 `ctrl=0x0`，按用户要求停止，记录为未完成。
- 2026-05-31 已按用户要求把 controller-split demo 作为新存档点移入
  `bitstream_archive/2026-05-31-tapa-pcg-controller-split-demo/`。`.xclbin` 只做本地
  留档，`.xclbin.info` 和归档 README 记录 UUID、clock、测试结论和硬件报告入口。
- 2026-05-31 packed timing 实验 `hw` bitstream 构建成功，并作为当前
  `395bitstream/cuper-tapa-pcg-fpga-u55c-20260531-demo.xclbin` 同步槽。该文件 UUID
  为 `f5b4fb4b-d7cc-f559-b5ba-29e2e6a88668`，SHA256 为
  `a8df40e1bf21774c7608c329fd591012b84744a18dcf4e8b0dd36672d64ccf72`，
  DATA/KERNEL/HBM clock 为 `172/500/405 MHz`。
- 2026-05-31 packed timing demo-only 上板测试完成：
  `thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、
  `thermal2_n262144` 和完整 `thermal2` 的 init-only 与 1iter 全部返回，
  direct ctrl 均为 `0x4 -> 0xe`，数值校验通过。完整 `thermal2` 的
  1iter `kernel_reported=944.123210 ms`，比归档 controller-split demo 的
  `954.0779 ms` 略快；`thermal2_n262144` 的 1iter 为
  `210.319328 ms`，仍慢于标准版 `188.8202 ms` 和上一 demo `182.5644 ms`。
  当前 `B/M_inv/X/R/Z/P` 已是 512-bit `double_v8` 端口，但完整 `thermal2`
  1iter 中 controller 内 `pcg_vector_total=730.9200 ms`，`init_spmv +
  iter_spmv_recv_dot=189.3382 ms`；瓶颈仍主要在 controller/vector 阶段，而不是
  raw SpMV 本体。
- 2026-05-31 main reduction 源码实验完成到软件仿真和 XO/HLS 报告：
  `init_zp` / `update_z` 的 `rz/rr` FP64 reduction 改成 8-bank 累加器。
  `thermal2_n16` 与 `thermal2_n1024` 的 1iter 软件仿真通过；HLS 中
  `init_zp` packet loop 从 II=5 降到 II=4，`update_z` packet loop 从 II=5
  降到 II=2。`p^T AP` banked reduction 试验会把 `iter_spmv_stream` 拉到
  II=11，已撤回；`2.1` 保持为存档对照，实际改动在 `main` 工作区。
- 2026-07-05 Callipepla 式 update 拆分源码验证完成：
  host 编译通过；`make build-cuper-tapa-pcg TARGET=sw_emu` 已完成 TAPA HLS、
  XO 生成和 XO patch，但 Vitis link 报该 `CuperPcg.xo` 只支持 `hw_emu/hw`、
  不支持 `sw_emu`；替代 local smoke
  `data/generated/cgsolver/n512 MAX_ITERS=1 DIFF_TOL=1e-3` 通过，host 输出已切到
  `update_x` / `update_rz_reduce` 标签。
- 2026-07-07 PCG 向量阶段 worker 拆分完成：
  `make cuper-tapa-pcg-fpga-host` 和 `make cuper-tapa-pcg-host` 通过；
  `make build-cuper-tapa-pcg TARGET=sw_emu` 的 TAPA HLS/XO/patch 通过，Vitis link
  仍因 XO target 只支持 `hw_emu/hw` 而失败；替代 local smoke
  `data/generated/cgsolver/n512 MAX_ITERS=1 DIFF_TOL=1e-3` 通过，
  `thermal2_n16 MAX_ITERS=1 DIFF_TOL=1e-3` 通过。
- 2026-07-07 已启动并完成 `make cuper-tapa-pcg-hw-tmux`：
  `CuperPcg.xclbin` 生成成功，`Run vpl: FINISHED. Run Status: impl Complete!`，
  v++ 总耗时 `5h 5m 33s`。同步到
  `395bitstream/cuper-tapa-pcg-fpga-u55c-20260707-demo.xclbin`；该 demo 尚未上板。

尚未完成：

- 未按本轮要求重跑四个标准 bitstream；旧标准数据只复用既有 HTML/Markdown 记录。
- 未证明当前 2026-07-07 vector phase worker full-PCG demo 达到标准替换性能；
  当前只有软件 smoke 和 `hw` 构建结果，没有上板计时。
- 未对当前 2026-07-07 demo 补跑 init-only / 1iter demo-only 上板测试；现有
  demo-only 性能结论仍来自历史 2026-05-31 packed timing demo。
- 当前 2026-07-07 demo routed timing 未收敛，板上测试前应把 DATA/HBM clock 已被
  Vitis 降到 `228/422 MHz`、timing WNS `-1.043 ns` 作为风险记录。

## 是否建议晋级

暂不建议直接晋级为标准版。

理由：

- 正面：当前 vector phase worker 实验能完成完整 `hw` 构建并生成可上板 xclbin；
  软件 smoke 覆盖 `n512` 和 `thermal2_n16` 的 `MAX_ITERS=1`，数值校验通过；
  controller 已从大段向量 HBM 循环中解耦出来，更接近 Callipepla 式标量调度器。
- 风险：新 bitstream routed timing 未收敛，虽然 xclbin info 显示 DATA/HBM
  已降到 `228/422 MHz`，仍需板上确认功能、稳定性和性能；当前没有 init-only /
  1iter demo-only 上板数据，不能与 2026-05-31 packed timing demo 或标准版做性能结论。
- 结论：它是“full-PCG vector phase worker 实验候选”，不是标准替换候选。

下一步不要继续把主要精力放在 single SpMV 本体上；它作为回归基线保留。full-PCG
优化应直接面向 `detail/pcg_controller.hpp` 及相关 service/timer 路径。当前
`init_zp/update_rz_reduce` reduction 已有局部改善，下一步更应看
`update_x/update_p` 和 residual/preconditioner 阶段是否还能继续拆分，以及 AP
stream 能否直接喂给后续 dot/update，减少 `AP_spmv` 写回再读。更新 HTML 时，`TAPA PCG 分段时间` 和
`Init 与 1iter 差值` 必须展示当前 full-PCG demo-only 数据；single SpMV demo 只进入
SpMV/demo-only 和 SpMV 对比区域。

2026-07-07 这轮已经生成新的 `cuper-tapa-pcg` demo xclbin，但尚未完成上板性能
验证，也未更新正式 `source.diff`。后续只有完成 demo-only 上板测试并确认核心
性能指标改善后，才应刷新正式补丁或考虑晋级。
