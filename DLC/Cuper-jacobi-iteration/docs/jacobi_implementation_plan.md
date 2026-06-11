# Jacobi Kernel 实现方案

本文说明当前 demo 怎样在 `CuperJacobiIteration(...)` 中实现 Jacobi iteration
kernel。核心选择是：**不改 Cuper core 里的矩阵解码逻辑，
在 host 侧先把 CSR 拆成 `A = D + R`，再让 vector loader 读 `X0/X1` 时取负，
让 Cuper service 直接计算 `-R*x`，然后在后级 vector update 中计算
`(b + (-R*x)) * D^{-1}`。**

当前代码已经实现第一版 demo：

- `Jacobi_Controller`：多轮发起 SpMV、发送 update frame、收敛判断、写回状态。
- `Jacobi_Vector_Loader`：按 `X0/X1` 双缓冲选择 SpMV 输入向量，并把输入取负。
- `Jacobi_Update_Service`：消费 `-R*x_old`，读取 `B/Diag_inv/x_old`，写出 `x_next`。
- host：支持 Matrix Market `.mtx` 和 Project-XPlus CSR 目录输入。

当前 demo 用 `packet_count` 表达一轮 `-Rx` 的结束，没有额外的
`RxPacket.last` wrapper；controller 也直接分别发送 SpMV command 和 update frame，
没有拆出独立 `Jacobi_Frame_Broadcaster`。这两项保留为后续增强。

## 1. 总体策略

Jacobi iteration 的数学式是：

$$
x^{(k+1)} = D^{-1}(b - R x^{(k)})
$$

host 侧先做：

$$
A = D + R
$$

当前 Cuper service 的矩阵仍然是 `R`，但输入向量在 `Jacobi_Vector_Loader` 中先取负，
因此直接给出：

$$
\mathrm{neg\_rx}^{(k)} = R(-x^{(k)}) = -R x^{(k)}
$$

因此 update stage 可以用下面这个式子完成一轮 Jacobi：

$$
x_i^{(k+1)}
= \left(b_i + \mathrm{neg\_rx}_i^{(k)}\right)\mathrm{diag\_inv}_i
$$

这个做法的好处是不用在 Cuper 的 packed `SpElement` 里恢复原始全局行号，也不用让
core 判断 `i == j`。Cuper 当前的 `rowIdx` 已经被 host reordering 改成内部
accumulator 地址，在 core 内直接识别对角项不稳。

## 2. 顶层 ABI

当前顶层已经从 single SpMV ABI 扩成 Jacobi ABI：

```cpp
void CuperJacobiIteration(
    tapa::mmap<INDEX_TYPE> SpElement_list_ptr,
    tapa::mmaps<ap_uint<512>, HBM_CHANNEL_NUM> Matrix_data,
    tapa::mmap<float_v16> B,
    tapa::mmap<float_v16> Diag_inv,
    tapa::mmap<float_v16> X0,
    tapa::mmap<float_v16> X1,
    tapa::mmap<INDEX_TYPE> Status,
    tapa::mmap<double> Metrics,
    INDEX_TYPE Batch_num,
    INDEX_TYPE Matrix_len,
    INDEX_TYPE Row_num,
    INDEX_TYPE Column_num,
    INDEX_TYPE Max_iters,
    float Tau);
```

其中：

- `B`：右端项 `b`，按 `float_v16` 打包。
- `Diag_inv`：对角线逆，按 `float_v16` 打包。
- `X0` / `X1`：解向量双缓冲。
- `Status[0]`：退出状态，例如 converged、max-iter、breakdown。
- `Status[1]`：最终结果在哪个 buffer，`0` 表示 `X0`，`1` 表示 `X1`。
- `Metrics`：调试统计数组，当前用 `double` 承载 diff、包数和 cycle 计数。

第一版用 `float_v16` 是为了贴合 Cuper 当前 FP32 SpMV 数据通路。后续如果需要更稳的
数值，可以再讨论把 `B/Diag_inv/X` 换成 `double_v8`，但那会增加打包、带宽和
转换成本。

当前 `Metrics` 布局：

| Metrics | 含义 |
| --- | --- |
| `[0]` | 最后一轮 `diff_max` |
| `[1]` | 已完成 Jacobi 迭代轮数 |
| `[2]` | 每轮 `float_v16` 包数 |
| `[3]` | 已处理的 SpMV/update 包数累计 |
| `[4]` | SpMV+update 累计 cycle |
| `[5]` | controller 主体累计 cycle |
| `[6]` | timer 存活总 cycle |
| `[7]` | 平均每轮 SpMV+update cycle |

## 3. 数据流

每一轮 Jacobi 迭代的数据流：

```text
controller
  -> 发送本轮 FrameHeader，指定本轮读取 X0 或 X1
  -> Jacobi vector loader 读取旧解并取负
  -> Cuper service 计算 -Rx
  -> Jacobi update service 消费 -Rx，并从同一个旧解 buffer 读取 x_old
  -> 写入另一个 buffer 作为 x_next
  -> 返回 diff_max 和 breakdown
  -> controller 判断继续迭代还是 stop
```

注意：当前 Cuper 的最终 `y` 不是每个矩阵块算完就立刻输出，而是 accumulator 完成本轮
SpMV 累加后再按 `float_v16` 连续吐出。因此 update stage 设计成“消费 SpMV 输出包”，
而不是“跟随矩阵块同步更新”。

控制方式采用“每轮一个 header、固定 `packet_count` 个数据包”的数据驱动结构。
controller 不参与每个 `-Rx` 数据包，只在一轮开始时发 header，在一轮结束时读
update result。后续可以在数据包上再补 `last` 标识，用作边界一致性检查。

## 4. 模块划分

当前代码拆成下面这些模块。这里的“模块”基本对应一个 TAPA task 或一组已有
TAPA task，比实际代码略抽象一层。

```text
CuperJacobiIteration top
  |
  |-- Jacobi_Controller
  |     |-- 发 FrameHeader
  |     |-- 收 Update result
  |     |-- 写 Status/Metrics
  |     `-- 广播 stop
  |
  |-- Cuper SpMV service
  |     |-- SpmvService_SpElementPtrLoader
  |     |-- Jacobi_Vector_Loader
  |     |-- SpmvService_MatrixLoader[16]
  |     |-- SpmvService_Core[16]
  |     |-- SpmvService_Accumulator[16]
  |     |-- SpmvService_VectorChecker[8]
  |     `-- SpmvService_MultSortTree
  |
  |-- Jacobi_Update_Service
  |     |-- 读 B/Diag_inv/x_old
  |     |-- 消费 SpMV 输出 -Rx
  |     |-- 写 x_next
  |     `-- 归约 diff_max/breakdown
  |
  `-- Drain/Stop tasks
        |-- SpmvService_DestroyInt
        `-- SpmvService_DestroyFloatV16
```

各模块职责：

| 模块 | 职责 | 是否新增 |
| --- | --- | --- |
| `CuperJacobiIteration` | 顶层端口、stream 声明、task graph 连接 | 改造 |
| `Jacobi_Controller` | 迭代级控制、发 frame header、收敛判断、stop 广播 | 新增 |
| `Jacobi_Frame_Broadcaster` | 把一份 frame header 分发给 SpMV 和 update 两侧 | 后续增强 |
| `Jacobi_Vector_Loader` | 根据 frame header 选择从 `X0` 或 `X1` 读取 SpMV 输入向量，并在输出到 Cuper 前取负 | 改造 |
| `SpmvService_SpElementPtrLoader` | 读取 Cuper batch 边界表，喂给 core 参数链 | 复用 |
| `SpmvService_MatrixLoader[16]` | 读取 16 路 packed 矩阵 HBM | 复用 |
| `SpmvService_Core[16]` | 解码 `SpElement` 并产生局部乘积 | 复用 |
| `SpmvService_Accumulator[16]` | 累加局部乘积，形成每行 SpMV 结果 | 复用 |
| `SpmvService_VectorChecker[8]` | 过滤 Cuper padding 输出 | 复用 |
| `SpmvService_MultSortTree` | 把 8 路 `float_v2` 拼成 `float_v16` 的 `-Rx` 包 | 复用 |
| `Jacobi_Update_Service` | 用 `-Rx/B/Diag_inv/x_old` 算 `x_next` 和 `diff_max` | 新增 |
| `SpmvService_Destroy*` | 消费链尾并响应 stop | 复用 |

## 5. Stream 和端口连接

顶层保留 Cuper service 原有 stream，并新增 controller/update 之间的控制和结果
stream。当前 demo 的核心连接如下：

```text
Jacobi_Controller
  -> Command_Stream[0]
  -> Command_Stream[1]
  -> Matrix_Command_Stream[0..15]
  -> Update_Frame_Stream

Command_Stream[0] / frame metadata
  -> SpmvService_SpElementPtrLoader
  -> PE_Param[0]
  -> SpmvService_Core[0..15]
  -> PE_Param[16]
  -> SpmvService_DestroyInt

Command_Stream[1] / frame metadata
  -> Jacobi_Vector_Loader
  -> 读 X0/X1 并输出 -x_old
  -> Vector_X_Stream[0]
  -> SpmvService_Core[0..15]
  -> Vector_X_Stream[16]
  -> SpmvService_DestroyFloatV16

Matrix_Command_Stream[0..15]
  -> SpmvService_MatrixLoader[0..15]
  -> Matrix_A_Stream[0..15]
  -> SpmvService_Core[0..15]

SpmvService_Core[0..15]
  -> Vector_Y_Param[0..15]
  -> Matrix_Mult_Vector_Stream[0..15]
  -> SpmvService_Accumulator[0..15]
  -> Vector_Y_Stream[0..15]
  -> SpmvService_VectorChecker[0..7]
  -> Vector_Y_Stream_Aftck[0..7]
  -> SpmvService_MultSortTree
  -> Neg_Rx_Stream(float_v16)

Update_Frame_Stream
  -> Jacobi_Update_Service

Neg_Rx_Stream(float_v16)
  -> Jacobi_Update_Service
  -> Update_Result_Stream
  -> Jacobi_Controller
```

新增 stream 建议：

| Stream | 方向 | 内容 |
| --- | --- | --- |
| `Update_Frame_Stream` | controller -> update | update 侧需要的 frame header |
| `Update_Result_Stream` | update -> controller | `diff_max`、breakdown、已写 buffer |
| `Neg_Rx_Stream` | SpMV sort tree -> update | `float_v16` packed 的 `-R*x_old` |

`Neg_Rx_Stream` 取代 smoke 版的 `Spmv_Stream -> Y_out` 写回路径。当前 Jacobi kernel
不把每轮 `-Rx` 写回 HBM，除非以后为了调试额外保留 optional debug port。

后续如果要进一步降低 controller 的分发职责，可以把 `Frame_Stream` 和
`Jacobi_Frame_Broadcaster` 加回来：controller 只发一份 frame，由 broadcaster 分发给
SpMV command shell 和 update service。

## 6. Frame 和 Packet 结构

为了减少中心 controller 对数据流的介入，每轮迭代只发一个 frame header，后续模块按
header 自动处理固定数量的数据包。当前 demo 用 `packet_count` 判断一轮结束。
后续可以给数据包末尾加 `last` 标识，用作一致性检查。

建议定义：

```cpp
struct JacobiFrame {
    INDEX_TYPE stop;
    INDEX_TYPE read_from_x1;
    INDEX_TYPE write_to_x1;
    INDEX_TYPE row_num;
    INDEX_TYPE packet_count;
    INDEX_TYPE iter;
};

struct JacobiUpdateResult {
    float diff_max;
    INDEX_TYPE breakdown;
    INDEX_TYPE wrote_x1;
    INDEX_TYPE iter;
};
```

`JacobiFrame` 的语义：

- `stop != 0`：所有常驻 task 退出。
- `read_from_x1`：本轮 SpMV 和 update 读取哪个旧解 buffer。
- `write_to_x1`：本轮 update 写哪个新解 buffer。
- `row_num`：真实向量长度，update 用它屏蔽尾包 padding lane。
- `packet_count`：`Cuper_NumFloatV16Packets(row_num)`，各模块按这个数量自动读写。
- `iter`：调试和 metrics 用，不参与数据计算。

后续可选增强：

```cpp
struct JacobiRxPacket {
    float_v16 value;
    INDEX_TYPE last;
};
```

`JacobiRxPacket::last` 可以由 SpMV 输出端打在最后一个 `-Rx` 包上。update stage 主要
依赖 `packet_count` 控制循环，`last` 用作一致性检查和调试；如果 `last` 提前或缺失，
update 返回 breakdown。

这种设计保留必要的迭代级控制，但取消逐包控制：controller 不再看每个 `-Rx` 包，也不
参与 update 的每个 lane。

## 7. 模块接口草案

### 7.1 Controller

`Jacobi_Controller` 不直接访问矩阵 HBM，只负责控制和状态：

```text
inputs:
  Update_Result_Stream
  Max_iters, Row_num, Tau

outputs:
  Command_Stream[0..1]
  Matrix_Command_Stream[0..15]
  Update_Frame_Stream
  Checker_Stop_Stream[8]
  Sort_Stop_Stream
  Vector_Destroy_Stop_Stream
  Status
  Metrics
```

它每轮发一组 SpMV command 和一个 `JacobiFrame`，然后等待
`JacobiUpdateResult`。这样 update task 可以在 `Neg_Rx_Stream` 上阻塞等待 SpMV 输出，
不需要 controller 参与每个数据包。

### 7.2 Frame broadcaster

当前 demo 没有单独的 `Jacobi_Frame_Broadcaster`。controller 直接向 SpMV service
广播 `CuperSpmvServiceCommand`，同时向 update service 发送 `JacobiFrame`。

后续如果要让 controller 只发一份 frame，可以加一个很薄的控制分发模块：

```text
inputs:
  Frame_Stream

outputs:
  Spmv_Frame_Stream
  Update_Frame_Stream
```

它把同一轮 frame 复制给 SpMV 侧和 update 侧。这样 controller 只维护一个 frame
出口，避免 controller 同时管理多条 command stream。

SpMV 侧收到 `Spmv_Frame_Stream` 后，再由一个轻量 shell 生成现有
`CuperSpmvServiceCommand`，广播给 ptr/vector/matrix loader。这样可以继续复用
`spmv_service_tasks.hpp` 的 stop 和 command 机制。

### 7.3 Vector loader

`Jacobi_Vector_Loader` 是双缓冲版本的向量 loader：

```text
inputs:
  X0, X1
  Command_Stream[1]
  Column_num

outputs:
  Vector_X_Stream[0]
```

它只根据 command 里的 `vector_source` 选择 `X0` 或 `X1`，并把读出的 `x_old`
逐 lane 取负后送进 `Vector_X_Stream[0]`，不做任何 Jacobi update。

### 7.4 SpMV service

SpMV service 保持现有结构，模块间连接不变。它的输入是：

```text
SpElement_list_ptr
Matrix_data[0..15]
Vector_X_Stream[0]
Command streams
```

输出是：

```text
Neg_Rx_Stream(float_v16)
```

这里的 `Neg_Rx_Stream` 是 `SpmvService_MultSortTree` 产出的连续 `float_v16` 包。
每个包最多对应 16 个行结果，数值含义是 `-R*x_old`；最后一个包需要 update stage
按 `Row_num` 屏蔽 padding lane。
当前 demo 直接用 frame 里的 `packet_count` 控制消费数量。后续如果要带 `last`，
可以在 sort tree 后面加一个很薄的 packet wrapper，按 frame 的 `packet_count`
给最后一包打 `last=1`。

### 7.5 Update service

`Jacobi_Update_Service` 是真正新增的算法模块：

```text
inputs:
  B, Diag_inv
  X0, X1
  Neg_Rx_Stream(float_v16)
  Update_Frame_Stream

outputs:
  X0 or X1
  Update_Result_Stream
```

它每轮顺序消费 `Cuper_NumFloatV16Packets(Row_num)` 个 `-Rx` 包。对于每个 lane：

$$
x_i^{(k+1)}
= \left(b_i + \mathrm{neg\_rx}_i^{(k)}\right)\mathrm{diag\_inv}_i
$$

同时更新本轮局部最大差值：

$$
\mathrm{diff\_max} = \max_i |x_i^{(k+1)} - x_i^{(k)}|
$$

最后把 `diff_max`、breakdown 标志和写入 buffer 编号写回 controller。

### 7.6 Stop/drain

controller 决定退出后发：

```text
Frame stop
checker/sort/vector-destroy stop token
```

SpMV service 的 stop 路径沿用当前 `spmv_service_tasks.hpp` 和 `spmv_service_drains.hpp`。Update
service 收到 stop frame 后直接返回，不再等待 `Neg_Rx_Stream`。

## 8. 双缓冲

Jacobi 必须避免边读 `x_old` 边覆盖同一份数据，否则下一行/下一包可能读到新旧混合值。
所以当前 demo 用两个向量 buffer：

```text
iter 0: read X0, write X1
iter 1: read X1, write X0
iter 2: read X0, write X1
...
```

controller 每轮维护一个 `read_from_x1` 标志：

- `read_from_x1 == 0`：vector loader 从 `X0` 读，update 写 `X1`。
- `read_from_x1 == 1`：vector loader 从 `X1` 读，update 写 `X0`。

收敛后不强制 copy 到固定输出 buffer，而是写 `Status[1]` 告诉 host 最终解在哪个
buffer。这样少一次全向量 copy。若后续 host 侧希望总是从固定 `X_out` 读，也可以
再加一个 final copy stage。

## 9. SpMV service 改动

当前 `Jacobi_Vector_Loader` 已经改成双输入：

```text
Jacobi_Vector_Loader(Column_num, X0, X1, Frame_or_Command_in, Vector_X_Stream)
```

当前内部复用 `CuperSpmvServiceCommand::vector_source`：

- `0`：读 `X0`
- `1`：读 `X1`

其它 Cuper service task 尽量不动：

- `SpmvService_SpElementPtrLoader`
- `SpmvService_MatrixLoader`
- `SpmvService_Core`
- `SpmvService_Accumulator`
- `SpmvService_VectorChecker`
- `SpmvService_MultSortTree`
- drain/stop task

这能保持当前已经 smoke 通过的 SpMV 生命周期。

## 10. Jacobi update service

新增的常驻 update task 是：

```text
Jacobi_Update_Service(Row_num, B, Diag_inv, X0, X1, Update_Frame_in, Neg_Rx_Stream_in, Update_Result_out)
```

每收到一条 update command，它做一轮 packed 向量更新：

$$
x_i^{(k+1)}
= \left(b_i + \mathrm{neg\_rx}_i^{(k)}\right)\mathrm{diag\_inv}_i
$$

同时计算：

$$
\mathrm{diff\_max} = \max_i |x_i^{(k+1)} - x_i^{(k)}|
$$

每个 `float_v16` 包内展开 16 个 lane：

```text
for each packet:
  neg_rx = Neg_Rx_Stream_in.read()
  b = B[p]
  diag_inv = Diag_inv[p]
  x_old = X0[p] or X1[p]
  compute x_next by the Jacobi update formula above
  write x_next to the other buffer
  update local diff_max
```

尾包超过 `Row_num` 的 lane 要屏蔽，不参与 diff，也不要用 padding 影响 NaN/breakdown。
后续如果加 `last`，可以在 `last` 和 `packet_count` 不一致时返回 breakdown，方便发现
stream 边界错误。

## 11. Controller

controller 的主循环是：

```text
read_from_x1 = 0
for iter in 0..Max_iters-1:
  send frame(read_from_x1, write_to_x1, row_num, packet_count, iter)
  wait update result
  if breakdown:
    status = breakdown
    break
  if diff_max <= Tau:
    status = converged
    final_buffer = 1 - read_from_x1
    break
  read_from_x1 = 1 - read_from_x1
if not converged:
  status = max_iter
  final_buffer = read_from_x1
send stop frame
write Status/Metrics
```

这里 `final_buffer` 的含义要小心：

- 每轮读 `read_from_x1`，写 `1 - read_from_x1`。
- 如果本轮收敛，最终解在刚写完的 `1 - read_from_x1`。
- 如果跑满 `Max_iters`，最终解也应该是最后一轮写出的 buffer。

实现时建议用一个 `last_written_x1` 显式变量，避免奇偶边界写错。

## 12. Breakdown 和状态

当前定义：

```text
Status[0] = 0: converged
Status[0] = 1: max_iter
Status[0] = 2: breakdown
Status[1] = final_buffer
Status[2] = iterations_done
```

breakdown 条件：

- `diag_inv` 是 NaN/Inf。
- update 结果是 NaN/Inf。
- `Tau < 0`。
- `Max_iters == 0` 且不允许零迭代。

当前代码已经检查 NaN；`Tau < 0` 和 Inf/last 一致性检查还可以作为后续补强。

`A[i, i] == 0` 最好在 host 预处理时就拦住，不要等 kernel 里除法。kernel 只消费
`Diag_inv`，如果 `Diag_inv` 已经异常，就按 breakdown 返回。

## 13. Host 侧准备

host 当前从 `.mtx` 或 CSR 目录里生成：

- `B`：CSR 目录存在 `b.txt` 时读取数据集 RHS；`.mtx` 路径默认构造 `A * ones`。
- `R`：扫描 CSR，去掉所有 `col == row` 的对角项后重新生成 CSR/COO，再走 Cuper 打包。
- `Diag_inv`：扫描 CSR 的对角项并计算 `1 / A[i, i]`。
- `X0`：初始解，例如全 0 或用户给定。
- `X1`：全 0 初始化，作为双缓冲写入目标。

如果某行找不到对角元，或者对角元为 0，host 应直接报错。

## 14. 验证顺序

当前验证按风险从低到高推进：

1. `cant.mtx` 跑 `MAX_ITERS=1`，和 CPU Jacobi 一轮对比。
2. `cant.mtx` 跑 `MAX_ITERS=2`，检查 `final_buffer` 奇偶是否正确。
3. `thermal2_n65536` CSR 目录跑 `MAX_ITERS=1`，使用数据集 `b.txt`。
4. `thermal2_n262144` CSR 目录跑 `MAX_ITERS=1`，使用数据集 `b.txt`。
5. 后续可补缺对角或零对角输入，确认 host 或 kernel breakdown。
6. 后续可补 XO compile / hardware smoke。

当前版本已经采用 host 预处理 `R = A - D` 的路径；验证重点是确认 kernel 的
`-R*x` 中间流和 CPU reference 的 `b - R*x` 使用同一数学口径。
