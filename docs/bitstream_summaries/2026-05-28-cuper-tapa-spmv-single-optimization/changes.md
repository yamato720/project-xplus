# 这一目标要改什么

## 当前阶段

本目录第一轮 demo 已生成：先不做性能优化，先把当前 TAPA-PCG 里的服务化 SpMV
抽成单 SpMV kernel，生成可单独上板测试的 `cuper-tapa-spmv` demo bitstream。

## 2026-05-28：CuperPcgSpmv 抽出版

本轮新增内容：

- 新增 `CuperPcgSpmv(...)` TAPA 顶层，外部 ABI 对齐 `Cuper(...)`：
  `SpElement_list_ptr`、`Matrix_data[0..15]`、`X`、`Y_out` 和 SpMV 尺寸参数。
- 新增 `Pcg_SingleSpmv_Controller`，只发送一次 SpMV command，等待 writer 完成后
  广播 stop，避免 PCG service task 无限常驻导致 kernel 不返回。
- 新增 `Pcg_Single_Vector_Loader`，保留 PCG service 的 command/stop 语义，但单
  SpMV 只读一个 packed `X` 输入端口。
- 新增 `Pcg_Single_Vector_Writer`，把 service sort tree 输出的 `float_v16`
  写回 `Y_out`，再通知 controller 关闭服务链。
- 新增 U55C connectivity：
  `cfg/connectivity_cuper_tapa_pcg_spmv_u55c.cfg`。
- 新增 Makefile 构建入口：
  `build-cuper-tapa-pcg-spmv-{sw,hw}` 和 `cuper-tapa-pcg-spmv-hw-tmux`。
- host `run-cuper-tapa-pcg-spmv` 通过 `--pcg-spmv-service` 调用
  `CuperPcgSpmv`，便于之后 demo-only single SpMV 对比。
- 已生成硬件 bitstream 并放入当前 demo 槽：
  `395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin`。
- 旧 demo 槽中的 full-PCG packed feed/AP 候选版已移到
  `bitstream_archive/2026-05-28-tapa-pcg-packed-ap-demo-before-spmv-demo/`。

本轮不做的事：

- 不调整 standalone `Cuper(...)` 和 `detail/cuper_spmv_tasks.hpp`；
- 不优化 HBM 排布、FIFO 深度、core 链或 row 编码；
- 不更新正式 `source.diff`，因为 demo-only 上板 smoke 未通过，没有性能提升结果。

## 2026-05-28 demo-only 结果补充

`395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin` 已做 single SpMV
demo-only smoke。`thermal2_n16` 第一次运行和 retry 均在 180s timeout，日志都停在
`[tapa-invoke] after ReadFromDevice before Finish`。本轮没有产生 `spmv_avg`
或 diff，因此停止后续数据集 sweep。

这说明当前抽出版可以生成 bitstream 并加载到 U55C，但 kernel/host 返回路径在
最小数据集仍未闭合。它不是可晋级版本，也不是可覆盖正式 `source.diff` 的性能
改进补丁。

## 优化对象

只优化 single TAPA SpMV：

```text
DLC/Cuper/kernels/Cuper.cpp
DLC/Cuper/kernels/detail/cuper_spmv_tasks.hpp
cfg/connectivity_cuper_spmv_u55c.cfg
host / Makefile 中 run-cuper-tapa-spmv 相关路径
```

不把 `CuperPcg` 的 PCG controller、FP64 dot/update、`init_spmv` / `iter_spmv`
阶段作为本目标的直接评价对象。

## 优先问题

1. 复核 `thermal2_n262144` 和完整 `thermal2` 的 timeout/边界是否仍存在。
2. 若存在，先定位是数据规模、row 编码、stream drain、HBM 访问还是 host/XRT
   启动/等待逻辑造成。
3. 任何性能优化都必须保持和 CPU SpMV 校验一致，不能只看 kernel 返回。
4. demo 结果必须和当前标准 `cuper-tapa-spmv-u55c-20260522.xclbin` 以及既有
   HTML 记录同口径对比。

## source.diff 规则

本目标遵循先测试后写正式 diff：

- 测试失败、性能退步或未完成板上验证时，不覆盖正式 `source.diff`。
- 若只是为了记录探索过程，写入 `changes.md` / `testing.md`，不要把草稿补丁当成有效补丁。
- 当 demo 在板上确认有效后，再生成本目录 `source.diff`。
