# 两次迭代数据通路

本文只解释当前 `CuperPcg(...)` full-PCG TAPA 版的数据如何在 host、HBM、
TAPA stream 和 controller 阶段之间传递。例子假设 `Max_iters=2`，也就是完成
初始化、第一次 PCG 迭代和第二次 PCG 迭代后退出或继续检查收敛。

相关源码：

```text
DLC/Cuper/include/Cuper.h
DLC/Cuper/kernels/detail/cuper_top_graphs.hpp
DLC/Cuper/kernels/detail/pcg_common.hpp
DLC/Cuper/kernels/detail/pcg_spmv_service.hpp
DLC/Cuper/kernels/detail/pcg_controller.hpp
host/cuper_tapa_pcg_fpga_main.cpp
cfg/connectivity_cuper_tapa_pcg_u55c.cfg
```

## 0. Host 侧进入 kernel 前

host 先准备两类数据。

第一类是 Cuper 原本的 SpMV 矩阵格式：

| 数据 | HBM | 类型 | 说明 |
| --- | --- | --- | --- |
| `SpElement_list_ptr` | HBM[0] | `INDEX_TYPE` | Cuper batch/slice 边界表 |
| `Matrix_data[0..15]` | HBM[0..15] | `ap_uint<512>` | 16 路 HBM matrix beat，每路一个 matrix loader |

第二类是 FPGA 内 PCG 状态和 SpMV 辅助副本：

| 数据 | HBM | 类型 | 初始状态 |
| --- | --- | --- | --- |
| `B` | HBM[16] | `double` | 右端项 `b` |
| `M_inv` | HBM[17] | `double` | Jacobi 对角逆 |
| `X` | HBM[18] | `double` | 初始解 `x0`，kernel 内更新为最终解 |
| `R` | HBM[19] | `double` | 置零，kernel 内写残差 |
| `Z` | HBM[20] | `double` | 置零，kernel 内写预条件残差 |
| `P` | HBM[21] | `double` | 置零，kernel 内写搜索方向 |
| `AP_spmv` | HBM[22] | `float_v16` | 置零，缓存最近一次 `A*p` |
| `X_spmv` | HBM[24] | `float_v16` | host 把 `X(double)` 降成 packed FP32 |
| `P_spmv` | HBM[25] | `float_v16` | 置零，kernel 内维护当前 `p` 的 FP32 副本 |
| `Metrics` | HBM[26] | `double` | kernel 写阶段计时/调试值 |
| `Status` | HBM[26] | `INDEX_TYPE` | kernel 写状态码和完成迭代数 |

这里的混精边界是：

```text
PCG 主状态：B/M_inv/X/R/Z/P 是 FP64 double
Cuper SpMV：矩阵值、输入向量和输出向量是 FP32 float_v16
```

因此 host 会先把 `X` 打成 `X_spmv`。矩阵值也在 host 侧按 `VALUE_TYPE=float`
写入 Cuper 矩阵格式。

## 1. 顶层 task graph

`CuperPcg(...)` 只 launch 一次。kernel 内部有一套常驻 SpMV service 和一个
PCG controller：

```text
Pcg_Controller
  -> Command_Stream[0]          -> Pcg_SpElement_list_ptr_Loader
  -> Command_Stream[1]          -> Pcg_Vector_Loader
  -> Matrix_Command_Stream[0..15] -> Pcg_Matrix_Loader[0..15]

Pcg_SpElement_list_ptr_Loader -> PE_Param[0]
Pcg_Vector_Loader             -> Vector_X_Stream[0]
Pcg_Matrix_Loader[i]          -> Matrix_A_Stream[i]

PE_Param / Vector_X_Stream 串过 Pcg_Core[0..15]
Matrix_A_Stream[0..15] 分别进入 Pcg_Core[0..15]

Pcg_Core[0..15]
  -> Matrix_Mult_Vector_Stream[0..15]
  -> Pcg_Accumulator[0..15]
  -> Pcg_Vector_Checker[0..7]
  -> Pcg_Mult_Sort_Tree
  -> Pcg_Spmv_Stream
  -> Pcg_Controller
```

`PE_Param[0..16]` 和 `Vector_X_Stream[0..16]` 是串接转发链；`Matrix_data[0..15]`
和 `Matrix_A_Stream[0..15]` 才是 16 路 HBM 并行矩阵输入。

controller 每需要一次 SpMV，就广播一条 `CuperSpmvCommand`：

| command | 向量来源 | 表示的计算 |
| --- | --- | --- |
| `vector_source = X` | `X_spmv` | 初始化 `A*x0` |
| `vector_source = P` | `P_spmv` | 每轮迭代的 `A*p` |
| `stop = 1` | 不读向量 | 所有常驻 service 退出 |

## 2. 初始化：计算 `A*x0`

初始化不是 PCG 迭代的一部分，但它会先跑一次共用 SpMV service。

controller 发命令：

```text
pcg_send_spmv_command(..., kPcgVectorSourceX)
```

数据通路是：

```text
X_spmv(HBM[24], FP32 packed)
  -> Pcg_Vector_Loader
  -> Vector_X_Stream[0]
  -> Pcg_Core[0] -> ... -> Pcg_Core[15]
  -> Vector_X_Stream[16] -> Pcg_Destroy_float_v16

Matrix_data[0..15](HBM[0..15])
  -> Pcg_Matrix_Loader[0..15]
  -> Matrix_A_Stream[0..15]
  -> Pcg_Core[0..15]

SpElement_list_ptr(HBM[0])
  -> Pcg_SpElement_list_ptr_Loader
  -> PE_Param[0]
  -> Pcg_Core[0] -> ... -> Pcg_Core[15]
  -> PE_Param[16] -> Pcg_Destroy_int

Pcg_Core 局部乘积
  -> Pcg_Accumulator
  -> Pcg_Vector_Checker
  -> Pcg_Mult_Sort_Tree
  -> Pcg_Spmv_Stream
  -> Pcg_Controller
```

controller 从 `Pcg_Spmv_Stream` 收到一包包 `float_v16`，这时语义是
`A*x0`。它不会把 `A*x0` 写成单独的 HBM 向量，而是立刻生成初始残差：

```text
ap_packet[lane] = FP32 A*x0
ap_value        = static_cast<double>(ap_packet[lane])
r0              = B[index] - ap_value
R[index]        = r0
```

这一段的结果：

```text
R = b - A*x0       写 HBM[19]，FP64
X 仍是 x0          在 HBM[18]，FP64
X_spmv 只读不改    在 HBM[24]，FP32 packed
```

## 3. 初始化：生成 `z0/p0/P_spmv0`

随后 controller 进入 `init_zp_reduce`。这一段不走 SpMV service，只走
controller 自己的 HBM 读写：

```text
R(HBM[19], FP64) + M_inv(HBM[17], FP64)
  -> z0 = M_inv * r0
  -> Z(HBM[20], FP64)
  -> P(HBM[21], FP64)
  -> P_spmv(HBM[25], FP32 packed)

rz0 = r0^T z0
rr0 = r0^T r0
```

这里第一次发生 kernel 内的 FP64 -> FP32：

```text
P[index]          = z0              // FP64 权威搜索方向
P_spmv[packet][lane] = float(z0)    // 下一轮 Cuper SpMV 的 packed 输入
```

初始化结束后，状态可以记成：

```text
X0       = x0
R0       = b - A*x0
Z0       = M_inv * R0
P0       = Z0
P_spmv0  = float(P0) packed
rz0      = R0^T Z0
rr0      = R0^T R0
```

## 4. 第 1 次 PCG 迭代：计算 `AP0 = A*p0`

进入 `pcg_loop` 的 `iter=0`。

controller 发命令：

```text
pcg_send_spmv_command(..., kPcgVectorSourceP)
```

这次共用 SpMV service 的数据通路和初始化相同，区别只有向量来源：

```text
P_spmv0(HBM[25], FP32 packed)
  -> Pcg_Vector_Loader
  -> Vector_X_Stream[0..16]
  -> Pcg_Core[0..15]

Matrix_data[0..15] + SpElement_list_ptr
  -> 同一套 Cuper SpMV service

Pcg_Mult_Sort_Tree
  -> Pcg_Spmv_Stream
  -> Pcg_Controller
```

controller 收到的 `float_v16` 现在语义是 `AP0 = A*p0`。当前实现把它先缓存到
HBM：

```text
AP_spmv = AP0 packed FP32    写 HBM[22]
```

注意：这里没有直接把 `AP0` 接到 `dot_p_ap/update_xr`。当前路径是：

```text
Pcg_Spmv_Stream -> controller -> AP_spmv(HBM) -> controller 后续阶段再读
```

这也是目前 full-PCG 里值得继续优化的一个数据通路点。

## 5. 第 1 次 PCG 迭代：计算 `alpha0`

`dot_p_ap` 读两个来源：

```text
P0(HBM[21], FP64)
AP_spmv0(HBM[22], FP32 packed)
```

每个 lane 会做 FP32 -> FP64：

```text
ap_value = static_cast<double>(AP_spmv[packet][lane])
p_ap0   += P0[index] * ap_value
```

然后：

```text
alpha0 = rz0 / p_ap0
```

这一段只更新 controller 内部标量 `p_ap` 和 `alpha`，不会改变向量 HBM。

## 6. 第 1 次 PCG 迭代：更新 `X1/R1`

`update_xr` 读：

```text
X0(HBM[18], FP64)
P0(HBM[21], FP64)
R0(HBM[19], FP64)
AP_spmv0(HBM[22], FP32 packed)
alpha0(controller 标量)
```

每个元素执行：

```text
ap0 = double(AP_spmv0[index])
X1  = X0 + alpha0 * P0
R1  = R0 - alpha0 * ap0
```

写回：

```text
X(HBM[18]) = X1
R(HBM[19]) = R1
```

`P/P_spmv/Z` 此时还没变。

## 7. 第 1 次 PCG 迭代：更新 `Z1`、`rz1/rr1`

`update_z_reduce` 读：

```text
R1(HBM[19], FP64)
M_inv(HBM[17], FP64)
```

每个元素执行：

```text
Z1    = M_inv * R1
rz1  += R1 * Z1
rr1  += R1 * R1
```

写回：

```text
Z(HBM[20]) = Z1
```

然后 controller 标量更新：

```text
beta0 = rz1 / rz0
```

## 8. 第 1 次 PCG 迭代：更新 `P1/P_spmv1`

`update_p` 读：

```text
Z1(HBM[20], FP64)
P0(HBM[21], FP64)
beta0(controller 标量)
```

每个元素执行：

```text
P1 = Z1 + beta0 * P0
```

写回两份：

```text
P(HBM[21])      = P1                 // FP64 权威搜索方向
P_spmv(HBM[25]) = float(P1) packed   // 第二轮 A*p 的 Cuper 输入
```

第一轮结束时 controller 更新标量：

```text
rz = rz1
rr = rr1
iterations = 1
```

状态可以记成：

```text
X1, R1, Z1, P1 是 FP64 主状态
P_spmv1 是 P1 的 FP32 packed 副本
AP_spmv 仍缓存 AP0，但下一轮会被 AP1 覆盖
```

## 9. 第 2 次 PCG 迭代：计算 `AP1 = A*p1`

进入 `iter=1`。流程和第一轮完全同构。

controller 再次发：

```text
pcg_send_spmv_command(..., kPcgVectorSourceP)
```

这次 `Pcg_Vector_Loader` 读的是刚才写好的 `P_spmv1`：

```text
P_spmv1(HBM[25], FP32 packed)
  -> Pcg_Vector_Loader
  -> Pcg_Core[0..15]
  -> Pcg_Accumulator/Checker/Sort
  -> Pcg_Spmv_Stream
  -> Pcg_Controller
```

controller 收到 `AP1` 后覆盖：

```text
AP_spmv(HBM[22]) = AP1 packed FP32
```

## 10. 第 2 次 PCG 迭代：计算 `alpha1`、更新向量

`dot_p_ap`：

```text
p_ap1 = P1^T AP1
alpha1 = rz1 / p_ap1
```

`update_xr`：

```text
X2 = X1 + alpha1 * P1
R2 = R1 - alpha1 * AP1
```

`update_z_reduce`：

```text
Z2 = M_inv * R2
rz2 = R2^T Z2
rr2 = R2^T R2
beta1 = rz2 / rz1
```

`update_p`：

```text
P2 = Z2 + beta1 * P1
P_spmv2 = float(P2) packed
```

第二轮结束时：

```text
X(HBM[18]) = X2
R(HBM[19]) = R2
Z(HBM[20]) = Z2
P(HBM[21]) = P2
P_spmv(HBM[25]) = P_spmv2
AP_spmv(HBM[22]) = AP1
rz = rz2
rr = rr2
iterations = 2
```

如果 `Max_iters=2`，下一次 loop 条件失败，controller 进入收尾；如果 `rr <= Tau`，
则状态为 converged，否则通常是 max_iter。

## 11. 两次迭代的总览表

| 阶段 | SpMV 向量输入 | SpMV 输出 | controller 读 | controller 写 |
| --- | --- | --- | --- | --- |
| init `A*x0` | `X_spmv` | `A*x0` stream | `B`、`A*x0` | `R0` |
| init `z/p` | 不走 SpMV | 无 | `R0`、`M_inv` | `Z0`、`P0`、`P_spmv0` |
| iter0 `A*p0` | `P_spmv0` | `AP0` stream | `AP0` stream | `AP_spmv=AP0` |
| iter0 dot | 无 | 无 | `P0`、`AP_spmv(AP0)` | `p_ap0/alpha0` 标量 |
| iter0 update xr | 无 | 无 | `X0/R0/P0/AP0` | `X1/R1` |
| iter0 update z | 无 | 无 | `R1/M_inv` | `Z1`、`rz1/rr1` |
| iter0 update p | 无 | 无 | `Z1/P0` | `P1`、`P_spmv1` |
| iter1 `A*p1` | `P_spmv1` | `AP1` stream | `AP1` stream | `AP_spmv=AP1` |
| iter1 dot | 无 | 无 | `P1`、`AP_spmv(AP1)` | `p_ap1/alpha1` 标量 |
| iter1 update xr | 无 | 无 | `X1/R1/P1/AP1` | `X2/R2` |
| iter1 update z | 无 | 无 | `R2/M_inv` | `Z2`、`rz2/rr2` |
| iter1 update p | 无 | 无 | `Z2/P1` | `P2`、`P_spmv2` |

## 12. 关键结论

1. `X/R/Z/P` 是 FP64 主状态，最终结果只看 `X`。
2. `X_spmv/P_spmv/AP_spmv` 是为了适配 Cuper FP32 `float_v16` SpMV 的辅助缓冲。
3. `X_spmv` 只在初始化 `A*x0` 读取，kernel 内不再更新。
4. `P_spmv` 每轮在 `update_p` 末尾更新，下一轮 SpMV 读取它。
5. `AP_spmv` 每轮被最新 `A*p` 覆盖，供当前轮 `dot_p_ap/update_xr` 读取。
6. 当前 full-PCG 不是把 SpMV 输出直接旁路到 dot/update，而是先写 `AP_spmv`
   HBM，再由 dot/update 读回。
7. 优化共用 SpMV service 会同时影响 init 的 `A*x0` 和每轮的 `A*p`；优化
   `dot_p_ap/update_xr/update_z/update_p` 只影响 PCG 迭代部分。
8. 两次迭代不是简单两倍 `1iter`，因为初始化只做一次，而 `P_spmv/AP_spmv`
   每轮覆盖复用。
