# Jacobi-PCG 数学原理与 XRT 执行流程

这份文档说明 `Project-XPlus` 当前默认 XRT 路径里的 Jacobi 预条件共轭梯度算法，以及这些数学步骤如何落到 host、BO 和单顶层 `pcg_control_kernel` 上。

当前默认硬件路径已经把主循环控制放进 FPGA kernel：

```text
host:
  从 CSR 构造 4x4 block/bitmap SpMV 矩阵
  准备 block 矩阵 / b / x0 / r / z / p / ap / m_inv BO
  launch pcg_control_kernel 一次
  回读 x / metrics / status

kernel:
  init SpMV
  init r/z/p/rz/rr
  PCG 主循环
  alpha / beta
  rr<=tau 收敛判断
  breakdown 判断
  max_iter 判断
```

配套源码链路解析见：

- [hls_source_walkthrough_zh.md](hls_source_walkthrough_zh.md)
- [jacobi_pcg_xrt_flowchart.html](jacobi_pcg_xrt_flowchart.html)

---

## 1. 要解的问题

当前求解的是线性方程组：

```text
A x = b
```

其中：

1. `A` 原始输入是 CSR 稀疏矩阵：`row_ptr / col_idx / values`
2. `b` 是右端向量
3. `x0` 是初始解
4. `x` 是迭代得到的近似解

Jacobi-PCG 默认要求数据适合 CG，也就是实际矩阵应当满足对称正定这类条件。代码不在 host 侧完整证明 SPD，只检查维度、CSR 结构和 Jacobi 对角项是否可用。

---

## 2. Jacobi 预条件器

预条件矩阵取：

```text
M = diag(A)
```

算法里实际使用：

```text
z = M^{-1} r
```

因为 `M` 是对角矩阵，host 只需要提前构造一个向量：

```text
m_inv[i] = 1 / A[i, i]
```

kernel 内部每轮直接做：

```text
z[i] = m_inv[i] * r[i]
```

对应源码：

```text
host/xrt_host.cpp                 build_jacobi_inverse(...)
kernels/pcg_control_kernel.cpp    init_vectors / update_xrz_loop
```

---

## 3. PCG 数学递推

### 3.1 初始化

给定 `x0`，先算：

```text
ax0 = A * x0
r0  = b - ax0
z0  = M^{-1} r0
p0  = z0
rz0 = r0^T z0
rr0 = r0^T r0
```

`rr` 是残差范数平方：

```text
rr = ||r||_2^2
```

所以当前阈值判断是：

```text
rr <= tau
```

不是：

```text
sqrt(rr) <= tau
```

### 3.2 每轮迭代

第 `k` 轮开始时，已知：

```text
x_k, r_k, z_k, p_k, rz_k, rr_k
```

先做：

```text
ap_k  = A * p_k
pAp_k = p_k^T ap_k
```

步长：

```text
alpha_k = rz_k / pAp_k
```

更新：

```text
x_{k+1}  = x_k + alpha_k * p_k
r_{k+1}  = r_k - alpha_k * ap_k
z_{k+1}  = M^{-1} r_{k+1}
rz_{k+1} = r_{k+1}^T z_{k+1}
rr_{k+1} = r_{k+1}^T r_{k+1}
```

如果：

```text
rr_{k+1} <= tau
```

当前 `pcg_control_kernel` 会在这里直接退出，不再为了准备下一轮去算 `beta` 和 `p_{k+1}`。

否则继续：

```text
beta_k  = rz_{k+1} / rz_k
p_{k+1} = z_{k+1} + beta_k * p_k
```

---

## 4. 为什么初始化仍要先做一次 SpMV

初始化阶段的第一次 SpMV 是：

```text
ax0 = A * x0
```

它不是第 0 轮主循环里的：

```text
ap0 = A * p0
```

原因是 `p0` 还不存在。必须先有：

```text
r0 = b - A*x0
z0 = M^{-1} r0
p0 = z0
```

主循环才能开始。

如果某个数据集固定 `x0 = 0`，理论上 `A*x0 = 0`，可以专门优化掉初始化 SpMV。当前实现支持一般 `x0`，所以统一保留这一步。

---

## 5. 当前 XRT BO 映射

当前 host 先把 CSR 转成 4x4 block/bitmap，并进一步按 x-window 分组，然后给单顶层 kernel 分配必要 BO：

| 数学对象 / 状态 | XRT BO | kernel 端口 | 说明 |
| --- | --- | --- | --- |
| windowed block row pointer | `a_win_row_ptr_bo` | `a_win_row_ptr` | window-major 4x4 block row 指针，只读 |
| windowed block column index | `a_win_col_idx_bo` | `a_win_col_idx` | 每个 windowed 非零块的绝对 block column，只读 |
| windowed block payload | `a_win_blocks_bo` | `a_win_blocks` | 每个 4x4 块的 bitmap 和紧凑 values，只读 |
| `b` | `b_bo` | `b` | 右端向量，只读 |
| `M^{-1}` | `m_inv_bo` | `m_inv` | Jacobi 对角逆，只读 |
| `x0 / x_final` | `x_bo` | `x` | 输入初始解，kernel 原地写回最终解 |
| `r_k` | `r_bo` | `r` | HBM 常驻残差向量 |
| `z_k` | `z_bo` | `z` | HBM 常驻预条件残差向量 |
| `p_k` | `p_bo` | `p` | HBM 常驻搜索方向 |
| `ax0 / ap_k` | `ap_bo` | `ap` | HBM 常驻 SpMV 输出复用缓冲 |
| `[rz, rr, pAp, alpha]` | `metrics_bo` | `metrics` | kernel 结束后回写标量摘要 |
| `[status, iterations]` | `status_bo` | `status` | kernel 结束状态和实际迭代轮数 |

`x / r / z / p / ap` 都是 HBM 上的 XRT BO。`pcg_control_kernel` 不再声明整条向量的 BRAM 数组，因此向量长度不再受 `kMaxN` 片上数组限制。host 仍然不参与每轮 `alpha / beta / rr` 控制，也不再每轮 launch 多个 kernel。

---

## 6. Host/XRT 总执行流程

`host/xrt_host.cpp` 当前硬件路径：

```text
1. 解析命令行和默认路径
2. 读取 CSR 数据集
3. 跑 CPU golden，作为最终校验基准
4. host 构造 Jacobi 对角逆 m_inv
5. 打开 XRT device 并下载 xclbin
6. 创建 pcg_control_kernel
7. host 把 CSR 转成 a_win_row_ptr / a_win_col_idx / a_win_blocks
8. 分配 block 矩阵 / b / m_inv / x / r / z / p / ap / metrics / status BO
9. host->device 同步输入
10. launch pcg_control_kernel 一次
11. device->host 回读 metrics / status / x
12. CPU 侧重新计算残差并比较 golden
13. 输出终端摘要和 txt/json/html 报告
```

CPU golden 只用于最终比较，不参与 FPGA 的迭代状态更新。

---

## 7. `pcg_control_kernel` 内部执行流程

源码：

```text
kernels/pcg_control_kernel.cpp
```

内部主流程：

```text
init spmv:
  ap = A * x

init_vectors:
  r = b - ap
  z = m_inv * r
  p = z
  rz = r^T z
  rr = r^T r

pcg_loop:
  if rr <= tau:
    status = converged
    exit

  ap = A * p
  pAp = p^T ap

  if pAp breakdown or rz breakdown:
    status = breakdown
    exit

  alpha = rz / pAp

  x = x + alpha * p
  r = r - alpha * ap
  z = m_inv * r
  rz_new = r^T z
  rr_new = r^T r

  if rr_new <= tau:
    status = converged
    exit

  beta = rz_new / rz_old
  p = z + beta * p

store_x:
  x 已经在 HBM 上原地更新
  metrics = [rz, rr, pAp, alpha]
  status = [status_code, iterations]
```

状态码：

| status code | 含义 |
| --- | --- |
| `0` | converged |
| `1` | max_iter |
| `2` | breakdown 或非法输入 |

---

## 8. 主循环出口

当前主循环出口都在 FPGA kernel 内判断：

| 出口 | 判断位置 | 行为 |
| --- | --- | --- |
| 初始已收敛 | 每轮开头，第一次进入循环前也会检查 `rr <= tau` | 不做主循环迭代，直接写回状态 |
| 正常收敛 | `update_xrz_loop` 之后检查 `rr_new <= tau` | 立即退出，不再计算本轮 `beta/update_p` |
| breakdown | `rz`、`pAp`、`alpha`、`beta` 出现 NaN 或接近 0 | 写 `status=2` 并退出 |
| 达到上限 | `iteration == max_iters` | 写 `status=1` |

这和原 host 控制版本不同：原版本在 `update_xrz` 后还会计算一次 `beta/update_p`，下一轮循环头才发现 `rr <= tau`。现在收敛可以在 kernel 内及时退出。

---

## 9. 全流程中可以复用的量

跨全流程复用：

| 量 | 复用方式 |
| --- | --- |
| `a_win_row_ptr / a_win_col_idx / a_win_blocks` | 按 x-window 分组的 block/bitmap 矩阵全程只读复用 |
| `x_bo` | HBM 上原地保存当前解和最终解 |
| `r_bo / z_bo / p_bo / ap_bo` | HBM 上保存 PCG 主循环向量状态 |
| `b` | 初始化和最终验证的数学对象，kernel 内只读 |
| `m_inv` | Jacobi 对角逆全程只读复用 |
| `metrics_bo` | 输出最终 `rz / rr / pAp / alpha` 摘要 |
| `status_bo` | 输出 `status_code / iterations` |

kernel 内只保留小标量和 SpMV 片上缓存，例如 `rz / rr / pAp / alpha / beta`、
`x_window_ping/pong` 以及 row tile 的 `y_tile`。标量通常综合成寄存器；
`x_window` 和 `y_tile` 已用 `BIND_STORAGE` 明确约束为 BRAM。

---

## 10. 和原 5-kernel 版本的关系

源码里仍保留：

```text
kernels/spmv_csr_kernel.cpp
kernels/init_pcg_kernel.cpp
kernels/dot_kernel.cpp
kernels/update_xrz_kernel.cpp
kernels/update_p_kernel.cpp
kernels/cg_kernels.cpp
host/multi_kernel_solver.hpp
```

它们的作用是：

1. 作为本地多-kernel 基线
2. 帮助单独验证每个数学阶段
3. 作为 `pcg_control_kernel` 内部流程的拆分参考

默认 `Makefile` 当前只把 `pcg_control_kernel.cpp` 编译/link 到 XRT xclbin。
