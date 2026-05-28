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

## 2026-05-28：finite-exit 修复尝试

本次只处理 `CuperPcgSpmv` 单 SpMV demo 的有限退出问题，不改 standalone
`Cuper(...)` 标准路径，也不改 full-PCG `CuperPcg(...)` 的服务协议。

源码改动：

- 在 `pcg_spmv_service.hpp` 中新增 `Pcg_Single_Vector_Checker`：
  按一次 SpMV 的 `Row_num` 计算上游 accumulator 会产生的对齐输出包数，完整
  消费 padding，只把有效输出转发给下一段。
- 在 `pcg_spmv_service.hpp` 中新增 `Pcg_Single_Mult_Sort_Tree`：
  从 8 路 `float_v2` 收齐后打包成 `float_v16`，输出固定
  `ceil(Row_num/16)` 包后自然返回。
- 调整 `Pcg_SingleSpmv_Controller`：
  不再向 checker/sort 发送 stop token；writer 完成后只负责关闭 loader/core
  和 vector destroy 这类仍保持常驻服务语义的任务。
- 调整 `cuper_top_graphs.hpp` 中的 `CuperPcgSpmv(...)` task graph：
  单 SpMV 抽出版改接 `Pcg_Single_Vector_Checker` 和
  `Pcg_Single_Mult_Sort_Tree`，full-PCG 仍使用原来的 stop-driven
  `Pcg_Vector_Checker` / `Pcg_Mult_Sort_Tree`。

预期收益：

- 避免上一版 writer 写完 1 个有效 `Y_out` 包后立刻通知 controller 发 stop，
  导致 checker/sort tree 或上游 padding 输出未 drain 完，进而让硬件
  `Finish` 永久等待。
- 让单 SpMV demo 的尾端更接近原始 `Cuper(...)` 一次性 task graph：固定数据量
  结束，而不是依赖异步 stop 抢停。

已完成验证：

```bash
git diff --check
timeout 180s make run-cuper-tapa-pcg-spmv \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  SPMV_REPEATS=1 DIFF_TOL=1e-1
```

软件仿真结果为 `status=ok`，`max_abs_diff=3.755767679081e-07`，
`spmv_avg=26.542003 ms`。

硬件构建已进入 VPL：

```text
session: project-xplus-cuper-tapa-pcg-spmv-hw
log: logs/cuper_tapa_pcg_spmv_hw_20260528_161221.log
```

当前仍不更新正式 `source.diff`。只有新 xclbin 上板后确认至少
`thermal2_n16` 能稳定返回，并继续完成 demo-only sweep 或明确证明边界改善时，
才考虑把本补丁写入 `source.diff`。

## 2026-05-28：service 内部去掉 Iteration_num

本次按用户要求，把单 SpMV 抽出版和 full `CuperPcg` 共同使用的
`pcg_spmv_service.hpp` 统一为“一条 command 只执行一次 SpMV”。PCG 的多轮迭代
由 controller 多次发送 command 表示，不再把 `Iteration_num` 塞进 command 让
service 内部循环。

源码改动：

- `pcg_common.hpp`：`CuperSpmvCommand` 删除 `iteration_num`，只保留
  `stop` 和 `vector_source`；
- `pcg_controller.hpp`：init 阶段和每轮 A*p 阶段发送普通 command，不再写
  `command.iteration_num`；
- `pcg_spmv_service.hpp`：
  - ptr/vector/matrix loader 收到一条 command 后只发/读一次 SpMV 所需数据；
  - `Pcg_Core`、`Pcg_Accumulator` 不再读取或转发 `Iteration_num`；
  - `Pcg_Single_Vector_Checker`、`Pcg_Single_Mult_Sort_Tree`、
    `Pcg_Single_Vector_Writer` 都只按一次 SpMV 的固定输出量返回；
- `cuper_top_graphs.hpp`：`CuperPcgSpmv(...)` 仍保留 ABI 参数 `Iteration_num`，
  但内部 `(void)Iteration_num`，不再把它传给 service task。

保留不变：

- standalone `Cuper(...)` 仍使用 `Iteration_num` 作为 benchmark 重复次数；
- full `CuperPcg(...)` 的 PCG 迭代次数仍由 `Max_iters` 控制；
- 本轮不启动新的硬件构建，不更新正式 `source.diff`。

本次软件验证：

```bash
make download-suitesparse-data DATASETS="thermal2_n4096 thermal2_n16384 thermal2_n65536 thermal2_n131072 thermal2_n262144 thermal2"
git diff --check
make cuper-tapa-pcg-host
make cuper-tapa-pcg-fpga-host
timeout 180s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n16 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 180s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n16 MAX_ITERS=1 DIFF_TOL=1e-1
timeout 240s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 240s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 MAX_ITERS=1 DIFF_TOL=1e-1
timeout 300s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n4096 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 300s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n4096 MAX_ITERS=1 DIFF_TOL=1e-1
```

结果：`CuperPcgSpmv` service single SpMV 在 `n16/n1024/n4096` 均 `status=ok`；
full `CuperPcg` 软件仿真在 `n16` 收敛，在 `n1024/n4096` 与 CPU 1iter 结果对齐。

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
