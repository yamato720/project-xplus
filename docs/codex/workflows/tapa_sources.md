# TAPA 源码组织与 SpMV 优化纪律

## 1. 源码组织

1. `DLC/Cuper/kernels/Cuper.cpp` 里有两个 TAPA top：
   - `Cuper(...)`：TAPA single SpMV
   - `CuperPcg(...)`：TAPA Cuper + FPGA 内 PCG
2. `CuperPcg` 是 `tapa compile -t CuperPcg` 的顶层。
3. 若拆文件，优先拆成被 `Cuper.cpp` include 的 `.hpp` / `.inc` 片段，让 TAPA
   仍看到一个 translation unit。
4. 不要直接拆成多个 `.cpp`，除非同步修改 Makefile/TAPA compile 命令并验证。
5. TAPA task graph 里的 stream、core 链、HBM channel 映射要加中文注释；尤其是：
   - `PE_Param[0..16]`
   - `Vector_X_Stream[0..16]`
   - `Matrix_A_Stream[0..15]`
   - `Matrix_Mult_Vector_Stream[0..15]`
   - `Destroy_*` 链尾消费
6. 对 Cuper 内部 row 编码必须说明它不是原始全局 row，避免误判 `65535` 边界。

## 2. TAPA full-PCG SpMV 优化目标

当前 `cuper-tapa-pcg` 的核心优化目标是：让 `CuperPcg` 内嵌 SpMV 的性能逐步
向 `cuper-tapa-spmv` / TAPA Cuper single SpMV 靠拢。判断一次优化是否有效时，
优先看 `init_spmv`、`iter_spmv`、`controller_total` 和 `kernel_reported` 的
动态对比，而不是只看时钟频率或资源。

当前目标记录目录固定为：

```text
docs/bitstream_summaries/2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/
```

围绕该目标的连续源码、demo bitstream、测试结论和 `source.diff` 更新都写进这个
目录。只有当用户要求另起一条目标或该优化目标结束时，才新建其它版本目录。

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
