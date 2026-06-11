# Cuper TAPA Jacobi Iteration 主线记录

这份记录对应第五条 Cuper 主线：`cuper-tapa-jacobi`。源码目录是：

```text
DLC/Cuper-jacobi-iteration/
```

当前状态：已完成第一版 software/TAPA simulation demo，并接入 Project-XPlus 根
`Makefile` 的 `cuper-jacobi-*` 目标；已生成并同步第一版硬件 demo xclbin，但 routed
timing 未收敛，还没有上板测试数据。

## 版本定位

| 项目 | 内容 |
| --- | --- |
| 主线 | `cuper-tapa-jacobi` |
| 顶层 kernel | `CuperJacobiIteration` |
| 源码入口 | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` |
| 默认构建目录 | `cuper-jacobi-iteration-build/` |
| 当前 bitstream | `395bitstream/cuper-tapa-jacobi-u55c-20260611-demo.xclbin` |
| 版本状态 | software/TAPA simulation 通过；硬件 demo 已封装但 timing fail |
| 是否建议晋级标准 | 不建议，需先修 timing 并完成 demo-only 上板测试 |

这条主线做普通 Jacobi iteration：

$$
x^{(k+1)} = D^{-1}(b - R x^{(k)})
$$

它不是 Jacobi 预条件子 PCG，不计算 PCG 的 `alpha/beta`，也不维护 `r/z/p`。

## 当前数据流

host 侧先把矩阵拆成 `A = D + R`。kernel 侧复用 Cuper 的 single SpMV service，
但 `Jacobi_Vector_Loader` 读取 `X0/X1` 时把输入向量取负，所以 Cuper service
输出的是 `-R*x_old`。后级 `Jacobi_Update_Service` 直接计算：

$$
x_i^{(k+1)} = (b_i + (-R x^{(k)})_i)\mathrm{diag\_inv}_i
$$

`X0/X1` 做双缓冲，`Status[1]` 返回最终结果所在 buffer。

## 当前 HBM/ABI

当前 connectivity 是 demo ABI，还没有做 HBM 压缩：

| 数据 | HBM |
| --- | --- |
| `SpElement_list_ptr` | HBM[0] |
| `Matrix_data_0..15` | HBM[0..15] |
| `B` | HBM[20] |
| `Diag_inv` | HBM[21] |
| `X0` | HBM[22] |
| `X1` | HBM[23] |
| `Status` / `Metrics` | HBM[24] |

当前设计仍显式使用矩阵 16 个 HBM 通道之外的向量/状态通道。后续如果目标是压回
16 个 HBM，需要重新设计 X 转发、B/Diag_inv 供给和结果写回策略；这还没有进入本轮实现。

## 当前 demo bitstream

| 项目 | 内容 |
| --- | --- |
| 文件 | `395bitstream/cuper-tapa-jacobi-u55c-20260611-demo.xclbin` |
| `.info` | `395bitstream/cuper-tapa-jacobi-u55c-20260611-demo.xclbin.info` |
| 构建目录 | `cuper-jacobi-iteration-build/` |
| UUID | `a7c95d3c-ec98-c287-67be-d81f71f7c95e` |
| SHA256 | `a622e1600628e9c4ed34fe7dd7d5f2a2afcb374789fddaa4436b1ba9408e8172` |
| DATA / KERNEL / HBM clock | `220 MHz` / `500 MHz` / `442 MHz` |
| 时序状态 | routed timing 未收敛，WNS `-1.203 ns`，TNS `-22607.805 ns` |

这个文件只作为当前调试 demo artifact 同步，不是标准 bitstream。原始 tmux 构建在
最后包装阶段因本地 XRT 2.18 缺 Boost 1.83 动态库失败；使用 Vitis 2022.2
自带 `xclbinutil` 从 `xclbinutil` 阶段恢复后成功生成当前 `.xclbin`。

## 当前已记录测试

| 数据集 | 迭代 | 状态 | 关键输出 |
| --- | ---: | --- | --- |
| `cant.mtx` | 2 | 当前 timed 版通过 | `Final diff=0.73218`, `Error Num=0`, `spmv_update=143582 cycles` |
| `thermal2_n65536` | 1 | 当前 timed 版通过 | `Final diff=1.11631`, `Error Num=0`, `spmv_update=41698 cycles` |
| `thermal2_n262144` | 1 | 早期 software run 通过 | `Final diff=1.41496`, `Error Num=0`；需用当前 timed/root target 补跑 |

详细命令和字段见 `testing.md`。

## 相关文档

```text
DLC/Cuper-jacobi-iteration/docs/jacobi_iteration.md
DLC/Cuper-jacobi-iteration/docs/jacobi_implementation_plan.md
DLC/Cuper-jacobi-iteration/docs/testing.md
docs/codex/coding.md
docs/codex/testing.md
395bitstream/README.md
```

## 待补

- 当前 timed/root target 下补跑 `thermal2_n262144`。
- 若要继续硬件路线，先处理 timing fail；主要看 `Jacobi_Update_Service` 和
  300 MHz DATA clock 下的 update/writeback 路径。
- 对当前 demo 做最小上板 smoke，确认 timing fail 是否表现为运行不稳定。
- 完成 demo-only 上板测试后，再更新 HTML 报告和本目录测试表。
- `source.diff` 暂不生成；当前还没有硬件 demo-only 性能确认。
