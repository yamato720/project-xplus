# TAPA 源码组织与 SpMV 优化纪律

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

## 2. TAPA SpMV 优化目标

当前新目标是优化 standalone/native TAPA Cuper single SpMV 本身，也就是
`Cuper(...)` + `detail/cuper_spmv_tasks.hpp` 这条 `cuper-tapa-spmv` 主线。
本轮不要把 full-PCG 的 controller、FP64 dot/update、service drain 或
`init_spmv` / `iter_spmv` 混进评价口径。

仍然要分清两套 SpMV：

| 名称 | 入口/文件 | 用途 |
| --- | --- | --- |
| 满血 Cuper SpMV | `Cuper(...)` + `detail/cuper_spmv_tasks.hpp` | 当前 single TAPA SpMV 优化对象 |
| PCG 服务化 SpMV | `CuperPcg(...)` + `detail/pcg_spmv_service.hpp` | 旧 full-PCG embedded-SpMV 目标，不作为本轮 single SpMV 评价对象 |

判断一次 single SpMV 优化是否有效时，优先看：

1. `spmv_avg` 是否下降；
2. GFLOP/s 是否提升；
3. `thermal2_n262144` / 完整 `thermal2` 的 timeout 或失败边界是否改善；
4. CPU SpMV 校验 diff 是否仍在阈值内；
5. 是否引入新的小规模退化。

当前目标记录目录固定为：

```text
docs/bitstream_summaries/2026-05-28-cuper-tapa-spmv-single-optimization/
```

围绕该目标的连续源码改动说明、demo bitstream 和测试结论都写进这个目录。
正式 `source.diff` 只有在测试确认性能提升，或用户明确要求保留功能边界修复补丁后
才更新；性能退步或失败的 demo 不覆盖上一份有效补丁。

优化时优先处理 single TAPA SpMV 自身的问题：

1. 复核大规模 timeout/边界是否仍存在；
2. 检查 row 编码、stream drain、HBM burst/bank 使用和 host 等待逻辑；
3. 保持 `float_v16` packed 数据粒度，不把 single SpMV 路径引入不必要的串行化；
4. 新 demo 的性能结论必须写清楚提升落在矩阵 loader、vector loader、core 链、
   accumulator/checker、HBM 访问还是 host/runtime 边界。
