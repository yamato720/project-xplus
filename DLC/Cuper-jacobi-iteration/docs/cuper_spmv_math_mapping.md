# Cuper SpMV 数学到算法映射

本文原本说明 standalone `Cuper(...)` 的 SpMV 数学原理如何映射到 Cuper 算法。
在 `Cuper-jacobi-iteration` 中，`CuperJacobiIteration(...)` 复用同一套矩阵编码
和 SpMV 底层数据通路，但通过 service command/stop 协议触发。
这里不讨论完整 PCG controller、`r/z/p` 更新、收敛判断或性能计时。

当前 Jacobi demo 的 host 侧传入矩阵不是完整 `A`，而是 `R=A-D`。kernel 的
`Jacobi_Vector_Loader` 会把 `x_old` 取负后送入 Cuper Core，所以 service 输出的中间
向量是 `-R*x_old`，再由 update stage 加上 `b` 并乘 `Diag_inv`。

## 1. 数学目标

standalone Cuper SpMV 只计算：

$$
y = A x
$$

对每一行：

$$
y_i = \sum_j A_{ij} x_j
$$

其中：

- `A` 是稀疏矩阵；
- `x` 是输入向量；
- `y` 是输出向量；
- 非零项 $(i, j, A_{ij})$ 才需要参与乘加。

`Cuper(...)` 的顶层接口对应关系是：

| 数学对象 | Cuper 端口 / 参数 |
| --- | --- |
| 矩阵结构和值 `A` | `SpElement_list_ptr` + `Matrix_data[0..15]` |
| 输入向量 `x` | `X` |
| 输出向量 `y` | `Y_out` |
| 行数 | `Row_num` |
| 列数 | `Column_num` |
| 重复执行次数 | `Iteration_num` |

`Cuper(...)` 不保存 PCG 的 `B/M_inv/R/Z/P/AP`，也不计算 $\alpha/\beta$。

## 2. 数据结构一一对应

这一节把数学对象、host 预处理结构、kernel 端口和 kernel 内部中间包对应起来。

### 2.1 Cuper 接收数据的树状结构

`Cuper(...)` kernel 最终只接收四类东西：矩阵、输入向量、输出向量和尺寸参数。
`SparseSlice`、`SpElement_list_pes` 等都是 host 侧预处理的中间结构，不是 kernel
顶层端口。

```text
Cuper(...)
├── 矩阵 A 的 Cuper 格式
│   ├── SpElement_list_ptr
│   │   ├── 类型: INDEX_TYPE[]
│   │   ├── 作用: batch 边界表
│   │   ├── 全局共享索引，不属于任何单个 Matrix_data channel
│   │   ├── SpElement_list_ptr[b]     = 第 b 个 batch 起点
│   │   └── SpElement_list_ptr[b + 1] = 第 b 个 batch 终点
│   │
│   └── Matrix_data[16]
│       ├── 类型: 16 路 HBM，每路 ap_uint<512>[]
│       ├── 每路有效读取 Matrix_len 个 512-bit beat
│       ├── Matrix_data[channel]
│       │   └── 512-bit beat[i]
│       │       ├── 64-bit slot[0] -> PE = channel * 8 + 0
│       │       ├── 64-bit slot[1] -> PE = channel * 8 + 1
│       │       ├── ...
│       │       └── 64-bit slot[7] -> PE = channel * 8 + 7
│       │
│       └── 每个 64-bit SpElement slot
│           ├── bits [63:50] colIdx  局部列号
│           ├── bits [49:32] rowIdx  Cuper 内部 row 编码
│           ├── bits [31:0]  value   FP32 非零值
│           └── rowIdx[17] = 1 表示 padding，不是有效非零元
│
├── 输入向量 x
│   └── X
│       ├── 类型: float_v16[]
│       ├── X[packet][0]  = x[16 * packet + 0]
│       ├── ...
│       └── X[packet][15] = x[16 * packet + 15]
│
├── 输出向量 y
│   └── Y_out
│       ├── 类型: float_v16[]
│       ├── Y_out[packet][0]  = y[16 * packet + 0]
│       ├── ...
│       └── Y_out[packet][15] = y[16 * packet + 15]
│
└── 标量参数
    ├── Batch_num
    ├── Matrix_len
    ├── Row_num
    ├── Column_num
    └── Iteration_num
```

host 侧从 CSR 到 kernel 输入的生成树是：

```text
CSR
├── row_ptr
├── col_idx
└── values
    ↓ CSR_2_COO
COO
├── RowIdx
├── ColIdx
└── Val
    ↓ Create_SparseSlice
SparseSlice
├── sliceColPtr
├── sliceRowIdx
└── sliceVal[]
    └── Matrix_COO per non-empty slice
        ├── RowIdx
        ├── ColIdx
        └── Val
    ↓ Create_SpElement_list_for_all_PEs
SpElement_list_pes[128]
└── SpElement
    ├── colIdx
    ├── rowIdx
    └── val
    ↓ Create_SpElement_list_for_all_channels
Matrix_data[16]
└── ap_uint<512> beat
    └── 8 个 64-bit SpElement slot
```

上面两棵树的关键区别是：第一棵树是 `Cuper(...)` 真正的 ABI；第二棵树是 host 为了
构造这个 ABI 做的预处理过程。

当前 standalone Cuper 的 `DLC/Cuper/cfg/connectivity.cfg` 里，HBM 绑定是：

| 顶层端口 | HBM |
| --- | --- |
| `SpElement_list_ptr` | `HBM[0]` |
| `Matrix_data_0` | `HBM[0]` |
| `Matrix_data_1..15` | `HBM[1]..HBM[15]` |
| `X` | `HBM[0]` |
| `Y_out` | `HBM[1]` |

所以 `SpElement_list_ptr` 会占用 `HBM[0]`，并和 `Matrix_data_0`、`X` 共用这个 bank。
不过它读的是 batch 边界表，每次 SpMV 只读 `Batch_num + 1` 个 `INDEX_TYPE`，数据量
通常远小于矩阵流；真正重的矩阵读取仍在 `Matrix_data[0..15]`。如果观察到 HBM[0]
瓶颈，需要同时考虑 `SpElement_list_ptr`、`Matrix_data_0` 和 `X` 的共享关系。

### 2.2 基础类型

相关类型在 `DLC/Cuper/include/Cuper.h` 中大致长这样：

```cpp
#define VALUE_TYPE float
#define INDEX_TYPE int

using float_v2  = tapa::vec_t<VALUE_TYPE, 2>;
using float_v8  = tapa::vec_t<VALUE_TYPE, 8>;
using float_v16 = tapa::vec_t<VALUE_TYPE, 16>;
```

| Cuper 类型 | 定义 | 数学/算法含义 |
| --- | --- | --- |
| `VALUE_TYPE` | `float` | 矩阵值、SpMV 输入/输出和局部乘积的数值类型。 |
| `INDEX_TYPE` | `int` | 行号、列号、batch 边界和长度参数。 |
| `float_v2` | 2 个 `float` | accumulator/checker 的两个相邻输出 lane。 |
| `float_v8` | 8 个 `float` | 一个 512-bit matrix beat 解码后对应的 8 个局部乘积。 |
| `float_v16` | 16 个 `float` | `X` 和 `Y_out` 的向量 HBM 包，16 个连续向量元素一包。 |

`X` / `Y_out` 的 packet 对应关系是：

$$
X[packet][lane] = x_{16 \cdot packet + lane}
$$

$$
Y\_out[packet][lane] = y_{16 \cdot packet + lane}
$$

### 2.3 原始输入和 COO

COO 的 host 侧结构体在 `DLC/Cuper/include/Cuper_common.h` 中大致是：

```cpp
struct Matrix_COO {
    INDEX_TYPE m;
    INDEX_TYPE n;
    INDEX_TYPE nnzR;

    vector<INDEX_TYPE> ColIdx;
    vector<INDEX_TYPE> RowIdx;
    vector<VALUE_TYPE> Val;
};
```

| 数学对象 | host 数据结构 | 字段含义 |
| --- | --- | --- |
| 矩阵维度 | `Matrix_COO::m`、`Matrix_COO::n` | 行数和列数。 |
| 非零元数量 | `Matrix_COO::nnzR` | 当前矩阵或 slice 中真实非零元数量。 |
| 非零项行号 $i$ | `Matrix_COO::RowIdx[k]` | 第 `k` 个非零元的原始全局行号。 |
| 非零项列号 $j$ | `Matrix_COO::ColIdx[k]` | 第 `k` 个非零元的原始全局列号。 |
| 非零项值 $A_{ij}$ | `Matrix_COO::Val[k]` | 第 `k` 个非零元的 FP32 值。 |

CSR 的 `row_ptr / col_idx / values` 先由 `CSR_2_COO(...)` 展开成上述 COO 三元组。
这一步只把“按行压缩”的表达改成“每个非零元一条记录”，数学含义仍是
$y_i \mathrel{+}= A_{ij} x_j$。

### 2.4 SparseSlice

`SparseSlice` 是 `COO -> SpElement` 之间的 host 侧分块索引结构：

```cpp
struct SparseSlice {
    INDEX_TYPE sliceSize;
    INDEX_TYPE numColSlices;
    INDEX_TYPE numRowSlices;
    INDEX_TYPE numSlices;

    vector<INDEX_TYPE> sliceColPtr;
    vector<INDEX_TYPE> sliceRowIdx;
    vector<Matrix_COO> sliceVal;
};
```

| 结构字段 | 对应对象 | 含义 |
| --- | --- | --- |
| `SparseSlice::sliceSize` | `Slice_SIZE` | 每个列 slice 覆盖的列宽。 |
| `SparseSlice::numColSlices` | 列 slice 数 | 矩阵按列切成多少个 slice。 |
| `SparseSlice::numRowSlices` | 行 slice 数 | 行方向 slice 数。 |
| `SparseSlice::sliceColPtr` | slice 指针 | 类似 CSR 的 ptr，描述每个列 slice 在 `sliceVal` 中的范围。 |
| `SparseSlice::sliceRowIdx` | slice 行索引 | slice 级别的行位置索引。 |
| `SparseSlice::sliceVal` | `Matrix_COO` 数组 | 每个 slice 内部的 COO 非零元。 |

`SparseSlice` 的作用是把原始 $A$ 拆成列窗口。后续每个 batch 会处理一组列 slice，
因此 core 只需要加载当前窗口内的 `x`。

### 2.5 SpElement

`SpElement` 是 host 重排后的单个矩阵元素。它后续会被压进 `Matrix_data[channel]`
的 64-bit slot：

```cpp
struct SpElement {
    INDEX_TYPE colIdx;
    INDEX_TYPE rowIdx;
    VALUE_TYPE val;
};
```

| `SpElement` 字段 | 对应数学量 | 说明 |
| --- | --- | --- |
| `colIdx` | 局部列号 | 等于原始列号减去当前 batch 的 `base_col_index`，用于访问 `local_X[colIdx]`。 |
| `rowIdx` | Cuper 内部 row 编码 | 不是原始全局行号；用于定位 accumulator 的本地累加槽位。 |
| `val` | $A_{ij}$ | 非零元值，FP32。 |

`rowIdx` 的编码含义：

| bit | 含义 |
| --- | --- |
| `bit0` | 原始 row 的奇偶，选择 ping/pong 累加侧。 |
| `bit[17:1]` | `org_row_idx`，作为局部 URAM 累加地址。 |
| `bit17 = 1` | 空元素或 padding 标记，不参与乘加。 |

所以一个有效 `SpElement` 的算法含义是：

$$
SpElement(colIdx,\ rowIdx,\ val) \Rightarrow sum[rowIdx] \mathrel{+}= val \cdot local\_X[colIdx]
$$

### 2.6 PE list 和 batch 边界

| host 结构 | 对应 kernel 输入 | 含义 |
| --- | --- | --- |
| `SpElement_list_pes[pe][idx]` | 打包前的 PE 私有非零元列表 | 第 `pe` 个物理 PE 在第 `idx` 个位置要处理的 `SpElement`。 |
| `SpElement_list_ptr[b]` | `SpElement_list_ptr` HBM | 第 `b` 个 batch 在各 PE list 中的起始 beat 索引。 |
| `SpElement_list_ptr[b + 1]` | `SpElement_list_ptr` HBM | 第 `b` 个 batch 的结束 beat 索引。 |
| `Batch_num` | 顶层参数 | batch 数，等于 `SpElement_list_ptr.size() - 1`。 |
| `Matrix_len` | 顶层参数 | 每个 `Matrix_data[channel]` 需要读取的 512-bit beat 数，通常等于 `SpElement_list_ptr[Batch_num]`。 |

`SpElement_list_ptr` 是 batch 边界表，不是原始 CSR 的 row pointer。它控制的是
每个 batch 在 Cuper 重排后矩阵流中的开始和结束位置。

`Batch_num` 和 `Matrix_len` 的具体含义：

| 参数 | 由谁生成 | 含义 |
| --- | --- | --- |
| `Batch_num` | host | Cuper 把列 slice 按每 `BATCH_SIZE` 个一组打包后的 batch 数。代码上等于 `SpElement_list_ptr.size() - 1`。 |
| `Matrix_len` | host | 每个 `Matrix_data[channel]` 在一次 SpMV 中要顺序读取的 512-bit beat 总数。代码上等于 `SpElement_list_ptr[Batch_num]`。 |

如果 `numColSlices` 是列 slice 总数，则：

$$
Batch\_num = \left\lceil \frac{numColSlices}{BATCH\_SIZE} \right\rceil
$$

对第 `b` 个 batch：

$$
start_b = SpElement\_list\_ptr[b]
$$

$$
end_b = SpElement\_list\_ptr[b + 1]
$$

这个 batch 内，每个 PE list 要处理的槽位范围是 `[start_b, end_b)`。这些槽位已经在
host 侧按所有 PE 补齐到同一长度，所以 16 路 HBM 可以同步读取。

最后一个边界值就是整条矩阵流的长度：

$$
Matrix\_len = SpElement\_list\_ptr[Batch\_num]
$$

因此 `Batch_num` 描述“有多少个列窗口批次”，`Matrix_len` 描述“每一路 HBM 总共要读
多少个 512-bit beat”。前者用于 core 按 batch 加载不同的 `x` 窗口，后者用于
`Matrix_Loader` 控制矩阵流总读取长度。

### 2.7 Matrix_data 打包

| 打包层级 | 结构 | 对应关系 |
| --- | --- | --- |
| host buffer | `matrix_fpga_data_[channel]` | 第 `channel` 个 HBM bank 的 packed 矩阵数据。 |
| kernel 端口 | `Matrix_data[channel]` | TAPA 端看到的 512-bit beat 流。 |
| 512-bit beat | `ap_uint<512>` | 包含 8 个 64-bit `SpElement` 槽位。 |
| 64-bit slot | `[colIdx, rowIdx, value]` | 一个 PE lane 的非零元或 padding。 |

这里的 `Matrix_data[16]` 可以理解成 16 个并行矩阵数组：

```text
Matrix_data[0]  -> HBM channel 0
Matrix_data[1]  -> HBM channel 1
...
Matrix_data[15] -> HBM channel 15
```

每个 `Matrix_data[channel]` 的有效读取长度由 `Matrix_len` 给出。host buffer 可能
为了对齐分配得更长，但 kernel 只从每一路读取 `Matrix_len` 个 512-bit beat。因此
单路 HBM 的有效槽位数是：

$$
Matrix\_len \cdot 8
$$

16 路合起来的槽位数是：

$$
Matrix\_len \cdot 16 \cdot 8 = Matrix\_len \cdot 128
$$

这些槽位不是都对应真实非零元。为了让 128 个 PE lane 按统一步长并行读取，host
会把各 PE list 补齐到同一个长度；补出来的空槽用 `rowIdx = 0x3ffff` 标记。实际
有效非零元数量要数 `rowIdx[17] == 0` 的 slot，padding slot 不参与乘加。

从 64-bit slot 的 payload 看，真正的数值数据只有 `value` 的 32 bit；`colIdx` 和
`rowIdx` 是坐标/路由信息：

| 字段 | bit 数 | 用途 |
| --- | ---: | --- |
| `colIdx` | 14 | 当前 batch 内的局部列号，用来取 `x`。 |
| `rowIdx` | 18 | Cuper 内部 row 编码，用来累加到对应输出槽位。 |
| `value` | 32 | FP32 矩阵非零元值。 |

第 `channel` 个 HBM 的第 `i` 个 512-bit beat 中，第 `lane` 个 64-bit slot 对应：

$$
pe = channel \cdot 8 + lane
$$

$$
SpElement = SpElement\_list\_pes[pe][i]
$$

空槽会被打成 `rowIdx = 0x3ffff`，硬件端看到 `rowIdx[17] = 1` 后不参与乘加。

### 2.8 Kernel 内部中间结构

core 到 accumulator 的中间包在 `DLC/Cuper/kernels/detail/cuper_spmv_tasks.hpp`
中定义：

```cpp
struct Matrix_Mult_X {
    ap_uint<18> row[8];
    float_v8 val;
};
```

| 内部结构 / stream | 字段 | 对应算法量 |
| --- | --- | --- |
| `local_X` | `local_X[colIdx]` | 当前 batch 的输入向量窗口，代表 $x_{batch\_base + colIdx}$。 |
| `Matrix_Mult_X` | `row[8]` | 8 个 `SpElement` 的 Cuper row 编码。 |
| `Matrix_Mult_X` | `val[8]` | 8 个局部乘积 $A_{ij} x_j$。 |
| `local_part_Y_ping/pong` | URAM 累加槽 | 按 `rowIdx` 保存部分和。 |
| `Vector_Y_Stream` | `float_v2` | accumulator 输出的两个相邻结果 lane。 |
| `Vector_Y_Stream_Aftck` | `float_v2` | checker 过滤 padding 后的有效结果。 |
| `Vector_Y_Stream_Ans` | `float_v16` | sort-tree 拼好的连续 16 个 `y` 元素。 |

`Matrix_Mult_X` 是 core 到 accumulator 的关键中间包。一条 `Matrix_Mult_X` 对应一个
512-bit matrix beat 解出来的 8 个非零元槽位；其中有效槽位满足：

$$
Matrix\_Mult\_X.val[lane] = A_{ij} x_j
$$

$$
Matrix\_Mult\_X.row[lane] = rowIdx(i)
$$

## 3. 从 CSR 到 Cuper 算法格式

原始数学表达通常来自 CSR：

对第 $i$ 行，CSR 使用 `row_ptr[i]` 到 `row_ptr[i+1] - 1` 这一段索引，
找到对应的 `(col_idx[k], values[k])`。

它直接表达的是：

$$
y_i \mathrel{+}= values[k] \cdot x[col\_idx[k]]
$$

Cuper 为了并行计算，会在 host 侧把 CSR 改写成更适合硬件消费的格式。转换链路是：
`CSR` -> `COO` -> `SparseSlice` -> `SpElement_list_pes` ->
`SpElement_list_ptr + Matrix_data[0..15]`。

这个转换不改变数学结果，只改变非零项的访问顺序和分组方式。每个非零项仍然代表：

$$
(row,\ col,\ val) \Rightarrow y_{row} \mathrel{+}= val \cdot x_{col}
$$

## 4. 列 slice 和 batch

Cuper 按列方向把矩阵分成 slice，再把若干 slice 合成一个 batch。相关常量：

$$
\begin{aligned}
Slice\_SIZE  &= HBM\_CHANNEL\_NUM \cdot ROW\_HBM\_NUM \\
BATCH\_SIZE  &= \frac{8192}{Slice\_SIZE} \\
Slice\_WIDTH &= Slice\_SIZE \cdot BATCH\_SIZE
\end{aligned}
$$

算法含义是：

1. 一次 batch 只处理输入向量 `x` 的一个连续列窗口；
2. 这个窗口内的非零项使用局部列号 `col_local`；
3. core 先加载该窗口内的 `x`，再消费属于该窗口的非零项；
4. 所有 batch 的贡献累加起来，得到完整的 `y`。

数学上，这等价于把求和拆段：

$$
y_i = \sum_{\text{batch}} \sum_{j \in \text{batch}} A_{ij} x_j
$$

拆 batch 后每段只需要访问当前窗口的 $x_j$，降低每个 core 同时需要缓存的向量范围。

## 5. SpElement 表示一个非零项

Cuper 的矩阵数据最终按 512-bit word 存入 `Matrix_data[channel]`。每个 512-bit word
包含 8 个 64-bit `SpElement`：

| bit 范围 | 字段 | 含义 |
| --- | --- | --- |
| `[63:50]` | `colIdx` | 局部列号 |
| `[49:32]` | `rowIdx` | Cuper 内部 row 编码 |
| `[31:0]` | `value` | FP32 非零值 |

算法含义仍然是：

$$
\begin{aligned}
partial &= value \cdot x_{colIdx} \\
sum[rowIdx] &\mathrel{+}= partial
\end{aligned}
$$

`rowIdx` 不是直接用于输出数组的原始行号。host 预处理会把原始 row 映射成
Cuper 内部 row 编码，使后续 accumulator、checker、sort-tree 能按固定顺序累加和
拼回 `float_v16` 输出。空槽用特殊 row 标记，不参与乘加。

## 6. 非零项分配到 PE / HBM channel

一个数学非零项 `(row, col, val)` 会根据输出位置被分配到某个物理 PE 槽位：

$$
\begin{aligned}
packet\_id  &= \lfloor row / 2 \rfloor \\
checker\_id &= packet\_id \bmod 8 \\
acc\_offset &= \lfloor packet\_id / 8 \rfloor \bmod 2 \\
pe\_in\_acc &= \lfloor packet\_id / 16 \rfloor \bmod 8 \\
pe          &= (checker\_id \cdot 2 + acc\_offset) \cdot 8 + pe\_in\_acc
\end{aligned}
$$

随后：

$$
channel = \lfloor pe / 8 \rfloor,\qquad lane = pe \bmod 8
$$

这样做的目标是让最终输出顺序和 `float_v2 -> float_v16` 的拼包顺序对齐。数学上它
只是给同一个求和式安排并行执行位置：

$$
y_{row} \mathrel{+}= val \cdot x_{col}
$$

并不改变 `row/col/value` 的含义。

## 7. Core 阶段：乘法

每个 core 处理一个 HBM channel 的矩阵分片。对一个有效 `SpElement`，core 执行：

$$
\begin{aligned}
val      &= SpElement.value \\
col      &= SpElement.colIdx \\
row\_code &= SpElement.rowIdx \\
product  &= val \cdot local\_X[col]
\end{aligned}
$$

然后输出：

$(row\_code,\ product)$

这对应数学公式中的单项：

$$
A_{ij} x_j
$$

`local_X` 是当前 batch 的输入向量窗口。因为 `colIdx` 是局部列号，所以访问的是：

$$
x_{batch\_base + colIdx}
$$

而不是全局 `x[colIdx]`。

## 8. Accumulator 阶段：按行求和

core 输出的是局部乘积，不是最终 `y`。Accumulator 对相同 row 编码的 partial 做累加：

$$
sum[row\_code] \mathrel{+}= product
$$

这一步对应数学公式里的求和：

$$
y_i = \sum_j A_{ij} x_j
$$

Cuper 内部使用 ping/pong 或 packed 存储把两个相邻输出 lane 组织成 `float_v2`。
这只是硬件存储和输出顺序的安排；数学上仍然是同一行 $y_i$ 的累加。

每轮 accumulator 会先清空本地部分和，再消费本轮 SpMV 的所有 batch 贡献，最后输出
按 Cuper 内部顺序排列的 `float_v2`。

## 9. Checker 和 sort-tree：恢复输出向量顺序

Accumulator 产生的结果包含硬件对齐和 padding。后处理分两步：

1. `Vector_Checker` 过滤超出 `Row_num` 的无效输出；
2. `Mult_Sort_Tree` 把 8 路 `float_v2` 拼成 1 路 `float_v16`。

最终 `Vector_Writer` 写回：

$$
Y\_out[packet] = \{y_{16 \cdot packet + 0},\ldots,y_{16 \cdot packet + 15}\}
$$

最后一个 packet 如果不足 16 个真实行，尾部 lane 是 padding，不属于有效数学输出。

## 10. `Iteration_num` 的算法含义

`Iteration_num` 表示在同一次 kernel launch 里重复执行多少次同样的 SpMV：

$$
\text{for } iter = 0,\ldots,Iteration\_time - 1:\quad y = A x
$$

其中：

$$
Iteration\_time =
\begin{cases}
1, & Iteration\_num = 0 \\
Iteration\_num, & Iteration\_num \ne 0
\end{cases}
$$

这不是 PCG 迭代次数。Project-XPlus 的 host-side PCG 路径通常每次调用 `Cuper(...)`
只做一次 SpMV，因此传 `Iteration_num = 1`，避免把 benchmark 重复次数和 PCG 算法轮次
混在一起。

## 11. 一句话总结

Cuper 对 SpMV 的算法映射可以压缩成：

- 把 CSR 非零项重排成按列窗口、PE 和 HBM channel 分组的 `SpElement`；
- 每个 core 计算 $val \cdot x_{col}$；
- accumulator 按 row 累加 partial；
- checker/sort-tree 把内部 row 编码结果拼回连续 `y`。

它优化的是 $y = A x$ 的执行组织，不改变 SpMV 的数学定义。
