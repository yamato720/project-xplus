# CuperPcg 数学原理说明

本文说明 full `CuperPcg(...)` 里 Jacobi-PCG 的数学递推，以及这些数学量如何映射
到 Cuper/TAPA 当前实现。SpMV 的硬件数据流见 `pcg_spmv.md`；本文重点放在
`为什么要算这些量` 和 `每个阶段数学上代表什么`。

## 1. 要解的问题

PCG 求解线性方程组：

$$
A x = b
$$

其中：

- `A` 是稀疏矩阵；
- `b` 是右端向量；
- `x0` 是初始解；
- `x` 是迭代得到的近似解。

Conjugate Gradient 要求有效问题接近对称正定矩阵场景。代码不在硬件里证明
`A` 一定 SPD，而是按输入数据集和 host 预处理约定使用 PCG。若矩阵不适合 CG，
可能表现为不收敛、`p^T A p` 太小、NaN 或 breakdown。

## 2. Jacobi 预条件器

当前使用 Jacobi 预条件：

$$
M = \operatorname{diag}(A)
$$

实际硬件不保存矩阵 `M`，而是保存对角逆向量：

$$
M_{\mathrm{inv}}[i] = \frac{1}{A_{i,i}}
$$

每次需要预条件残差时直接逐元素计算：

$$
z_i = M_{\mathrm{inv}}[i] \cdot r_i
$$

数学上，预条件器的作用是把原问题换成更容易迭代的等价方向选择问题。它不改变
最终要求的 `A x = b`，但会改变搜索方向 `p` 的生成方式。没有预条件时可以近似看成
`M = I`，于是 `z = r`；Jacobi-PCG 则用 `z = M^{-1}r` 参与内积和方向更新。

## 3. 残差、预条件残差和搜索方向

三个核心向量含义如下：

| 数学量 | 含义 | CuperPcg 主状态 |
| --- | --- | --- |
| `r_k` | 当前残差，$b - A x_k$ | `R` |
| `z_k` | 预条件残差，$M^{-1} r_k$ | `Z` |
| `p_k` | 共轭搜索方向 | `P`，另有 SpMV 输入副本 `P_spmv` |

`r` 衡量当前解错了多少；`z` 是用 Jacobi 对残差做缩放后的版本；`p` 是下一次沿着
哪个方向前进。CG 的关键不只是沿负梯度方向走，而是构造一组关于 `A` 共轭的方向，
让每轮更新尽量不破坏前面已经优化过的方向。

## 4. 初始化为什么先做 `A*x0`

PCG 主循环开始前必须知道初始残差：

$$
r_0 = b - A x_0
$$

所以初始化的第一步是一次 SpMV：

$$
ax_0 = A x_0
$$

之后才能得到：

$$
\begin{aligned}
r_0  &= b - ax_0 \\
z_0  &= M^{-1} r_0 \\
p_0  &= z_0 \\
rz_0 &= r_0^T z_0 \\
rr_0 &= r_0^T r_0
\end{aligned}
$$

这里的 $A x_0$ 不是第 0 轮迭代里的 $A p_0$。因为 `p0` 要等 `r0/z0` 生成后才存在。
即使某些测试里 `x0 = 0`，当前实现也按一般情况保留初始化 SpMV，避免把算法正确性
绑到特殊输入上。

## 5. 每轮 PCG 递推

第 `k` 轮开始时，已知：

$$
x_k,\ r_k,\ z_k,\ p_k,\ rz_k,\ rr_k
$$

先做一次 SpMV：

$$
ap_k = A p_k
$$

再计算方向上的曲率：

$$
pAp_k = p_k^T ap_k = p_k^T A p_k
$$

步长为：

$$
\alpha_k = \frac{rz_k}{pAp_k}
$$

然后更新解和残差：

$$
\begin{aligned}
x_{k+1} &= x_k + \alpha_k p_k \\
r_{k+1} &= r_k - \alpha_k ap_k
\end{aligned}
$$

接着重新应用 Jacobi 预条件：

$$
\begin{aligned}
z_{k+1}  &= M^{-1} r_{k+1} \\
rz_{k+1} &= r_{k+1}^T z_{k+1} \\
rr_{k+1} &= r_{k+1}^T r_{k+1}
\end{aligned}
$$

若仍未收敛，则计算：

$$
\begin{aligned}
\beta_k  &= \frac{rz_{k+1}}{rz_k} \\
p_{k+1} &= z_{k+1} + \beta_k p_k
\end{aligned}
$$

`alpha` 决定沿当前方向走多远；`beta` 决定新方向里保留多少旧方向成分。用
$rz = r^T z$ 而不是只用 $rr = r^T r$，是预条件 CG 的标准递推形式。

## 6. 收敛判断和 breakdown

当前硬件记录两个标量：

$$
rr = r^T r,\qquad rz = r^T z
$$

收敛判断使用：

$$
rr \le \mathrm{Tau}
$$

这里的 `rr` 是残差二范数的平方，不是 `sqrt(rr)`。因此 `Tau` 的含义是
$\lVert r \rVert_2^2$ 阈值。

breakdown 主要来自这些情况：

- `pAp` 太小或 NaN；
- `rz` 太小或 NaN；
- `alpha / beta` 生成 NaN；
- `update_z` 归约产生 NaN；
- 输入参数非法，例如 `Row_num <= 0`、`Tau <= 0`。

数学上，理想 SPD 场景里 $p^T A p$ 应该为正。若它接近 0 或变成 NaN，继续做
$\alpha = rz / pAp$ 会污染整条向量，所以实现会设置 breakdown 状态并退出。

## 7. CuperPcg 阶段和公式对应

| 阶段 | 数学公式 | 主要实现位置 |
| --- | --- | --- |
| `init_spmv` | $ax_0 = A x_0$，$r_0 = b - ax_0$ | `Pcg_Controller::init_spmv_stream` |
| `init_zp` | $z_0 = M^{-1} r_0$，$p_0 = z_0$，`rz0/rr0` | `Pcg_Controller::init_zp_reduce` |
| `iter_spmv_recv_dot` | $ap = A p$，$pAp = p^T ap$ | `Pcg_Controller::iter_spmv_stream` |
| `update_xr` | $x = x + \alpha p$，$r = r - \alpha ap$ | `Pcg_Controller::update_xr` |
| `update_z` | $z = M^{-1} r$，新 `rz/rr` | `Pcg_UpdateZ_*_Service` |
| `update_p` | $p = z + \beta p$，同步 `P_spmv` | `Pcg_UpdateP_*_Service` |

注意 `iter_spmv_recv_dot` 不是纯 $A p$。当前实现把 $p^T ap$ 融进 AP 接收路径，
所以这个 stage 同时包含 SpMV 输出接收、`AP_spmv` 写入和 FP64 dot 累加。

## 8. 数学量和 HBM 缓冲对应

| 数学量 | HBM / stream 表示 | 说明 |
| --- | --- | --- |
| `b` | `B` | FP64 packed，右端项。 |
| `M^{-1}` | `M_inv` | FP64 packed，Jacobi 对角逆。 |
| `x` | `X` | FP64 packed，输入 `x0`，输出最终解。 |
| `r` | `R` | FP64 packed，残差。 |
| `z` | `Z` | FP64 packed，预条件残差。 |
| `p` | `P` | FP64 packed，权威搜索方向。 |
| `x0` 的 SpMV 输入 | `X_spmv` | FP32 packed，只给 $A x_0$ 的 vector loader。 |
| `p` 的 SpMV 输入 | `P_spmv` | FP32 packed，`P` 的 Cuper 输入副本。 |
| $ap = A p$ | `Pcg_Spmv_Stream` / `AP_spmv` | SpMV 输出流，迭代中缓存为 FP32 packed。 |

主状态用 `double_v8` 是为了 PCG 递推、残差和归约尽量保留 FP64 精度；SpMV 输入输出
用 `float_v16` 是为了复用 Cuper 的 packed SpMV 数据通路。两者之间的转换点主要是：

- `init_spmv_stream`：$A x_0$ 从 FP32 SpMV 输出转成 FP64 残差 `R`；
- `init_zp`：FP64 `P` 同步打包成 FP32 `P_spmv`；
- `iter_spmv_stream`：FP32 $A p$ 缓存到 `AP_spmv`，同时参与 FP64 `pAp`；
- `update_p`：FP64 新 `P` 同步打包成下一轮 FP32 `P_spmv`。

## 9. 为什么 SpMV 是 PCG 的核心但不是全部瓶颈

从数学上看，每轮 PCG 必须至少做一次 $A p$，初始化还要做一次 $A x_0$。这使 SpMV
天然处在算法核心位置。

但 full `CuperPcg(...)` 的一轮迭代还包含大量非 SpMV 工作：

- $p^T A p$；
- `x/r` 更新；
- $M^{-1}r$；
- `rz/rr` reduction；
- `p` 更新；
- FP64 主状态和 FP32 SpMV 副本同步。

因此评估优化时不能只看 standalone SpMV。若 $A p$ 更快，但 `update_xr`、
`update_z`、`update_p` 或 reduction 没有下降，`kernel_reported` 仍可能不变。
这也是当前 full-PCG 优化要同时关注 controller/vector 阶段的原因。

## 10. 一个最小例子

假设 `x0 = 0`，则：

$$
\begin{aligned}
ax_0 &= 0 \\
r_0  &= b \\
z_0  &= M^{-1} b \\
p_0  &= z_0
\end{aligned}
$$

第一轮：

$$
\begin{aligned}
ap_0     &= A p_0 \\
pAp_0    &= p_0^T ap_0 \\
\alpha_0 &= \frac{rz_0}{pAp_0} \\
x_1      &= \alpha_0 p_0 \\
r_1      &= r_0 - \alpha_0 ap_0 \\
z_1      &= M^{-1} r_1 \\
\beta_0  &= \frac{r_1^T z_1}{r_0^T z_0} \\
p_1      &= z_1 + \beta_0 p_0
\end{aligned}
$$

从第二轮开始，`p1` 会经 `P_spmv` 喂给 Cuper SpMV，计算 $A p_1$。这就是
full `CuperPcg(...)` 中 “`update_p` 同步 `P_spmv` -> 下一轮 `iter_spmv` 读取
`P_spmv`” 的数学原因。
