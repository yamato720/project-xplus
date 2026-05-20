# Project-XPlus 滑动窗口 SpMV 与 dataflow 实验记录

这份文档记录 `pcg_control_kernel` 里 SpMV 从“直接读 HBM 上的整条 x 向量”切到“滑动窗口缓存 x”的实验结果。实验数据集是：

```text
data/suitesparse/Nasa/csr/nasa2910
```

---

## 1. 这次要解决的问题

原来的单 kernel PCG 路径已经把 `x / r / z / p / ap` 放在 HBM 上，取消了早期 `x_local[kMaxN]` 这种整条向量片上缓存。因此 `nasa2910` 这类 `n > 1024` 的数据集已经不会被 `kMaxN=1024` 卡住。

但 SpMV 里还有一个效率问题：

```text
y = A * x
```

如果每个非零块都直接按列号随机读 HBM 上的 `x[col]`，规模变大后会出现：

1. HBM 随机读多
2. 访存局部性差
3. SpMV 内层流水线更容易被访存和累加依赖限制

滑动窗口的目标不是再把完整 `x` 放上片，而是每次只缓存一段 `x`：

```text
x_window = x[window_begin : window_end)
```

这样单个窗口占用固定 BRAM，矩阵/向量总长度可以继续增长。

---

## 2. 当前实现位置

主要代码在：

```text
include/cg_common.hpp
kernels/pcg_control_kernel.cpp
```

关键常量：

```cpp
static constexpr int kSpmvWindowSize = 512;
static constexpr int kSpmvBlockSize = 4;
static constexpr int kSpmvRowTileBlockRows = 512;
```

`kSpmvWindowSize=512` 的含义是：每次只把 512 个 `double` 缓存到片上。一个窗口约 4 KiB；当前使用 ping-pong 双缓冲，所以窗口缓存约 8 KiB，不随完整矩阵维度增长。

`512` 也是 `4x4 block` 的整数倍，所以一个 block column 不会跨两个窗口。

`kSpmvRowTileBlockRows=512` 的含义是：每次在片上保留 512 个
4-row block row 的 `y/ap` 部分和，也就是最多 2048 个 `double`。
这个 `y_tile` 约 16 KiB，跨所有 x-window 累加完以后再写回 HBM。

---

## 3. 当前滑动窗口算法

当前 host 会把 CSR 矩阵转换成 4x4 block/bitmap，并进一步按 x-window 分组：

```text
a_win_row_ptr
a_win_col_idx
a_win_blocks
```

`a_win_row_ptr` 是 window-major row pointer。对第 `w` 个 x-window、第 `br`
个 block row：

```text
base = w * (num_block_rows + 1)
a_win_row_ptr[base + br] : a_win_row_ptr[base + br + 1]
```

就是 `A[:, W_w]` 在这个 block row 里的非零 4x4 block 子流。

kernel 内的 SpMV 逻辑变成二维分块：

```text
for each row tile:
  zero y_tile in BRAM

  load x window 0 into ping buffer

  for each window except the last:
    compute current window contribution for this row tile:
      use a_win_row_ptr to read this window/row-tile's block ranges directly
      scan only blocks already grouped into current x window
      accumulate partial sums into y_tile

    prefetch next x window into the other buffer

  compute last window contribution into y_tile
  write y_tile back to HBM y/ap
```

也就是：

```text
y = sum_over_windows A[:, window] * x[window]
```

每个窗口只需要片上保存当前窗口的 `x`，不会再声明 `x_local[n]`。

当前优化版也不再先单独执行 `zero_vector(y,n)`。每个 row tile 开始时
只清零 BRAM 中的 `y_tile`；后续所有 x-window 的贡献都累到这个
`y_tile`，直到该 row tile 处理完才写回 HBM。这样每个输出元素在一次
SpMV 中只需要最后写一次 HBM，不再随 x-window 数量反复
read/modify/write。

另外，A 也已经按 x-window 分组。每个原始 4x4 block 只属于一个
window，host 会把它写入对应 window 的连续 block 流。kernel 处理
当前 window 时直接用 `a_win_row_ptr` 取范围，不再在原始 block row
里做 `lower_bound`，也不再访问不属于当前 window 的 block metadata。

---

## 4. ping-pong 双缓冲和 dataflow

代码里有两个 BRAM 缓冲：

```cpp
data_t x_window_ping[kSpmvWindowSize];
data_t x_window_pong[kSpmvWindowSize];
```

并用 `BIND_STORAGE` 明确要求它们综合为 BRAM：

```cpp
#pragma HLS BIND_STORAGE variable = x_window_ping type = ram_2p impl = bram
#pragma HLS BIND_STORAGE variable = x_window_pong type = ram_2p impl = bram
```

窗口循环里使用两个无条件 helper：

```text
process_ping_tile_and_prefetch_pong
process_pong_tile_and_prefetch_ping
```

每个 helper 内部放 `#pragma HLS DATAFLOW`，固定并行两个任务：

```text
accumulate current window into current row tile
load next x window
```

这里不能把 `if (ping/pong)` 和 `if (has_next_window)` 直接写进 `DATAFLOW` 区域。Vitis HLS 2022.2 会报：

```text
conditional execution ... is not supported
```

因此条件选择必须放在 dataflow helper 外面，dataflow helper 内部保持固定任务图。

---

## 5. 为什么 PCG 主循环不能整体 dataflow

PCG 每轮有严格标量依赖：

```text
ap = A*p
pAp = p^T ap
alpha = rz / pAp
x = x + alpha*p
r = r - alpha*ap
z = M^{-1}r
rz_new = r^T z
rr_new = r^T r
beta = rz_new / rz_old
p = z + beta*p
```

其中：

1. `alpha` 必须等完整 `pAp` 归约结束
2. `x/r/z` 更新必须等 `alpha`
3. `beta` 必须等完整 `rz_new` 归约结束
4. 下一轮 `p` 必须等 `beta`
5. 收敛出口 `rr_new <= tau` 必须在 `update_xrz` 后才能准确判断

所以不能把完整 PCG 主循环简单写成一个无停顿 dataflow 流水线。可以做的是：

1. SpMV 内部做 load/compute 局部 dataflow
2. 向量更新和归约循环各自 pipeline
3. 后续把 SpMV 改成更适合 streaming 的矩阵格式

---

## 6. nasa2910 软件仿真结果

基线版本是窗口改造前的 4x4 block/bitmap + HBM 向量版本。表里的窗口版本是早期双缓冲滑动窗口实现。
当前代码又进一步把 A 按 window 分组，并加入 row tile 的片上 `y_tile` 部分和缓存；
下面表格尚未重新生成 sw_emu 报告。

| 项 | 基线 HBM x 版本 | 滑动窗口版本 |
| --- | ---: | ---: |
| 数据集 | `nasa2910` | `nasa2910` |
| `n / nnz` | `2910 / 174296` | `2910 / 174296` |
| 4x4 非零块数 | `20508` | `20508` |
| 迭代轮数 | `1653` | `1653` |
| final `rr` | `9.707556191561e-11` | `9.707556191561e-11` |
| final residual | `9.854222584519e-06` | `9.854222584519e-06` |
| max abs diff vs CPU | `3.106584656404e-11` | `3.106584656404e-11` |
| `sw_emu` kernel time | `798.844 ms` | `1485.492 ms` |

结论：

1. 滑动窗口版本数值正确，收敛轮数和最终误差与基线一致
2. 对 `nasa2910`，尺寸问题已经解决
3. 软件仿真时间变长不能直接代表硬件性能；当前 row tile 版本需要后续再用 sw_emu 或 HLS report 评估

---

## 7. 当前版本的局限

当前矩阵已经从单一 block-row 存储改成 window-major block-row 存储：

```text
window -> block row -> blocks inside this window and row
```

滑动窗口按列窗口工作：

```text
column window -> blocks inside this window
```

row tile 按输出行工作：

```text
row tile -> y_tile partial sums in BRAM
```

这三个顺序现在组合成：

```text
row tile -> column window -> block rows inside this tile/window
```

kernel 处理一个 row tile 时，会依次扫过所有 x-window，只顺序读
`A[row_tile, W_w]` 对应的 block 子流。`y_tile` 在 BRAM 中跨所有
window 保留，累完后再写回 HBM。

当前还没有解决的是：每个 row tile 都会重新加载所有 x-window；如果
row tile 很小，x-window 的重复加载会增加。`y_tile` 也是 BRAM 上的
read-modify-write 归约目标，同一行连续多个 block 累加时存在真实数据依赖。

---

## 8. 当前二维分块格式

当前 host 已经生成按列窗口分组的 metadata：

```text
a_win_row_ptr[window][block_row]
a_win_col_idx[entry]
a_win_blocks[entry]
```

kernel 就可以变成：

```text
for each row tile:
  zero y_tile in BRAM
  for each x window:
    load x_window
    for block rows inside this row tile:
      use a_win_row_ptr[window][block_row]
      scan entries in this tile/window row only
      accumulate into y_tile
  write y_tile back to HBM
```

每次只保留：

1. 一个 `x` 列窗口
2. 一个 `y` 行 tile 的部分和
3. 当前 tile/window 的非零块流

这才更接近超大矩阵场景下可持续扩展的 SpMV 数据流。
