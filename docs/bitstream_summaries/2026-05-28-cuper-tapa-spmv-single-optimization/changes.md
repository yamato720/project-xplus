# 这一目标要改什么

## 当前阶段

本目录第一轮 demo 曾把当前 TAPA-PCG 里的服务化 SpMV 抽成单 SpMV kernel，
并生成可单独上板测试的 `cuper-tapa-spmv` demo bitstream。该历史 demo 在
`thermal2_n16` 上板 smoke 中 timeout，没有晋级。

当前源码已经改为新的边界：`CuperPcgSpmv(...)` 不再复用 PCG service 控制壳，
而是保留 kernel 名和 host/demo 入口，内部回到 Cuper 风格 one-shot SpMV 图。

2026-05-29 已按这个边界重新生成 one-shot `CuperPcgSpmv` demo bitstream，并覆盖
`395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin`。旧 service 抽出版的
timeout 结论只保留为历史记录，不再对应当前同名 demo 文件。当前 one-shot demo
已完成 demo-only 上板测试，可返回到完整 `thermal2`；共同成功点性能略慢于
standalone TAPA Cuper SpMV 标准，因此本轮仍不更新正式 `source.diff`。

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

## 2026-05-29：one-shot Cuper-compatible demo 构建完成

当前 `CuperPcgSpmv(...)` 已改为复用 `detail/cuper_spmv_tasks.hpp` 中的 Cuper
one-shot task graph，不再接 `pcg_spmv_service.hpp` 的 command/stop/service
控制壳。这样 single SpMV demo 的测量口径接近满血 `Cuper(...)`，不会把 PCG
service 控制开销混进单 SpMV 结果。

构建结果：

```text
log: logs/cuper_tapa_pcg_spmv_hw_parallel_20260528_222446.log
xclbin: cuper-tapa-spmv-u55c-20260528-demo-build/hw/CuperPcgSpmv.xclbin
demo: 395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin
UUID: c95c1dfc-20ca-9152-279e-bafdf35fdc3d
SHA256: 19d227179db7f22adfd12e78da119a99d102c59ebe25df686a652c6715ea95f2
DATA/KERNEL/HBM clock: 147 / 500 / 418 MHz
Total elapsed time: 7h 29m 0s
```

2026-05-29 已完成 demo-only 上板测试，日志在
`logs/codex_two_demo_test_20260529_1300/`。`thermal2_n16`、
`thermal2_n65536`、`thermal2_n131072`、`thermal2_n262144` 和完整
`thermal2` 均返回且 diff 通过；完整 `thermal2` 的
`spmv_avg=1.781541 ms`。共同成功点上相对 standalone TAPA Cuper SpMV 标准约为
`1.03x` 到 `1.08x`，即略慢；但标准旧记录在 `thermal2_n262144` 和完整
`thermal2` 为 timeout，本 demo 成功边界更大。正式 `source.diff` 仍不更新。

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

## 2026-05-28：统一 command/stop 广播 helper

本次继续做协议层统一，不改变 full-PCG 或 single SpMV 的任务图结构：

- `pcg_common.hpp` 新增 `pcg_make_spmv_command()` 和
  `pcg_make_spmv_stop_command()`，集中定义普通 SpMV command 与 stop command
  的字段；
- `pcg_common.hpp` 新增 `pcg_send_spmv_command()` 和
  `pcg_send_spmv_stop()`，统一向 `Command_Stream[0..1]` 与
  `Matrix_Command_Stream[0..15]` 广播；
- `Pcg_Controller` 的 init `A*x0`、每轮 `A*p` 和最终 stop 改为调用公共 helper；
- `Pcg_SingleSpmv_Controller` 的单次 SpMV command 和 stop 也改为调用同一组
  helper。

这个统一的边界：

- 没有把 full `Pcg_Controller` 塞进 `CuperPcgSpmv`；
- 没有改变 writer-done drain 屏障；
- 没有改变 full-PCG 的 checker/sort stop、stage timer 或 metrics；
- 没有改变 standalone `Cuper(...)`。

预期收益：

- 以后若 command 字段、stop 语义或 16 路 HBM matrix loader 发令顺序需要调整，
  single service SpMV 和 full `CuperPcg` 会同步改到；
- 减少两边 controller 漂移，避免 single SpMV demo 修了一处但 full-PCG 漏改。

已完成软件验证：

```bash
git diff --check
make cuper-tapa-pcg-host
make cuper-tapa-pcg-fpga-host
timeout 180s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n16 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 180s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n16 MAX_ITERS=1 DIFF_TOL=1e-1
timeout 240s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 240s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 MAX_ITERS=1 DIFF_TOL=1e-1
```

结果：`n16/n1024` 的 service single SpMV 和 full `CuperPcg` 软件仿真都通过。
本轮只是源码统一和软件正确性验证，不启动硬件构建，不更新正式 `source.diff`。

## 2026-05-28：共享 SpMV 包数/对齐计数 helper

本次继续补“单 SpMV 优化能反馈到 PCG”的代码边界。之前 service 里有多处重复的
包数和 padding 公式，例如：

- packed `float_v16` 包数：`ceil(n/16)`；
- accumulator 清零组数：按 `HBM_CHANNEL_NUM * 16` 对齐；
- accumulator 输出组数：按 `HBM_CHANNEL_NUM * 2` 对齐；
- checker 需要完整消费的 PE 对齐输出数。

这些公式同时影响 `CuperPcgSpmv(...)` 和 full `CuperPcg(...)` 的 service SpMV。
如果以后排查大规模边界、padding drain 或 65536 附近问题，不应该在 single demo
和 full-PCG 路径各改一份。

源码改动：

- `pcg_common.hpp` 新增：
  - `pcg_num_float_v16_packets()`
  - `pcg_num_accumulator_init_groups()`
  - `pcg_num_accumulator_outputs()`
  - `pcg_num_checker_pe_outputs()`
- `pcg_controller.hpp` 的 PCG 向量 packet 数改用公共 helper；
- `pcg_spmv_service.hpp` 的 vector loader、accumulator、full-PCG checker、
  single checker、single sort tree 和 single writer 均改用公共 helper；
- `pcg_spmv_service.hpp` 文件头补充同步边界：哪些 task 同时服务
  `CuperPcgSpmv(...)` 和 full `CuperPcg(...)`，哪些只是单 SpMV demo 包装层。

这个改动本身不追求性能提升，但它降低后续优化漂移风险：涉及包数、padding 和
drain 的修复会先落在共享 helper，再自然反馈到 PCG。

## 2026-05-28：共享 vector/checker/sort 细粒度 helper

本次继续清理 `pcg_spmv_service.hpp` 内 single SpMV demo 和 full
`CuperPcg` 的重复代码。目标不是把 full controller 塞进 single demo，而是把真正
会影响 SpMV 数据通路的基础循环抽成共享 helper。

源码改动：

- 新增 `pcg_read_vector_packets(...)`：
  - `Pcg_Vector_Loader` 用它读取 `X_spmv` / `P_spmv`；
  - `Pcg_Single_Vector_Loader` 用它读取单输入 `X`；
  - 后续如果优化 packed vector 读取节奏，两条路径会同步受益。
- 新增 `pcg_try_forward_checker_value(...)`：
  - 统一 checker 对一拍 `float_v2` 的读取、padding 过滤、`c_idx/o_idx`
    轮转和有效数据转发；
  - full-PCG checker 和 single checker 都调用它。
- 保留 `pcg_checker_forward_round(...)`：
  - 只作为“按固定输出数量 drain 一轮”的有限 helper；
  - 当前主要给 `Pcg_Single_Vector_Checker` 使用。
- 新增 `pcg_try_pack_float_v16(...)`：
  - full `Pcg_Mult_Sort_Tree` 和 single `Pcg_Single_Mult_Sort_Tree` 都用它把
    8 路 `float_v2` 合成 1 包 `float_v16`。

这次修正了一个抽象过粗导致的 full-PCG 死锁风险。最初版本让
`Pcg_Vector_Checker` 直接调用整轮 `pcg_checker_forward_round(...)`，只在两轮之间
检查 stop。`thermal2_n16` full-PCG 软件仿真因此在 180s timeout：checker 可能在
controller 还没来得及发 stop 时抢先进下一轮，然后等待不存在的新一轮输入。

最终做法：

- single SpMV checker：没有 stop stream，固定 drain 一轮后自然返回；
- full-PCG checker：共享同一个“单步转发” helper，但在等待每个输入期间持续检查
  `Stop_in`，保持原来的常驻服务退出语义。

已完成验证：

```bash
git diff --check
make -B cuper-tapa-pcg-host
make -B cuper-tapa-pcg-fpga-host
timeout 180s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n16 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 180s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n16 MAX_ITERS=1 DIFF_TOL=1e-1
timeout 240s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 240s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 MAX_ITERS=1 DIFF_TOL=1e-1
```

结果：四个软件点全部通过。`n1024` full-PCG 使用 `MAX_ITERS=1`，所以
`status=max_iter` 是预期结果。本轮未生成新 xclbin，正式 `source.diff` 不更新。

## 2026-05-28：single SpMV 去控制壳，回到 Cuper one-shot

本次按最新边界处理：single SpMV 不再承载 PCG service/control 优化。之前的
`Pcg_Single*` 包装层虽然能让 service 链在软件仿真里有限返回，但它只修 demo
控制壳，不能代表 full `CuperPcg(...)` 的优化；同时历史硬件 bitstream 已经在
最小数据集 timeout。

源码改动：

- `CuperPcgSpmv(...)` 保留历史 kernel 名、ABI 和 `run-cuper-tapa-pcg-spmv`
  host 入口；
- `CuperPcgSpmv(...)` 内部改用 `Cuper(...)` 同款一次性 task graph：
  `SpElement_list_ptr_Loader`、`Vector_Loader`、`Matrix_Loader`、`Core`、
  `Accumulator`、`Vector_Checker`、`Mult_Sort_Tree`、`Vector_Writer`；
- 删除当前源码中的 `Pcg_SingleSpmv_Controller`、`Pcg_Single_Vector_Loader`、
  `Pcg_Single_Vector_Checker`、`Pcg_Single_Mult_Sort_Tree`、
  `Pcg_Single_Vector_Writer`；
- `pcg_spmv_service.hpp` 文件头明确：本文件只服务 full `CuperPcg(...)` 的常驻
  SpMV service；
- host 侧仍保留 `--pcg-spmv-service` 兼容 flag，但输出标签改为
  `tapa-cuper-compat-demo`。

已完成验证：

```bash
git diff --check
make -B cuper-tapa-pcg-host
make -B cuper-tapa-pcg-fpga-host
timeout 180s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n16 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 240s make run-cuper-tapa-pcg-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 SPMV_REPEATS=1 DIFF_TOL=1e-1
timeout 180s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n16 MAX_ITERS=1 DIFF_TOL=1e-1
timeout 240s make run-cuper-pcg-tapa-fpga DATASET=data/suitesparse/Schmid/csr/thermal2_n1024 MAX_ITERS=1 DIFF_TOL=1e-1
```

结果：

- `CuperPcgSpmv` one-shot single SpMV：`n16/n1024` 软件仿真均 `status=ok`；
- full `CuperPcg`：`n16` 收敛，`n1024` 与 CPU 1iter 对齐；
- `n16` full-PCG 软件仿真仍有 TAPA stream leftover warning
  `Vector_Y_Stream[13]`，但结果返回且 diff 通过；该 warning 需要作为后续
  service drain 观察点；
- 当前没有启动新硬件构建，也没有生成新的 one-shot demo xclbin；
- `395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin` 仍是历史 service
  抽出版 bitstream，不代表当前源码。

## 优化对象

当前只优化两类明确分开的路径：

- single SpMV demo：`CuperPcgSpmv(...)` 作为 Cuper-compatible one-shot 图；
- full-PCG service/control：`CuperPcg(...)` 实际使用的 `Pcg_*` 常驻服务路径。

```text
DLC/Cuper/kernels/Cuper.cpp
DLC/Cuper/kernels/detail/cuper_top_graphs.hpp
DLC/Cuper/kernels/detail/pcg_spmv_service.hpp
DLC/Cuper/kernels/detail/pcg_common.hpp
cfg/connectivity_cuper_spmv_u55c.cfg
host / Makefile 中 run-cuper-tapa-pcg-spmv 相关路径
```

`CuperPcgSpmv(...)` 的 one-shot 改动只说明 single SpMV demo 路径；不能声明会同步
提升 PCG。PCG 性能/正确性优化必须落在 full `CuperPcg(...)` service 路径，并补
full-PCG 验证。涉及包数、padding、对齐输出和 command/stop 的公共规则，优先写进
`pcg_common.hpp`，不要在不同路径各写一份。

## 优先问题

1. 复核 `thermal2_n262144` 和完整 `thermal2` 的 timeout/边界是否仍存在。
2. 若存在，先定位是数据规模、row 编码、stream drain、HBM 访问还是 host/XRT
   启动/等待逻辑造成。
3. 任何性能优化都必须保持和 CPU SpMV 校验一致，不能只看 kernel 返回。
4. demo 结果必须和当前标准 `cuper-tapa-spmv-u55c-20260522.xclbin` 以及既有
   HTML 记录同口径对比；
5. demo 上有效后必须补 full `CuperPcg(...)` 软件或上板 smoke，确认同一 service
   改动没有破坏 PCG。

## source.diff 规则

本目标遵循先测试后写正式 diff：

- 测试失败、性能退步或未完成板上验证时，不覆盖正式 `source.diff`。
- 若只是为了记录探索过程，写入 `changes.md` / `testing.md`，不要把草稿补丁当成有效补丁。
- 当 demo 在板上确认有效后，再生成本目录 `source.diff`。
