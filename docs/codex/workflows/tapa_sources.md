# TAPA 源码组织与 full-PCG 优化纪律

## 1. 源码组织

1. `DLC/Cuper/kernels/Cuper.cpp` 里有两个 TAPA top：
   - `Cuper(...)`：TAPA single SpMV
   - `CuperPcg(...)`：TAPA Cuper + FPGA 内 PCG
2. `CuperPcg` 是 `tapa compile -t CuperPcg` 的顶层。
3. 当前代码里已有两套 SpMV 形态，读代码和做 demo 时必须分清：
   - 满血 standalone Cuper SpMV：`Cuper(...)` 调用
     `detail/cuper_spmv_tasks.hpp`，对应标准 `cuper-tapa-spmv` 路线；
   - PCG 服务化 SpMV：`CuperPcg(...)` 调用 `detail/pcg_spmv_service.hpp`，
     这是从 full-PCG 内部为了重复触发、stop token、stage 计时和 TAPA 编译约束
     调整过的 Cuper SpMV 路径。
4. 若拆文件，优先拆成被 `Cuper.cpp` include 的 `.hpp` / `.inc` 片段，让 TAPA
   仍看到一个 translation unit。
5. 不要直接拆成多个 `.cpp`，除非同步修改 Makefile/TAPA compile 命令并验证。
6. TAPA task graph 里的 stream、core 链、HBM channel 映射要加中文注释；尤其是：
   - `PE_Param[0..16]`
   - `Vector_X_Stream[0..16]`
   - `Matrix_A_Stream[0..15]`
   - `Matrix_Mult_Vector_Stream[0..15]`
   - `Destroy_*` 链尾消费
7. 对 Cuper 内部 row 编码必须说明它不是原始全局 row，避免误判 `65535` 边界。

## 2. 当前优化目标

当前阶段的目标已从 single SpMV 本体切到 full-PCG controller/dot/update 路径。
2026-05-29 one-shot `CuperPcgSpmv(...)` demo 已能跑完整 `thermal2`，共同成功点
接近满血 `Cuper(...)`；它现在主要作为 single-SpMV 回归基线和边界检查。

边界仍然是把 single SpMV 和 full-PCG 控制拆开：

- `Cuper(...)` 是满血 standalone TAPA Cuper single SpMV 标准路径；
- `CuperPcgSpmv(...)` 只保留历史 kernel 名和 demo 构建入口，内部应采用和
  `Cuper(...)` 一样的 one-shot Cuper SpMV task graph；
- `CuperPcg(...)` 才是当前性能优化对象，优先看 `detail/pcg_controller.hpp`
  中的 `dot_p_ap`、`update_xr`、`update_p`，以及 `P_spmv` / `AP_spmv`
  消费、HBM 访问模式、stage timer、service drain/stop 开销。

因此 single SpMV demo 的性能结论只说明纯 SpMV/demo 路径表现，不能自动说明
full PCG 会同步受益。若要证明某个改动提升 full-PCG，必须改 full
`CuperPcg(...)` 实际路径并补跑 full-PCG 软件或硬件验证。

仍然要分清两套 SpMV：

| 名称 | 入口/文件 | 用途 |
| --- | --- | --- |
| 满血 Cuper SpMV | `Cuper(...)` + `detail/cuper_spmv_tasks.hpp` | 标准 single SpMV 基准 |
| Cuper-compatible demo SpMV | `CuperPcgSpmv(...)` + `detail/cuper_spmv_tasks.hpp` | 保留历史 kernel 名的 single SpMV demo，不承载 PCG 控制优化 |
| full PCG controller/update | `CuperPcg(...)` + `detail/pcg_controller.hpp` / `detail/pcg_spmv_service.hpp` | 当前性能优化和同步验证对象 |

判断 single SpMV demo 是否回归时，优先看：

1. `spmv_avg` 是否下降；
2. GFLOP/s 是否提升；
3. `thermal2_n262144` / 完整 `thermal2` 的 timeout 或失败边界是否改善；
4. CPU SpMV 校验 diff 是否仍在阈值内；
5. 是否引入新的小规模退化。

判断 full-PCG 优化是否有效时，必须看 full `CuperPcg(...)` 的软件仿真或上板
smoke 是否仍与 CPU 同口径结果对齐，并查看 full-PCG stage metrics。当前优先指标：

1. `1iter kernel_reported` 是否下降；
2. `controller_total` 是否下降；
3. `dot_p_ap`、`update_xr`、`update_p` 是否下降；
4. `AP path = iter recv + dot_p_ap` 是否保持合理；
5. 完整 `thermal2` 返回边界和 diff 是否保持。

single SpMV baseline/回归记录目录：

```text
docs/bitstream_summaries/2026-05-28-cuper-tapa-spmv-single-optimization/
```

full-PCG controller/dot/update 优化记录目录：

```text
docs/bitstream_summaries/2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/
```

围绕对应目标的连续源码改动说明、demo bitstream 和测试结论写进对应目录。
正式 `source.diff` 只有在测试确认性能提升，或用户明确要求保留功能边界修复补丁后
才更新；性能退步或失败的 demo 不覆盖上一份有效补丁。

优化时遵守：

1. single SpMV demo 不再引入 `Pcg_Single*` controller/command/stop/writer-done
   控制壳；这类控制只会污染 single SpMV 口径。
2. PCG controller/dot/update 改动只放在 full `CuperPcg(...)` 路径。重点检查
   `dot_p_ap`、`update_xr`、`update_p`、row 编码、stream drain、HBM burst/bank
   使用、stage timer 和 controller 等待逻辑。
3. 保持 `float_v16` packed 数据粒度，不把 full-PCG service SpMV 引入不必要的串行化。
4. 若改动只影响 `CuperPcgSpmv(...)` one-shot demo，必须标成 single SpMV demo
   改动，不能当作“会同步进 PCG”的性能优化。
5. 抽共享 helper 时要注意常驻服务和 finite-exit task 的差异。full
   `CuperPcg(...)` 的 checker/sort 这类 stop-driven task 必须在等待输入时持续
   检查 stop。
