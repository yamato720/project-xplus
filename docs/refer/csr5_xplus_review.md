# CSR5 对 Project-XPlus / FPGA PCG SpMV 的可行性评估备忘

> 目标：给 Codex 阅读，用来判断是否值得改 `Project-XPlus` 的 SpMV/PCG 数据格式与 kernel。
> 结论先行：**不建议立刻把 XPlus 全面改成原版 CSR5；建议先做一个“CSR5-inspired tiled SpMV”实验分支**。CSR5 的核心思想——按非零元均匀切 tile、tile 内转置、给 row segment 加紧凑元数据、把不规则 row-based CSR 转成更适合 SIMD/并行的 nnz-space 执行——很值得参考。但原版 CSR5主要面向 CPU/GPU/Xeon Phi 的 SIMD/warp；在 FPGA/HBM 上更关键的是 **HBM channel streaming、PE load balance、RAW/partial-sum 合并、x 向量复用、跨 SLR/资源/Fmax**，所以要结合 XPlus 的 HLS/HBM 架构重新落地。

---

## 1. CSR5 是什么

CSR5，全称 **Compressed Sparse Row 5**，是 Liu & Vinter 在 ICS 2015 提出的 SpMV 存储格式。它不是完全推翻 CSR，而是直接扩展 CSR：

- 保留 CSR 的 `row_ptr`。
- 将 `col_idx` 和 `val` 在固定大小 tile 内做 **in-place tile transpose**，使 SIMD lane / warp 访问更连续。
- 增加两类辅助信息：`tile_ptr` 与 `tile_desc`。
- 因为整体有 5 组数据：`row_ptr`, `tile_ptr`, `col_idx`, `val`, `tile_desc`，所以叫 CSR5。

它的目标是解决传统 CSR SpMV 在不规则矩阵上的两个痛点：

1. **row-based 并行负载不均衡**：长短行差异大时，按行分配任务会导致某些线程/PE 很忙、某些空闲。
2. **segmented-sum 虽然负载均衡但内存/同步开销大**：CSR5 试图保留 CSR 的压缩优势，同时把 segmented-sum 的负载均衡能力拿过来。

---

## 2. CSR5 的核心数据结构

设矩阵非零元数为 `nnz`，CSR5 把非零元数组按固定 tile 大小划分：

```text
tile_size = omega * sigma
p = ceil(nnz / tile_size)
```

其中：

- `omega`：tile 宽度，通常对应 SIMD 宽度 / warp lane 数 / FPGA 并行 lane 数。
- `sigma`：tile 高度，控制每个 lane 连续处理多少个元素。

CSR5 包含：

```cpp
row_ptr[m + 1]        // CSR 原 row pointer，基本不变
col_idx[nnz]          // 每个完整 tile 内从 row-major 转成 column-major
a_val[nnz]            // 同上，和 col_idx 同步 tile transpose
tile_ptr[p + 1]       // 每个 tile 起始非零元所在的矩阵行，用于定位 y 输出位置
tile_desc[p]          // 每个完整 tile 的描述信息
```

`tile_desc` 内部可理解为四类提示信息：

```cpp
bit_flag[omega * sigma]   // 标记某个非零元是否是一行/segment 的起点
y_offset[omega]           // 每个 tile column 输出到 y 的相对偏移
seg_offset[omega]         // 加速 tile 内 segmented sum
empty_offset[]            // 可选；当 tile 覆盖空行时，用于修正 y 位置
```

### 直观理解

原始 CSR 是按行连续存：

```text
row0: a00 a01 a02
row1: a10
row2: a20 a21 ...
```

CSR5 更像是按 `nnz` 均匀切块：

```text
nonzeros -> [tile0][tile1][tile2]...
```

然后每个 tile 内部按适合并行 lane 的方向转置，让 lane 能以更规则的方式读 `val/col_idx`，再用 `tile_desc` 告诉计算单元“哪些元素属于同一行，该在哪里结束累加、输出到 y”。

---

## 3. CSR -> CSR5 预处理流程

从标准 CSR 转 CSR5，大概包括 4 步：

1. **内存分配**：分配 `tile_ptr`、`tile_desc` 等辅助数组。
2. **生成 `tile_ptr`**：对每个 tile 的起始非零元位置，在 `row_ptr` 中二分查找其对应行号。
3. **生成 `tile_desc`**：包括 `bit_flag`、`y_offset`、`seg_offset`，以及必要时的 `empty_offset`。
4. **tile 内转置 `col_idx` 和 `val`**：完整 tile 内从原 CSR 顺序转成 tile-column-major 顺序。

注意：CSR5 论文强调转换开销较低，GPU 上平均可以低到几个 SpMV 的开销量级；x86 CPU 上可能更高，约十几个 SpMV 的开销量级。对 PCG/CG 这种同一个矩阵反复迭代的场景，预处理开销更容易被 amortize 掉。

---

## 4. CSR5 SpMV 执行逻辑

CSR5 SpMV 的核心是：**每个 tile 独立并行处理，tile 内每个 lane 负责一列**。

伪逻辑可以理解为：

```cpp
for each tile tid in parallel:
    tmp[omega] = 0
    last_tmp[omega] = 0

    for lane i in 0..omega-1 in parallel:
        sum = 0
        for j in 0..sigma-1:
            ptr = tid * omega * sigma + j * omega + i
            sum += val[ptr] * x[col_idx[ptr]]

            if bit_flag indicates end of a complete segment:
                y[tile_ptr[tid] + y_offset[i]] = sum
                y_offset[i]++
                sum = 0
            else if bit_flag indicates cross-lane/cross-segment boundary:
                tmp[...] = sum
                sum = 0

        last_tmp[i] = sum

    fast_segmented_sum(tmp, seg_offset)
    merge tmp into last_tmp
    write final partial sums to y
```

关键点：

- CSR5 将行内/跨行累加问题变成 tile 内 segmented-sum 问题。
- `bit_flag` 负责描述行 segment 边界。
- `seg_offset` 把复杂 segmented-sum 转成更快的 prefix-sum scan + 少量算术。
- 对跨 tile 的同一行，仍可能需要处理 partial sum 合并；GPU 上可能用 atomic/additional merge，FPGA 上需要专门设计 accumulator/reduction stage。

---

## 5. 为什么它可能适合 PCG / XPlus

XPlus 的核心瓶颈大概率仍然在 SpMV：

```text
PCG iteration:
    SpMV: Ap = A * p
    dot / axpy / residual update
```

如果矩阵 `A` 固定，CSR -> CSR5 的转换只需要做一次。只要迭代次数足够多，预处理成本就能被摊薄。

CSR5 对 XPlus 可能有价值的点：

1. **更均匀的非零元级别划分**
   对不规则矩阵，比按 row 切分更容易负载均衡。

2. **tile 内转置利于宽口/并行 lane 读数据**
   FPGA 上可以把 `omega` 映射成 `PE lane` 数或 `AXI/HBM packet` 宽度。

3. **保留 `row_ptr`，格式迁移成本比完全换 COO/ELL/HYB 小**
   Host 端仍可从 CSR 输入开始，兼容现有矩阵加载流程。

4. **适合重复 SpMV 的 solver 场景**
   PCG 迭代 50、100、500 次时，格式转换开销更容易摊薄。

5. **给 HLS 设计提供一个“固定 tile + metadata decoder + accumulator”的结构**
   可以作为 XPlus 后续从 CSR row pipeline 走向 tiled dataflow 的中间方案。

---

## 6. 为什么不应该直接照搬原版 CSR5

原版 CSR5 不等于 FPGA/HBM 最优格式，原因如下：

1. **CSR5 主要面向 SIMD CPU/GPU/Xeon Phi，不是 HBM FPGA**
   它的 `omega` 设计强依赖 SIMD/warp 宽度。FPGA 上更重要的是 AXI burst、HBM pseudo-channel 分配、crossbar、PE 数量、II、Fmax 和资源。

2. **CSR5 解决 load balance，但不充分解决 x 向量复用**
   SpMV 很多时候瓶颈不只是 `val/col_idx` 流式读，还包括 `x[col]` 的随机读。TileSpMV、Cuper 这类后续工作更重视 2D tile spatial locality、vector reuse、HBM-compatible dataflow。

3. **tile_desc 解码和 segmented-sum 在 FPGA 上可能变成控制复杂度**
   `bit_flag/y_offset/seg_offset/empty_offset` 需要 decoder。HLS 中如果写得不好，可能增加分支、FIFO、BRAM/URAM、控制路径，导致 II/Fmax 下降。

4. **partial sum 合并可能很麻烦**
   CSR5 的 tile 是按 `nnz` 切的，同一行可能跨 tile。GPU 可以用 atomic 或额外 kernel/primitive；FPGA 上需要设计 RAW-aware accumulator、排序/归并或按 row-range 限制 tile 切分，否则会出现写冲突或乱序合并问题。

5. **对规则矩阵未必明显收益**
   如果 XPlus 当前测试矩阵比较规则、每行非零数接近、带状/有限元结构明显，基础 CSR row pipeline 可能已经足够好。CSR5 的收益主要在不规则行长度、负载不均衡明显的矩阵。

---

## 7. 和 TileSpMV / Cuper 的关系

### CSR5 vs TileSpMV

TileSpMV 是后续 GPU tiled SpMV 方法。它指出 CSR/ELL 等基础格式没有很好利用 sparse matrix 的 2D spatial structure，导致 `x` 向量复用不足。TileSpMV 把矩阵划成固定 16x16 sparse tile，并对每个 tile 选择 CSR/COO/ELL/HYB/dense/dense-row/dense-column 等不同小格式。

这对 XPlus 的启发是：

- 原版 CSR5 是按 `nnz` 均匀切 tile。
- TileSpMV 是按二维矩阵空间切 tile。
- FPGA/HBM 上，如果目标是改善 HBM burst、x reuse、PE load balance，**二维 tile 可能比原版 CSR5 更贴近硬件需求**。

### CSR5 vs Cuper

Cuper 是 HBM-equipped FPGA 上的 SpMV accelerator。它明确指出 CSR/CSC 的 pointer array 会阻碍对 nonzeros 的 fully streaming access，而且连续 nonzeros 存储也不利于 row/column vectorized delivery。Cuper 采用更 HBM-compatible 的 sparse slice/dataflow，并用 two-step reordering 降低 RAW conflicts、提升 vector reuse。

这对 XPlus 的启发是：

- 不要只问“要不要上 CSR5”，而要问：
  - 当前 XPlus 的 HBM PC 是否能流式读满？
  - `x` 向量随机访问是否拖慢？
  - partial sum / RAW 是否导致 pipeline stall？
  - row-based workload 是否负载不均？
- 如果这些问题明显存在，CSR5 的思想可以参考，但 Cuper/TileSpMV 的 HBM/tile/dataflow 思路可能更适合 FPGA。

---

## 8. 给 Codex 的代码审查任务

请在 XPlus 仓库中检查以下内容，再决定是否改：

### 8.1 找当前 SpMV 数据路径

请定位：

- Host 端矩阵加载和 CSR 构建代码。
- HLS kernel 的 SpMV 主循环。
- `row_ptr`, `col_idx`, `val`, `x`, `y` 的 buffer 定义。
- HBM bank / pseudo-channel 分配策略。
- 是否每次 PCG 迭代都重新搬运矩阵，还是矩阵常驻 HBM。
- 是否支持多 kernel / 多 CU 并行。

### 8.2 判断现有 CSR 的瓶颈属于哪类

请用现有 benchmark/log/profile 判断：

```text
A. HBM bandwidth 没吃满？
B. row 长度不均导致 PE idle？
C. x[col] 随机访问导致 cache/BRAM 命中差？
D. y 累加/partial sum/RAW 导致 stall？
E. HLS pipeline II > 1 或 Fmax 被控制逻辑拖低？
F. 多 HBM PC 分配不均或跨 SLR 线太长？
```

如果主要问题是 B，CSR5-like tiling 更有价值。
如果主要问题是 C，优先参考 TileSpMV/Cuper 的 2D tiling、column locality、x reuse。
如果主要问题是 D，优先设计 conflict-aware accumulator/reordering，而不是先改 CSR5。
如果主要问题是 A/F，优先优化 HBM channel mapping、burst、bank partition、data packing。

### 8.3 先不要大改，建议做实验分支

建议新建实验分支：

```bash
git checkout -b exp/csr5-inspired-spmv
```

先实现最小版本：

1. Host 端添加 CSR -> CSR5-like converter。
2. 新增数据结构：

```cpp
struct Csr5Matrix {
    int m, n;
    int nnz;
    int omega;
    int sigma;
    int num_tiles;
    std::vector<int> row_ptr;
    std::vector<int> tile_ptr;
    std::vector<int> col_idx_tiled;
    std::vector<float_or_double> val_tiled;
    std::vector<uint32_t> bit_flag;
    std::vector<uint16_t_or_int> y_offset;
    std::vector<uint16_t_or_int> seg_offset;
    std::vector<int> empty_offset;
};
```

3. HLS kernel 先只支持：
   - FP32 或当前 XPlus 主精度。
   - 固定 `omega`。
   - 固定 `sigma`。
   - 不处理极端空行优化，先保证 correctness。
4. 对不完整 tile fallback 到原 CSR kernel。
5. 保留原 CSR kernel，通过运行参数切换：

```bash
--spmv-format=csr
--spmv-format=csr5_exp
```

### 8.4 FPGA 参数建议

如果当前 XPlus 是 U55C/HBM + HLS：

- `omega` 不要照抄 GPU 的 32/64。应按硬件选择：
  - 512-bit AXI/HBM 读口一次能装多少 `(val, col)` pair；
  - PE lane 数能否稳定 II=1；
  - DSP/BRAM/URAM/FIFO 资源是否够；
  - 跨 SLR 布线是否恶化 Fmax。
- FP32 情况下，如果 `(value32 + col32)` 打包成 64-bit，则 512-bit 口可自然对应 `omega = 8` lanes。
- FP64 情况下，不要强行追求过大 `omega`，否则 DSP/布线/归约树会很重。
- `sigma` 先做小范围 sweep，例如：

```text
sigma = 4, 8, 16, 32
```

然后测：

```text
kernel time
HBM effective bandwidth
II
Fmax
LUT/FF/BRAM/URAM/DSP
PCG end-to-end time
correctness residual
```

---

## 9. 建议的 benchmark 判据

请至少测这些 case：

1. XPlus 当前默认矩阵，例如 `data/generated/cgsolver/n512`。
2. 规则/带状矩阵：检验 CSR5 是否没有负收益。
3. 行长度极不均匀矩阵：检验 CSR5 是否改善 load balance。
4. 图类矩阵/随机结构矩阵：检验 `x` 随机访问是否成为瓶颈。
5. 大矩阵：检验 HBM streaming 和多 PC 分配。
6. 小矩阵：检验预处理和 kernel launch/调度开销是否不划算。

建议输出表：

```markdown
| Matrix | m,n,nnz | avg nnz/row | nnz/row CV | Format | SpMV ms | PCG total ms | Effective GB/s | II | Fmax | Resource delta | Residual |
|---|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|
```

推荐接受标准：

- SpMV kernel 对目标矩阵平均提升至少 `1.2x`，最好 `1.5x+`。
- PCG end-to-end 至少提升 `1.1x`，不能只看单次 SpMV。
- 资源增加不超过 `20%~30%`，或者仍能放下多 CU/多 HBM PC。
- Fmax 不明显下降；如果 Fmax 降太多，CSR5 decoder/accumulator 可能得不偿失。
- residual / convergence iteration 数与原 CSR 基本一致。

---

## 10. 最终建议

### 建议做的

- 做一个 **CSR5-inspired experimental path**，不要替换现有 CSR。
- 重点验证：
  - 非零元均匀 tile 是否改善负载均衡；
  - tile 内转置是否改善 HBM burst/packet delivery；
  - metadata decoder 是否能保持 II=1；
  - partial sum 合并是否会成为新瓶颈。
- 同时参考 TileSpMV/Cuper，考虑二维 tile / sparse slice / HBM-compatible dataflow，而不是只照搬 CSR5。

### 暂时不要做的

- 不要一次性把整个 PCG solver 的矩阵格式都换掉。
- 不要在没有 profile 的情况下重构 accumulator。
- 不要为了复现论文而强行实现完整 `empty_offset`、复杂 segmented-sum、所有边界情况；先做最小正确版本。

### 一句话结论

**CSR5 值得作为 XPlus 的 SpMV 优化参考，但更像“过渡型/启发型方案”，不是 U55C/HBM FPGA PCG 的最终答案。建议 Claude Code 先做 profile + 小分支实验；如果主要瓶颈是 row load imbalance，再推进 CSR5-like；如果主要瓶颈是 HBM streaming、x reuse 或 RAW conflict，应优先转向 TileSpMV/Cuper 风格的 2D tiling / sparse slice / dataflow reordering。**
