# Cuper 大矩阵拆分运行方案

本文记录把大规模 SpMV 拆成多个小矩阵交给 `DLC/Cuper` 计算时需要注意的点。

当前目标不是改 Cuper kernel，而是在 host 侧把一次大矩阵乘法

```text
y = A * x
```

拆成若干个 Cuper 可以处理的小任务，再把局部结果累加回完整 `y`。

## 1. 现状判断

Cuper 子项目目前是独立工程，入口在：

- `DLC/Cuper/include/Cuper.h`
- `DLC/Cuper/include/Cuper_common.h`
- `DLC/Cuper/kernels/Cuper.cpp`
- `DLC/Cuper/host/main.cpp`

关键常量在 `Cuper.h`：

```cpp
constexpr INDEX_TYPE HBM_CHANNEL_NUM = 16;
constexpr INDEX_TYPE ROW_HBM_NUM = 4;
constexpr INDEX_TYPE Slice_SIZE = HBM_CHANNEL_NUM * ROW_HBM_NUM; // 64
constexpr INDEX_TYPE BATCH_SIZE = 8192 / Slice_SIZE;             // 128
const INDEX_TYPE Slice_WIDTH = Slice_SIZE * BATCH_SIZE;          // 8192
```

也就是说，Cuper 的矩阵格式先按 `64 x 64` slice 划分，再把 128 个列 slice 组成一个列 batch。一个列 batch 的宽度就是 `128 * 64 = 8192`。

代码上看，`Core` 里有：

```cpp
VALUE_TYPE local_X[X_BRAM_DEPTH][Slice_WIDTH];
```

因此每个 Core 一次只能把一个 `8192` 宽的 `x` 片段缓存在片上。现有 host 如果让 `Column_num > 8192`，`Batch_num` 会大于 1，但 `Vector_Loader` 每轮只发送一次完整 `x`，而 `Core` 会按每个 batch 都再读一遍完整 `x`。这会导致多 batch 路径存在死锁或读空风险。因此，当前不改 kernel 的前提下，应把每次 Cuper 调用的列数限制在 `<= 8192`。

行数限制没有同样明确地写成 8192。`Accumulator` 的局部结果地址大约按 `Row_num / 256` 映射到 URAM；从常量看行方向理论上可能大于 8192。但既然当前已观测到“最大 8192 维”这个工程边界，第一版适配建议保守地把每次调用做成 `m_tile <= 8192` 且 `n_tile <= 8192`。等验证稳定后，再尝试只切列、不切行。

## 2. 推荐拆分模型

把矩阵按二维 tile 拆开：

```text
A =
[ A00 A01 A02 ... ]
[ A10 A11 A12 ... ]
[ A20 A21 A22 ... ]
[ ...             ]
```

其中每个 `Aij` 的尺寸满足：

```text
rows(Aij) <= 8192
cols(Aij) <= 8192
```

完整 SpMV 可以写成：

```text
y_i = sum_j Aij * x_j
```

其中：

- `x_j` 是全局 `x` 在第 `j` 个列 tile 上的子向量
- `y_i` 是全局 `y` 在第 `i` 个行 tile 上的子向量
- 每次 Cuper 只计算一个局部乘法 `partial = Aij * x_j`
- host 负责执行 `y_i += partial`

第一版建议：

```text
tile_rows = 8192
tile_cols = 8192
```

后续如果确认 Cuper 行方向可以稳定跑更大，可以改成：

```text
tile_rows = larger safe row size
tile_cols = 8192
```

这样能减少 kernel launch 次数和重复搬运 `x_j` 的次数。

## 3. Host 侧流程

### 3.1 预处理阶段

矩阵 `A` 在 PCG 中不变，所以 tile 划分和 Cuper 矩阵格式转换应只做一次。

建议新增一个类似 `CuperSpmvPlan` 的 host 侧结构：

```cpp
struct CuperTile {
    int row_begin;
    int row_end;
    int col_begin;
    int col_end;
    int local_rows;
    int local_cols;
    int nnz;

    std::vector<INDEX_TYPE> sp_element_list_ptr;
    std::vector<aligned_vector<unsigned long>> matrix_fpga_data;
};

struct CuperSpmvPlan {
    int global_rows;
    int global_cols;
    int tile_rows;
    int tile_cols;
    std::vector<CuperTile> tiles;
};
```

构建每个 tile 时，从全局 CSR 中扫描对应行区间：

```text
for row in [row_begin, row_end):
    for offset in row_ptr[row] .. row_ptr[row + 1):
        col = col_idx[offset]
        if col_begin <= col < col_end:
            local_row = row - row_begin
            local_col = col - col_begin
            value = values[offset]
            append(local_row, local_col, value)
```

然后对这个局部 COO 调用现有 Cuper 转换链：

```text
Create_SparseSlice(local_rows, local_cols, local_nnz, Slice_SIZE, ...)
Create_SpElement_list_for_all_PEs(...)
Create_SpElement_list_for_all_channels(...)
```

空 tile 直接跳过，不需要调用 Cuper。

### 3.2 每次 SpMV 阶段

每次要算 `y = A * x` 时：

```text
clear y to 0

for each col tile j:
    prepare x_sub = x[col_begin : col_end]

    for each non-empty row tile i in this col tile:
        run Cuper(Aij, x_sub) -> partial
        y[row_begin : row_end] += partial
```

注意 Cuper kernel 当前没有输入 `Y`，只写 `Y_out`。所以不能指望 kernel 内部帮我们做跨列 tile 累加；累加必须在 host 侧完成，或者以后改 Cuper 接口增加 `Y_in/Y_accum`。

### 3.3 PCG 集成方式

如果把 Cuper 接进 Project-XPlus 的 PCG 流程，SpMV 每轮都要执行一次：

```text
ap = A * p
```

因此矩阵 tile 的 `matrix_fpga_data` 可以复用，但每轮的输入向量 `p` 都会变，`x_sub` 必须每轮重新准备和同步到 device。

建议封装成统一接口：

```cpp
void run_cuper_spmv(const CuperSpmvPlan& plan,
                    const std::vector<float>& x,
                    std::vector<float>& y);
```

如果上层仍然是 double PCG，需要明确做一次精度策略选择：

- Cuper 当前 `VALUE_TYPE` 是 `float`
- Project-XPlus 主流程当前 `data_t` 是 `double`
- 混用时建议 host 侧用 `double` 累加 partial，再转回需要的格式
- 收敛阈值、残差校验要按 float 误差重新评估

## 4. 关键约束和坑点

### 4.1 每次调用列数必须小于等于 8192

这是当前最重要的约束。`Column_num > 8192` 会让 `Batch_num > 1`，而现有 `Vector_Loader/Core` 的数据流并没有正确支持多 batch 重读 `x`。

如果后续要从 kernel 层修这个问题，有两个方向：

- 让 `Vector_Loader` 每个 batch 都重新发送一次 `x`
- 或者让 `Core` 每个 batch 只读当前 batch 对应的 `x` 片段

在不改 kernel 的方案里，直接把列 tile 限制到 `<= 8192` 最稳。

### 4.2 局部列号必须重新编号

Cuper 打包时存的是局部列号。构建 tile 时必须做：

```text
local_col = global_col - col_begin
```

不能把全局列号直接塞进去。否则 `Core` 会用全局列号访问本地 `local_X`，结果越界或读错。

### 4.3 局部行号也必须重新编号

构建 tile 时必须做：

```text
local_row = global_row - row_begin
```

Cuper 输出的是局部 `partial[0 : local_rows)`。host 累加时再映射回：

```text
y[global_row] += partial[local_row]
```

### 4.4 `Y_out` 是局部 partial，不是全局 y

每个 tile 的输出只代表：

```text
partial = Aij * x_j
```

如果同一个行 tile 有多个列 tile，必须累加所有 partial 才是最终结果。

### 4.5 空 tile 要跳过

大稀疏矩阵二维切分后会有很多空块。空 tile 不应创建 BO，也不应调用 kernel。否则 kernel launch overhead 会非常高。

### 4.6 避免每轮重复格式转换

`Create_SparseSlice` 和 PE/channel 打包只依赖矩阵，不依赖输入向量。对于 PCG，应在加载矩阵后一次性完成并缓存。

每轮只做：

- 拷贝当前向量子段 `x_sub`
- 运行 Cuper
- 累加 partial

### 4.7 注意 padding

现有 host 对 `X` 和 `Y` 都按 16 和 1024 做 padding：

```cpp
X_fpga_data_column_size = ((n + 16 - 1) / 16) * 16;
X_fpga_data_channel_size = ((X_fpga_data_column_size + 1023) / 1024) * 1024;
```

拆分后也要对每个 `x_sub` 和 `partial` 使用同样 padding。读回时只取 `local_rows` 个有效元素。

### 4.8 `Iteration_num` 不要默认用性能测试值

`Cuper.h` 里当前有：

```cpp
constexpr INDEX_TYPE ITERATION_NUM = 2;
```

这是独立 benchmark 的重复运行次数。接入 PCG 时，一次 SpMV 调用通常只需要传 `Iteration_num = 1`。否则 kernel 内部会重复算多次，虽然最终结果通常相同，但时间和调度语义会混乱。

### 4.9 精度会变

Cuper 当前是 float SpMV，而 Project-XPlus 是 double PCG。即使拆分逻辑正确，结果也不会和 double CSR bitwise 一致。验证时不要用过严的 double 阈值。

建议至少保留三套校验：

- CPU double CSR：原始 golden
- CPU float CSR：对齐 Cuper 精度的 golden
- Cuper split：实际硬件结果

优先比较 `Cuper split` vs `CPU float CSR`。

### 4.10 启动次数可能很高

如果矩阵是 `N x N`，并且采用 `8192 x 8192` tile，则一次 SpMV 的最多 kernel 调用数约为：

```text
ceil(N / 8192) * ceil(N / 8192)
```

例如 `N = 65536` 时，最坏是 `8 * 8 = 64` 次 Cuper 调用一次 SpMV。PCG 每轮都要 SpMV，这个 overhead 很大。

优化优先级建议：

1. 跳过空 tile
2. 预转换并缓存所有 tile
3. 在确认安全后增大 `tile_rows`
4. 按列 tile 复用同一个 `x_sub` BO
5. 后续再考虑改 kernel 支持多列 batch

## 5. 建议实现顺序

第一步，做离线/host-only tile 验证：

1. 从 CSR 构建 tile 列表
2. 用 CPU 对每个 tile 做 `partial = Aij * x_j`
3. host 累加得到 split CPU 结果
4. 和原始 CPU CSR 结果比较

第二步，替换单个 tile 的 CPU partial 为 Cuper：

1. 先选 `N <= 8192` 的矩阵
2. 保证只有一个 tile
3. 对齐现有 `DLC/Cuper/host/main.cpp` 的结果

第三步，测试二维拆分：

1. 构造 `N > 8192` 的小型可控矩阵
2. 先测试只有列拆分，例如 `4096 x 16384`
3. 再测试行列都拆，例如 `16384 x 16384`
4. 每个 tile 输出 partial 后立即和 CPU tile partial 比较

第四步，再接入 PCG：

1. 先只替换一次 `SpMV(p -> ap)`
2. 比较每轮 `ap` 的误差
3. 再观察 `alpha / beta / rr` 的漂移
4. 最后验证整体收敛和残差

## 6. 推荐的最小接口

建议不要直接把拆分逻辑塞进现有 `host/main.cpp`。先新增一个适配层，形成清晰边界：

```cpp
class CuperSplitSpmv {
public:
    explicit CuperSplitSpmv(const CsrMatrix& matrix,
                            int tile_rows = 8192,
                            int tile_cols = 8192);

    void multiply(const std::vector<float>& x,
                  std::vector<float>& y);

private:
    CuperSpmvPlan plan_;
};
```

这样后续有两种演进路径：

- 保持 host 侧拆分，继续复用 Cuper kernel
- 改 Cuper kernel 支持多 batch 后，只替换适配层内部实现

上层 PCG 不需要知道底层到底是单次 CSR SpMV、Cuper 单 tile，还是 Cuper split tile。

## 7. 当前结论

在不改 Cuper kernel 的前提下，大矩阵拆分方案可行，但必须把每次 Cuper 调用的列数控制在 `<= 8192`，并由 host 负责跨列 tile 的结果累加。

第一版建议使用保守二维拆分：

```text
tile_rows = 8192
tile_cols = 8192
```

这会带来更多 kernel launch，但最容易验证正确性。等跑通后，再根据实测把 `tile_rows` 放大，或者改 Cuper 的多 batch 数据流，减少拆分数量。
