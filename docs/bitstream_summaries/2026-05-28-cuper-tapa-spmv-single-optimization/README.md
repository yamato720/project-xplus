# 2026-05-28 Cuper TAPA single SpMV 优化目标记录

## 版本信息

- 主线：`cuper-tapa-spmv`
- 状态：PCG service SpMV 抽出版第一版 demo bitstream 已生成并放入
  `395bitstream/`；2026-05-28 demo-only 上板 smoke 在 `thermal2_n16`
  两次 180s timeout，未晋级。之后已做 finite-exit 修复尝试；本轮进一步
  清理 PCG service 内部 `Iteration_num` 重复语义，软件仿真已通过，尚未
  启动新的硬件构建，正式 `source.diff` 不更新
- 当前标准版：`395bitstream/cuper-tapa-spmv-u55c-20260522.xclbin`
- 当前 demo 命名：`395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin`
- 标准基线入口：`DLC/Cuper/kernels/Cuper.cpp` 中的 `Cuper(...)`
- 本轮抽出版入口：`DLC/Cuper/kernels/Cuper.cpp` 中的 `CuperPcgSpmv(...)`
- 标准 SpMV 文件：`DLC/Cuper/kernels/detail/cuper_spmv_tasks.hpp`
- 本轮抽出版文件：`DLC/Cuper/kernels/detail/pcg_spmv_service.hpp`
- 构建目录：`cuper-tapa-spmv-u55c-20260528-demo-build/`
- 构建日志：`logs/cuper_tapa_pcg_spmv_hw_20260528_023906.log`
- 生成文件：`395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin`
- UUID：`08f1f2dc-8c44-007f-a0a5-4dce1236ddd9`
- SHA256：`0be3ed806febc39ad488ed833c063390978bb2911d4fa298c2056ef2e5ce6356`
- DATA/KERNEL/HBM clock：222 / 500 / 450 MHz

## 目标

本目录专门负责 **single TAPA SpMV** 的优化，不再混入 full-PCG 的
controller/dot/update 路径。

目标是把 `cuper-tapa-spmv` 这条原生 TAPA Cuper SpMV 路线本身先做稳、做快：

1. 保持或提升当前小中规模 `spmv_avg` 性能；
2. 优先排查大规模矩阵的 timeout、边界和数据正确性问题；
3. 所有结论只看 single SpMV 口径，不使用 `init_spmv`、`iter_spmv`、
   `controller_total` 等 full-PCG 指标；
4. 只有 single SpMV demo 经板上测试确认有效后，才考虑把对应思想迁移回
   `CuperPcg`。

## 和旧目标的区别

旧目录：

```text
docs/bitstream_summaries/2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/
```

旧目标是让 `CuperPcg` 内嵌 SpMV 逐步接近 standalone/native TAPA Cuper SpMV，
属于 full-PCG 体系内的 SpMV 服务路径优化。

本目录只研究 `Cuper(...)` single SpMV 自身。它是标准基准线，不是
`CuperPcg` 的 PCG 服务化 SpMV。

## 2026-05-28 补充：先生成 PCG service 单 SpMV

用户本轮要求先不优化，先给当前 TAPA-PCG 版补一个单 SpMV bitstream。因此这次
新增 `CuperPcgSpmv(...)` 顶层：外部仍按 `cuper-tapa-spmv` demo 归类，ABI 保持
`Matrix_data + X -> Y_out`，内部则走 `CuperPcg` 当前使用的
`pcg_spmv_service.hpp` 服务化 SpMV 链。它用于回答“full-PCG 内嵌 SpMV 单独拉出来
到底有多快”，不是标准 `Cuper(...)` 的优化补丁。

构建产物：

```text
cuper-tapa-spmv-u55c-20260528-demo-build/hw/CuperPcgSpmv.xclbin
```

构建成功后已复制为：

```text
395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin
```

替换前的 full-PCG packed feed/AP 旧 demo 已本地归档到：

```text
bitstream_archive/2026-05-28-tapa-pcg-packed-ap-demo-before-spmv-demo/
```

## 2026-05-28 上板 smoke 结论

测试日志：

```text
logs/codex_spmv_demo_only_test_20260528_143556/
```

本轮只测试当前 demo，不重跑四个标准 bitstream。运行入口为：

```bash
make run-cuper-tapa-pcg-spmv TARGET=hw \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  BITFILE=395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin \
  SPMV_REPEATS=3 DIFF_TOL=1e-1
```

结果：`thermal2_n16` 第一次和 reset 后重试均在 180s 外层 timeout
终止，退出码均为 `124`。两次日志都停在：

```text
[tapa-invoke] after ReadFromDevice before Finish
```

因此没有产生 `spmv_avg`、GFLOP/s 或 CPU diff，本轮没有继续跑更大数据集。
这版只是失败的 `cuper-tapa-spmv` demo 记录，不建议晋级，也不更新正式
`source.diff`。

失败原因分析记录见：

```text
docs/bitstream_summaries/2026-05-28-cuper-tapa-spmv-single-optimization/failure_analysis.md
```

本版代码阅读指南见：

```text
docs/bitstream_summaries/2026-05-28-cuper-tapa-spmv-single-optimization/code_reading_guide.md
```

## 2026-05-28 补充：finite-exit 修复尝试

针对上一版 `Finish` timeout，当前源码只改 `CuperPcgSpmv` 单 SpMV demo 路径：

- `CuperPcgSpmv(...)` 不再把单 SpMV 输出尾端接到 stop-driven
  `Pcg_Vector_Checker` / `Pcg_Mult_Sort_Tree`；
- 新增 `Pcg_Single_Vector_Checker`，按 `Row_num` 计算 Cuper 对齐后 PE 输出包数，
  读完整个 padding 后只转发有效 `float_v2`；
- 新增 `Pcg_Single_Mult_Sort_Tree`，只打包并输出 `ceil(Row_num/16)` 个
  `float_v16` 包后自然返回；
- `Pcg_SingleSpmv_Controller` 仍在 writer 写完 `Y_out` 后关闭 loader/core/destroy，
  但不再异步抢停 checker/sort tree。

软件级验证已通过：

```bash
timeout 180s make run-cuper-tapa-pcg-spmv \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  SPMV_REPEATS=1 DIFF_TOL=1e-1
```

关键输出：

```text
[done] mode=spmv_only spmv_calls=1 status=ok
[check] max_abs_diff=3.755767679081e-07 max_rel_diff=7.633263769275e-08 diff_tol=1.000000000000e-01
[timing-ms] ... spmv_avg=2.654200300000e+01 ...
```

新的硬件构建已启动：

```text
session: project-xplus-cuper-tapa-pcg-spmv-hw
log: logs/cuper_tapa_pcg_spmv_hw_20260528_161221.log
build_dir: cuper-tapa-spmv-u55c-20260528-demo-build/
xclbin: cuper-tapa-spmv-u55c-20260528-demo-build/hw/CuperPcgSpmv.xclbin
```

截至 2026-05-28 16:14，XO 已生成并完成 FSM patch，Vitis link 已进入 VPL。
该修复版是否真正解决板上 `Finish` timeout，必须等新 xclbin 生成后用
demo-only `thermal2_n16` smoke 验证。验证前仍不建议晋级，也不更新正式
`source.diff`。

## 2026-05-28 补充：去掉 service 内部 Iteration_num

本轮按用户要求把 `CuperPcgSpmv` 单 SpMV 抽出版和 full `CuperPcg`
共享的 PCG service SpMV 协议统一成：

```text
一条 CuperSpmvCommand == 一次 SpMV
```

具体变化：

- `CuperSpmvCommand` 删除 `iteration_num` 字段；
- `Pcg_Controller` 在 init 和每轮 PCG 迭代各发送一条 command，不再让 SpMV
  service 内部重复跑同一个命令；
- `Pcg_SpElement_list_ptr_Loader`、`Pcg_Vector_Loader`、`Pcg_Matrix_Loader`、
  `Pcg_Core`、`Pcg_Accumulator` 以及单 SpMV 尾端 checker/sort/writer 都按
  一条 command 处理一次 SpMV；
- `CuperPcgSpmv(...)` 顶层 ABI 仍保留 `Iteration_num` 参数以兼容 host/脚本，
  但内部显式忽略该参数；
- standalone `Cuper(...)` 的 `Iteration_num` 没改，它仍是原生 single SpMV
  benchmark 的重复次数。

本轮只做软件验证，没有启动 `TARGET=hw` 构建。已补齐 HTML 报告使用的
`thermal2` 系列数据，并挑选 `thermal2_n16`、`thermal2_n1024`、
`thermal2_n4096` 做两条路径验证：

| 路径 | 数据集 | 结果 | 关键误差 |
| --- | --- | --- | --- |
| `CuperPcgSpmv` service single SpMV | `thermal2_n16` | `status=ok` | `max_abs_diff=3.7558e-07` |
| `CuperPcgSpmv` service single SpMV | `thermal2_n1024` | `status=ok` | `max_abs_diff=1.3506e-06` |
| `CuperPcgSpmv` service single SpMV | `thermal2_n4096` | `status=ok` | `max_abs_diff=2.3366e-06` |
| full `CuperPcg` FPGA-PCG software sim | `thermal2_n16` | `converged` | `max_abs_diff=1.0868e-08` |
| full `CuperPcg` FPGA-PCG software sim | `thermal2_n1024` | `max_iter`，与 CPU 1iter 对齐 | `max_abs_diff=9.2782e-10` |
| full `CuperPcg` FPGA-PCG software sim | `thermal2_n4096` | `max_iter`，与 CPU 1iter 对齐 | `max_abs_diff=4.0935e-09` |

`n1024` 和 `n4096` 的 full-PCG 测试使用 `MAX_ITERS=1`，所以 `status=max_iter`
是预期结果；这里看的是 FPGA-PCG 软件模型和 CPU 同口径 1 次迭代是否一致。

## 当前基线

根据 `docs/codex/testing.md` 和既有 HTML 记录：

| 项目 | 当前记录 |
| --- | --- |
| 标准 bitstream | `395bitstream/cuper-tapa-spmv-u55c-20260522.xclbin` |
| kernel | `Cuper` |
| UUID | `428b48ff-ec3b-e2d4-536b-97a8e654fea3` |
| DATA/HBM | 174/448 MHz |
| 已知成功范围 | `thermal2_n16` 到 `thermal2_n131072` |
| 已知问题 | `thermal2_n262144` 和完整 `thermal2` 在旧记录中 180s timeout |

## 记录策略

- 后续 single TAPA SpMV 的源码改动、demo bitstream、测试结论和 HTML 摘要都写入本目录。
- `README.md` 写当前状态和是否建议晋级。
- `changes.md` 写每轮 single SpMV demo 具体改了什么。
- `testing.md` 写 demo-only 测试命令、数据、边界和关键输出。
- 正式 `source.diff` 只在板上测试确认性能提升、边界修复有效，或用户明确要求保留补丁时再生成。
