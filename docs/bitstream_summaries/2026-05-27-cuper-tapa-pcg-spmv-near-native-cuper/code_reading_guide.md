# 代码阅读指南

本文只对应本目录记录的 `cuper-tapa-pcg` packed feed/AP demo。它不是全仓库通用
设计文档；阅读这版代码时，以这里的文件顺序和数据流为准。

如果要按“两次 PCG 迭代”追踪 `X/R/Z/P`、`X_spmv/P_spmv/AP_spmv` 和
TAPA stream 如何传递，先看同目录的 `two_iteration_dataflow.md`。

## 先看结论

这一版要解决的问题是：`CuperPcg` 虽然用了 TAPA Cuper 的 16 路 SpMV task graph，
但旧路径在 controller 附近把 SpMV 输入/输出重新串行化了。

本版做了两件事：

1. `X/P` 输入侧新增 packed `X_spmv/P_spmv`，让 `Pcg_Vector_Loader` 直接读
   `float_v16`，不再让 controller 从 `double X/P` 逐元素打包。
2. `AP` 输出侧新增 packed `AP_spmv`，让 controller 直接缓存 Cuper 输出的一包
   `float_v16`，后续 dot/update 再按包读取。
3. controller-split demo 又把 `p^T AP` 合入 `iter_spmv_stream` 接收路径，
   并把 `update_xr` / `update_p` 各自拆成 compute/store 两段。
4. 当前 packed timing demo 把 `B/M_inv/X/R/Z/P` 主状态改成 packed `double_v8`
   mmap，`Metrics[5..15]` 改为 packed memory packet work，真实分段时间看
   `[stage-cycles]` / `[stage-ms]`。

所以读代码时不要把 `X` 和 `X_spmv`、`P` 和 `P_spmv`、旧 `AP` 和 `AP_spmv`
混成同一个东西：

| 名称 | 类型 | 作用 |
| --- | --- | --- |
| `X` | `double_v8*` | PCG 最终解和 FP64 更新状态，512-bit packed |
| `X_spmv` | `float_v16*` | 初始化 `A*x0` 的 packed SpMV 输入 |
| `P` | `double_v8*` | PCG 搜索方向的 FP64 状态，512-bit packed |
| `P_spmv` | `float_v16*` | 每轮 `A*p` 的 packed SpMV 输入 |
| `AP_spmv` | `float_v16*` | 每轮 `A*p` 的 packed SpMV 输出缓存 |

当前 single SpMV demo 已经回到 `CuperPcgSpmv(...)` one-shot Cuper-compatible
图，主要作为回归基线和边界检查。full-PCG 的主优化对象仍是
`CuperPcg(...)` 中的 controller/dot/update 路径。因此读代码时要分清两套
SpMV 实现和一套 PCG 后处理：

| 形态 | 文件 | 当前角色 |
| --- | --- | --- |
| 满血 Cuper SpMV | `detail/cuper_spmv_tasks.hpp` | `Cuper(...)` 标准基准，当前最快的 standalone TAPA Cuper SpMV |
| Cuper-compatible demo SpMV | `CuperPcgSpmv(...)` + `detail/cuper_spmv_tasks.hpp` | 保留历史 kernel 名的 single SpMV demo，用于回归/边界检查 |
| PCG 服务化 SpMV | `detail/pcg_spmv_service.hpp` | 为 `CuperPcg(...)` 重复触发、stop token 和 stage 计时调整过的共用 SpMV service |
| PCG controller/update | `detail/pcg_controller.hpp` | 当前 full-PCG 性能主战场，包含 init 专用和 PCG 迭代专用向量阶段 |

single SpMV demo 的结论看 `spmv_avg`、timeout 边界和 diff；full-PCG 结论必须看
`CuperPcg(...)` 实际路径里的 `pcg-spmv-ms`、`pcg-control-ms`、
`controller_total` 和 `kernel_reported`。

## 阶段归属速记

full `CuperPcg(...)` 的阶段可以按“init 专用 / PCG 迭代专用 / init+迭代共用”
来读：

| 阶段 | 源码标签 | 归属 | 说明 |
| --- | --- | --- | --- |
| `init_spmv` | `init_spmv_stream` | 共用 SpMV service 的 init 调用 + init 专用 R 生成 | 通过 `vector_source=X` 读 `X_spmv`，算 `A*x0`，controller 生成 `R=B-A*x0` |
| `init_zp` | `init_zp_reduce` | init 专用 | 读 `R/M_inv`，初始化 `Z/P`，累计初始 `rz/rr`，并生成第一轮 `P_spmv` |
| `iter_spmv` | `iter_spmv_stream` | 共用 SpMV service 的 PCG 调用 + 迭代专用 AP 接收 | 通过 `vector_source=P` 读 `P_spmv`，算 `A*p`，controller 把 packed AP 暂存到 `AP_spmv`，当前 demo 同时计算 `p^T AP` |
| `dot_p_ap` / `p^T AP` | `iter_dot_p_ap_lanes` | PCG 迭代专用子循环，嵌在 `iter_spmv_stream` 内 | 读 FP64 `P` 和当前 AP packet，转 double 后算 `p^T AP`；当前不再有独立 stage timer |
| `update_xr` | `update_xr` | PCG 迭代专用 | 更新 FP64 `X/R`，读 `AP_spmv` 时做 FP32->FP64 转换 |
| `update_z` | `update_z_reduce` | PCG 迭代专用 | 根据新 `R` 更新 `Z`，累计新的 `rz/rr` |
| `update_p` | `update_p` | PCG 迭代专用 | 更新 FP64 `P`，同时写下一轮 SpMV 需要的 packed FP32 `P_spmv` |

`Pcg_SpElement_list_ptr_Loader`、`Pcg_Vector_Loader`、`Pcg_Matrix_Loader[0..15]`、
`Pcg_Core[0..15]`、`Pcg_Accumulator[0..15]`、`Pcg_Vector_Checker[0..7]` 和
`Pcg_Mult_Sort_Tree` 是 init 和迭代共用的 SpMV service。若优化这些共享模块，
理论上会同时影响 `init_spmv` 和 `iter_spmv`；若只改
`iter_spmv_recv_dot/update_xr/update_z/update_p` 这类 controller 阶段，主要影响
1iter/多 iter 增量，不会等比例加速 init-only。

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

其中 `B/M_inv/X/R/Z/P` 的 mmap 元素类型在当前 packed timing demo 中是
`double_v8`，host 负责在读写前后做 pack/unpack。旧文档或旧 `source.diff` 中的
`double*` 表述只对应早期 packed feed/AP 版本。

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

这些 Pcg_* SpMV task 是 init 和 PCG 迭代共用的 service。controller 通过
`CuperSpmvCommand::vector_source` 选择本轮是 `A*x0` 还是 `A*p`：

```text
init_spmv -> vector_source = X -> 读 X_spmv
iter_spmv -> vector_source = P -> 读 P_spmv
```

因此优化 `Pcg_Matrix_Loader/Core/Accumulator/Checker/Sort_Tree` 这类模块时，
应同时观察 `init_spmv` 和 `iter_spmv`；优化 `Pcg_Controller` 的 dot/update
阶段时，主要看 `1iter - init-only` 的迭代增量。

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

1. `pcg_send_spmv_command(..., kPcgVectorSourceX)`：
   - 共用 SpMV service 的 init 调用；
   - 命令 vector loader 读 `X_spmv`，命令 16 个 matrix loader 读矩阵。
2. `init_spmv_stream`：
   - 共用 SpMV service 返回 `A*x0`；
   - init 专用地写初始残差 `R = B - A*x0`。
3. `init_zp_reduce`：
   - init 专用；
   - 读 `R/M_inv`，写 `Z/P`，同步写第一轮 `P_spmv`，累计初始 `rz/rr`。
4. `pcg_send_spmv_command(..., kPcgVectorSourceP)`：
   - 共用 SpMV service 的迭代调用；
   - 命令 vector loader 读 `P_spmv`。
5. `iter_spmv_stream`：
   - PCG 迭代专用接收 `A*p`；
   - 直接写 packed `AP_spmv`；
   - 当前 packed timing demo 在接收 AP 时同步读 packed `P` 并计算
     `p_ap = p^T A p`，不再另开独立 `dot_p_ap` stage。
7. `update_xr`：
   - PCG 迭代专用；
   - 读 `X/P/R/AP_spmv`，先在 `update_xr_compute_lanes` 里计算新 `X/R`，
     再在 `update_xr_store_lanes` 里写回。
8. `update_z_reduce`：
   - PCG 迭代专用；
   - 读 `R/M_inv`，更新 `Z`，累计新的 `rz/rr`。
9. `update_p`：
   - PCG 迭代专用；
   - 先在 `update_p_compute_lanes` 里计算 `P = Z + beta * P`，再在
     `update_p_store_lanes` 里写回 `P`，最后同步更新下一轮 SpMV 需要的 packed
     `P_spmv`。
10. stop 广播和 metrics 写回：
   - 收尾控制，不属于算法阶段；
   - 这部分和 service drain 会反映到 `unaccounted_controller` 或 `controller 外`。

`Metrics` 当前分两套口径：

| Metrics | 含义 |
| --- | --- |
| `[0..3]` | `rz/rr/p_ap/alpha` |
| `[4]` | `float_v16` packet 数 |
| `[5..15]` | packed memory packet work / packet 数，不是实测 cycle |
| `[16..24]` | `Pcg_Stage_Timer` 统计 cycle |

板上看性能时，优先对比：

- `pcg-spmv-ms`：full-PCG 内嵌 SpMV 的 `spmv_total/spmv_avg`；
- `pcg-control-ms`：`pcg_vector_total`、`unaccounted_controller`、
  `kernel_minus_controller`；
- `stage-ms`：`init_spmv`、`init_zp`、`iter_spmv_recv_dot`、`update_xr`、
  `update_z`、`update_p`；
- `[timing-ms] kernel_reported`：host/XRT 看到的完整 kernel 时间。

当前 packed timing demo 的 stage 口径有一个变化：`kPcgStageDotPAp` 不再单独
发 begin/end event，host 也不再把 `dot_p_ap` 作为独立 `stage-ms` 字段打印；
`p^T AP` 的真实工作被并入 `iter_spmv_stream`。更新 HTML 或比较历史数据时，
应把这一项标成 `iter recv + dot` 或显式写出新旧口径差异，不能直接把 raw
`iter_spmv` 当作纯 SpMV 接收成本。

`1iter kernel` 没有去掉 init；它包含 `init_spmv + init_zp + iter_spmv +
dot/update + 收尾`。要看迭代增量，应使用 `1iter kernel - init-only kernel`，
再结合 `pcg-control-ms` 判断增量落在 SpMV、`pcg_vector_total` 还是
controller 外。

## 当前最大规模观测锚点

下面这组数据来自 2026-05-31 packed timing demo-only 上板测试：

```text
logs/codex_packed_timing_demo_test_20260531_195109_proper/
dataset: thermal2
N = 1,228,045
nnz = 8,580,313
```

这是目前观察 packed `double_v8` 架构优化方向的最大数据点。单位均为 ms：

| 指标 | 数值 | 读法 |
| --- | ---: | --- |
| `init-only kernel` | 302.7442 | 只跑初始化路径，包含 `A*x0 + init_zp + 收尾` |
| `1iter kernel` | 944.1232 | 完整 `init + 1 次 PCG 迭代 + 收尾`，没有去掉 init |
| `kernel delta` | 641.3790 | `1iter kernel - init-only kernel`，近似第 1 次迭代增量 |
| `controller_total` | 920.2593 | FPGA 内 controller 计到的主体时间 |
| `controller 外` | 23.8639 | `kernel_reported - controller_total`，主要是边界/drain/同步余量 |
| `init_spmv + iter_spmv_recv_dot` | 189.3382 | init SpMV 加迭代 AP 接收和 fused dot |
| `pcg_vector_total` | 730.9200 | `init_zp + update_xr + update_z + update_p` |
| `SpMV/recv/dot 占 controller` | 20.6% | 当前不是 controller 内最大项 |
| `pcg_vector_total 占 controller` | 79.4% | 当前 controller 内主瓶颈 |
| `unaccounted ctrl` | 约 0.001 | 已命名 stage 之外的 controller 内部余量很小 |

主要阶段：

| 阶段 | 数值 | 归属 |
| --- | ---: | --- |
| `init_spmv` | 89.7385 | 共用 SpMV service 的 init 调用 |
| `init_zp` | 197.2362 | init 专用，含 FP64 `rz/rr` reduction |
| `iter_spmv_recv_dot` | 99.5997 | PCG 迭代专用，含 `A*p` 接收、`AP_spmv` 写入和 `p^T AP` |
| `update_xr` | 211.3258 | PCG 迭代专用 |
| `update_z` | 169.0716 | PCG 迭代专用，含 FP64 `rz/rr` reduction |
| `update_p` | 153.2865 | PCG 迭代专用，同步维护 `P_spmv` |

这组数据给出的优化方向：

1. `B/M_inv/X/R/Z/P` 已改成 512-bit packed `double_v8` 端口，但完整规模下
   `pcg_vector_total` 仍占 controller 约 79.4%；单纯把端口吃宽还没有解决
   controller/vector 阶段的吞吐问题。
2. `init_spmv + iter_spmv_recv_dot` 占 controller 约 20.6%。这里的
   `iter_spmv_recv_dot` 已包含 `p^T AP`，不能按纯 SpMV 本体解读；当前更合理的
   判断是瓶颈仍主要在 SpMV 之外。
3. 优先继续看 `update_xr`、`init_zp/update_z` reduction 和 `update_p`，其次看
   `iter_spmv_recv_dot` 内 fused dot 的 FP64 recurrence。
4. `controller 外` 约 `23.86 ms`，比向量阶段小得多；优化应先落到 controller
   里的 HBM 读写、FP32/FP64 转换、副本同步、stage 串行和归约结构。
5. 共同成功点 `thermal2_n262144` 仍是当前 packed timing demo `210.3193 ms`
   慢于 TAPA full-PCG 标准版 `188.8202 ms`，所以它还不是标准替换候选。

2026-05-31 main reduction 实验补充：

- `init_zp` / `update_z` 的 `rz/rr` 已改成 8-bank per-lane accumulator，
  对应 HLS 约从 II=5 改到 `init_zp` II=4、`update_z` II=2；
- 同样方法试到 `p^T AP` 时，`iter_spmv_stream` 会变成 II=11，整体接收 AP
  更慢，所以不要照搬；
- `update_xr/update_p` 当前 lane 子循环仍可到 II=1，真正问题在外层 packet
  stage 串行和 HBM 往返，不是单纯给 lane 再加 unroll。

## Host 侧怎么读

`host/cuper_tapa_pcg_fpga_main.cpp` 做四件事：

1. 读取 Project-XPlus CSR 数据集；
2. 用 Cuper 工具函数把 CSR 转成 `SpElement_list_ptr` 和 16 路 `Matrix_data`；
3. 准备 packed `double_v8` FP64 PCG 状态和 packed `X_spmv/P_spmv/AP_spmv`；
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

- `pcg-spmv-ms spmv_avg` 是否接近 single SpMV 回归基线；
- `controller_total` 是否下降；
- `pcg_vector_total` 是否下降；
- `iter_spmv_recv_dot`、`init_zp/update_z`、`update_xr/update_p` 是否下降；
- `kernel_reported` 是否下降；
- 完整 `thermal2` 的 `ctrl=0x0` 失败边界是否变化。

如果 `pcg-spmv-ms` 快了但 `kernel_reported` 几乎不变，说明瓶颈已经转移到
controller vector/update 阶段、task graph 同步、service drain，或当前仍未命名的
外围时间。
