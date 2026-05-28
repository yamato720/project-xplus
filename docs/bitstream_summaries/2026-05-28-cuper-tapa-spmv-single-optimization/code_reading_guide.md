# CuperPcgSpmv finite-exit 版代码阅读指南

这份文档对应 2026-05-28 的 `CuperPcgSpmv` 单 SpMV demo，尤其是服务器侧修复后的
finite-exit 版本。它解释这版代码怎么读、哪些路径是从 `CuperPcg` 抠出来的、以及
为什么这版和满血 standalone `Cuper(...)` 不是同一个实现。

## 1. 先分清三条入口

| 入口 | 文件 | 作用 |
| --- | --- | --- |
| `Cuper(...)` | `DLC/Cuper/kernels/detail/cuper_top_graphs.hpp` | 满血 standalone TAPA Cuper single SpMV，当前标准基线 |
| `CuperPcg(...)` | `DLC/Cuper/kernels/detail/cuper_top_graphs.hpp` | TAPA Cuper SpMV + FPGA 内 PCG，全流程 kernel |
| `CuperPcgSpmv(...)` | `DLC/Cuper/kernels/detail/cuper_top_graphs.hpp` | 从 `CuperPcg` 的 PCG service SpMV 链抽出的 single SpMV demo |

当前这版 demo 的 kernel 名是 `CuperPcgSpmv`，不是 `Cuper`。因此上板测试必须走：

```bash
make run-cuper-tapa-pcg-spmv \
  TARGET=hw \
  DATASET=<dataset> \
  BITFILE=395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin \
  SPMV_REPEATS=3 \
  DIFF_TOL=1e-1
```

普通 `make run-cuper-tapa-spmv` 会按 `Cuper` 入口调用，不适合这个 demo。

## 2. Host 到 kernel 的调用链

阅读顺序：

1. `Makefile`
   - `run-cuper-tapa-pcg-spmv` 会调用 host，并加上 `--spmv-only --pcg-spmv-service`。
2. `host/cuper_tapa_pcg_main.cpp`
   - `--pcg-spmv-service` 让 `CuperTapaSpmv` 选择 `CuperPcgSpmv`。
   - host 仍复用 Cuper 的矩阵预处理：CSR -> COO -> SparseSlice -> 16 路 HBM matrix data。
   - `tapa::invoke(CuperPcgSpmv, ...)` 传入：
     `SpElement_list_ptr`、`Matrix_data[0..15]`、packed FP32 `X`、packed FP32 `Y_out`、
    `Batch_num`、`Matrix_len`、`Row_num`、`Column_num`、`Iteration_num=1`。
    当前 service 版内部忽略 `Iteration_num`；这个参数只为了保持
    `CuperPcgSpmv(...)` ABI 和 host 调用兼容。
3. `DLC/Cuper/include/Cuper.h`
   - 声明 `CuperPcgSpmv(...)` ABI。
4. `DLC/Cuper/kernels/detail/cuper_top_graphs.hpp`
   - 定义 TAPA task graph。
5. `DLC/Cuper/kernels/detail/pcg_spmv_service.hpp`
   - 定义实际 service SpMV task。

## 3. `CuperPcgSpmv` task graph 怎么看

核心图：

```text
Pcg_SingleSpmv_Controller
  -> Pcg_SpElement_list_ptr_Loader
  -> Pcg_Single_Vector_Loader
  -> Pcg_Matrix_Loader[0..15]
  -> Pcg_Core[0..15]
  -> Pcg_Accumulator[0..15]
  -> Pcg_Single_Vector_Checker[0..7]
  -> Pcg_Single_Mult_Sort_Tree
  -> Pcg_Single_Vector_Writer
  -> Writer_Done_Stream
  -> Pcg_SingleSpmv_Controller sends stop
```

几类 stream 要分开理解：

| stream | 形态 | 含义 |
| --- | --- | --- |
| `Command_Stream[0..1]` | 两路命令 | `[0]` 给 ptr loader，`[1]` 给 vector loader |
| `Matrix_Command_Stream[0..15]` | 16 路命令 | 每个 HBM matrix loader 一路 |
| `PE_Param[0..16]` | 串接链 | ptr loader 产生参数，16 个 core 逐级转发 |
| `Vector_X_Stream[0..16]` | 串接链 | vector loader 产生 packed X，16 个 core 逐级转发 |
| `Matrix_A_Stream[0..15]` | 并行数组 | 16 个 HBM bank 的矩阵数据 |
| `Matrix_Mult_Vector_Stream[0..15]` | 并行数组 | 16 个 core 的局部乘积输出 |
| `Vector_Y_Stream[0..15]` | 并行数组 | accumulator 输出的 `float_v2` |
| `Vector_Y_Stream_Aftck[0..7]` | 8 路数组 | checker 过滤 padding 后给 sort tree |
| `Pcg_Spmv_Stream` | 单路 | sort tree 拼出的 `float_v16` 输出，给 writer |

`[0..15]` 是 16 路 HBM/PE 并行；`[0..16]` 里的最后一项是串接链尾，不是第 17 路计算。

当前 PCG service 的命令语义是“一条 `CuperSpmvCommand` 只跑一次 SpMV”。full
`CuperPcg(...)` 需要 init `A*x0` 或每轮 `A*p` 时，由 `Pcg_Controller` 多次发送
command；`CuperPcgSpmv(...)` 单 SpMV demo 则只发一条普通 command，再在 writer
完成后发 stop。不要再从 command 里找 `iteration_num`。

## 4. finite-exit 修复读法

第一版 demo 在板上卡在：

```text
[tapa-invoke] after ReadFromDevice before Finish
```

含义是 `Y_out` 的 D2H 读可能已经返回，但 kernel 自身没有完成。根因方向是
service task 的退出/drain 协议，而不是 CPU diff。

服务器修复后的关键变化：

1. `Pcg_Vector_Checker` / `Pcg_Mult_Sort_Tree` 仍保留给 full-PCG `CuperPcg(...)` 用。
   它们是 stop-driven，依赖 PCG controller 在合适时机发 stop。
2. `CuperPcgSpmv(...)` 改用：
   - `Pcg_Single_Vector_Checker`
   - `Pcg_Single_Mult_Sort_Tree`
3. 这两个 single 版本不再接收 stop stream，而是按固定输出数量自然返回。

这个区别很重要：单 SpMV demo 的 writer 只需要 `ceil(Row_num/16)` 个有效
`float_v16` 包，但 accumulator/checker 上游会产生按 Cuper PE 对齐后的 padding。
如果 writer 一写完有效包就让 controller 发 stop，checker 可能还没把 padding
从上游读空，上游会被反压，最终 `Finish` 等不到所有 task done。

## 5. 单 SpMV 尾端三个 task

### `Pcg_Single_Vector_Checker`

位置：`DLC/Cuper/kernels/detail/pcg_spmv_service.hpp`

职责：

- 从 `Vector_Y_Stream` 完整消费 accumulator 产生的对齐输出；
- 只把真实 `Row_num` 对应的有效 `float_v2` 转发给 sort tree；
- padding 不转发，但必须读掉。

关键计数：

```text
num_pe_output = ceil(Row_num / (HBM_CHANNEL_NUM * 2)) * (HBM_CHANNEL_NUM / 8)
num_out       = ceil(Row_num / 16)
```

`num_pe_output` 是要从上游读掉的数量，`num_out` 是要继续转发的有效数量。

### `Pcg_Single_Mult_Sort_Tree`

职责：

- 等齐 8 路 `float_v2`；
- 拼成 1 个 `float_v16`；
- 写够 `ceil(Row_num/16)` 包后返回。

它不处理 padding，因为 padding 已经被 checker 读掉并过滤。

### `Pcg_Single_Vector_Writer`

职责：

- 从 `Pcg_Spmv_Stream` 读 `float_v16`；
- 写入 `Y_out` async mmap；
- 等所有 AXI write response 回来；
- 向 controller 写 `Writer_Done_Stream`。

`Writer_Done_Stream` 是这个 demo 的 drain 屏障。controller 收到 done 后，才给仍然常驻的
ptr/vector/matrix loader、core 链和 vector destroy 发送 stop。

## 6. stop token 现在只管哪些 task

finite-exit 版本里，stop token 不再管 checker/sort/writer。

仍由 stop token 退出的路径：

```text
controller stop command
  -> Pcg_SpElement_list_ptr_Loader
  -> PE_Param[0]
  -> Pcg_Core[0..15]
  -> PE_Param[16]
  -> Pcg_Destroy_int

controller stop command
  -> Pcg_Single_Vector_Loader

controller stop command
  -> Pcg_Matrix_Loader[0..15]

Vector_Destroy_Stop_Stream
  -> Pcg_Destroy_float_v16
```

自然返回的路径：

```text
Pcg_Single_Vector_Checker
Pcg_Single_Mult_Sort_Tree
Pcg_Single_Vector_Writer
```

读代码时不要把 full-PCG 的 stop-driven `Pcg_Vector_Checker` / `Pcg_Mult_Sort_Tree`
和 single demo 的 finite-exit 版本混在一起。

## 7. 和满血 `Cuper(...)` 的差异

`CuperPcgSpmv` 复用了 PCG service 版 SpMV，不是标准 `Cuper(...)`：

- `Cuper(...)` 调用 `detail/cuper_spmv_tasks.hpp`，一次性 task graph；
- `CuperPcgSpmv(...)` 调用 `detail/pcg_spmv_service.hpp`，loader/core 是常驻服务模型；
- `Cuper(...)` 的 `Vector_Checker` 按 `Iteration_num` 固定跑完；
- `CuperPcgSpmv(...)` 的 `Iteration_num` ABI 参数当前被忽略，service 版只靠
  command 次数表达 SpMV 次数；
- `CuperPcgSpmv(...)` 的 single checker/sort 是为了让 service 链也能有限退出；
- 性能结果不能直接等价为满血 Cuper 性能，只能说明“PCG service SpMV 抽出版”的表现。

## 8. 继续调试时先看什么

如果新 finite-exit bitstream 仍然不通，优先看：

1. 是否仍卡在 `after ReadFromDevice before Finish`；
2. `Pcg_Single_Vector_Writer` 是否等不到全部 write response；
3. `Pcg_Destroy_float_v16` 是否过早收到 stop，导致 `Vector_X_Stream[16]` 未 drain；
4. `PE_Param` stop token 是否能通过 16 个 `Pcg_Core` 传到 `Pcg_Destroy_int`；
5. checker 计算的 `num_pe_output` 是否和 accumulator 实际输出完全一致。

如果能返回，再做性能和边界判断：

```text
thermal2_n16 -> thermal2_n65536 -> thermal2_n131072 -> thermal2_n262144 -> thermal2
```

每个数据点都要记录 `spmv_avg`、GFLOP/s、CPU diff、timeout 和退出码。
