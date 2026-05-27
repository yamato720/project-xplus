# 代码阅读指南

本文只对应本目录记录的 `cuper-tapa-pcg` packed feed/AP demo。它不是全仓库通用
设计文档；阅读这版代码时，以这里的文件顺序和数据流为准。

## 先看结论

这一版要解决的问题是：`CuperPcg` 虽然用了 TAPA Cuper 的 16 路 SpMV task graph，
但旧路径在 controller 附近把 SpMV 输入/输出重新串行化了。

本版做了两件事：

1. `X/P` 输入侧新增 packed `X_spmv/P_spmv`，让 `Pcg_Vector_Loader` 直接读
   `float_v16`，不再让 controller 从 `double X/P` 逐元素打包。
2. `AP` 输出侧新增 packed `AP_spmv`，让 controller 直接缓存 Cuper 输出的一包
   `float_v16`，后续 dot/update 再按包读取。

所以读代码时不要把 `X` 和 `X_spmv`、`P` 和 `P_spmv`、旧 `AP` 和 `AP_spmv`
混成同一个东西：

| 名称 | 类型 | 作用 |
| --- | --- | --- |
| `X` | `double*` | PCG 最终解和 FP64 更新状态 |
| `X_spmv` | `float_v16*` | 初始化 `A*x0` 的 packed SpMV 输入 |
| `P` | `double*` | PCG 搜索方向的 FP64 状态 |
| `P_spmv` | `float_v16*` | 每轮 `A*p` 的 packed SpMV 输入 |
| `AP_spmv` | `float_v16*` | 每轮 `A*p` 的 packed SpMV 输出缓存 |

## 推荐阅读顺序

### 1. 顶层 ABI

先看：

```text
DLC/Cuper/include/Cuper.h
cfg/connectivity_cuper_tapa_pcg_u55c.cfg
host/cuper_tapa_pcg_fpga_main.cpp
```

重点确认三件事：

- `CuperPcg(...)` 顶层参数顺序；
- connectivity 里的 HBM bank 映射；
- host 里的 `kCuperPcgMemoryGroups` 和 direct-register offset。

这三处必须同步。只改其中一个，会导致 host 写错 BO 地址或 Vitis link 找不到
对应 port。

当前新 ABI 是 28 个 memory args：

```text
0      SpElement_list_ptr
1..16  Matrix_data_0..15
17     B
18     M_inv
19     X
20     R
21     Z
22     P
23     AP_spmv
24     X_spmv
25     P_spmv
26     Metrics
27     Status
```

旧标准 bitstream 是 26 个 memory args，没有 `X_spmv/P_spmv/AP_spmv`。host 里
`--legacy-abi` 专门保留给旧标准 bitstream 对比，不要删。

### 2. TAPA task graph

再看：

```text
DLC/Cuper/kernels/detail/cuper_top_graphs.hpp
```

`CuperPcg(...)` 是 TAPA compile 的顶层。这里能看到完整 task graph：

```text
Pcg_Controller
  -> Command_Stream / Matrix_Command_Stream

Pcg_SpElement_list_ptr_Loader
Pcg_Vector_Loader
Pcg_Matrix_Loader[0..15]
Pcg_Core[0..15]
Pcg_Accumulator[0..15]
Pcg_Vector_Checker[0..7]
Pcg_Mult_Sort_Tree
  -> Pcg_Spmv_Stream
  -> Pcg_Controller
```

读这个文件时要特别注意两条链：

- `PE_Param[0..16]` 和 `Vector_X_Stream[0..16]` 是串接转发链；
- `Matrix_A_Stream[0..15]` 和 `Matrix_Mult_Vector_Stream[0..15]` 才是真正的
  16 路 HBM/SpMV 并行路径。

`[16]` 是链尾，不代表第 17 个矩阵 channel。

### 3. 命令和状态枚举

再看：

```text
DLC/Cuper/kernels/detail/pcg_common.hpp
```

这里定义了三类小结构/常量：

- `CuperSpmvCommand`：controller 发给 SpMV 服务任务的一次运行命令；
- `PcgStageEvent`：controller 发给 stage timer 的 begin/end/stop 事件；
- `kPcgStage*` / `kPcgStatus*`：metrics/status 的编码。

`vector_source` 是本版 packed feed 的关键字段：

```text
kPcgVectorSourceX -> Pcg_Vector_Loader 读 X_spmv
kPcgVectorSourceP -> Pcg_Vector_Loader 读 P_spmv
```

### 4. SpMV 服务任务

再看：

```text
DLC/Cuper/kernels/detail/pcg_spmv_service.hpp
```

这里不是新写一套 SpMV，而是把原 TAPA Cuper 的 loader/core/accumulator/checker
改成常驻服务。

阅读重点：

- `Pcg_SpElement_list_ptr_Loader`：每次命令重新广播 batch 边界；
- `Pcg_Vector_Loader`：本版直接从 `X_spmv/P_spmv` 读 packed 向量；
- `Pcg_Matrix_Loader`：16 个实例并行读 `Matrix_data_0..15`；
- `Pcg_Core`：每个 channel 解码 512-bit matrix beat 并做局部乘法；
- `Pcg_Accumulator`：用 Cuper 内部 row 编码累加部分和；
- `Pcg_Vector_Checker`：过滤 padding；
- `Pcg_Mult_Sort_Tree`：把 8 路 `float_v2` 合成 1 路 `float_v16`。

注意 Cuper 内部 `row` 是重排后的 18-bit 编码，不是原始全局行号。不要直接拿它
判断 `65535` 行边界。

### 5. PCG controller

最后看：

```text
DLC/Cuper/kernels/detail/pcg_controller.hpp
```

这是 full-PCG 的主循环。按阶段读最清楚：

1. `send_init_command` / `send_init_matrix_command`
   - 命令 vector loader 读 `X_spmv`；
   - 命令 16 个 matrix loader 读矩阵。
2. `init_spmv_stream`
   - 消费 `A*x0`；
   - 写初始残差 `R = B - A*x0`。
3. `init_zp_reduce`
   - 读 `R/M_inv`；
   - 写 `Z/P`；
   - 同步写 `P_spmv`；
   - 累计 `rz/rr`。
4. `iter_spmv_stream`
   - 命令 vector loader 读 `P_spmv`；
   - 消费 `A*p`；
   - 直接写 packed `AP_spmv`。
5. `dot_p_ap`
   - 读 `P` 和 `AP_spmv`；
   - 计算 `p_ap = p^T A p`。
6. `update_xr`
   - 读 `X/P/R/AP_spmv`；
   - 更新 `X/R`。
7. `update_z_reduce`
   - 读 `R/M_inv`；
   - 更新 `Z`；
   - 累计新的 `rz/rr`。
8. `update_p`
   - 更新 `P = Z + beta * P`；
   - 同步更新 packed `P_spmv`。
9. stop 广播和 metrics 写回。

`Metrics` 当前分两套口径：

| Metrics | 含义 |
| --- | --- |
| `[0..3]` | `rz/rr/p_ap/alpha` |
| `[4..14]` | 手工 work-tick 估算 |
| `[16..24]` | `Pcg_Stage_Timer` 统计 cycle |

板上看性能时，优先对比 `init_spmv`、`iter_spmv`、`controller_total` 和
`kernel_reported`。`dot/update` 当前 stage 计时还不够精确，不能单独作为结论。

## Host 侧怎么读

`host/cuper_tapa_pcg_fpga_main.cpp` 做四件事：

1. 读取 Project-XPlus CSR 数据集；
2. 用 Cuper 工具函数把 CSR 转成 `SpElement_list_ptr` 和 16 路 `Matrix_data`；
3. 准备 FP64 PCG 状态和 packed `X_spmv/P_spmv/AP_spmv`；
4. 软件仿真时用 `tapa::invoke`，硬件运行时用 XRT direct-register 启动。

host 里最容易看错的是两套 ABI：

- 默认新 ABI：28 memory args，给本版 demo 用；
- `--legacy-abi`：26 memory args，给旧标准 bitstream 用。

测试旧标准版时必须加：

```bash
LEGACY_ABI=1
```

测试本版 demo 时不要加。

## 这一版代码阅读时的判断标准

这版改动是否有效，不要只看资源或频率，要看动态结果：

- `iter_spmv` 是否下降；
- `controller_total` 是否下降；
- `kernel_reported` 是否下降；
- 完整 `thermal2` 的 `ctrl=0x0` 失败边界是否变化。

如果 `iter_spmv` 快了但 `kernel_reported` 几乎不变，说明瓶颈已经转移到
controller 其它阶段、task graph 同步、或当前 metrics 尚未拆准的外围时间。
