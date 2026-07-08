# Callipepla PCG Reference

这个目录是 Callipepla PCG/vector task 组织方式的只读参考快照，复制来源：

```text
/home/pyx/Callipepla/Callipepla/src/
```

本目录不接入 Project-XPlus 构建，也不改变 `CuperPcg(...)` ABI、HBM 映射或 Cuper 数据格式。

## 文件

- `callipepla.cpp`：Callipepla 顶层 TAPA task graph，能看到 PCG 各阶段如何拆成 task/stream。
- `callipepla-host.cpp`：Callipepla host 侧入口，作为数据装载和调用方式参考。
- `callipepla.h`：Callipepla 顶层接口、常量和基础类型。
- `detail/callipepla_common.hpp`：公共类型、stream 包装和辅助函数。
- `detail/callipepla_pcg_tasks.hpp`：PCG 标量和向量阶段 task，包括 `updt_x`、`updt_r`、`left_div`、`dot_rznew`、`updt_p`。
- `detail/callipepla_vector_tasks.hpp`：`P/AP/X/R/diagA` 的向量内存控制 task。
- `detail/callipepla_spmv_tasks.hpp`：Callipepla SpMV task 组织参考。
- `CALLIPEPLA_LICENSE`：上游 Callipepla 仓库 MIT license 副本。

## 和当前 Cuper PCG 的对应关系

Callipepla 已把 PCG 向量阶段几乎全部拆成独立 task，核心参考点是：

- `updt_x` 对应 Cuper full-PCG 中的 `update_x` 语义。
- `updt_r + left_div + dot_rznew` 对应 Cuper 当前计划中的 `update_rz_reduce` 语义。
- `updt_p` 对应 Cuper 的 `update_p` 语义。
- `ctrl_P`、`ctrl_AP`、`ctrl_X`、`ctrl_R` 是 Callipepla 的向量 HBM/stream controller，Cuper 目前还没有按这个粒度完全拆开。

## 注意

- Callipepla 使用自己的稀疏矩阵格式和 task graph，不是 Cuper 的 `SpElement_list_ptr`/`Matrix_data[0..15]` 格式。
- 这里的代码只用于阅读和对照，不应直接 include 到 `DLC/Cuper`。
- 后续改 `DLC/Cuper/kernels/detail/pcg_controller.hpp` 时，应继续保留 Cuper 的 SpMV service、`AP_spmv` HBM 断点和现有 host BO register ABI。
