# SpMV accumulator 乱序方案 1：Core 输出显式携带 core_id

本文记录一个备用实现方案：让 Core 输出数据包自身携带来源 Core 信息，再交给后级
乱序 Router/Accumulator。当前代码已先实现了方案 2，即 Router 通过输入 stream 下标
识别来源 Core；本文只作为方案 1 的设计留档。

## 目标

目标是把 SpMV 后端 accumulator 的所有权从“来源 Core/HBM channel”改为“最终输出
row/packet 的 owner bank”。这样任意 Core 产生的 row contribution 都可以被路由到
对应 owner accumulator，后端不再要求 Core[i] 固定连接 Accumulator[i]。

旧路径大致是：

```text
Core[i] -> Matrix_Mult_Vector_Stream[i] -> Accumulator[i] -> checker/sort/writer
```

方案 1 的目标路径是：

```text
Core[i]
  -> Matrix_Mult_X_Tagged(core_id=i, row[8], val[8])
  -> RowRouter / RouterTree
  -> OwnerAccumulatorBank[owner(row)]
  -> TaggedScatterWriter
```

## 数据协议

当前 Core 输出结构是：

```cpp
struct Matrix_Mult_X {
    ap_uint<18> row[8];
    float_v8 val;
};
```

方案 1 增加一个显式来源字段：

```cpp
struct Matrix_Mult_X_Tagged {
    ap_uint<5> core_id;
    ap_uint<18> row[8];
    float_v8 val;
};
```

`core_id` 需要覆盖 8/16/24/32 路 Cuper HBM/Core 实验，所以 5 bit 足够。`row[8]` 和
`val[8]` 保持现有含义，不改变 Core 的乘法逻辑：

| 字段 | 含义 |
| --- | --- |
| `core_id` | 当前 beat 来自哪个 Core/HBM channel。 |
| `row[p][17]` | padding 标记，1 表示该 slot 无效。 |
| `row[p][0]` | ping/pong，也就是最终 float_v2 内的低/高 scalar lane。 |
| `row[p](17,1)` | 当前 Cuper row group / 局部累加地址。 |
| `val[p]` | `A[i,j] * x[j]` 的局部乘积。 |

Router 对每个 slot `p` 生成后级 scalar event：

```text
packet_idx
pair_lane
scalar_lane
value
```

其中：

```text
group_size  = HBM_CHANNEL_NUM / 8
acc_offset  = core_id % group_size
pair_lane   = core_id / group_size
packet_idx  = row_group * HBM_CHANNEL_NUM + p * group_size + acc_offset
scalar_lane = row[0]
```

这里的 `p` 是 512-bit matrix beat 内的 slot/lane 下标，范围 0..7。

## 模块连接

建议拆成三层。

### 1. Tagged Core

把 `CuperSpmvOnly_CoreStrip` 或对应 Core 变体的输出 stream 从：

```text
tapa::ostream<Matrix_Mult_X>
```

改为：

```text
tapa::ostream<Matrix_Mult_X_Tagged>
```

Core 内部每次写出 `matmultx` 前补上：

```text
matmultx.core_id = Core_id
```

这一层不改变矩阵读取、X 装载、FLEX_REUSE 或乘法本身。

### 2. RowRouter / RouterTree

Router 读入 `Matrix_Mult_X_Tagged`，不再依赖“这是第几路 stream”。因此后续允许先
merge、多级转发、分组 arbiter，再路由到 owner bank。

第一版可以是中心 Router：

```text
Matrix_Mult_X_Tagged[0..N-1] -> RowRouter -> Owner_Scalar_Stream[0..B-1]
```

性能版应改成多级 RouterTree：

```text
Core[0..N-1]
  -> LocalRouterGroup[0..G-1]
  -> OwnerBankInput[0..B-1]
```

避免所有 Core 输出在一个中心点串行化。

### 3. OwnerAccumulatorBank

Owner bank 按最终输出坐标拥有 partial sum：

```text
owner = packet_idx % BANK_NUM
addr  = packet_idx / BANK_NUM
slot  = pair_lane * 2 + scalar_lane
```

每个 bank 内部存：

```text
partial_sum[16][depth]
```

`16` 对应一个 `float_v16` 输出 packet 内的 16 个 scalar 元素。

## 和方案 2 的区别

| 对比项 | 方案 1：Core 包显式带 `core_id` | 方案 2：Router 用输入下标 |
| --- | --- | --- |
| Core 输出类型 | 需要新增 `Matrix_Mult_X_Tagged` | 保持 `Matrix_Mult_X` 不变 |
| stream 宽度 | 每个 Core 输出 beat 多约 5 bit | 不增加 |
| 接口改动范围 | Core、Router、下游 task 都要换类型 | 主要改 Router 后端 |
| 多级 router | 更方便，来源信息不会丢 | 第一层后必须重新打 tag |
| 错配风险 | 低，来源和数据绑定 | 低，但只在未 merge 前成立 |
| 第一版开发代价 | 中等 | 低 |
| 长期扩展性 | 更好 | 中等 |

方案 2 更适合低侵入验证；方案 1 更适合后续做真正多级乱序网络。

## 性能与资源判断

方案 1 本身不会明显增加乘法路径开销，因为 `core_id` 只是随包携带的 tag。主要代价在：

1. Core 输出 stream 稍微变宽；
2. 所有后级 stream/FIFO 类型变化；
3. RouterTree 需要更多仲裁和 FIFO；
4. Owner accumulator 需要按输出 row 分片的 partial-sum URAM。

`core_id` 的 bit 宽不是主要资源问题。真正要控制的是 RouterTree 的 fan-in/fan-out
和 owner bank 的 URAM 分片方式。不能为每个 bank 复制完整 partial sum；应该让
每个 bank 只拥有自己负责的 packet 分片。

## 正确性风险

1. FP32 加法顺序会改变。若不同 Core 的同一 row contribution 到达 owner bank 的顺序
   与旧路径不同，默认 `1e-4` 精度下可能出现误差放大。必要时需要更宽松阈值、FP64
   partial sum、Kahan，或保留每 row 内的稳定顺序。
2. `FLEX_REUSE` 不应在 Core 前被随意重排。方案 1 的 router 放在 Core 后，Core 已经
   输出 `row,val`，因此不破坏 matrix slot 内的 FLEX_REUSE 语义。
3. `packet_idx/pair_lane/scalar_lane` 的反解必须和 host 的 `CuperHostMapRowToPe`
   以及旧 `CuperSpmvOnly_TaggedPacketIndex` 保持一致。

## 建议落地步骤

1. 新增宏，例如 `JACOBI_SPMV_CORE_TAGGED=1`，不要覆盖当前方案 2。
2. 新增 `Matrix_Mult_X_Tagged`，保留旧 `Matrix_Mult_X`。
3. 只在 SpMV-only `JACOBI_SPMV_LANE_STATIC_REAL` 实验路径中替换 Core 输出类型。
4. 先实现中心 Router，验证 `thermal2_n1024`、`thermal2_n65536` 和完整 `thermal2`
   software/TAPA simulation。
5. 中心 Router 功能通过后，再拆成多级 RouterTree，目标是避免中心串行瓶颈。
6. 观察 HLS 报告中的 Router/OwnerAccumulator II、URAM、FIFO、LUT 和时序。
7. 只有 software/TAPA simulation 全过，且 HLS 资源没有明显失控，再生成硬件 demo。

## 当前状态

截至本文记录时，代码里已实现的是方案 2：

```text
JACOBI_SPMV_LANE_STATIC_REAL=1
JACOBI_SPMV_OOO_ACCUMULATE=1
```

它通过输入 stream 下标识别来源 Core，并已在 software/TAPA simulation 中通过：

```text
thermal2_n1024
thermal2_n65536
thermal2
```

方案 1 尚未实现，本文用于防止后续上下文丢失。
