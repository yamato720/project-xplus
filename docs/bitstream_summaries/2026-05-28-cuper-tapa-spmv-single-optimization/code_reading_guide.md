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

当前 `CuperPcgSpmv(...)` 不是从 `CuperPcg(...)` service 链抠出来的版本。它不使用
`Pcg_SingleSpmv_Controller`、`Pcg_Single_Vector_Loader` 或 writer-done stop
控制壳。

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
