# 2026-05-27 Cuper TAPA full-PCG demo 总结

## 版本信息

- 主线：`cuper-tapa-pcg`
- 状态：demo 候选，未替换当前标准版
- demo bitstream：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin`
- 对应标准版：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260525.xclbin`
- kernel：`CuperPcg`
- demo UUID：`9474ef8e-571b-ae13-f898-890e3af8ae5e`
- demo SHA256：`440d6969ff869d47aae5b12ab6d86d51b80794c07445799599ae13594a9166c5`
- demo DATA/HBM：216/450 MHz
- 测试日志：`logs/codex_bitstream_test_20260527_165723/`
- HTML 报告：`395bitstream/cuper_spmv_u55c_compare_20260524.html`

## 测试范围

本轮按 demo 规则动态对比当前 TAPA full-PCG 标准版和 demo 版：

- init-only：`TAU=1e100 MAX_ITERS=1 DIFF_TOL=1e-1`
- 1iter：`MAX_ITERS=1 DIFF_TOL=1e-4`
- 数据集：`thermal2_n16`、`thermal2_n1024`、`thermal2_n4096`、
  `thermal2_n16384`、`thermal2_n65536`、`thermal2_n131072`、
  `thermal2_n262144`、完整 `thermal2`

同时参考已记录的 TAPA single SpMV、no-TAPA full-PCG init proxy 和 no-TAPA
4ch SpMV 基线；旧基线后续默认复用本次 HTML 和本目录记录，不需要每次新 demo
都重跑。

## 退出边界

| 路线 | `n16` | `n1024` | `n4096` | `n16384` | `n65536` | `n131072` | `n262144` | `thermal2` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 标准 init | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 2 |
| demo init | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 2 |
| 标准 1iter | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 2 |
| demo 1iter | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 2 |

完整 `thermal2` 上标准版和 demo 都失败在 direct-register 运行后的 `ctrl=0x0`，
host 报 `direct register run did not complete`。demo 没有扩大可运行规模。

## 关键性能差值

数值为 `demo - standard`，负数表示 demo 更快。

| Dataset | Mode | kernel delta ms | kernel delta | ctrl delta ms | init SpMV delta ms | iter SpMV delta ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `thermal2_n65536` | init | +0.273 | +1.09% | -0.073 | -0.073 | 0.000 |
| `thermal2_n65536` | 1iter | -1.347 | -2.41% | -0.293 | -0.072 | -1.459 |
| `thermal2_n131072` | init | -1.185 | -2.96% | -0.147 | -0.147 | 0.000 |
| `thermal2_n131072` | 1iter | -3.263 | -3.27% | -0.583 | -0.146 | -2.916 |
| `thermal2_n262144` | init | -2.480 | -3.60% | -0.295 | -0.295 | 0.000 |
| `thermal2_n262144` | 1iter | -6.145 | -3.26% | -1.178 | -0.291 | -5.828 |

完整逐点表见 HTML 报告中的 `2026-05-27 Demo 动态对比` 章节。

## 结论

demo 对大规模 1iter 有稳定但有限的收益。最清楚的数据点是
`thermal2_n262144`：

- 1iter `kernel_reported`：`188.7094 ms -> 182.5644 ms`
- `iter_spmv`：`32.9576 ms -> 27.1300 ms`
- 总 kernel 约快 `3.26%`
- 第二轮 SpMV 分段约快 `17.7%`

这说明这版确实优化了第二轮 `A*p` 的 SpMV 接收路径，但 full-PCG 1iter 总时间
并没有质变。剩余瓶颈仍在 controller/service 周边路径、向量更新/归约、TAPA
task graph 同步或当前分段计时尚未覆盖的外围开销。

## 是否建议晋级

不建议立刻替换标准版。

原因：

- demo 没有解决完整 `thermal2` 的 `ctrl=0x0` 失败边界。
- 端到端 1iter 只快约 3%，提升幅度较小。
- 新分段计时里 `dot_p_ap`、`update_xr`、`update_z`、`update_p` 的 `stage-ms`
  仍接近 1 cycle，说明非 SpMV 开销还没有被准确拆开。

可以把它保留为 V2 demo 性能候选；下一步优先把非 SpMV 的 controller/service
时间拆准，再决定是否继续优化或晋级。
