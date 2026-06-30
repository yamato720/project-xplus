# CuperPcgSpmv one-shot 版代码阅读指南

这份文档对应当前源码状态：`CuperPcgSpmv(...)` 保留历史 kernel 名和
`run-cuper-tapa-pcg-spmv` host 入口，但内部已经回到 Cuper 风格的一次性 SpMV
task graph。历史上那版 `Pcg_Single*` finite-exit service demo 已上板 timeout，
只作为失败分析保留在 `failure_analysis.md` 和历史变更记录里。

## 1. 先分清三条入口

| 入口 | 文件 | 作用 |
| --- | --- | --- |
| `Cuper(...)` | `DLC/Cuper/kernels/detail/cuper_top_graphs.hpp` | 满血 standalone TAPA Cuper single SpMV 标准路径 |
| `CuperPcgSpmv(...)` | `DLC/Cuper/kernels/detail/cuper_top_graphs.hpp` | 保留旧 kernel 名的 Cuper-compatible single SpMV demo |
| `CuperPcg(...)` | `DLC/Cuper/kernels/detail/cuper_top_graphs.hpp` | TAPA Cuper SpMV + FPGA 内 PCG，全流程 kernel |
| `CuperSpmvServiceOnly(...)` | `DLC/Cuper-jacobi-iteration/kernels/detail/cuper_spmv_service_only_top_graphs.hpp` | Jacobi 目录下的 SpMV-only 协议实验入口 |

当前 `CuperPcgSpmv(...)` 不是从 `CuperPcg(...)` service 链抠出来的版本。它不使用
`Pcg_SingleSpmv_Controller`、`Pcg_Single_Vector_Loader` 或 writer-done stop
控制壳。

2026-06-18 的 `compact16` demo 不是 `CuperPcgSpmv(...)`，而是
`DLC/Cuper-jacobi-iteration` 下的 `CuperSpmvServiceOnly(...)`。它属于
`cuper-tapa-spmv` demo 对比口径，但用于探索 Jacobi 目录里 service 化 Cuper SpMV 的
数据打包协议。

## 2. Host 到 kernel 的调用链

阅读顺序：

1. `Makefile`
   - `run-cuper-tapa-pcg-spmv` 会调用 host，并加上
     `--spmv-only --pcg-spmv-service`。
2. `host/cuper_tapa_pcg_main.cpp`
   - 这个 flag 仍选择 `CuperPcgSpmv`，但当前输出标签是
     `tapa-cuper-compat-demo`。
   - host 仍复用 Cuper 的矩阵预处理：CSR -> COO -> SparseSlice -> 16 路 HBM
     matrix data。
   - `tapa::invoke(CuperPcgSpmv, ...)` 传入：
     `SpElement_list_ptr`、`Matrix_data[0..15]`、packed FP32 `X`、packed FP32
     `Y_out`、`Batch_num`、`Matrix_len`、`Row_num`、`Column_num`、
     `Iteration_num=1`。
3. `DLC/Cuper/include/Cuper.h`
   - 声明 `CuperPcgSpmv(...)` ABI，并说明它是 Cuper-compatible demo。
4. `DLC/Cuper/kernels/detail/cuper_top_graphs.hpp`
   - 定义实际 one-shot task graph。
5. `DLC/Cuper/kernels/detail/cuper_spmv_tasks.hpp`
   - 定义 `SpElement_list_ptr_Loader`、`Vector_Loader`、`Matrix_Loader`、
     `Core`、`Accumulator`、`Vector_Checker`、`Mult_Sort_Tree`、`Vector_Writer`。

## 3. 当前 `CuperPcgSpmv` task graph

核心图：

```text
SpElement_list_ptr_Loader
Vector_Loader
Matrix_Loader[0..15]
  -> Core[0..15]
  -> Accumulator[0..15]
  -> Vector_Checker[0..7]
  -> Mult_Sort_Tree
  -> Vector_Writer
```

几类 stream 要分开理解：

| stream | 形态 | 含义 |
| --- | --- | --- |
| `PE_Param[0..16]` | 串接链 | ptr loader 产生参数，16 个 core 逐级转发 |
| `Vector_X_Stream[0..16]` | 串接链 | vector loader 产生 packed X，16 个 core 逐级转发 |
| `Matrix_A_Stream[0..15]` | 并行数组 | 16 个 HBM bank 的矩阵数据 |
| `Matrix_Mult_Vector_Stream[0..15]` | 并行数组 | 16 个 core 的局部乘积输出 |
| `Vector_Y_Stream[0..15]` | 并行数组 | accumulator 输出的 `float_v2` |
| `Vector_Y_Stream_Aftck[0..7]` | 8 路数组 | checker 过滤 padding 后给 sort tree |
| `Vector_Y_Stream_Ans` | 单路 | sort tree 拼出的 `float_v16` 输出，给 writer |

`[0..15]` 是 16 路 HBM/PE 并行；`[0..16]` 的最后一项是串接链尾，不是第 17 路计算。

## 4. 和 full `CuperPcg` 的边界

当前 `CuperPcgSpmv(...)` 和 full `CuperPcg(...)` 不共用 service 控制逻辑：

| 路径 | SpMV 形态 | 控制方式 |
| --- | --- | --- |
| `CuperPcgSpmv(...)` | one-shot Cuper graph | `Iteration_num` 固定次数，自然返回 |
| `CuperPcg(...)` | 常驻 `Pcg_*` service graph | `Pcg_Controller` 发 command/stop |

因此：

- 单 SpMV demo 的性能结果不能自动说明 full PCG 会提升；
- PCG 的 service/control 优化应看 `pcg_spmv_service.hpp`、`pcg_controller.hpp`、
  `pcg_stage_timer.hpp`；
- 要声明“优化同步进入 PCG”，必须补 full `CuperPcg(...)` 软件仿真或上板验证。

## 5. 历史 service demo 为什么不再作为当前实现

历史 service demo 的路径大致是：

```text
Pcg_SingleSpmv_Controller
  -> Pcg_* service SpMV chain
  -> Pcg_Single_Vector_Writer
  -> Writer_Done_Stream
  -> controller sends stop
```

它在软件仿真中能算出正确结果，但 2026-05-28 上板 `thermal2_n16` 两次 timeout，
日志停在：

```text
[tapa-invoke] after ReadFromDevice before Finish
```

这说明单独抽 service 链会额外引入 drain/stop 完成边界风险。当前策略是把 single
SpMV demo 恢复为 Cuper one-shot，把 PCG service/control 问题放回 full
`CuperPcg(...)` 里处理。

## 6. 继续调试时先看什么

测试当前 one-shot `CuperPcgSpmv(...)`：

```bash
make run-cuper-tapa-pcg-spmv \
  TARGET=hw \
  DATASET=<dataset> \
  BITFILE=395bitstream/cuper-tapa-spmv-u55c-YYYYMMDD-demo.xclbin \
  SPMV_REPEATS=3 \
  DIFF_TOL=1e-1
```

测试 full `CuperPcg(...)`：

```bash
make run-cuper-pcg-tapa-fpga \
  TARGET=hw \
  DATASET=<dataset> \
  BITFILE=395bitstream/cuper-tapa-pcg-fpga-u55c-YYYYMMDD-demo.xclbin \
  MAX_ITERS=1 \
  DIFF_TOL=1e-1
```

如果 full `CuperPcg` 软件仿真或硬件 smoke 卡住，优先检查 stop-driven 任务：

1. `Pcg_Vector_Checker` 是否仍在等待输入时检查 `Stop_in`；
2. `Pcg_Mult_Sort_Tree` 是否仍每拍检查 `Sort_Stop_Stream`；
3. controller 是否在消费完预期 `Pcg_Spmv_Stream` packet 后才广播 checker/sort stop；
4. 不要把 single one-shot 或 finite-exit 的自然返回逻辑直接搬到 full-PCG 常驻任务里。

## 7. 2026-06-18 compact16 阅读入口

compact16 相关路径按这个顺序读：

1. `DLC/Cuper-jacobi-iteration/host/main.cpp`
   - 搜 `JACOBI_SPMV_COMPACT_PE`；
   - host 在 SpMV-only 分支调用 compact 打包，并打印
     `[spmv-only-compact-pe] original_read_beats=... compact_read_beats=...`。
2. `DLC/Cuper-jacobi-iteration/include/Cuper_common.h`
   - compact lane tag 编码在这里定义；
   - `rowIdx[17]` 是 padding，`rowIdx[16:14]` 是原 PE lane，
     `rowIdx[13:0]` 是原 Cuper 行号。
3. `DLC/Cuper-jacobi-iteration/kernels/detail/cuper_spmv_service_only_top_graphs.hpp`
   - `CuperSpmvOnly_CoreCompactPe` 解 compact slot；
   - `CuperSpmvOnly_AccumulatorCompactPe` 按 lane tag 累加回对应 PE lane；
   - 当前实现偏功能验证，beat 内 slot 分发仍是串行结构。
4. `DLC/Cuper-jacobi-iteration/tools/pack_profile.c`
   - 用纯 C 评估 current/per-HBM/per-PE/compact512 的读取密度；
   - 这个工具不代表硬件吞吐，只用于判断打包协议还能挤掉多少 padding。

构建和运行必须同时设置：

```bash
JACOBI_TOP=CuperSpmvServiceOnly
JACOBI_SPMV_ONLY=1
JACOBI_HBM_CHANNELS=16
JACOBI_SPMV_COMPACT_PE=1
```

## 8. 2026-06-18 lanereal16 阅读入口

lanereal16 相关路径按这个顺序读：

1. `DLC/Cuper-jacobi-iteration/host/main.cpp`
   - 搜 `JACOBI_SPMV_LANE_STATIC_REAL`；
   - host 在 SpMV-only 分支调用 lane-static real/batch 打包，并打印
     `[spmv-only-lane-static-real] original_read_beats=... lane_static_read_beats=...`。
2. `DLC/Cuper-jacobi-iteration/include/Cuper_common.h`
   - `Create_SpElement_list_for_all_channels_lane_static_real_batch` 是 host 打包入口；
   - 它在每个 HBM、每个 batch 内只保留真实元素，但保持 `slot p -> lane p`；
   - 这版不写 compact16 的动态 lane tag。
3. `DLC/Cuper-jacobi-iteration/kernels/detail/cuper_spmv_service_only_top_graphs.hpp`
   - lanereal16 复用 strip-style `CuperSpmvOnly_StripPtrLoader`、
     `CuperSpmvOnly_MatrixLoaderStrip` 和 `CuperSpmvOnly_CoreStrip`；
   - 后端继续走普通 `SpmvService_Accumulator`，不是 compact accumulator。
4. `DLC/Cuper-jacobi-iteration/kernels/detail/cuper_spmv_tasks.hpp`
   - `JACOBI_SPMV_LANE_STATIC_REAL` 下关闭旧 reorder window dependence pragma；
   - HLS 报告显示当前 accumulator II=5，这是该版最需要关注的性能风险。

构建和运行必须同时设置：

```bash
JACOBI_TOP=CuperSpmvServiceOnly
JACOBI_SPMV_ONLY=1
JACOBI_HBM_CHANNELS=16
JACOBI_SPMV_LANE_STATIC_REAL=1
```

## 9. 2026-07-01 ownerbank8 阅读入口

ownerbank8 仍从 `DLC/Cuper-jacobi-iteration` 的 `CuperSpmvServiceOnly(...)` 进入，
但和 20260627 的 scoreboard-only 分支不同。读代码时按下面顺序：

1. `DLC/Cuper-jacobi-iteration/kernels/detail/cuper_spmv_service_only_top_graphs.hpp`
   - 搜 `CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC`；
   - `SourceLaneSplitterOoo[source]` 把每个 core 的 8 条 pair-lane 分发到 owner；
   - `RtlOwnerBankAccumulatorOoo[owner]` 每个 owner bank 接 8 条 pair-lane；
   - `TaggedScatterWriterOoo<8>` 轮询 8 条 owner-bank output stream。
2. `verilog/tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v`
   - 这是 hotpatch 到 XO 的 owner-bank wrapper；
   - wrapper 内部实例化 8 个 lane accumulator，并统计输出 token 完成后才 `ap_done`。
3. `verilog/tapa/CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo.v`
   - 单 lane RAW scoreboard、URAM partial sum 和 FP32 add pipeline 在这里；
   - Verilator 下通过 `CUPER_VERILATOR_DPI_FP` 调用 C++ FP32 add 模型。
4. `verilog/csrc/sim_tapa_splitter16_bank16_dataset.cpp`
   - 同一个 harness 支持 8-HBM 和 16-HBM；
   - 8-HBM target 用 `make -C verilog tapa-ownerbank8-dataset-cpp-sim`。

构建和运行必须同时设置：

```bash
JACOBI_TOP=CuperSpmvServiceOnly
JACOBI_SPMV_ONLY=1
JACOBI_HBM_CHANNELS=8
JACOBI_SPMV_STRIP_PADDING=1
JACOBI_SPMV_LANE_STATIC_REAL=1
JACOBI_SPMV_OOO_ACCUMULATE=1
JACOBI_SPMV_OOO_ACCUMULATE_RTL=1
```

注意两点：

- 不要同时打开 `JACOBI_SPMV_OOO_SCOREBOARD_RTL`；那是旧 scoreboard-only 分支；
- 当前 xclbin 的 `Y_out` kernel signature 是 `float* Y_out` / 32-bit scalar writer，
  而 strip8 是 `float_v16* Y_out` / 512-bit writer。host 参数名和 HBM bank mapping
  没变，但 writer 微架构不同，性能要以上板实测为准。
