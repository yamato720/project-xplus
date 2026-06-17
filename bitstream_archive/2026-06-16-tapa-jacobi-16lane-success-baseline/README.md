# 2026-06-16 TAPA Jacobi 16 路成功基线归档

本目录保存 `cuper-tapa-jacobi` 第五主线里已经板测通过的 16 路
`CuperJacobiIteration` demo，作为后续 no-debug / wide-HBM 实验的回退基线。

## 归档对象

| 项目 | 内容 |
| --- | --- |
| 原路径 | `395bitstream/cuper-tapa-jacobi-u55c-20260615-demo.xclbin` |
| Kernel | `CuperJacobiIteration` |
| ABI | master-controller full graph，`JACOBI_TRACE_LIGHT=1`，16 路 Matrix_data |
| UUID | `c37ecdbf-92ab-5d06-11bd-e2f9edc7f720` |
| SHA256 | `78c4ffdb9268aa5c1635bf2eefeed3b828e8a26e60ab3ccb8d795c9484d975a7` |
| `.info` SHA256 | `1bbb850c8b50973a3b620c1c0346afae99b12ed22386aa10104149ebe0f8b103` |
| DATA / KERNEL / HBM clock | `150/500/450 MHz` |
| timing | WNS `0.003 ns`，TNS `0.000 ns`，setup failing endpoints `0` |

## 已知表现

这版是 `20260615` master-controller light-trace full graph demo，已完成
demo-only 上板测试。`MAX_ITERS=1` 从 `thermal2_n16` 到完整 `thermal2` 均返回并
校验通过；完整固定轮数覆盖 `thermal2_n1024`、`thermal2_n65536`、
`thermal2_n131072`、`thermal2_n262144` 和完整 `thermal2`。

测试日志：

```text
logs/jacobi_full_graph_hw_20260615_223100_master_controller/
```

## 归档原因

2026-06-16 的 `JACOBI_WIDE_HBM=1` 24 路 no-debug 实验版在服务器侧
`thermal2_n16 MAX_ITERS=1` 上板 timeout，失败边界比上一版更靠前。后续准备重新构建
当前源码的 16 路 no-debug 版本，用于观察删除 Debug 和降低 HBM 通道压力后的表现。
因此先把这版已板测通过的 16 路 light-trace demo 复制留档。

本目录里的 `.xclbin` 只做本地归档，受 `.gitignore` 保护，默认不提交到 GitHub；
`.xclbin.info` 和本 README 可随 Git 同步。
