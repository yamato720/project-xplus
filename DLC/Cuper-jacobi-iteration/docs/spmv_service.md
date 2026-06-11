# Service SpMV Boundary

当前 `DLC/Cuper-jacobi-iteration` 复用可重复触发的 Cuper single SpMV service，
但输出不再写回 `Y_out`，而是直接喂给 Jacobi update stage。

## 保留内容

- `detail/cuper_spmv_tasks.hpp`：Cuper SpMV 的底层 helper。
- `detail/spmv_service_common.hpp`：`CuperSpmvServiceCommand`、stop token 和广播 helper。
- `detail/spmv_service_tasks.hpp`：可由 command 重复触发的 loader/core/accumulator/checker/sort service。
- `detail/spmv_service_drains.hpp`：常驻 service 的链尾 drain/stop 逻辑。
- `detail/cuper_jacobi_top_graphs.hpp`：当前 Jacobi demo 顶层。

## 当前顶层

`CuperJacobiIteration(...)` 当前 ABI 是：

```text
SpElement_list_ptr + Matrix_data[0..15]
B + Diag_inv
X0 + X1
Status + Metrics
Batch_num + Matrix_len + Row_num + Column_num + Max_iters + Tau
```

其中 `Matrix_data[0..15]` 不再保存完整 `A`，而是 host 侧拆分后的
`R = A - D`。对角线只通过 `Diag_inv` 进入 update stage。`Jacobi_Vector_Loader`
在读 `X0/X1` 时把输入向量取负，所以 Cuper Core 看到的是 `-x_old`。

每轮迭代中，controller 发一条 SpMV command：

```text
Jacobi_Controller
  -> CuperSpmvServiceCommand
  -> SpmvService_SpElementPtrLoader
  -> Jacobi_Vector_Loader
  -> SpmvService_MatrixLoader[16]
  -> SpmvService_Core[16]
  -> SpmvService_Accumulator[16]
  -> SpmvService_VectorChecker[8]
  -> SpmvService_MultSortTree
  -> Jacobi_Update_Service
```

`SpmvService_MultSortTree` 输出的是 `-R*x_old`，按 `float_v16` 连续包排列。
`Jacobi_Update_Service` 用下面公式完成更新：

$$
x_i^{(k+1)}
= \left(b_i + (-Rx^{(k)})_i\right)\mathrm{diag\_inv}_i
$$

这样等价于：

$$
x^{(k+1)} = D^{-1}(b - R x^{(k)})
$$

因为：

$$
R x^{(k)} = A x^{(k)} - D x^{(k)}
$$

## 当前 demo 的边界

- 已有双缓冲 `X0/X1`，每轮读旧 buffer、写另一个 buffer。
- 已有 `Status[0..2]` 和 `Metrics[0..7]` 返回退出状态、最终 buffer、迭代次数、
  最后 `diff_max`、包数和 Jacobi stage cycle。
- 已支持 `.mtx` 和 Project-XPlus CSR 目录输入；CSR 目录存在 `b.txt` 时使用数据集 RHS。
- 当前 demo 仍用 `packet_count` 控制一轮 `-Rx` 的边界，没有额外封装 `RxPacket.last`。
- 当前 demo 没有独立 `Jacobi_Frame_Broadcaster` task，controller 直接发 SpMV command 和 update frame。

后续如果要进一步数据驱动化，可以在 sort tree 后加一个很薄的 packet wrapper，
给最后一个 `-Rx` 包打 `last`，再把 frame 广播拆成独立 task。
