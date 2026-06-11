# Jacobi 迭代算法说明

本文说的是求解线性方程组

$$
A x = b
$$

的经典 Jacobi iteration，不是 Jacobi preconditioner PCG。两者都会用到矩阵对角线
`D`，但算法位置不同：

- Jacobi iteration 是一个完整迭代求解器，每轮直接生成新的 `x`。
- Jacobi preconditioner PCG 只把 `D^{-1}` 当作预条件器，用在 PCG 的残差修正里。

## 1. 矩阵拆分

把矩阵 `A` 拆成对角部分和非对角部分：

$$
A = D + R
$$

其中：

- `D` 只包含 `A` 的对角线，`D[i, i] = A[i, i]`。
- `R = A - D`，包含所有非对角元素。

原方程可写成：

$$
D x = b - R x
$$

如果 `D[i, i]` 都非零，就可以按行除以对角线，得到 Jacobi 更新式：

$$
x_i^{(k+1)}
= \frac{b_i - \sum_{j \ne i} A_{ij} x_j^{(k)}}{A_{ii}}
$$

等价的向量形式是：

$$
x^{(k+1)} = D^{-1}(b - R x^{(k)})
$$

## 2. 和 SpMV 的关系

当前 `CuperJacobiIteration` 目录已经保留了 service 化 single SpMV。host 侧先把
矩阵拆成：

$$
A = D + R
$$

当前 demo 又把减号前移到 vector loader：kernel 读 `X0/X1` 时先输出 `-x_old`，
因此 Cuper service 直接得到非对角乘积的相反数：

$$
\mathrm{neg\_offdiag}^{(k)} = R(-x^{(k)}) = -R x^{(k)}
$$

这个做法让 update stage 少读一条 `Diag` 向量，也不再做 `diag[i] * x_old[i]`。
同时 update stage 里不再显式做 `b - Rx` 的减法，而是做 `b + (-Rx)`。
对角项仍在 host 侧扫描 CSR 时用于构造 `diag_inv[i] = 1 / A[i, i]`。

## 3. 一轮 Jacobi 的数据流

按当前 service SpMV 边界，最小硬件数据流可以写成：

1. 输入旧解向量 $x^{(k)}$，vector loader 在送入 Cuper Core 前取负。
2. service SpMV 计算：

$$
\mathrm{neg\_rx}^{(k)} = R(-x^{(k)}) = -R x^{(k)}
$$

3. vector update 计算：

$$
x_i^{(k+1)}
= \left(b_i + \mathrm{neg\_rx}_i^{(k)}\right)\mathrm{diag\_inv}_i
$$

4. convergence check 计算：

$$
\mathrm{diff\_max} = \max_i |x_i^{(k+1)} - x_i^{(k)}|
$$

5. 如果不收敛，则交换或复制 $x^{(k)} \leftarrow x^{(k+1)}$。

其中 `diag_inv[i] = 1 / A[i, i]`。kernel 只消费 `Diag_inv`，不再需要 `Diag`。

## 4. 收敛条件

Jacobi iteration 常见停止条件有两类：

$$
\max_i |x_i^{(k+1)} - x_i^{(k)}| < \tau
$$

或

$$
\|b - A x^{(k+1)}\| < \tau
$$

第一种便宜，只需要比较新旧解向量；第二种更接近真实残差，但每轮要多一次
`A*x_next`，或者把残差计算安排在下一轮 SpMV 之后。

对这个 TAPA 实验目录，建议先用第一种：

$$
\mathrm{diff\_max} = \max_i |x_i^{(k+1)} - x_i^{(k)}|
$$

当

$$
\mathrm{diff\_max} \le \tau
$$

时停止迭代。

它的硬件代价是一个向量 update 加一个 max reduction，比 PCG 的
`dot/update_p/update_rz` 路径简单得多。

## 5. 收敛前提和风险

Jacobi iteration 不像 PCG 那样适合所有 SPD 矩阵。它通常要求矩阵满足足够强的
收敛条件，例如严格对角占优：

$$
|A_{ii}| > \sum_{j \ne i} |A_{ij}|
$$

或者迭代矩阵

$$
B = -D^{-1}R
$$

的谱半径小于 1：

$$
\rho(B) < 1
$$

如果条件不满足，Jacobi 可能很慢，甚至发散。工程上要至少处理：

- `A[i, i] == 0`：不能做 Jacobi 除法，必须报错或换算法。
- `diag_inv` 过大：可能导致数值爆炸。
- `diff_max` 变成 NaN/Inf：应返回 breakdown 状态。
- `MAX_ITERS` 达到上限：正常返回 max-iter，而不是卡住。

## 6. 和 Jacobi-PCG 的区别

Jacobi preconditioner PCG 的核心变量是：

$$
r = b - A x
$$

$$
z = D^{-1}r
$$

以及 `p`、`alpha`、`beta`。

它每轮用一次 `A*p`，再通过 dot product 和向量更新推进共轭方向。

Jacobi iteration 没有 `p`、`alpha`、`beta`，也没有共轭方向。它只反复做：

$$
x^{(k+1)} = D^{-1}(b - R x^{(k)})
$$

所以如果后续把当前 `CuperJacobiIteration` 顶层扩展成真正 Jacobi kernel，新增的
主要端口应是：

- `B`：右端项 `b`
- `Diag_inv`
- `X_old` / `X_next`，或一个双缓冲 `X`
- `Status`
- 可选 `Metrics`

不需要引入 PCG 的 `R/Z/P/AP_spmv` 全套状态。

## 7. 当前目录里的 demo 实现

当前 `detail/cuper_jacobi_top_graphs.hpp` 已经按 host 拆 `A = D + R` 的方式做了 demo：

1. `Jacobi_Controller` 每轮发送 SpMV command，并向 update stage 发送本轮 frame。
2. `Jacobi_Vector_Loader` 根据 `X0/X1` 标识读取旧解，并把送入 Cuper Core 的值取负。
3. Cuper service 计算 host 打包后的 `-R*x_old`。
4. `Jacobi_Update_Service` 消费 `-R*x_old`，同时读取 `B/Diag_inv/x_old`，计算
   `x_next` 和 `diff_max`。
5. controller 根据 `diff_max <= tau` 或 `iter == max_iters` 决定继续迭代还是发送 stop。

当前 demo 不在 Cuper core 里识别对角项；对角拆分发生在 host 侧 CSR 预处理阶段。
这样 update stage 可以少做 `diag*x_old` 乘法，也少读一条 `Diag` HBM 向量；减号则由
vector loader 的取负动作吸收。
