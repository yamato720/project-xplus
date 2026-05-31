# 2026-05-31 TAPA PCG controller-split demo 存档

本目录保存 2026-05-31 从 `395bitstream/` 移出的 `cuper-tapa-pcg`
full-PCG controller-split demo。该 demo 已完成 `hw` 构建、demo-only 上板测试和一组
partial full-run 补测，但未晋级为标准 bitstream。

## 归档对象

```text
cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin
cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin.info
```

## 关键参数

| 项目 | 数值 |
| --- | --- |
| 主线 | `cuper-tapa-pcg` |
| Kernel | `CuperPcg` |
| UUID | `1d536c39-f561-340b-7efc-ac2c8440543d` |
| SHA256 | `bc58605b36c98b29d84ce14939b95f8fc6b84bb7a505007fda95458545a349b8` |
| DATA/KERNEL/HBM | 211 / 500 / 450 MHz |
| 构建目录 | `cuper-tapa-pcg-controller-split-build/` |
| 构建日志 | `logs/cuper_tapa_pcg_controller_split_hw_20260531_020548.log` |
| demo-only 测试日志 | `logs/codex_controller_split_demo_test_20260531_140333/` |
| full-run 补测日志 | `logs/codex_controller_split_fullrun_20260531_142400/` |

## 主要改动

- `dot_p_ap` 合入 `iter_spmv_stream`，接收 `A*p` 时同步计算 `p^T AP`。
- `update_xr` 拆成 compute/store 两段。
- `update_p` 拆成 compute/store 两段，并单独维护 packed `P_spmv`。
- HLS 报告中 `update_xr_compute_lanes`、`update_xr_store_lanes`、
  `update_p_compute_lanes` 和 `update_p_store_lanes` 均达到 II=1。
- `iter_dot_p_ap_lanes` 和 `update_z_reduce` 仍受 FP64 reduction recurrence 影响，
  achieved II=5。

## 测试结论

demo-only 上板测试中，`thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、
`thermal2_n262144` 和完整 `thermal2` 的 init-only 与 `MAX_ITERS=1` 均返回，
direct ctrl 均为 `0x4 -> 0xe`，数值校验通过。

关键性能点：

| 数据集 | 模式 | kernel_reported |
| --- | --- | ---: |
| `thermal2_n262144` | 1iter | 211.3790 ms |
| `thermal2` | init-only | 361.4214 ms |
| `thermal2` | 1iter | 954.0779 ms |
| `thermal2_n262144` | full-run, 120 iter | 15263.805830 ms |

相对上一 II=1 demo，`thermal2` 1iter 从 `1767.8254 ms` 降到 `954.0779 ms`，
`thermal2_n262144` 1iter 从 `385.1288 ms` 降到 `211.3790 ms`。不过共同成功点仍略慢于
当前 TAPA full-PCG 标准版旧记录，`thermal2_n262144` 标准版 1iter 为
`188.8202 ms`。

full-run 补测确认 `thermal2_n16` 到 `thermal2_n262144` 多轮收敛，迭代数分别为
`1/60/81/96/104/113/120`。完整 `thermal2` 禁用 host 60 秒超时后约 490 秒仍为
`ctrl=0x0`，按用户要求停止；该点记录为未完成，不作为收敛失败结论。

## 硬件报告

关键报告路径：

```text
cuper-tapa-pcg-controller-split-build/hw/tapa_CuperPcg/report/Pcg_Controller/csynth.rpt
cuper-tapa-pcg-controller-split-build/hw/_x_temp/reports/link/imp/impl_1_hw_bb_locked_timing_summary_routed.rpt
cuper-tapa-pcg-controller-split-build/hw/_x_temp/reports/link/imp/impl_1_full_util_routed.rpt
cuper-tapa-pcg-controller-split-build/hw/_x_temp/reports/link/imp/impl_1_slr_util_routed.rpt
```

实现层需要注意：最终 xclbin 生成成功并能上板运行，但 routed timing report 显示
timing constraints are not met，`WNS=-1.405 ns`，`TNS=-26153.619 ns`。

资源快照：

| 指标 | routed util |
| --- | ---: |
| CLB LUTs | 372632 / 1303680 = 28.58% |
| CLB Registers | 523947 / 2607360 = 20.09% |
| BRAM Tile | 995 / 2016 = 49.36% |
| URAM | 512 / 960 = 53.33% |
| DSP | 922 / 9024 = 10.22% |
| Total SLLs | 18670 |

## Git 状态

本目录中的 `.xclbin` 受 `.gitignore` 保护，只做本地留档；`.xclbin.info` 和本
README 可进入 Git，用来记录归档位置、UUID、clock、测试结论和报告入口。
