# 这一目标要改什么

## 当前阶段

本目录第一轮 demo 曾把当前 TAPA-PCG 里的服务化 SpMV 抽成单 SpMV kernel，
并生成可单独上板测试的 `cuper-tapa-spmv` demo bitstream。该历史 demo 在
`thermal2_n16` 上板 smoke 中 timeout，没有晋级。

当前源码已经改为新的边界：`CuperPcgSpmv(...)` 不再复用 PCG service 控制壳，
而是保留 kernel 名和 host/demo 入口，内部回到 Cuper 风格 one-shot SpMV 图。

2026-05-29 已按这个边界重新生成 one-shot `CuperPcgSpmv` demo bitstream，并覆盖
`395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin`。旧 service 抽出版的
timeout 结论只保留为历史记录，不再对应当前同名 demo 文件。当前 one-shot demo
已完成 demo-only 上板测试，可返回到完整 `thermal2`；共同成功点性能略慢于
standalone TAPA Cuper SpMV 标准，因此本轮仍不更新正式 `source.diff`。

## 2026-06-18：CuperSpmvServiceOnly compact16 协议实验

本次新增的是 `DLC/Cuper-jacobi-iteration` 目录下的 SpMV-only 实验，不改变
`DLC/Cuper` 原标准 `Cuper(...)` 或历史 `CuperPcgSpmv(...)` one-shot demo。目标是
验证“在每个 HBM channel 内把 8 条 PE lane 紧凑打包”是否能作为下一步动态、均衡
SpMV 协议的可行边界。

源码和构建开关：

- `JACOBI_SPMV_COMPACT_PE=1`：打开 compact-PE host 打包和 kernel compact 路径；
- `DLC/Cuper-jacobi-iteration/host/main.cpp`：SpMV-only 模式下调用 compact 打包，
  打印 original/compact read beats 和节省比例；
- `DLC/Cuper-jacobi-iteration/include/Cuper_common.h`：定义 compact lane tag 编码；
- `DLC/Cuper-jacobi-iteration/kernels/detail/cuper_spmv_service_only_top_graphs.hpp`：
  新增 compact core/accumulator 路径；
- `DLC/Cuper-jacobi-iteration/tools/pack_profile.c`：扩展 C 侧打包 profile，输出
  per-HBM dynamic、per-PE lower bound、compact512/batch 和 compact512/stream 估算；
- 根 `Makefile`、子目录 `Makefile`、`CMakeLists.txt`、`scripts/build_xo_u55c.sh`
  和 `scripts/launcher.py`：向 host、TAPA/HLS 和 hw build 传播
  `JACOBI_SPMV_COMPACT_PE`。

当前 compact 协议：

- `rowIdx[17]` 继续表示 padding；
- `rowIdx[16:14]` 记录原 PE lane id；
- `rowIdx[13:0]` 记录原 Cuper 行号编码；
- host 对 `rowIdx < 2^14` 做检查，避免 silent overflow。

已生成 demo bitstream：

```text
395bitstream/cuper-tapa-spmv-u55c-20260618-compact16-demo.xclbin
```

上板结果：

- 日志：`logs/spmv_compact16_hw_sweep_20260618_174007/`；
- `thermal2_n16` 到完整 `thermal2` 共 8 点全部 `rc=0`、`Status=1`、
  `Error Num=0`；
- 完整 `thermal2` 时间为 `30.2804 ms`，GFLOP/s 为 `0.5667`；
- 完整 `thermal2` matrix read beats 从 `1,373,424` 降到 `1,187,402`，节省
  `13.54%`；
- 相比 strip16 完整 `thermal2` 的 `1.29158 ms`，compact16 只有 `0.043x` 速度比。

当前代价和限制：

- 它只覆盖 `CuperSpmvServiceOnly`，不覆盖普通 Jacobi iteration full graph；
- 它减少 HBM 读取 beat，但还没有消除 PE 内部 reorder holes；
- compact accumulator 当前按 512-bit beat 内 slot 串行分发，功能正确但未必比原
  16 路并行 accumulator 更快；
- 服务器上板已确认性能显著退步，因此本轮不更新正式 `source.diff`，也不建议晋级
  标准 bitstream。

## 2026-06-18：C 侧 reorder-free 打包上限评估

compact16 上板后确认“动态 lane tag 回填”会严重拖慢 accumulator。本次继续只改
纯 C 打包分析工具，评估如果未来重写 SpMV v2 协议，直接去掉 reorder holes 后的
读取上限。

新增/调整内容：

- `pack_profile.c` 追加 reorder-free 指标，不改变已有列含义；
- `lane-static real/batch`：每个 HBM、每个 batch 内只保留真实元素，但仍保持
  `slot p -> lane p`，适合固定 lane accumulator；
- `lane-static real/stream`：跨 batch 进一步按 lane 合并真实元素，估算固定 lane
  后端的最终读取量；
- `real compact512/batch` 和 `real compact512/stream`：只按真实 nonzero 打满
  512-bit beat，是需要动态 demux/lane 元数据的理论下限；
- `balanced compact512/stream`：假设真实任务能在 HBM 间完全均衡后的下限；
- `run-pack-profile` 新增 `DROP_DIAG=0`，用于完整 SpMV-only 的 `A` 口径；默认仍
  `--drop-diag`，服务 Jacobi 的 `R=A-D` 口径。

初步结论：对 `thermal2` 完整 A，16 路旧格式密度为 `78.09%`，strip16 为
`83.00%`，compact-scheduled 为 `90.40%`；如果改成 lane-static reorder-free stream，
密度可达 `99.83%`，与 real-compact 下限 `100.00%` 基本相同。也就是说后续硬件 v2
不一定要走 compact16 那种动态 lane 写回，优先保持固定 lane accumulator，同时重写
host packer 和长度协议，就已经接近理论下限。

## 2026-06-18：CuperSpmvServiceOnly lanereal16 固定 lane 硬件实验

本次把 C 侧 `lane-static real` 思路做成第一版硬件 demo。它仍然属于
`DLC/Cuper-jacobi-iteration` 下的 SpMV-only 实验，不改变原版 `DLC/Cuper` 标准
`Cuper(...)`。

源码和构建开关：

- `JACOBI_SPMV_LANE_STATIC_REAL=1`：打开 lane-static real/batch host 打包和 kernel
  strip-style 路径；
- `DLC/Cuper-jacobi-iteration/include/Cuper_common.h`：新增 host 侧行到 PE 的映射、
  dense SpElement 编码和 lane-static real/batch 打包函数；
- `DLC/Cuper-jacobi-iteration/host/main.cpp`：SpMV-only 模式下增加
  `[spmv-only-lane-static-real]` 读包统计；
- `DLC/Cuper-jacobi-iteration/kernels/detail/cuper_spmv_service_only_top_graphs.hpp`：
  让 ptr/loader/core 复用 strip-style per-HBM 边界；
- `DLC/Cuper-jacobi-iteration/kernels/detail/cuper_spmv_tasks.hpp`：在
  `JACOBI_SPMV_LANE_STATIC_REAL` 下关闭旧 reorder window 依赖 pragma，避免去掉空洞后
  继续套用旧调度距离；
- 根 `Makefile`、子目录 `Makefile`、`CMakeLists.txt`、`scripts/build_xo_u55c.sh`
  和 `scripts/launcher.py`：向 host、TAPA/HLS 和 hw build 传播
  `JACOBI_SPMV_LANE_STATIC_REAL`。

当前协议：

- 每个 HBM、每个 batch 内只保留真实元素；
- 仍保持 `slot p -> lane p`，因此 accumulator 继续走固定 lane 后端；
- 这版是 `lane-static real/batch`，不是跨 batch 合并的
  `lane-static real/stream`，因为现有 core 仍按 column batch 装载 X；
- 不使用 compact16 的动态 lane tag accumulator。

已生成 demo bitstream：

```text
395bitstream/cuper-tapa-spmv-u55c-20260618-lanereal16-demo.xclbin
```

构建结果：

- UUID：`98358acf-f40e-4f2f-b77f-4a25c24f4473`；
- SHA256：`c8ef2426248a1acd4d02a75da39d72439c1cabdd12450428cfa83ce0baf1b49d`；
- DATA/KERNEL/HBM clock：`197/500/450 MHz`；
- routed timing 未完全收敛：WNS `-0.073 ns`、TNS `-4.957 ns`、setup failing
  endpoints `215`；
- HBM 映射：`Matrix_data_0..15 -> HBM[0..15]`，`SpElement_list_ptr -> HBM[16]`，
  `X -> HBM[17]`，`Y_out -> HBM[18]`，`Status -> HBM[30]`，
  `Metrics -> HBM[31]`。

本机 software simulation 已通过 `thermal2_n1024` 和 `thermal2_n65536`：

- `thermal2_n1024` matrix read beats 从 `2624` 降到 `883`；
- `thermal2_n65536` matrix read beats 从 `68464` 降到 `57472`，节省 `16.0552%`。

当前风险：

- HLS 报告显示 lanereal16 的 `Accumulator_Pipeline_cuper_acc_accumulate` 达成 II=`5`；
- strip16 同一路径是 II=`2`；
- 服务器侧上板 sweep 已确认 8 个 `thermal2` 数据集全部 `rc=0`、`Status=1`、
  `Error Num=0`，日志在 `logs/spmv_lanereal16_hw_sweep_20260618_233431/`；
- 完整 `thermal2` 为 `2.35566 ms` / `7.2848 GFLOP/s`，matrix read beats 节省
  `16.1679%`；
- 这条线已在 HTML 主报告中直接替代前一条异常慢的 compact16 曲线，但仍慢于 strip16
  的 `1.29158 ms` / `13.2865 GFLOP/s`。因此不建议晋级，也不更新正式
  `source.diff`。

## 2026-06-20：CuperSpmvServiceOnly RTL owner-bank accumulator 实验

本次继续沿着 `lane-static real` 路线，只改 SpMV-only 后端 accumulator，不动
Jacobi full graph，也不动原版 `DLC/Cuper` 标准 `Cuper(...)`。

核心改动：

- 新增 `JACOBI_SPMV_OOO_ACCUMULATE_RTL=1` 开关，和
  `JACOBI_SPMV_OOO_ACCUMULATE=1`、`JACOBI_SPMV_LANE_STATIC_REAL=1` 配套使用；
- `CuperSpmvServiceOnly` 的 OOO 输出流从 128 条 owner-lane stream 收缩为
  16 条 owner-bank stream；
- 新增 `CuperSpmvOnly_RtlOwnerBankAccumulatorOoo` TAPA 自定义 RTL wrapper，每个
  owner bank 内部接 8 条 pair-lane 输入，并复用 RTL lane accumulator 做累加；
- `TaggedScatterWriterOoo` 改为轮询 16 条 owner-bank 输出，而不是 128 条
  owner-lane 输出；
- `build_xo_u55c.sh` 在 RTL 开关打开时，把
  `verilog/tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v` 和支持文件复制进 TAPA
  生成目录，并替换 HLS placeholder；
- 新增 `verilog/` 目录，包含 RTL 源码、小仿真 testbench 和 Makefile。

2026-06-20 修复版在上一版 owner-bank RTL 基础上又补了两个边界问题：

- `CuperSpmvOnly_RtlOwnerBankAccumulatorOoo` 不再只等 8 条 lane `done` 就拉高
  `ap_done`，而是同时统计该 owner-bank 应输出的 tagged pair 数，确认输出 token
  已全部写入 `TaggedScatterWriterOoo` 后才完成；
- `CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo` 不再从第一条输入 token 推断
  `owner_id` / `pair_lane`，改由 bank wrapper 显式传入 `Owner_id` 和固定
  `Pair_lane=0..7`，避免空 lane 或首 token 为 done 时 tag 错误。

这两个修复针对服务器侧旧 UUID `22b0a282-c282-cfaf-e45a-f8bebf4cc644` 的失败现象：
`thermal2_n16` Finish 返回但输出全 0，`thermal2_n1024` 卡在
`after ReadFromDevice before Finish`。

2026-06-21 latency=12 修复版继续沿用同名 SpMV demo 槽。Vivado/xsim 使用真实
`floating_point` IP 验证后发现 lane accumulator 的浮点加法结果相对 issue tag
是 12 cycle；旧 RTL 按 13/14 cycle 对齐时会把加法结果写错 ping/pong bank。当前版
把 `CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo` 的 `FADD_PIPE_LATENCY` 和
`NUM_STAGE` 统一改为 12，并补了 Vivado/xsim 后端数据集仿真 harness，用于直接把
TAPA 生成的 scatter RTL、真实 floating_point IP 和自定义 owner-bank RTL 接起来跑
CSR 数据集。

这版和前一版 128 owner-lane RTL 的区别：

| 项 | 128 owner-lane RTL | 16 owner-bank RTL |
| --- | --- | --- |
| RTL 实例数量 | owner * lane，约 128 个 | owner，16 个 wrapper |
| 输出 stream | 128 条 | 16 条 |
| 输出仲裁 | scatter writer 面对 128 输入 | bank 内 round-robin，再由 scatter writer 面对 16 输入 |
| 资源压力 | BRAM 和布线压力过高，硬件 link 失败 | routed xclbin 已生成 |
| 乱序强度 | owner-lane 粒度 | owner-bank 内 8 lane 粒度 |

已生成 demo bitstream：

```text
395bitstream/cuper-tapa-spmv-u55c-20260620-ooobank16-demo.xclbin
```

构建结果：

```text
Run vpl: FINISHED. Run Status: impl Complete!
Created .../cuper-tapa-spmv-ooobank16-lat12-hw-150m-20260621-build/CuperSpmvServiceOnly.xclbin
Total elapsed time: 3h 59m 3s
UUID: 58158740-a7ef-a803-0da5-1fd8b3206253
SHA256: 5e3f2e863cba4efca519ce18f9e9e735f05084af1242cd493e079c1844f777b3
DATA/KERNEL/HBM clock: 150 / 500 / 445 MHz
Timing: WNS -0.024 ns, TNS -0.086 ns, setup failing endpoints 8
```

当前状态：

- Vivado/xsim 后端数据集仿真已通过到 `thermal2_n65536`；
- bitstream 已覆盖同步到 `395bitstream/` 同名 SpMV demo 槽；
- 还没有服务器上板性能数据，暂不更新 HTML 曲线；
- 由于尚未确认性能提升，本轮不更新正式 `source.diff`。

## 2026-05-28：CuperPcgSpmv 抽出版

本轮新增内容：

- 新增 `CuperPcgSpmv(...)` TAPA 顶层，外部 ABI 对齐 `Cuper(...)`：
  `SpElement_list_ptr`、`Matrix_data[0..15]`、`X`、`Y_out` 和 SpMV 尺寸参数。
- 新增 `Pcg_SingleSpmv_Controller`，只发送一次 SpMV command，等待 writer 完成后
  广播 stop，避免 PCG service task 无限常驻导致 kernel 不返回。
- 新增 `Pcg_Single_Vector_Loader`，保留 PCG service 的 command/stop 语义，但单
  SpMV 只读一个 packed `X` 输入端口。
- 新增 `Pcg_Single_Vector_Writer`，把 service sort tree 输出的 `float_v16`
  写回 `Y_out`，再通知 controller 关闭服务链。
- 新增 U55C connectivity：
  `cfg/connectivity_cuper_tapa_pcg_spmv_u55c.cfg`。
- 新增 Makefile 构建入口：
  `build-cuper-tapa-pcg-spmv-{sw,hw}` 和 `cuper-tapa-pcg-spmv-hw-tmux`。
- host `run-cuper-tapa-pcg-spmv` 通过 `--pcg-spmv-service` 调用
  `CuperPcgSpmv`，便于之后 demo-only single SpMV 对比。
- 已生成硬件 bitstream 并放入当前 demo 槽：
  `395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin`。
- 旧 demo 槽中的 full-PCG packed feed/AP 候选版已移到
  `bitstream_archive/2026-05-28-tapa-pcg-packed-ap-demo-before-spmv-demo/`。

本轮不做的事：

- 不调整 standalone `Cuper(...)` 和 `detail/cuper_spmv_tasks.hpp`；
- 不优化 HBM 排布、FIFO 深度、core 链或 row 编码；
- 不更新正式 `source.diff`，因为 demo-only 上板 smoke 未通过，没有性能提升结果。

## 2026-05-28 demo-only 结果补充

`395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin` 已做 single SpMV
demo-only smoke。`thermal2_n16` 第一次运行和 retry 均在 180s timeout，日志都停在
`[tapa-invoke] after ReadFromDevice before Finish`。本轮没有产生 `spmv_avg`
或 diff，因此停止后续数据集 sweep。

这说明当前抽出版可以生成 bitstream 并加载到 U55C，但 kernel/host 返回路径在
最小数据集仍未闭合。它不是可晋级版本，也不是可覆盖正式 `source.diff` 的性能
改进补丁。

## 2026-05-29：one-shot Cuper-compatible demo 构建完成

当前 `CuperPcgSpmv(...)` 已改为复用 `detail/cuper_spmv_tasks.hpp` 中的 Cuper
one-shot task graph，不再接 `pcg_spmv_service.hpp` 的 command/stop/service
控制壳。这样 single SpMV demo 的测量口径接近满血 `Cuper(...)`，不会把 PCG
service 控制开销混进单 SpMV 结果。

构建结果：

```text
log: logs/cuper_tapa_pcg_spmv_hw_parallel_20260528_222446.log
xclbin: cuper-tapa-spmv-u55c-20260528-demo-build/hw/CuperPcgSpmv.xclbin
demo: 395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin
UUID: c95c1dfc-20ca-9152-279e-bafdf35fdc3d
SHA256: 19d227179db7f22adfd12e78da119a99d102c59ebe25df686a652c6715ea95f2
DATA/KERNEL/HBM clock: 147 / 500 / 418 MHz
Total elapsed time: 7h 29m 0s
```

2026-05-29 已完成 demo-only 上板测试，日志在
`logs/codex_two_demo_test_20260529_1300/`。`thermal2_n16`、
`thermal2_n65536`、`thermal2_n131072`、`thermal2_n262144` 和完整
`thermal2` 均返回且 diff 通过；完整 `thermal2` 的
`spmv_avg=1.781541 ms`。共同成功点上相对 standalone TAPA Cuper SpMV 标准约为
`1.03x` 到 `1.08x`，即略慢；但标准旧记录在 `thermal2_n262144` 和完整
`thermal2` 为 timeout，本 demo 成功边界更大。该 demo 已从 `395bitstream/`
移入 `bitstream_archive/2026-05-29-tapa-pcg-spmv-demo-candidates/`，未晋级标准版。
正式 `source.diff` 仍不更新。

## 2026-05-28：finite-exit 修复尝试

本次只处理 `CuperPcgSpmv` 单 SpMV demo 的有限退出问题，不改 standalone
`Cuper(...)` 标准路径，也不改 full-PCG `CuperPcg(...)` 的服务协议。

源码改动：

- 在 `pcg_spmv_service.hpp` 中新增 `Pcg_Single_Vector_Checker`：
  按一次 SpMV 的 `Row_num` 计算上游 accumulator 会产生的对齐输出包数，完整
  消费 padding，只把有效输出转发给下一段。
- 在 `pcg_spmv_service.hpp` 中新增 `Pcg_Single_Mult_Sort_Tree`：
  从 8 路 `float_v2` 收齐后打包成 `float_v16`，输出固定
  `ceil(Row_num/16)` 包后自然返回。
- 调整 `Pcg_SingleSpmv_Controller`：
  不再向 checker/sort 发送 stop token；writer 完成后只负责关闭 loader/core
  和 vector destroy 这类仍保持常驻服务语义的任务。
- 调整 `cuper_top_graphs.hpp` 中的 `CuperPcgSpmv(...)` task graph：
  单 SpMV 抽出版改接 `Pcg_Single_Vector_Checker` 和
  `Pcg_Single_Mult_Sort_Tree`，full-PCG 仍使用原来的 stop-driven
  `Pcg_Vector_Checker` / `Pcg_Mult_Sort_Tree`。

预期收益：

- 避免上一版 writer 写完 1 个有效 `Y_out` 包后立刻通知 controller 发 stop，
  导致 checker/sort tree 或上游 padding 输出未 drain 完，进而让硬件
  `Finish` 永久等待。
- 让单 SpMV demo 的尾端更接近原始 `Cuper(...)` 一次性 task graph：固定数据量
  结束，而不是依赖异步 stop 抢停。

已完成验证：

```bash
git diff --check
timeout 180s make run-cuper-tapa-pcg-spmv \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  SPMV_REPEATS=1 DIFF_TOL=1e-1
```

软件仿真结果为 `status=ok`，`max_abs_diff=3.755767679081e-07`，
`spmv_avg=26.542003 ms`。

硬件构建已进入 VPL：

```text
session: project-xplus-cuper-tapa-pcg-spmv-hw
log: logs/cuper_tapa_pcg_spmv_hw_20260528_161221.log
```

当前仍不更新正式 `source.diff`。只有新 xclbin 上板后确认至少
`thermal2_n16` 能稳定返回，并继续完成 demo-only sweep 或明确证明边界改善时，
才考虑把本补丁写入 `source.diff`。

## 2026-05-28：service 内部去掉 Iteration_num

本次按用户要求，把单 SpMV 抽出版和 full `CuperPcg` 共同使用的
`pcg_spmv_service.hpp` 统一为“一条 command 只执行一次 SpMV”。PCG 的多轮迭代
由 controller 多次发送 command 表示，不再把 `Iteration_num` 塞进 command 让
service 内部循环。

源码改动：

- `pcg_common.hpp`：`CuperSpmvCommand` 删除 `iteration_num`，只保留
  `stop` 和 `vector_source`；
- `pcg_controller.hpp`：init 阶段和每轮 A*p 阶段发送普通 command，不再写
  `command.iteration_num`；
- `pcg_spmv_service.hpp`：
  - ptr/vector/matrix loader 收到一条 command 后只发/读一次 SpMV 所需数据；
  - `Pcg_Core`、`Pcg_Accumulator` 不再读取或转发 `Iteration_num`；
  - `Pcg_Single_Vector_Checker`、`Pcg_Single_Mult_Sort_Tree`、
    `Pcg_Single_Vector_Writer` 都只按一次 SpMV 的固定输出量返回；
- `cuper_top_graphs.hpp`：`CuperPcgSpmv(...)` 仍保留 ABI 参数 `Iteration_num`，
  但内部 `(void)Iteration_num`，不再把它传给 service task。

保留不变：

- standalone `Cuper(...)` 仍使用 `Iteration_num` 作为 benchmark 重复次数；
- full `CuperPcg(...)` 的 PCG 迭代次数仍由 `Max_iters` 控制；
- 本轮不启动新的硬件构建，不更新正式 `source.diff`。

本次软件验证：

```bash
make download-suitesparse-data DATASETS="thermal2_n4096 thermal2_n16384 thermal2_n65536 thermal2_n131072 thermal2_n262144 thermal2"
git diff --check
make cuper-tapa-pcg-host
make cuper-tapa-pcg-fpga-host
timeout 180s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n16 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 180s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n16 MAX_ITERS=1 DIFF_TOL=1e-1
timeout 240s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 240s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 MAX_ITERS=1 DIFF_TOL=1e-1
timeout 300s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n4096 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 300s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n4096 MAX_ITERS=1 DIFF_TOL=1e-1
```

结果：`CuperPcgSpmv` service single SpMV 在 `n16/n1024/n4096` 均 `status=ok`；
full `CuperPcg` 软件仿真在 `n16` 收敛，在 `n1024/n4096` 与 CPU 1iter 结果对齐。

## 2026-05-28：统一 command/stop 广播 helper

本次继续做协议层统一，不改变 full-PCG 或 single SpMV 的任务图结构：

- `pcg_common.hpp` 新增 `pcg_make_spmv_command()` 和
  `pcg_make_spmv_stop_command()`，集中定义普通 SpMV command 与 stop command
  的字段；
- `pcg_common.hpp` 新增 `pcg_send_spmv_command()` 和
  `pcg_send_spmv_stop()`，统一向 `Command_Stream[0..1]` 与
  `Matrix_Command_Stream[0..15]` 广播；
- `Pcg_Controller` 的 init `A*x0`、每轮 `A*p` 和最终 stop 改为调用公共 helper；
- `Pcg_SingleSpmv_Controller` 的单次 SpMV command 和 stop 也改为调用同一组
  helper。

这个统一的边界：

- 没有把 full `Pcg_Controller` 塞进 `CuperPcgSpmv`；
- 没有改变 writer-done drain 屏障；
- 没有改变 full-PCG 的 checker/sort stop、stage timer 或 metrics；
- 没有改变 standalone `Cuper(...)`。

预期收益：

- 以后若 command 字段、stop 语义或 16 路 HBM matrix loader 发令顺序需要调整，
  single service SpMV 和 full `CuperPcg` 会同步改到；
- 减少两边 controller 漂移，避免 single SpMV demo 修了一处但 full-PCG 漏改。

已完成软件验证：

```bash
git diff --check
make cuper-tapa-pcg-host
make cuper-tapa-pcg-fpga-host
timeout 180s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n16 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 180s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n16 MAX_ITERS=1 DIFF_TOL=1e-1
timeout 240s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 240s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 MAX_ITERS=1 DIFF_TOL=1e-1
```

结果：`n16/n1024` 的 service single SpMV 和 full `CuperPcg` 软件仿真都通过。
本轮只是源码统一和软件正确性验证，不启动硬件构建，不更新正式 `source.diff`。

## 2026-05-28：共享 SpMV 包数/对齐计数 helper

本次继续补“单 SpMV 优化能反馈到 PCG”的代码边界。之前 service 里有多处重复的
包数和 padding 公式，例如：

- packed `float_v16` 包数：`ceil(n/16)`；
- accumulator 清零组数：按 `HBM_CHANNEL_NUM * 16` 对齐；
- accumulator 输出组数：按 `HBM_CHANNEL_NUM * 2` 对齐；
- checker 需要完整消费的 PE 对齐输出数。

这些公式同时影响 `CuperPcgSpmv(...)` 和 full `CuperPcg(...)` 的 service SpMV。
如果以后排查大规模边界、padding drain 或 65536 附近问题，不应该在 single demo
和 full-PCG 路径各改一份。

源码改动：

- `pcg_common.hpp` 新增：
  - `pcg_num_float_v16_packets()`
  - `pcg_num_accumulator_init_groups()`
  - `pcg_num_accumulator_outputs()`
  - `pcg_num_checker_pe_outputs()`
- `pcg_controller.hpp` 的 PCG 向量 packet 数改用公共 helper；
- `pcg_spmv_service.hpp` 的 vector loader、accumulator、full-PCG checker、
  single checker、single sort tree 和 single writer 均改用公共 helper；
- `pcg_spmv_service.hpp` 文件头补充同步边界：哪些 task 同时服务
  `CuperPcgSpmv(...)` 和 full `CuperPcg(...)`，哪些只是单 SpMV demo 包装层。

这个改动本身不追求性能提升，但它降低后续优化漂移风险：涉及包数、padding 和
drain 的修复会先落在共享 helper，再自然反馈到 PCG。

## 2026-05-28：共享 vector/checker/sort 细粒度 helper

本次继续清理 `pcg_spmv_service.hpp` 内 single SpMV demo 和 full
`CuperPcg` 的重复代码。目标不是把 full controller 塞进 single demo，而是把真正
会影响 SpMV 数据通路的基础循环抽成共享 helper。

源码改动：

- 新增 `pcg_read_vector_packets(...)`：
  - `Pcg_Vector_Loader` 用它读取 `X_spmv` / `P_spmv`；
  - `Pcg_Single_Vector_Loader` 用它读取单输入 `X`；
  - 后续如果优化 packed vector 读取节奏，两条路径会同步受益。
- 新增 `pcg_try_forward_checker_value(...)`：
  - 统一 checker 对一拍 `float_v2` 的读取、padding 过滤、`c_idx/o_idx`
    轮转和有效数据转发；
  - full-PCG checker 和 single checker 都调用它。
- 保留 `pcg_checker_forward_round(...)`：
  - 只作为“按固定输出数量 drain 一轮”的有限 helper；
  - 当前主要给 `Pcg_Single_Vector_Checker` 使用。
- 新增 `pcg_try_pack_float_v16(...)`：
  - full `Pcg_Mult_Sort_Tree` 和 single `Pcg_Single_Mult_Sort_Tree` 都用它把
    8 路 `float_v2` 合成 1 包 `float_v16`。

这次修正了一个抽象过粗导致的 full-PCG 死锁风险。最初版本让
`Pcg_Vector_Checker` 直接调用整轮 `pcg_checker_forward_round(...)`，只在两轮之间
检查 stop。`thermal2_n16` full-PCG 软件仿真因此在 180s timeout：checker 可能在
controller 还没来得及发 stop 时抢先进下一轮，然后等待不存在的新一轮输入。

最终做法：

- single SpMV checker：没有 stop stream，固定 drain 一轮后自然返回；
- full-PCG checker：共享同一个“单步转发” helper，但在等待每个输入期间持续检查
  `Stop_in`，保持原来的常驻服务退出语义。

已完成验证：

```bash
git diff --check
make -B cuper-tapa-pcg-host
make -B cuper-tapa-pcg-fpga-host
timeout 180s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n16 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 180s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n16 MAX_ITERS=1 DIFF_TOL=1e-1
timeout 240s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 240s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 MAX_ITERS=1 DIFF_TOL=1e-1
```

结果：四个软件点全部通过。`n1024` full-PCG 使用 `MAX_ITERS=1`，所以
`status=max_iter` 是预期结果。本轮未生成新 xclbin，正式 `source.diff` 不更新。

## 2026-05-28：single SpMV 去控制壳，回到 Cuper one-shot

本次按最新边界处理：single SpMV 不再承载 PCG service/control 优化。之前的
`Pcg_Single*` 包装层虽然能让 service 链在软件仿真里有限返回，但它只修 demo
控制壳，不能代表 full `CuperPcg(...)` 的优化；同时历史硬件 bitstream 已经在
最小数据集 timeout。

源码改动：

- `CuperPcgSpmv(...)` 保留历史 kernel 名、ABI 和 `run-cuper-tapa-pcg-spmv`
  host 入口；
- `CuperPcgSpmv(...)` 内部改用 `Cuper(...)` 同款一次性 task graph：
  `SpElement_list_ptr_Loader`、`Vector_Loader`、`Matrix_Loader`、`Core`、
  `Accumulator`、`Vector_Checker`、`Mult_Sort_Tree`、`Vector_Writer`；
- 删除当前源码中的 `Pcg_SingleSpmv_Controller`、`Pcg_Single_Vector_Loader`、
  `Pcg_Single_Vector_Checker`、`Pcg_Single_Mult_Sort_Tree`、
  `Pcg_Single_Vector_Writer`；
- `pcg_spmv_service.hpp` 文件头明确：本文件只服务 full `CuperPcg(...)` 的常驻
  SpMV service；
- host 侧仍保留 `--pcg-spmv-service` 兼容 flag，但输出标签改为
  `tapa-cuper-compat-demo`。

已完成验证：

```bash
git diff --check
make -B cuper-tapa-pcg-host
make -B cuper-tapa-pcg-fpga-host
timeout 180s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n16 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 240s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 180s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n16 MAX_ITERS=1 DIFF_TOL=1e-1
timeout 240s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 MAX_ITERS=1 DIFF_TOL=1e-1
```

结果：

- `CuperPcgSpmv` one-shot single SpMV：`n16/n1024` 软件仿真均 `status=ok`；
- full `CuperPcg`：`n16` 收敛，`n1024` 与 CPU 1iter 对齐；
- `n16` full-PCG 软件仿真仍有 TAPA stream leftover warning
  `Vector_Y_Stream[13]`，但结果返回且 diff 通过；该 warning 需要作为后续
  service drain 观察点；
- 当前没有启动新硬件构建，也没有生成新的 one-shot demo xclbin；
- `395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin` 仍是历史 service
  抽出版 bitstream，不代表当前源码。

## 优化对象

当前只优化两类明确分开的路径：

- single SpMV demo：`CuperPcgSpmv(...)` 作为 Cuper-compatible one-shot 图；
- full-PCG service/control：`CuperPcg(...)` 实际使用的 `Pcg_*` 常驻服务路径。

```text
DLC/Cuper/kernels/Cuper.cpp
DLC/Cuper/kernels/detail/cuper_top_graphs.hpp
DLC/Cuper/kernels/detail/pcg_spmv_service.hpp
DLC/Cuper/kernels/detail/pcg_common.hpp
cfg/connectivity_cuper_spmv_u55c.cfg
host / Makefile 中 run-cuper-tapa-pcg-spmv 相关路径
```

`CuperPcgSpmv(...)` 的 one-shot 改动只说明 single SpMV demo 路径；不能声明会同步
提升 PCG。PCG 性能/正确性优化必须落在 full `CuperPcg(...)` service 路径，并补
full-PCG 验证。涉及包数、padding、对齐输出和 command/stop 的公共规则，优先写进
`pcg_common.hpp`，不要在不同路径各写一份。

## 优先问题

1. 复核 `thermal2_n262144` 和完整 `thermal2` 的 timeout/边界是否仍存在。
2. 若存在，先定位是数据规模、row 编码、stream drain、HBM 访问还是 host/XRT
   启动/等待逻辑造成。
3. 任何性能优化都必须保持和 CPU SpMV 校验一致，不能只看 kernel 返回。
4. demo 结果必须和当前标准 `cuper-tapa-spmv-u55c-20260522.xclbin` 以及既有
   HTML 记录同口径对比；
5. demo 上有效后必须补 full `CuperPcg(...)` 软件或上板 smoke，确认同一 service
   改动没有破坏 PCG。

## 2026-06-22：strip16 window 300 MHz no-progress 同步

本轮没有改变标准 SpMV bitstream，只同步两份 `CuperSpmvServiceOnly` 实验 artifact，
用于服务器侧直接跑 300 MHz link 目标下的 no-progress window 版本：

```text
395bitstream/cuper-tapa-spmv-u55c-20260622-strip16-window14-300m-noprogress-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260622-strip16-window16-300m-noprogress-demo.xclbin
```

相关源码/工具记录：

- `CuperSpmvOnly_ProgressWriter` 在 `JACOBI_SPMV_OOO_ACCUMULATE_RTL` 下增加最小
  entry heartbeat，只写 `Status[8]`，用于把 RTL progress writer 是否进入口暴露给 host；
- 非 RTL 的 `JACOBI_SPMV_SEGMENTED_ACCUMULATE` 作为分段累加探索保留在宏后面，
  但当前 HLS 结果仍不适合硬件构建；
- `pack_profile` 增加 HBM/PE 负载均衡与瓶颈 beat 指标，用于解释 strip/window
  padding 和负载分布；
- `DLC/Cuper-jacobi-iteration/docs/` 增加 accumulator 优化路线与打包负载/padding
  对比记录。

两份 300 MHz artifact 均已生成 xclbin，但 routed timing 未满足 300 MHz 目标：
window14 自动降到 DATA `202 MHz`，window16 自动降到 DATA `192 MHz`。因此这轮只适合
服务器侧风险测试，不建议晋级标准，正式 `source.diff` 仍不更新。

## 2026-06-22：RTL owner-bank heartbeat-clean 同步

本轮同步此前启动的 `CuperSpmvServiceOnly` RTL owner-bank heartbeat-clean 构建产物，
用于服务器侧继续验证 RTL accumulator 路线：

```text
395bitstream/cuper-tapa-spmv-u55c-20260622-ooobank16-heartbeat-clean-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260622-ooobank16-heartbeat-clean-demo.xclbin.info
```

这版沿用 lane-static real + owner-bank RTL accumulator 宏组合：

```text
JACOBI_SPMV_LANE_STATIC_REAL=1
JACOBI_SPMV_OOO_ACCUMULATE=1
JACOBI_SPMV_OOO_ACCUMULATE_RTL=1
```

构建已完成并生成 xclbin，DATA/KERNEL/HBM clock 为 `150/500/450 MHz`，routed timing
clean：WNS `0.003 ns`、TNS `0.000 ns`、setup failing endpoints `0`。

这轮没有新的服务器侧上板数据，因此只是同步待测 demo，不建议晋级标准，正式
`source.diff` 仍不更新。

## 2026-06-23：RTL issue scoreboard debug 同步

本轮同步 `CuperSpmvServiceOnly` scoreboard-only RTL debug 构建产物，用于服务器侧
验证“只把冲突调度放到 RTL，累加后端继续用 HLS”的中间分支：

```text
395bitstream/cuper-tapa-spmv-u55c-20260623-scoreboard16-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260623-scoreboard16-demo.xclbin.info
```

核心改动：

- 新增 `JACOBI_SPMV_OOO_SCOREBOARD_RTL=1`，与全后端
  `JACOBI_SPMV_OOO_ACCUMULATE_RTL=1` 互斥；
- 新增 `CuperSpmvOnly_RtlOwnerScoreboardOoo` 自定义 RTL wrapper，负责从 owner 内
  8 条 lane FIFO 选择一个安全 head 发射；
- 新增 `CuperSpmvOnly_RtlIssueScoreboard8` RTL primitive，按
  `{lane, addr, ping/pong}` 维护 in-flight scoreboard；
- 新增 `CuperSpmvOnly_OwnerAccumulatorScheduledOoo` HLS 后端，接收 RTL 输出的
  `{lane + TaggedScalar}`，继续执行 FP32 加法、URAM partial sum 和写回；
- 新增轻量 `JACOBI_SPMV_SCOREBOARD_DEBUG=1` pulse 监测，记录 core/issue/acc 三段
  lane 计数，避免之前宽 event stream 导致 HLS 资源报告阶段内存暴涨；
- `build_xo_u55c.sh` 新增 `JACOBI_TAPA_ENABLE_SYNTH_UTIL=0`，本轮跳过 TAPA
  post-synthesis resource utilization reports，避免重复的 per-task 面积统计综合。

构建已完成并生成 xclbin，但 DATA clock 被工具自动降到 `39 MHz`，routed timing
大幅未收敛：WNS `-18.950 ns`、TNS `-32059.350 ns`、setup failing endpoints
`28858`。因此这轮只同步为功能/监测边界实验，不建议晋级标准，正式 `source.diff`
仍不更新。

## 2026-06-27：8-HBM SpMV-only 构建同步

本轮按 8-HBM 目标生成并同步三份 `CuperSpmvServiceOnly` demo artifact：

```text
395bitstream/cuper-tapa-spmv-u55c-20260627-original8-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260627-strip8-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-demo.xclbin
```

核心改动：

- 新增 `JACOBI_HBM_CHANNELS=8` 支持，host、TAPA/HLS cflags、XO hotpatch 和 link
  connectivity 均能生成 8 路 `Matrix_data_0..7` graph；
- 8-HBM SpMV-only connectivity 使用 `Matrix_data_0..7 -> HBM[0..7]`，
  `SpElement_list_ptr -> HBM[8]`，`X -> HBM[9]`，`Y_out -> HBM[10]`，
  `Status -> HBM[30]`，`Metrics -> HBM[31]`；
- full Jacobi graph 的 update pair 宏也补了 8-HBM 分支，使每个 update pair 只消费
  1 路 accumulator 输出；
- RTL hotpatch 会按 `JACOBI_HBM_CHANNELS` 改写 custom RTL wrapper 的
  `HBM_CHANNEL_NUM` 参数，避免 8-HBM scoreboard 构建仍使用 16 路默认参数；
- 单记分板分支从单 lane issue token 改为 8-lane scheduled vector：RTL issue
  scoreboard 每拍可同时发射所有无 RAW hazard 的 lane，HLS scheduled accumulator
  继续负责 FP32 加法、URAM partial sum 和 tagged 写回；
- `verilog/README.md` 补了 RTL 文件索引，明确“全 RTL owner-bank 后端”和
  “单记分板 RTL 调度器 + 8-lane primitive”的区别。

本轮构建结果：

- original8、strip8、lanereal8-scoreboard 三版均为 `150/500/450 MHz`；
- 三版 routed timing 均 clean：WNS `0.003 ns`、TNS `0.000 ns`、setup failing
  endpoints `0`；
- 服务器侧上板已确认 original8 和 strip8 可跑完整 `thermal2`，但完整点分别为
  `2.74379 ms` / `2.71420 ms`，慢于既有 16-HBM strip 线；
- `lanereal8-scoreboard` 在 `thermal2_n16` 通过，但 `thermal2_n1024` 300s timeout，
  说明问题集中在 RTL issue scoreboard + HLS scheduled accumulator 分支的进度/排空
  边界，而不是 8-HBM 基础 connectivity；
- 正式 `source.diff` 仍不更新。

## 2026-06-27：lanereal8 scoreboard head 缓存修复

针对服务器侧 `lanereal8-scoreboard` 在 `thermal2_n1024` 上 300s timeout，本轮修改
`verilog/tapa/CuperSpmvOnly_RtlOwnerScoreboardOoo.v`，把 owner-lane FIFO head 从
组合直连改为每 lane 本地缓存：

- wrapper 新增 `head_valid_reg` / `head_payload_reg`；
- `Owner_Lane_Stream_*_s_read` 只在对应 lane 本地 head 为空且上游非空时拉高；
- issue scoreboard 只看本地缓存 head，RAW hazard bubble 或 downstream full 时不重复
  读取上游 FIFO；
- 下游成功 transfer 后只清本地缓存，下一拍再重新填充，避免 pop 同拍读到变化后的
  `s_dout`；
- done token 仍按普通非 padding lane 发给 downstream，并继续由 `done_seen` 统计每轮
  8 个 owner lane 的完成。

新增 Verilator wrapper smoke：

```text
make -C verilog tapa-owner-scoreboard-sim
```

该测试覆盖 cached head 在 downstream backpressure 下不 reread、RAW hazard bubble 下
输出 padding 而不丢 head、以及 8 个 done token 后 `ap_done/ap_ready` 拉高。复测也通过：

```text
make -C verilog tapa-vector-scoreboard-sim
make -C verilog tapa-scoreboard-dataset-cpp-sim
```

新同步 artifact：

```text
395bitstream/cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-headreg-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-headreg-demo.xclbin.info
```

版本信息：

```text
UUID: 39eb30df-2178-51aa-3333-f1cbb9a0e389
SHA256: 1ac7664203e0eb3b6bd8bd35a932ecd8a1afdfb81d34dedf6cc26f98663870e1
DATA/KERNEL/HBM clock: 150 / 500 / 450 MHz
Routed timing: WNS 0.003 ns, TNS 0.000 ns, setup failing endpoints 0
Build dir: cuper-jacobi-spmv-lanereal8-scoreboard-headreg8-hw-150m-build/
v++ link elapsed: 2h 59m 18s
```

这版是旧 `thermal2_n1024` timeout 的复测候选；当前还没有服务器侧上板结果，因此仍不更新
正式 `source.diff`。

## 2026-06-28：lanereal8 scoreboard done-drain 修复

head-register 修正后，下一版把 `done` token 语义改成 per-lane drain barrier：

- `CuperSpmvOnly_RtlIssueScoreboard8` 新增逐 lane scoreboard empty 检查；
- 普通数据 token 仍按 `{lane, addr, ping/pong}` hazard 判断；
- `done` token 不再无条件绕过 scoreboard，必须等同 lane window 全空后才 issue；
- blocked done 或 hazard-blocked data 期间继续输出 all-padding beat 推进 scoreboard shift；
- HLS C++ 等价占位调度器同步相同语义，保持 software/TAPA sim 与 custom RTL 一致；
- primitive/wrapper testbench 新增 done-drain 覆盖。

本地验证已通过：

```text
git diff --check
verilator --lint-only ... CuperSpmvOnly_RtlOwnerScoreboardOoo
make -C verilog tapa-vector-scoreboard-sim
make -C verilog tapa-owner-scoreboard-sim
make -C verilog tapa-scoreboard-dataset-cpp-sim
```

同步 artifact：

```text
395bitstream/cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-donedrain-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260627-lanereal8-scoreboard-donedrain-demo.xclbin.info
```

版本信息：

```text
UUID: e90660d3-9efa-9066-7517-4547fc21097f
SHA256: 94d60c0d10dfd6e5384ec0e305e6836a4531ea82d74a24375d054c5546e1d7ad
DATA/KERNEL/HBM clock: 150 / 500 / 450 MHz
Routed timing: WNS 0.003 ns, TNS 0.000 ns, setup failing endpoints 0
Build dir: cuper-jacobi-spmv-lanereal8-scoreboard-donedrain8-hw-150m-build/
v++ link elapsed: 3h 15m 0s
```

这版仍是旧 `thermal2_n1024` timeout 的复测候选；正式 `source.diff` 继续等板上确认后再更新。

## source.diff 规则

本目标遵循先测试后写正式 diff：

- 测试失败、性能退步或未完成板上验证时，不覆盖正式 `source.diff`。
- 若只是为了记录探索过程，写入 `changes.md` / `testing.md`，不要把草稿补丁当成有效补丁。
- 当 demo 在板上确认有效后，再生成本目录 `source.diff`。
