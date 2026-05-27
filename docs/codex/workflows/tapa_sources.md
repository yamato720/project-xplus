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

最终目标仍然是让 `CuperPcg` 内嵌 SpMV 的性能逐步向满血
`cuper-tapa-spmv` / TAPA Cuper single SpMV 靠拢。但当前迭代目标不要先做
full-PCG demo，而是先把 `CuperPcg` 里的 PCG 服务化 SpMV 抠出来，做成
`cuper-tapa-spmv` 形态的单 SpMV demo 单独测试。

这样做的目的：

1. SpMV 本体先脱离 FP64 dot/update/controller 开销，能更快定位是否真的接近满血
   Cuper；
2. 抠出来的 PCG SpMV 模块可以被 full-PCG 路径复用；
3. 单 SpMV demo 改完、测完、确认有效后，再替换回 `CuperPcg`，避免每次都用
   full-PCG 1iter 才发现 SpMV 路径问题。

当前应同时保留并对比两套 SpMV：

| 名称 | 入口/文件 | 用途 |
| --- | --- | --- |
| 满血 Cuper SpMV | `Cuper(...)` + `detail/cuper_spmv_tasks.hpp` | 当前 `cuper-tapa-spmv` 标准基准 |
| PCG 抽出版 SpMV | 从 `CuperPcg(...)` / `detail/pcg_spmv_service.hpp` 抽出的单 SpMV demo | 当前优化 demo，确认后回填 `CuperPcg` |

判断一次 SpMV 优化是否有效时，当前阶段优先看 PCG 抽出版单 SpMV demo 的
`spmv_avg`、成功/timeout 边界和数值误差，并和满血 Cuper SpMV 标准曲线对比。
只有当该单 SpMV demo 本身有效后，再进入 full-PCG demo，观察 `init_spmv`、
`iter_spmv`、`controller_total` 和 `kernel_reported`。

当前目标记录目录固定为：

```text
docs/bitstream_summaries/2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/
```

围绕该目标的连续源码改动说明、demo bitstream 和测试结论都写进这个目录。即使
当前 demo 改为 `cuper-tapa-spmv` 单 SpMV 形态，也仍属于这个“PCG 内嵌 SpMV
接近满血 Cuper”的持续目标，不要另起无关目录。正式 `source.diff` 只有在测试确认
性能提升，或用户明确要求保留功能边界修复补丁后才更新；性能退步或失败的 demo
不覆盖上一份有效补丁。只有当用户要求另起一条目标或该优化目标结束时，才新建其它
版本目录。

优化时优先处理会把原 Cuper 16 路 SpMV 压成串行的周边路径：

1. SpMV 输入向量应尽量保持 `float_v16` packed 形态，避免 controller 从
   `double X/P` 逐元素读 16 次再临时打包。
2. SpMV 输出的 `AP` 也应优先保持 packed 形态，例如使用 `AP_spmv`
   缓存一包 `float_v16`，避免刚离开 Cuper 就拆成 16 个 double HBM 访问。
3. SpMV 服务化路径的命令、向量 loader、输出回收和流水 drain 要对齐
   standalone TAPA Cuper 的数据粒度。
4. PCG 的 FP64 dot/reduction 和 `x/r/z/p` 更新可以单独优化，但不要把这些
   串行 HBM 扫描误记成 Cuper SpMV 本体性能。
5. 新 demo 的性能结论必须说明：提升落在 SpMV 本体、vector feed、AP 回收、
   还是 PCG controller 其它阶段。
6. 当前阶段的首选 demo 是 PCG 抽出版 `cuper-tapa-spmv` 单 SpMV；不要先把未单测
   的 SpMV 改动直接塞进 full-PCG demo 当作性能结论。
