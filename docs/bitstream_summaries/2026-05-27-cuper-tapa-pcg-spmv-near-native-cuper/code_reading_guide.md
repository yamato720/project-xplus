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

所以读代码时不要把 `X` 和 `X_spmv`、`P` 和 `P_spmv`、旧 `AP` 和 `AP_spmv`
混成同一个东西：

| 名称 | 类型 | 作用 |
| --- | --- | --- |
| `X` | `double*` | PCG 最终解和 FP64 更新状态 |
| `X_spmv` | `float_v16*` | 初始化 `A*x0` 的 packed SpMV 输入 |
| `P` | `double*` | PCG 搜索方向的 FP64 状态 |
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
| `iter_spmv` | `iter_spmv_stream` | 共用 SpMV service 的 PCG 调用 + 迭代专用 AP 接收 | 通过 `vector_source=P` 读 `P_spmv`，算 `A*p`，controller 把 packed AP 暂存到 `AP_spmv` |
| `dot_p_ap` | `dot_p_ap` | PCG 迭代专用 | 读 FP64 `P` 和 packed FP32 `AP_spmv`，转 double 后算 `p^T AP` |
| `update_xr` | `update_xr` | PCG 迭代专用 | 更新 FP64 `X/R`，读 `AP_spmv` 时做 FP32->FP64 转换 |
| `update_z` | `update_z_reduce` | PCG 迭代专用 | 根据新 `R` 更新 `Z`，累计新的 `rz/rr` |
| `update_p` | `update_p` | PCG 迭代专用 | 更新 FP64 `P`，同时写下一轮 SpMV 需要的 packed FP32 `P_spmv` |

`Pcg_SpElement_list_ptr_Loader`、`Pcg_Vector_Loader`、`Pcg_Matrix_Loader[0..15]`、
`Pcg_Core[0..15]`、`Pcg_Accumulator[0..15]`、`Pcg_Vector_Checker[0..7]` 和
`Pcg_Mult_Sort_Tree` 是 init 和迭代共用的 SpMV service。若优化这些共享模块，
理论上会同时影响 `init_spmv` 和 `iter_spmv`；若只改 `dot_p_ap/update_xr/update_p`
这类 controller 阶段，主要影响 1iter/多 iter 增量，不会等比例加速 init-only。

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
   - 直接写 packed `AP_spmv`，后续 dot/update 会再读它。
6. `dot_p_ap`：
   - PCG 迭代专用；
   - 读 `P` 和 `AP_spmv`，计算 `p_ap = p^T A p`。
7. `update_xr`：
   - PCG 迭代专用；
   - 读 `X/P/R/AP_spmv`，更新 `X/R`。
8. `update_z_reduce`：
   - PCG 迭代专用；
   - 读 `R/M_inv`，更新 `Z`，累计新的 `rz/rr`。
9. `update_p`：
   - PCG 迭代专用；
   - 更新 `P = Z + beta * P`，同步更新下一轮 SpMV 需要的 packed `P_spmv`。
10. stop 广播和 metrics 写回：
   - 收尾控制，不属于算法阶段；
   - 这部分和 service drain 会反映到 `unaccounted_controller` 或 `controller 外`。

`Metrics` 当前分两套口径：

| Metrics | 含义 |
| --- | --- |
| `[0..3]` | `rz/rr/p_ap/alpha` |
| `[4..14]` | 手工 work-tick 估算 |
| `[16..24]` | `Pcg_Stage_Timer` 统计 cycle |

板上看性能时，优先对比：

- `pcg-spmv-ms`：full-PCG 内嵌 SpMV 的 `spmv_total/spmv_avg`；
- `pcg-control-ms`：`pcg_non_spmv_total`、`unaccounted_controller`、
  `kernel_minus_controller`；
- `stage-ms`：`init_zp`、`dot_p_ap`、`update_xr`、`update_z`、`update_p`；
- `[timing-ms] kernel_reported`：host/XRT 看到的完整 kernel 时间。

`1iter kernel` 没有去掉 init；它包含 `init_spmv + init_zp + iter_spmv +
dot/update + 收尾`。要看迭代增量，应使用 `1iter kernel - init-only kernel`，
再结合 `pcg-control-ms` 判断增量落在 SpMV、PCG 非 SpMV 还是 controller 外。

## 当前最大规模观测锚点

下面这组数据来自 2026-05-29 full-PCG demo-only 分层重跑：

```text
logs/pcg_breakdown_20260529_hostsplit/
dataset: thermal2
N = 1,228,045
nnz = 8,580,313
```

这是目前最好观察优化方向的最大数据点。单位均为 ms：

| 指标 | 数值 | 读法 |
| --- | ---: | --- |
| `init-only kernel` | 421.5857 | 只跑初始化路径，包含 `A*x0 + init_zp + 收尾` |
| `1iter kernel` | 1960.0357 | 完整 `init + 1 次 PCG 迭代 + 收尾`，没有去掉 init |
| `kernel delta` | 1538.4499 | `1iter kernel - init-only kernel`，近似第 1 次迭代增量 |
| `controller_total` | 1948.9022 | FPGA 内 controller 计到的主体时间 |
| `controller 外` | 11.1335 | `kernel_reported - controller_total`，主要是边界/drain/同步余量 |
| `SpMV 本身` | 184.6013 | `init_spmv + iter_spmv`，full-PCG 内两次 SpMV 合计 |
| `full-PCG SpMV 单次均值` | 92.3007 | `SpMV 本身 / 2`，仍远慢于 single SpMV demo |
| `single SpMV demo` | 1.781541 | 当前 one-shot `CuperPcgSpmv` 完整 `thermal2` 的 `spmv_avg` |
| `PCG 非 SpMV` | 1735.0604 | `init_zp + dot_p_ap + update_xr + update_z + update_p` |
| `unaccounted ctrl` | 29.2405 | 已命名 stage 之外的 controller 内部余量 |

主要阶段：

| 阶段 | 数值 | 归属 |
| --- | ---: | --- |
| `init_zp` | 230.7390 | init 专用 |
| `dot_p_ap` | 187.2422 | PCG 迭代专用 |
| `update_xr` | 718.4058 | PCG 迭代专用 |
| `update_p` | 598.6733 | PCG 迭代专用 |
| `update_z` | 未单列 | PCG 迭代专用；当前聚合 HTML 没有单独展示，不能按“0 成本”解读 |

这组数据给出的优化方向：

1. `PCG 非 SpMV` 占 `1iter kernel` 约 88.5%，当前最大瓶颈在
   `Pcg_Controller` 内的 FP64 向量更新和归约，不在 controller 外。
2. `kernel delta` 中约 97.8% 来自 PCG 非 SpMV 增量；如果要降低多迭代时间，
   优先处理 `update_xr`、`update_p`、`dot_p_ap`。
3. full-PCG 内 SpMV 单次均值约 `92.30 ms`，而 one-shot single SpMV demo 是
   `1.78 ms`；这说明 service/control/输出消费路径仍然没有把 Cuper SpMV 喂饱，
   但它不是当前 `1iter kernel` 的最大占比项。
4. `update_z` 源码中会读 `R/M_inv`、写 `Z` 并累计 `rz/rr`，完整规模下不应天然为
   0；若原始 `[pcg-control-ms] update_z` 仍接近 0，应优先排查 stage timer 或
   HTML 聚合脚本口径，而不是把它当作优化已经完成。
5. `controller 外` 只有约 `11.13 ms`，大矩阵上不是主瓶颈；优化应先落到
   controller 里的 HBM 读写、FP32/FP64 转换、副本同步和归约结构。

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

- `pcg-spmv-ms spmv_avg` 是否接近 single SpMV 回归基线；
- `controller_total` 是否下降；
- `pcg_non_spmv_total` 是否下降；
- `dot_p_ap`、`update_xr`、`update_p` 是否下降；
- `kernel_reported` 是否下降；
- 完整 `thermal2` 的 `ctrl=0x0` 失败边界是否变化。

如果 `pcg-spmv-ms` 快了但 `kernel_reported` 几乎不变，说明瓶颈已经转移到
PCG 非 SpMV 阶段、task graph 同步、service drain，或当前仍未命名的外围时间。
