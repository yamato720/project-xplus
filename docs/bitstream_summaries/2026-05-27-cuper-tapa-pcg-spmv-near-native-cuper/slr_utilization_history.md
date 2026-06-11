# Cuper TAPA full-PCG SLR 使用历史

本文记录 2026-05 下旬到 2026-06-02 几代 `CuperPcg` / Cuper 相关 bitstream 的
SLR 放置与路由资源情况，用于判断当前失败是否来自 SLR 约束、资源超限还是局部布线拥塞。

## 报告口径

- 成功完成 route 的版本优先使用 `slr_util_routed.rpt`。
- route 失败的版本只能使用 `slr_util_placed.rpt`，因此只代表 fully placed 后的资源分布，不代表最终可路由布局。
- 表中 `CLB%`、`BRAM%`、`URAM%`、`DSP%` 均按 `SLR0/SLR1/SLR2` 顺序记录。
- `SLL total`、`SLL 1-0`、`SLL 2-1` 来自 SLR connectivity 报告。

## 数据来源

| 版本 | 报告 |
| --- | --- |
| no-TAPA PCG | `cuper-pcg-notapa/hw/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/slr_util_routed.rpt` |
| no-TAPA SpMV 4ch | `cuper-notapa-spmv-4ch-build/hw/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/slr_util_routed.rpt` |
| TAPA full-PCG 20260525 旧构建 | `cuper-tapa-fpga-pcg-build/hw/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/slr_util_routed.rpt` |
| TAPA full-PCG packed AP | `cuper-tapa-pcg-packed-ap-build/hw/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/slr_util_routed.rpt` |
| TAPA SpMV demo | `cuper-tapa-spmv-u55c-20260528-demo-build/hw/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/slr_util_routed.rpt` |
| TAPA full-PCG standard build dir | `cuper-tapa-pcg-fpga-u55c-20260525-build/hw/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/slr_util_routed.rpt` |
| TAPA full-PCG II=1 | `cuper-tapa-pcg-ii1-build/hw/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/slr_util_routed.rpt` |
| TAPA full-PCG controller-split | `cuper-tapa-pcg-controller-split-build/hw/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/slr_util_routed.rpt` |
| worker-monitor 失败版 | `cuper-tapa-pcg-worker-monitor-build/hw/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/slr_util_placed.rpt` |
| chain SLR1 失败版 | `cuper-tapa-pcg-slr-split-rebuild-20260601_210210-build/hw/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/slr_util_placed.rpt` |
| observe 失败版 | `cuper-tapa-pcg-slr-split-build/hw/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/slr_util_placed.rpt` |

## 汇总表

| 版本 | 状态 | 报告阶段 | SLL total | SLL 1-0 | SLL 2-1 | CLB% | BRAM% | URAM% | DSP% |
| --- | --- | --- | ---: | ---: | ---: | --- | --- | --- | --- |
| no-TAPA PCG | routed ok | routed | 23685 | 13786 | 9899 | 51.84/39.86/29.33 | 82.29/93.01/86.01 | 50.00/50.00/60.00 | 6.94/6.51/7.94 |
| no-TAPA SpMV 4ch | routed ok | routed | 24841 | 14828 | 10013 | 28.55/39.07/23.68 | 8.93/92.56/89.29 | 0.00/20.00/20.00 | 0.00/2.60/2.73 |
| TAPA full-PCG 20260525 旧构建 | routed ok | routed | 19545 | 11386 | 8159 | 79.97/67.72/23.34 | 55.21/80.58/10.12 | 60.00/70.00/30.00 | 12.05/15.10/3.26 |
| TAPA full-PCG packed AP | routed ok | routed | 20163 | 11630 | 8533 | 77.46/59.02/18.68 | 57.29/83.33/7.44 | 60.00/72.50/27.50 | 12.05/15.36/2.99 |
| TAPA SpMV demo | routed ok | routed | 21947 | 11459 | 10488 | 62.99/52.02/25.06 | 71.80/72.69/47.17 | 60.00/70.00/30.00 | 12.50/12.76/5.60 |
| TAPA full-PCG standard build dir | routed ok | routed | 18781 | 11679 | 7102 | 76.83/57.78/22.16 | 57.74/82.89/7.44 | 60.00/70.00/30.00 | 12.05/15.10/3.26 |
| TAPA full-PCG II=1 | routed ok | routed | 18322 | 11111 | 7211 | 82.32/54.18/16.84 | 65.77/74.85/7.44 | 70.00/64.38/25.63 | 13.99/14.06/2.47 |
| TAPA full-PCG controller-split | routed ok | routed | 18670 | 11578 | 7092 | 79.90/58.80/19.57 | 58.18/82.44/7.44 | 60.00/67.50/32.50 | 12.43/14.97/3.39 |
| worker-monitor | route fail | placed | 24441 | 14237 | 10204 | 96.14/53.15/38.45 | 67.63/62.28/49.33 | 30.00/70.00/60.00 | 13.30/12.76/11.07 |
| chain SLR1 split | route fail | placed | 26289 | 15870 | 10419 | 82.85/85.07/24.99 | 73.74/78.05/27.46 | 80.00/60.00/20.00 | 15.66/25.55/4.56 |
| observe/fifo8 | route fail | placed | 24403 | 14387 | 10016 | 94.48/74.68/30.21 | 75.52/70.31/33.41 | 70.00/60.00/30.00 | 19.83/20.61/5.60 |

## 关键观察

1. 能 route 的 TAPA full-PCG 版本通常把 SLR0 CLB 控制在约 `76%` 到 `82%`，SLR1 在
   `54%` 到 `68%`，SLR2 多数低于 `25%`。SLL total 多在 `18k` 到 `20k`。
2. `worker-monitor` 开始失败时，SLR0 CLB 已到 `96.14%`，SLL total 升到 `24441`。
   这说明新增监控/状态服务虽然不一定占满全芯片资源，但把 SLR0 局部布线和控制网压力显著推高。
3. `chain SLR1 split` 试图把 update 链搬到 SLR1 后，SLR1 CLB 到 `85.07%`，
   SLL total 到 `26289`，是几代里跨 SLR 压力最重的一版。该方向没有缓解拥塞，反而把 SLR1 也压高。
4. 最新 `observe/fifo8` 已经没有强制 SLR pblock，hook 日志显示：
   `mode=observe`、`cleared USER_SLR_ASSIGNMENT`、SpMV/update/control 都是
   `no SLR pblock applied`。因此这次失败不能归因于旧的单 SLR pblock 或强制 update split。
5. 最新失败的 partially-conflicted nets 集中在 `Pcg_Accumulator_*` 的
   `cuper_acc_accumulate` / `cuper_acc_local_part_y` 相关 flow-control、比较和启动信号。
   结合 SLR0 `94.48%` CLB、SLR1 `74.68%` CLB、SLL total `24403`，当前主要问题是
   SpMV accumulator 及其周边控制/存储结构的局部 routing congestion。

## 对后续优化的含义

- 单纯继续移动 `update_z` / `update_p` 不太可能解决当前失败；强拆 update 链已经证明会增加 SLL 和 SLR1 压力。
- 优先方向应转到 `Pcg_Accumulator` / SpMV service 本身：
  - 降低 accumulator 内部控制扇出和 HLS flow-control 复杂度；
  - 检查 `Cuper_Accumulator_Compute_Round` 的 pipeline/loop 结构，减少局部比较、init、writer 控制网；
  - 减少大型 FIFO/SRL/BRAM 在 SLR0/SLR1 附近堆积；
  - 如继续做 floorplan，应按 SpMV lane group 做更细的成组约束，而不是把整条 update 链跨 SLR 搬走。
- SLR2 仍有明显余量，但它不直连全部 HBM，强行搬数据通路会增加跨 SLR SLL；如果使用 SLR2，应该优先放低 HBM 依赖、低吞吐控制/计时逻辑，或通过窄 FIFO 交互的计算模块。

## 复现解析命令

```bash
find . -path '*/hw/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/slr_util_placed.rpt' -printf '%TY-%Tm-%Td %TH:%TM %p\n' | sort
find . -path '*/hw/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/slr_util_routed.rpt' -printf '%TY-%Tm-%Td %TH:%TM %p\n' | sort
```

本表由上述报告中的 `SLR Connectivity` 和 `SLR CLB Logic and Dedicated Block Utilization`
章节人工汇总。后续若新增 route 成功或失败版本，应按同一口径追加一行，并注明使用的是
`placed` 还是 `routed` 报告。
