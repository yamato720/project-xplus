# 2026-05-28 Cuper TAPA single SpMV 优化目标记录

## 版本信息

- 主线：`cuper-tapa-spmv`
- 状态：PCG service SpMV 抽出版 demo bitstream 已生成并放入 `395bitstream/`；
  2026-05-28 demo-only 上板 smoke 在 `thermal2_n16` 两次 180s timeout，未晋级
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
