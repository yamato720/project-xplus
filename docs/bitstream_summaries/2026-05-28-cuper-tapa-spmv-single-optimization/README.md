# 2026-05-28 Cuper TAPA single SpMV 优化目标记录

## 版本信息

- 主线：`cuper-tapa-spmv`
- 状态：第一版 `CuperPcgSpmv` PCG service 抽出版 demo bitstream 已生成并放入
  `395bitstream/`，但 2026-05-28 demo-only 上板 smoke 在 `thermal2_n16`
  两次 180s timeout，未晋级。后续 finite-exit / service helper 清理均只作为
  历史探索记录保留。当前源码已经按最新边界改成 **Cuper-compatible one-shot
  single SpMV**：`CuperPcgSpmv(...)` 保留历史 kernel 名和 host/demo 入口，但
  内部复用 `Cuper(...)` 同款 `SpElement_list_ptr_Loader` / `Vector_Loader` /
  `Matrix_Loader` / `Core` / `Accumulator` / `Vector_Checker` / `Mult_Sort_Tree` /
  `Vector_Writer`，不再接 `pcg_spmv_service.hpp` 的 command/stop/service 控制壳。
  2026-05-29 已生成新的 one-shot demo xclbin 并覆盖 single-SpMV demo 槽；
  同日 demo-only 上板测试已通过到完整 `thermal2`。该 demo 随后已从
  `395bitstream/` 移入
  `bitstream_archive/2026-05-29-tapa-pcg-spmv-demo-candidates/`。本轮只更新测试报告，
  不更新正式 `source.diff`
- 当前标准版：`395bitstream/cuper-tapa-spmv-u55c-20260522.xclbin`
- 已归档 demo：`bitstream_archive/2026-05-29-tapa-pcg-spmv-demo-candidates/cuper-tapa-spmv-u55c-20260528-demo.xclbin`
- 标准基线入口：`DLC/Cuper/kernels/Cuper.cpp` 中的 `Cuper(...)`
- 本轮抽出版入口：`DLC/Cuper/kernels/Cuper.cpp` 中的 `CuperPcgSpmv(...)`
- 标准 SpMV 文件：`DLC/Cuper/kernels/detail/cuper_spmv_tasks.hpp`
- 当前 demo 实现文件：`DLC/Cuper/kernels/detail/cuper_top_graphs.hpp` +
  `DLC/Cuper/kernels/detail/cuper_spmv_tasks.hpp`
- 构建目录：`cuper-tapa-spmv-u55c-20260528-demo-build/`
- 构建日志：`logs/cuper_tapa_pcg_spmv_hw_parallel_20260528_222446.log`
- 生成文件：测试时同步为 `395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin`；
  当前已归档到
  `bitstream_archive/2026-05-29-tapa-pcg-spmv-demo-candidates/cuper-tapa-spmv-u55c-20260528-demo.xclbin`
- UUID：`c95c1dfc-20ca-9152-279e-bafdf35fdc3d`
- SHA256：`19d227179db7f22adfd12e78da119a99d102c59ebe25df686a652c6715ea95f2`
- DATA/KERNEL/HBM clock：147 / 500 / 418 MHz

## 2026-06-18 补充：Jacobi 目录 SpMV-only compact16 demo

本轮新增一个独立的 `CuperSpmvServiceOnly` demo artifact，用于探索 Cuper 数据格式在
单 HBM 内部继续消除 PE-lane padding 后的边界。它属于 `cuper-tapa-spmv` demo
候选，但源码入口在：

```text
DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp
```

顶层为：

```text
CuperSpmvServiceOnly
```

这版只计算 `Y=A*X`，不拆 `A=D+R`，不取负 `X`，也不执行 Jacobi update。它打开
`JACOBI_SPMV_COMPACT_PE=1`，host 在每个 HBM channel 的 batch 内把 8 条 PE lane
紧凑打包，并在 `rowIdx[16:14]` 记录原 lane id。kernel 端用 compact core 和
compact accumulator 解码 lane tag，再把结果累加回对应 lane。

生成文件：

```text
395bitstream/cuper-tapa-spmv-u55c-20260618-compact16-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260618-compact16-demo.xclbin.info
```

构建目录和日志：

```text
cuper-jacobi-spmv-compact16-build/
cuper-jacobi-spmv-compact16-build/logs/build_hw_tmux.log
```

版本信息：

```text
Kernel: CuperSpmvServiceOnly
UUID: 7f1e6302-e2a1-05e5-ab24-42a81b9f1488
SHA256: 2ec7758129ea44dfadd617b97587030de27a0f20d44b56f4bb727749768186b6
DATA/KERNEL/HBM clock: 200 / 500 / 448 MHz
Timing: HBM WNS -0.006 ns, TNS -0.007 ns, setup failing endpoints 2
```

本机 software simulation 已通过 `thermal2_n1024`、`thermal2_n65536` 和完整
`thermal2`。服务器侧上板 sweep 也已通过全部 8 个 `thermal2` 数据集，日志为：

```text
logs/spmv_compact16_hw_sweep_20260618_174007/
```

功能结论：compact16 可跑通完整 `thermal2`，全部 `rc=0`、`Status=1`、
`Error Num=0`。full `thermal2` 的矩阵读取 beat 从 `1,373,424` 降到
`1,187,402`，节省约 `13.54%`。

性能结论：不建议晋级标准，也不更新正式 `source.diff`。full `thermal2` 上
compact16 为 `30.2804 ms` / `0.5667 GFLOP/s`，只有 strip16 `1.29158 ms` 的
`0.043x`。当前 compact accumulator 为功能优先的串行 slot 分发结构，读 beat
节省被 lane tag 解码、slot 分发和回写累加路径吞掉；后续若要把读 beat 节省转化为
稳定性能提升，需要继续重写动态/均衡 SpMV 协议。

后续 C 侧 `pack_profile` 已补 reorder-free 上限评估。完整 A 口径下，`thermal2`
16 路当前格式密度为 `78.09%`，strip16 为 `83.00%`，compact-scheduled 为
`90.40%`；如果改成 lane-static reorder-free stream，密度可达 `99.83%`，几乎等于
real-compact 下限。因此下一步硬件 v2 应优先保持固定 lane accumulator，同时重写
host packer 和每 lane/每 HBM 的长度协议，而不是继续修 compact16 的动态 lane 写回。

## 2026-06-18 补充：Jacobi 目录 SpMV-only lanereal16 demo

本轮又新增一个 `CuperSpmvServiceOnly` demo artifact，用于验证固定 lane 后端下
“只传真实元素、去掉 PE 内部 reorder holes”的第一步硬件边界。它属于
`cuper-tapa-spmv` demo 候选，源码入口仍在：

```text
DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp
```

顶层为：

```text
CuperSpmvServiceOnly
```

这版只计算 `Y=A*X`，不拆 `A=D+R`，不取负 `X`，也不执行 Jacobi update。它打开
`JACOBI_SPMV_LANE_STATIC_REAL=1`，host 在每个 HBM channel、每个 batch 内按固定
lane 保留真实元素，仍保持 `slot p -> lane p`。kernel 端复用 strip-style
ptr/loader/core 和普通 `Accumulator`，避免 compact16 的动态 lane tag 累加路径。

生成文件：

```text
395bitstream/cuper-tapa-spmv-u55c-20260618-lanereal16-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260618-lanereal16-demo.xclbin.info
```

构建目录和日志：

```text
cuper-jacobi-spmv-lanereal16-build/
cuper-jacobi-spmv-lanereal16-build/logs/build_hw_tmux.log
```

版本信息：

```text
Kernel: CuperSpmvServiceOnly
UUID: 98358acf-f40e-4f2f-b77f-4a25c24f4473
SHA256: c8ef2426248a1acd4d02a75da39d72439c1cabdd12450428cfa83ce0baf1b49d
DATA/KERNEL/HBM clock: 197 / 500 / 450 MHz
Timing: WNS -0.073 ns, TNS -4.957 ns, setup failing endpoints 215
```

本机 software simulation 已通过 `thermal2_n1024` 和 `thermal2_n65536`，均为
`Correctness Verification: Passed`、`Error Num=0`。读包侧收益明显：
`thermal2_n1024` matrix read beats 从 `2624` 降到 `883`；
`thermal2_n65536` 从 `68464` 降到 `57472`，节省 `16.0552%`。

服务器侧上板 sweep 已完成，日志为：

```text
logs/spmv_lanereal16_hw_sweep_20260618_233431/
```

8 个 `thermal2` 数据集全部 `rc=0`、`Status=1`、`Correctness Verification: Passed`、
`Error Num=0`。完整 `thermal2` 上，lanereal16 时间为 `2.35566 ms`，吞吐为
`7.2848 GFLOP/s`，matrix read beats 从 `1,373,424` 降到 `1,151,370`，节省
`16.1679%`。

性能结论：lanereal16 已经替代 HTML 主报告里前一条异常慢的 compact16 曲线，作为
固定 lane 去 reorder-hole 的实测线；但它仍慢于 strip16。完整 `thermal2` 上 strip16
为 `1.29158 ms` / `13.2865 GFLOP/s`，lanereal16 只有 strip16 的约 `0.55x`。当前
限制仍是 `lane-static real/batch`，不是最终的 `lane-static real/stream`；现有 core
仍按 column batch 装载 X。HLS 报告显示
`Accumulator_Pipeline_cuper_acc_accumulate` 达成 II=`5`，而 strip16 对应路径为
II=`2`，上板结果也支持后端 accumulator 吞吐是主要瓶颈。因此这版不建议晋级，也不
更新正式 `source.diff`。

## 2026-06-20 补充：SpMV-only RTL owner-bank 乱序 accumulator demo

本轮新增一个 `CuperSpmvServiceOnly` demo artifact，用于验证把 lane-static real
路径后的 HLS accumulator 替换为自定义 RTL owner-bank accumulator 后，资源和时序是否
能够支撑完整 bitstream。它仍属于 `cuper-tapa-spmv` demo 候选，源码入口仍在：

```text
DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp
```

顶层为：

```text
CuperSpmvServiceOnly
```

这版只计算 `Y=A*X`，不拆 `A=D+R`，不取负 `X`，也不执行 Jacobi update。宏组合为：

```text
JACOBI_TOP=CuperSpmvServiceOnly
JACOBI_SPMV_ONLY=1
JACOBI_HBM_CHANNELS=16
JACOBI_SPMV_LANE_STATIC_REAL=1
JACOBI_SPMV_OOO_ACCUMULATE=1
JACOBI_SPMV_OOO_ACCUMULATE_RTL=1
```

生成文件：

```text
395bitstream/cuper-tapa-spmv-u55c-20260620-ooobank16-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260620-ooobank16-demo.xclbin.info
```

构建目录和日志：

```text
cuper-tapa-spmv-ooo-bank-rtl-hw-150m-20260619-build/
cuper-tapa-spmv-ooo-bank-rtl-hw-150m-20260619-build/logs/build_hw_tmux.log
```

版本信息：

```text
Kernel: CuperSpmvServiceOnly
UUID: 22b0a282-c282-cfaf-e45a-f8bebf4cc644
SHA256: a5ab4ba8a601bb12c3b737e318da28c29a3e4bdd2c037a9e670ac31a5a9f51b4
DATA/KERNEL/HBM clock: 149 / 500 / 450 MHz
Timing: WNS -0.005 ns, TNS -0.017 ns, setup failing endpoints 9
```

HBM 映射为：`Matrix_data_0..15 -> HBM[0..15]`，`SpElement_list_ptr -> HBM[16]`，
`X -> HBM[17]`，`Y_out -> HBM[18]`，`Status -> HBM[30]`，`Metrics -> HBM[31]`。
最终资源为 CLB LUT `27.22%`、CLB `53.20%`、BRAM Tile `65.48%`、URAM `53.33%`、
DSP `7.17%`。相比失败的 128 owner-lane RTL 实例方向，16 owner-bank 包装后 routed
完成，时序只剩 `-0.005 ns` 的极小 setup violation。

本机验证状态：

- Verilator owner-bank 小仿真通过：`cycles=69`、`real_events=40`、`outputs=16`、
  `cycles_per_event=1.725`；
- TAPA software smoke 通过 `thermal2_n1024`，`Correctness Verification: Passed`、
  `Error Num=0`；
- 硬件 bitstream 已生成并同步到 `395bitstream/`；
- 服务器上板 sweep 尚未执行，因此这版暂不写入 HTML 性能曲线，也不更新正式
  `source.diff`。

## 目标

本目录当前负责 **single SpMV demo 与 full-PCG service/control 的拆分边界**，
并作为后续 single SpMV 回归基线记录。最新结论是：单 SpMV demo 不再承载 PCG
控制优化；2026-05-29 one-shot demo 已能跑完整 `thermal2`，共同成功点接近但
略慢于满血 `Cuper(...)` 标准。

当前目标：

1. 将 `CuperPcgSpmv(...)` one-shot demo 作为 single SpMV 回归基线和边界检查；
2. 保持完整 `thermal2` 可返回、diff 通过，并监控共同成功点不要明显退化；
3. `CuperPcgSpmv(...)` 只做 Cuper 风格 one-shot single SpMV，不引入
   `Pcg_Single*` controller/command/stop/writer-done；
4. 主优化目标已切到 full `CuperPcg(...)` 的 controller/dot/update 路径，记录在
   `2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/`；
5. 若要证明 full-PCG 性能提升，必须修改 full `CuperPcg(...)` 实际路径，并补
   full-PCG 软件或硬件验证；不能只凭 single SpMV demo 结论判断。

## 和旧目标的区别

旧目录：

```text
docs/bitstream_summaries/2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/
```

旧目标是把 `CuperPcg` 内嵌 SpMV 逐步接近 standalone/native TAPA Cuper SpMV，
属于 full-PCG 体系内的 SpMV 服务路径优化记录。

本目录曾尝试把 full-PCG 的 service SpMV 抽成 `CuperPcgSpmv(...)` 单独测试，
但该路线在板上最小数据集 timeout，且容易把 single SpMV 和 PCG service 控制混在
一起。当前边界已改为：`CuperPcgSpmv(...)` 只保留 demo kernel 名和 ABI，
内部回到 Cuper 风格 one-shot 图；真正影响 full-PCG 的优化仍回到
`CuperPcg(...)` service 路径里做。

## 2026-05-28 补充：先生成 PCG service 单 SpMV

用户本轮要求先不优化，先给当前 TAPA-PCG 版补一个单 SpMV bitstream。因此这次
新增 `CuperPcgSpmv(...)` 顶层：外部仍按 `cuper-tapa-spmv` demo 归类，ABI 保持
`Matrix_data + X -> Y_out`，内部则走 `CuperPcg` 当前使用的
`pcg_spmv_service.hpp` 服务化 SpMV 链。它用于回答“full-PCG 内嵌 SpMV 单独拉出来
到底有多快”，并作为后续 PCG 可同步 SpMV 优化入口；不是标准 `Cuper(...)` 的
优化补丁。

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

## 2026-05-29 补充：one-shot Cuper-compatible demo

在去掉 single SpMV 路径里的 PCG service/control 外壳后，已重新生成
`CuperPcgSpmv` demo bitstream。当前文件仍使用历史 demo 文件名：

```text
395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin
```

但它已经不再是旧 service 抽出版。当前 UUID 为
`c95c1dfc-20ca-9152-279e-bafdf35fdc3d`，SHA256 为
`19d227179db7f22adfd12e78da119a99d102c59ebe25df686a652c6715ea95f2`，
DATA/KERNEL/HBM clock 为 `147/500/418 MHz`。旧 2026-05-28 timeout 结论只对应
旧 UUID `08f1f2dc-8c44-007f-a0a5-4dce1236ddd9`，不能再套到当前同名 demo 文件上。

构建日志：

```text
logs/cuper_tapa_pcg_spmv_hw_parallel_20260528_222446.log
```

构建结果：

```text
Run vpl: FINISHED. Run Status: impl Complete!
Created .../cuper-tapa-spmv-u55c-20260528-demo-build/hw/CuperPcgSpmv.xclbin
Total elapsed time: 7h 29m 0s
```

2026-05-29 已完成 demo-only 上板测试，日志目录：

```text
logs/codex_two_demo_test_20260529_1300/
```

本轮只跑当前 single-SpMV demo，不重跑四个标准 bitstream。测试命令口径：

```bash
timeout 180s make run-cuper-tapa-pcg-spmv TARGET=hw \
  DATASET=data/suitesparse/Schmid/csr/<dataset> \
  BITFILE=395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin \
  SPMV_REPEATS=3 DIFF_TOL=1e-1
```

结果：

| 数据集 | rc | spmv_avg ms | GFLOP/s | max_abs_diff | max_rel_diff |
| --- | ---: | ---: | ---: | ---: | ---: |
| `thermal2_n16` | 0 | 0.068825 | 0.000465 | 3.7558e-07 | 7.6333e-08 |
| `thermal2_n65536` | 0 | 0.149121 | 5.8610 | 2.3596e-06 | 8.2651e-04 |
| `thermal2_n131072` | 0 | 0.235847 | 7.3443 | 3.2888e-06 | 1.3183e-03 |
| `thermal2_n262144` | 0 | 0.425394 | 8.2229 | 3.2376e-06 | 3.1482e-03 |
| `thermal2` | 0 | 1.781541 | 9.6325 | 1.6333e-06 | 4.4911e-05 |

结论：当前 one-shot demo 功能边界明显好于旧 standalone TAPA Cuper SpMV 标准记录，
标准旧记录在 `thermal2_n262144` 和完整 `thermal2` 为 180s timeout，而本 demo
两个点都返回且 diff 通过。共同成功点上，当前 demo 比标准 SpMV 旧记录略慢：
`thermal2_n16` 约 `1.03x`、`thermal2_n65536` 约 `1.08x`、
`thermal2_n131072` 约 `1.07x`。因此它是成功边界改善候选，但还不是明确的性能
提升版；正式 `source.diff` 本轮不更新。

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

## 2026-05-28 补充：统一 SpMV command 广播 helper

上一节去掉 `Iteration_num` 后，full `Pcg_Controller` 和 single
`Pcg_SingleSpmv_Controller` 仍各自手写一份 command/stop 广播循环。本轮把这部分
收敛到 `pcg_common.hpp`：

- `pcg_make_spmv_command(vector_source)`：构造一次普通 SpMV command；
- `pcg_make_spmv_stop_command()`：构造 stop command；
- `pcg_send_spmv_command(...)`：同时写入 `Command_Stream[0..1]` 和
  `Matrix_Command_Stream[0..15]`；
- `pcg_send_spmv_stop(...)`：用同一套路径广播 stop。

这次不是把 full-PCG controller 直接复用于 single SpMV。两边仍保留不同控制语义：
full-PCG controller 负责 PCG 初始化、迭代、checker/sort stop 和 metrics；
single SpMV controller 仍用 writer-done 作为 drain 屏障，然后只关闭
loader/core/destroy 常驻服务链。统一范围只限 command 构造和广播协议。

本轮软件复测：

| 路径 | 数据集 | 结果 | 关键误差 |
| --- | --- | --- | --- |
| `CuperPcgSpmv` service single SpMV | `thermal2_n16` | `status=ok` | `max_abs_diff=3.7558e-07` |
| `CuperPcgSpmv` service single SpMV | `thermal2_n1024` | `status=ok` | `max_abs_diff=1.3506e-06` |
| full `CuperPcg` FPGA-PCG software sim | `thermal2_n16` | `converged` | `max_abs_diff=1.0868e-08` |
| full `CuperPcg` FPGA-PCG software sim | `thermal2_n1024` | `max_iter`，与 CPU 1iter 对齐 | `max_abs_diff=9.2782e-10` |

本轮没有生成新 xclbin，也不更新正式 `source.diff`。

## 2026-05-28 补充：共享向量/checker/sort helper

本轮继续把 single SpMV demo 和 full `CuperPcg` 共同依赖的 SpMV service 逻辑往
公共 helper 收敛：

- `pcg_read_vector_packets(...)`：统一 packed `float_v16` 向量 HBM 读取循环；
- `pcg_try_forward_checker_value(...)`：统一 checker 对一拍 `float_v2` 的
  padding 过滤和转发；
- `pcg_checker_forward_round(...)`：给 single SpMV finite-exit checker 使用，
  按固定输出数量完整 drain 一轮；
- `pcg_try_pack_float_v16(...)`：统一 8 路 `float_v2` 到 1 路 `float_v16`
  的打包逻辑。

这里踩到一个重要边界：full-PCG 的 `Pcg_Vector_Checker` 是常驻服务，不能只调用
“整轮 drain” helper 后再检查 stop。软件仿真中曾出现 `thermal2_n16`
`MAX_ITERS=1` 在 180s timeout，原因是 checker 在 stop token 到达前抢先进下一轮，
然后等待不存在的新一轮 `Vector_Y_Stream` 数据。最终修复为：共享“单步转发”逻辑，
但 full-PCG checker 在等待每个输入期间仍持续检查 `Stop_in`；single SpMV checker
则继续使用固定输出数量自然返回。

修正后软件复测：

| 路径 | 数据集 | 结果 | 关键误差 |
| --- | --- | --- | --- |
| `CuperPcgSpmv` service single SpMV | `thermal2_n16` | `status=ok` | `max_abs_diff=3.7558e-07` |
| `CuperPcgSpmv` service single SpMV | `thermal2_n1024` | `status=ok` | `max_abs_diff=1.3506e-06` |
| full `CuperPcg` FPGA-PCG software sim | `thermal2_n16` | `converged` | `max_abs_diff=1.0868e-08` |
| full `CuperPcg` FPGA-PCG software sim | `thermal2_n1024` | `max_iter`，与 CPU 1iter 对齐 | `max_abs_diff=9.2782e-10` |

本轮仍没有生成新 xclbin，也不更新正式 `source.diff`。

## 2026-05-28 补充：single SpMV 去掉 PCG service 控制壳

按最新边界，`CuperPcgSpmv(...)` 不再作为“从 full `CuperPcg(...)` service 链抠出
来的单 SpMV”。它现在只是保留历史 kernel 名和 `run-cuper-tapa-pcg-spmv`
入口的 Cuper-compatible demo：

- 删除当前源码中的 `Pcg_SingleSpmv_Controller`、`Pcg_Single_Vector_Loader`、
  `Pcg_Single_Vector_Checker`、`Pcg_Single_Mult_Sort_Tree`、
  `Pcg_Single_Vector_Writer` 等单 SpMV service 包装层；
- `CuperPcgSpmv(...)` 改用和 `Cuper(...)` 同款的一次性 task graph；
- `pcg_spmv_service.hpp` 只服务 full `CuperPcg(...)`；
- host 兼容保留 `--pcg-spmv-service` flag，但输出标签改为
  `tapa-cuper-compat-demo`，避免误读成 PCG service 抽出版。

软件验证：

| 路径 | 数据集 | 结果 | 关键误差 |
| --- | --- | --- | --- |
| `CuperPcgSpmv` Cuper-compatible one-shot | `thermal2_n16` | `status=ok` | `max_abs_diff=3.7558e-07` |
| `CuperPcgSpmv` Cuper-compatible one-shot | `thermal2_n1024` | `status=ok` | `max_abs_diff=1.3506e-06` |
| full `CuperPcg` FPGA-PCG software sim | `thermal2_n16` | `converged` | `max_abs_diff=1.0868e-08` |
| full `CuperPcg` FPGA-PCG software sim | `thermal2_n1024` | `max_iter`，与 CPU 1iter 对齐 | `max_abs_diff=9.2782e-10` |

注意：这段软件验证发生在 one-shot 硬件构建之前；当时
`395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin` 仍是历史 service 抽出版。
后续 2026-05-29 已重新生成并上板测试当前 one-shot demo。

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

- 后续 single SpMV demo / full-PCG service-control 边界相关源码改动、demo
  bitstream、测试结论和 HTML 摘要都写入本目录。
- `README.md` 写当前状态和是否建议晋级。
- `changes.md` 写每轮 single SpMV demo 具体改了什么。
- `testing.md` 写 demo-only 测试命令、数据、边界和关键输出。
- 正式 `source.diff` 只在板上测试确认性能提升、边界修复有效，或用户明确要求保留补丁时再生成。
