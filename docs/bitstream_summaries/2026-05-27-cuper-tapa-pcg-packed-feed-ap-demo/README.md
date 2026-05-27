# 2026-05-27 Cuper TAPA full-PCG packed feed/AP demo 总结

## 版本信息

- 主线：`cuper-tapa-pcg`
- 状态：源码/XO 候选，硬件 bitstream 构建中，尚未替换当前标准版
- 目标：把 `CuperPcg` 内嵌 SpMV 的输入/输出路径继续向 standalone
  `cuper-tapa-spmv` 靠拢
- 对应标准版：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260525.xclbin`
- 预期 demo 命名：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin`
- 构建目录：`cuper-tapa-pcg-packed-ap-build/`
- tmux 会话：`project-xplus-cuper-tapa-pcg-packed-ap-hw`
- 构建日志：`logs/cuper_tapa_pcg_packed_ap_hw_20260527_191340.log`

## 这一版做了什么

这一版不是单纯调频率，而是改 `CuperPcg` full-PCG 内部 SpMV 的周边数据路径：

1. 新增 `X_spmv` / `P_spmv` 两个 packed `float_v16` HBM 向量入口。
2. `Pcg_Vector_Loader` 不再等待 controller 从 `double X/P` 逐元素打包，而是直接
   从 `X_spmv` 或 `P_spmv` 顺序读包。
3. 新增 `AP_spmv` packed `float_v16` HBM 缓冲。
4. `iter_spmv` 收到 Cuper 的 `A*p` 输出后直接写 `AP_spmv[packet]`，避免先拆成
   16 个 double 写入旧 `AP`。
5. `dot_p_ap` 和 `update_xr` 改为按 `AP_spmv` 包读取，再在 lane 内转 double。
6. host 默认 ABI 更新到 `AP_spmv/X_spmv/P_spmv`，同时保留 `--legacy-abi`，方便
   用旧 host 路径跑当前标准 bitstream。

## 预期收益

目标收益应该首先体现在 `iter_spmv`，其次才可能反映到 `controller_total` 和
`kernel_reported`。这版不预期直接解决完整 `thermal2` 的 `ctrl=0x0` 边界问题。

如果板上实测有效，合理表现应是：

- `iter_spmv` 明显下降；
- `init_spmv` 有小幅下降或基本持平；
- full-PCG 1iter 总时间下降幅度小于 `iter_spmv`，因为 FP64 dot/update 仍在
  controller 路径里；
- `cuper-tapa-spmv` standalone 仍应作为 SpMV 性能上限。

## 当前验证结论

已完成：

- host 编译通过；
- `n512 MAX_ITERS=1` TAPA 软件仿真通过；
- `thermal2_n1024 MAX_ITERS=1` TAPA 软件仿真通过；
- `hw_emu` 目标的 TAPA/HLS/XO 生成通过；
- `source.diff` 已记录本版源码和脚本改动。
- `code_reading_guide.md` 已记录本版代码阅读顺序和关键数据流。

尚未完成：

- `hw` bitstream 仍在 tmux 构建中；
- 尚未生成新的 `.xclbin.info`、UUID、SHA256；
- 尚未上板做 demo vs 当前标准版动态对比；
- 尚未更新 `395bitstream/` HTML 报告。

## 是否建议晋级

目前不能晋级。原因是还没有硬件 bitstream 和板上数据。

如果 bitstream 生成成功，下一步按 `docs/codex/testing.md` 先以 `-demo` 后缀
放入 `395bitstream/`，再对比当前标准版：

- `thermal2_n16`
- `thermal2_n65536`
- `thermal2_n131072`
- `thermal2_n262144`
- 完整 `thermal2`

重点看 `init_spmv`、`iter_spmv`、`controller_total`、`kernel_reported`。
