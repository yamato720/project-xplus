# CuperPcg 内嵌 SpMV 数据流说明

本文按当前 `DLC/Cuper/kernels/detail/` 下的 full `CuperPcg(...)` TAPA 实现说明
PCG 里的 SpMV 部分。这里讨论的是 full-PCG 内部反复调用的常驻 SpMV service，
不是 `Cuper(...)` standalone SpMV，也不是 `CuperPcgSpmv(...)` single-SpMV demo。

## 1. 先分清三条路径

当前代码里容易把三个名字混在一起：

| 路径 | 顶层 | 作用 |
| --- | --- | --- |
| standalone SpMV | `Cuper(...)` | host 或测试程序单独发起一次/多次 Cuper SpMV。 |
| single-SpMV demo | `CuperPcgSpmv(...)` | 保留历史 kernel 名和 demo 入口，但内部已经回到 Cuper one-shot task graph。 |
| full FPGA-PCG | `CuperPcg(...)` | host 只 launch 一次，PCG 初始化和迭代都在 TAPA task graph 内完成。 |

本文只讲第三条：`CuperPcg(...)` 中的内嵌 SpMV。它把 Cuper 的
loader/core/accumulator/checker/sort-tree 改造成常驻 service，由
`Pcg_Controller` 每次发送命令触发一次 `A*x0` 或 `A*p`。

相关源码入口：

| 文件 | 重点 |
| --- | --- |
| `detail/cuper_top_graphs.hpp` | `CuperPcg(...)` 顶层 task graph 和 stream 连线。 |
| `detail/pcg_common.hpp` | `CuperSpmvCommand`、`vector_source`、stage/status 常量。 |
| `detail/pcg_spmv_service.hpp` | full-PCG 内嵌 SpMV 的常驻 service task。 |
| `detail/pcg_controller.hpp` | PCG 主控，负责发 SpMV 命令和消费 SpMV 输出。 |
| `detail/pcg_vector_services.hpp` | `update_p` 同步维护下一轮 SpMV 要读的 `P_spmv`。 |
| `detail/cuper_spmv_tasks.hpp` | standalone Cuper 和 PCG service 共用的底层读包/核心计算 helper。 |

## 2. 数据和精度边界

full `CuperPcg(...)` 同时保留两套向量形态：

| 缓冲 | 类型 | 角色 |
| --- | --- | --- |
| `B/M_inv/X/R/Z/P` | `double_v8` | PCG 的 FP64 主状态，512-bit packed。 |
| `X_spmv` | `float_v16` | host 预打包的初始 `x0`，只在初始化 `A*x0` 时由 SpMV vector loader 读取。 |
| `P_spmv` | `float_v16` | controller/worker 维护的搜索方向 `p` 的 FP32 packed 副本，每轮 `A*p` 时读取。 |
| `AP_spmv` | `float_v16` | 最近一次 `A*p` 的 packed 输出缓存，供 `p^T AP` 和 `update_xr` 使用。 |

这样做的目的不是把 PCG 全部降成 FP32，而是让 SpMV 的输入/输出形态贴近
standalone Cuper：SpMV 热路径吃 `float_v16`，PCG 数值状态仍保留在 `double_v8`
里。需要注意：

- `X_spmv` 不进 `Pcg_Controller`，初始化 SpMV 时由 `Pcg_Vector_Loader` 直接读；
- `P_spmv` 必须在 `init_zp` 和每轮 `update_p` 后同步更新，否则下一轮 `A*p`
  会读到旧搜索方向；
- `AP_spmv` 只缓存迭代里的 `A*p`，初始化的 `A*x0` 不写到这个缓冲。

## 3. 命令协议

SpMV service 通过 `CuperSpmvCommand` 被 controller 触发：

```text
struct CuperSpmvCommand {
  stop;
  vector_source;
}
```

一条非 stop 命令只表示“一次 SpMV”。PCG 多轮迭代不是 service 自己循环，而是
controller 每次需要矩阵向量乘时重新广播命令：

```text
init_spmv: pcg_send_spmv_command(..., kPcgVectorSourceX)
           -> Pcg_Vector_Loader 读 X_spmv

iter_spmv: pcg_send_spmv_command(..., kPcgVectorSourceP)
           -> Pcg_Vector_Loader 读 P_spmv
```

同一条命令会被广播到：

- `Command_Stream[0]`：给 `Pcg_SpElement_list_ptr_Loader`；
- `Command_Stream[1]`：给 `Pcg_Vector_Loader`；
- `Matrix_Command_Stream[0..15]`：给 16 个 `Pcg_Matrix_Loader`。

controller 结束时广播 stop。ptr/core/accumulator/checker/sort/vector-destroy
这些常驻 task 必须有限退出，否则 host 等不到 kernel `done`。

## 4. SpMV service 数据流

`CuperPcg(...)` 里的 SpMV task graph 可以按下面的硬件连线理解：

```text
Pcg_Controller
  |
  | CuperSpmvCommand
  v
+---------------------------+
| ptr/vector/matrix loaders |
+---------------------------+
  |              |                         |
  | PE_Param     | Vector_X_Stream         | Matrix_A_Stream[0..15]
  v              v                         v
Core0 -> Core1 -> ... -> Core15       16 路矩阵 HBM 并行读取
  |        |              |
  +--------+--------------+
           |
           v
Pcg_Accumulator[0..15]
  |
  v
Pcg_Vector_Checker[0..7]
  |
  v
Pcg_Mult_Sort_Tree
  |
  | float_v16
  v
Pcg_Spmv_Stream -> Pcg_Controller
```

更具体地说：

1. `Pcg_SpElement_list_ptr_Loader`
   收到一次命令后，重新向 `PE_Param[0]` 写入 `Batch_num`、`Row_num`、
   `Column_num` 以及每个 batch 的 SpElement 边界。

2. `Pcg_Vector_Loader`
   按 `Column_num` 计算需要读取的 `float_v16` 包数。初始化读 `X_spmv`，
   迭代读 `P_spmv`，输出到 `Vector_X_Stream[0]`。

3. `Pcg_Matrix_Loader[0..15]`
   每个实例只读自己的 `Matrix_data[channel]`。`HBM_CHANNEL_NUM` 当前是 16，
   因此这里是真正的 16 路 HBM 矩阵输入并行度。

4. `Pcg_Core[0..15]`
   16 个 core 串接转发 `PE_Param` 和 `Vector_X_Stream`。每个 core 一边把向量包
   传给下一级，一边把当前 slice 的 x 缓存在本地 BRAM，解码本通道的 512-bit
   SpElement 包，并输出局部 `val * x[col]`。

5. `Pcg_Accumulator[0..15]`
   对每个 HBM channel 的局部乘积做行累加，输出 `float_v2`。这里使用的是 Cuper
   重排后的 18-bit row 编码，不是原始全局行号。

6. `Pcg_Vector_Checker[0..7]`
   过滤 accumulator 为对齐和 padding 产生的无效输出，只保留 `Row_num` 内的值。

7. `Pcg_Mult_Sort_Tree`
   把 8 路 `float_v2` 合并成一包 `float_v16`，写入 `Pcg_Spmv_Stream`。
   full-PCG 里这条流直接回 controller，不再像 standalone SpMV 那样写 `Y_out` HBM。

`PE_Param[16]` 和 `Vector_X_Stream[16]` 只是 16 级串接链的尾端，不代表第 17 个
矩阵通道。链尾由 destroy task 持续消费，避免最后一级 core 写满 FIFO 后反压整条链。

## 5. Controller 怎么消费 SpMV

`Pcg_Controller` 把同一套 SpMV service 用在两个阶段。

### 5.1 初始化：`A*x0`

初始化阶段先发 `pcg_send_spmv_command(..., kPcgVectorSourceX)`。

随后在 `init_spmv_stream` 中消费 `packet_count = ceil(Row_num / 16)` 包
`float_v16` 输出。每包是 `A*x0` 的 16 个 FP32 lane。controller 将其转成
FP64 后写初始残差：

$$
R = B - A x_0
$$

这个阶段只负责接收 SpMV 输出并生成 `R`。之后 `init_zp` 再读 `R/M_inv`，
计算：

$$
\begin{aligned}
Z &= M_{\mathrm{inv}} \cdot R \\
P &= Z \\
P_{\mathrm{spmv}} &= \mathrm{float}(P) \\
rz &= R^T Z \\
rr &= R^T R
\end{aligned}
$$

因此第一轮 `A*p` 的输入 `P_spmv` 是在 `init_zp` 里准备好的。

### 5.2 迭代：`A*p`

每轮 PCG 迭代先发 `pcg_send_spmv_command(..., kPcgVectorSourceP)`。

`Pcg_Vector_Loader` 这次从 `P_spmv` 读当前搜索方向。controller 在
`iter_spmv_stream` 中接收 `A*p`，并做三件事：

- `AP_spmv[packet]` 接收 $A p$ 的 `float_v16` 输出；
- 累加 $p\_ap \mathrel{+}= P^T AP$；
- `received_packets` 加 1。

当前实现把 `p^T AP` 融进 `iter_spmv_stream`，所以没有单独的 `dot_p_ap`
stage。算出：

$$
\alpha = \frac{rz}{p\_ap}
$$

后，PCG 继续执行：

$$
\begin{aligned}
X &= X + \alpha P \\
R &= R - \alpha AP \\
Z &= M_{\mathrm{inv}} \cdot R \\
\beta &= \frac{rz_{\mathrm{new}}}{rz} \\
P &= Z + \beta P \\
P_{\mathrm{spmv}} &= \mathrm{float}(P)
\end{aligned}
$$

其中 `update_xr` 使用 `AP_spmv`，`update_z` 和 `update_p` 已拆到 vector service
worker。`P_spmv` 在 `Pcg_UpdateP_Write_Service` 里和 FP64 `P` 一起写回，保证下一轮
SpMV 读到最新搜索方向。

## 6. Metrics 和性能读法

`Metrics` 里有两类口径，不要混用：

| Metrics | 含义 |
| --- | --- |
| `[5..15]` | packed memory packet work，属于估算/计数，不是实测 cycle。 |
| `[16..24]` | `Pcg_Stage_Timer` 统计的真实 stage cycle。 |

与 SpMV 相关的 stage：

| Stage | 当前口径 |
| --- | --- |
| `init_spmv` | 共用 SpMV service 的初始化调用，加上 controller 接收 `A*x0` 并写 `R`。 |
| `iter_spmv` / `iter_spmv_recv_dot` | 共用 SpMV service 的迭代调用，加上接收 `A*p`、写 `AP_spmv`、融合计算 `p^T AP`。 |
| `dot_p_ap` | 当前只是兼容占位，不再单独发 begin/end event。 |

所以 `iter_spmv` 不能直接当作“纯 SpMV 本体耗时”。它已经包含 AP 接收路径和
FP64 `p^T AP` recurrence。比较历史报告时应标成 `iter recv + dot`，否则会把
SpMV service、controller 接收、AP 缓存写入和 dot 的成本混在一起。

判断 full-PCG 性能时也不要只看 single SpMV demo。`CuperPcgSpmv(...)` 能说明
Cuper one-shot SpMV 的边界和回归情况，但不能证明 full `CuperPcg(...)` 变快。
full-PCG 的总时间还会被 controller 向量更新、FP64 reduction、HBM 往返、service
drain 和 task 同步影响。

## 7. 常见误读

- `Pcg_*` service 不是重新实现一套数学上不同的 SpMV，而是把 Cuper 的一次性
  task 改成可由 controller 多次触发的常驻 task。
- `vector_source` 只决定向量输入来源，矩阵输入和 core/accumulator/checker/sort
  数据通路在 init 和 iter 中是共用的。
- `P_spmv` 是 FP32 packed 副本，不是 PCG 的权威状态。权威搜索方向仍是 FP64
  `P`。
- `AP_spmv` 缓存的是最近一次 `A*p`，不是初始化的 `A*x0`。
- Cuper 内部 row 是重排编码，不要按原始 CSR 行号直接解释。
- `[0..15]` 是 16 个 HBM matrix channel；`[16]` 只是广播链尾。
- stop 命令是 service 退出协议的一部分，不能省略。

## 8. 阅读顺序建议

如果要继续改这块代码，建议按这个顺序读：

1. `detail/cuper_top_graphs.hpp`：先看 `CuperPcg(...)` 的 stream 数组和 `.invoke(...)`
   连线。
2. `detail/pcg_common.hpp`：确认 `CuperSpmvCommand`、`kPcgVectorSourceX/P`、
   stage 编号。
3. `detail/pcg_spmv_service.hpp`：看每个 `Pcg_*` task 如何把一次命令转成一次
   Cuper SpMV。
4. `detail/pcg_controller.hpp`：跟 `init_spmv_stream`、`init_zp`、
   `iter_spmv_stream`、`update_xr` 的数据依赖。
5. `detail/pcg_vector_services.hpp`：确认 `update_z/update_p` worker 与
   `P_spmv` 同步点。

改动后优先验证这些现象：

- `init_spmv` 和 `iter_spmv_recv_dot` 的 stage 口径是否仍清楚；
- `P_spmv` 是否在每轮 `update_p` 后同步更新；
- stop/drain 是否仍能让 kernel 有限返回；
- `pcg-spmv-ms`、`pcg-control-ms`、`pcg_vector_total` 和 `kernel_reported`
  是否同步解释得通。
