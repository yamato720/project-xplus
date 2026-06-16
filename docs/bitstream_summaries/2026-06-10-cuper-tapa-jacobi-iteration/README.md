# Cuper TAPA Jacobi Iteration 主线记录

这份记录对应第五条 Cuper 主线：`cuper-tapa-jacobi`。源码目录是：

```text
DLC/Cuper-jacobi-iteration/
```

当前状态：已完成 software/TAPA simulation demo，并接入 Project-XPlus 根
`Makefile` 的 `cuper-jacobi-*` 目标。`CuperJacobiMmapProbeOnly` split-bank
mmap-only debug xclbin 已通过 native XRT 上板 smoke，用于排除基本 kernel launch、
split-bank m_axi 写回和 BO sync 边界；当前同步 demo 是 `JACOBI_TRACE_LIGHT=1`
的完整 `CuperJacobiIteration` master-controller full graph debug 版。新版把 DATA
clock 降到 150 MHz，routed timing 已收敛，并已完成 demo-only 上板：单轮
`MAX_ITERS=1` 覆盖到完整 `thermal2`，完整固定轮数覆盖 `thermal2_n1024`、
`thermal2_n65536`、`thermal2_n131072`、`thermal2_n262144` 和完整 `thermal2`。
2026-06-16 另生成并同步了 `JACOBI_WIDE_HBM=1` 的 24 路 Matrix_data 实验 artifact；
该版 build 完成但 routed timing 未收敛，尚未上板验证。

## 版本定位

| 项目 | 内容 |
| --- | --- |
| 主线 | `cuper-tapa-jacobi` |
| 顶层 kernel | `CuperJacobiIteration` |
| 源码入口 | `DLC/Cuper-jacobi-iteration/kernels/Cuper.cpp` |
| 当前构建目录 | `cuper-jacobi-iteration-build/` |
| 当前 bitstream | `395bitstream/cuper-tapa-jacobi-u55c-20260615-demo.xclbin` |
| 当前实验 bitstream | `395bitstream/cuper-tapa-jacobi-u55c-20260616-demo.xclbin` |
| 版本状态 | software/TAPA simulation 通过；150 MHz timing-clean master-controller light-trace full graph 已完成 demo-only 上板单轮和完整固定轮数测试；wide-HBM 24 路实验版已构建但 timing 未收敛 |
| 是否建议晋级标准 | 暂不建议，当前仍是 debug demo，且硬件没有内部收敛 early-exit |

这条主线做普通 Jacobi iteration：

$$
x^{(k+1)} = D^{-1}(b - R x^{(k)})
$$

它不是 Jacobi 预条件子 PCG，不计算 PCG 的 `alpha/beta`，也不维护 `r/z/p`。

## 当前数据流

host 侧先把矩阵拆成 `A = D + R`。kernel 侧复用 Cuper 的 single SpMV service，
但 `Jacobi_Vector_Loader` 读取单个 `X` buffer 时把输入向量取负，所以 Cuper
service 输出的是 `-R*x_old`。后级 update path 直接计算：

$$
x_i^{(k+1)} = (b_i + (-R x^{(k)})_i)\mathrm{diag\_inv}_i
$$

当前实现用单个 `X` buffer 原地更新；`Status[1]` 固定为 `0`，表示最终结果在 `X`。
轮次推进由 `Jacobi_MasterController` 统一控制：每轮显式发矩阵 loader、SpMV compute
和 update command，等待 `Jacobi_XHbmWriter` 回传 X 写回 done ack 后再进入下一轮；
最终由 controller 统一广播 stop 并写 Status/Metrics。

## 当前 HBM/ABI

已板测通过的 `20260615-demo` connectivity 是 demo ABI，还没有做 HBM 压缩：

| 数据 | HBM |
| --- | --- |
| `SpElement_list_ptr` | HBM[0] |
| `Matrix_data_0..15` | HBM[0..15] |
| `B` | HBM[20] |
| `Diag_inv` | HBM[21] |
| `X` | HBM[22] |
| `Status` | HBM[24] |
| `Metrics` | HBM[25] |
| `Debug` | HBM[26] |

当前设计仍显式使用矩阵 16 个 HBM 通道之外的向量/状态/debug 通道。后续如果目标是
压回 16 个 HBM，需要重新设计 X 转发、B/Diag_inv 供给和结果写回策略；这还没有进入
本轮实现。

`20260616-demo` 是 wide-HBM 实验 ABI：`Matrix_data_0..23` 映射到 HBM[0..23]，
`SpElement_list_ptr/B/Diag_inv/X/Status` 共享 HBM[30]，`Metrics/Debug` 共享 HBM[31]。
这版用于观察 24 路 Cuper 主矩阵通道的上限方向，但 timing 未收敛，不能作为已验证
功能或性能结论。

## 当前 demo bitstream

| 项目 | 内容 |
| --- | --- |
| 文件 | `395bitstream/cuper-tapa-jacobi-u55c-20260615-demo.xclbin` |
| `.info` | `395bitstream/cuper-tapa-jacobi-u55c-20260615-demo.xclbin.info` |
| 构建目录 | `cuper-jacobi-iteration-build/` |
| Kernel | `CuperJacobiIteration` |
| ABI | master-controller light-trace full graph；`B` HBM[20]，`Diag_inv` HBM[21]，`X` HBM[22]，`Status/Metrics/Debug` HBM[24]/HBM[25]/HBM[26] |
| UUID | `c37ecdbf-92ab-5d06-11bd-e2f9edc7f720` |
| SHA256 | `78c4ffdb9268aa5c1635bf2eefeed3b828e8a26e60ab3ccb8d795c9484d975a7` |
| DATA / KERNEL / HBM clock | `150 MHz` / `500 MHz` / `450 MHz` |
| 时序状态 | 已收敛，WNS `0.003 ns`，TNS `0.000 ns`，setup failing endpoints `0` |

这个文件是当前 full graph debug demo artifact，不是标准 bitstream。Vitis link 已完成
implementation 和 `.xclbin` 封装；v++ link 总耗时 `3h 27m 40s`。它覆盖了上一版
`20260614` timing-clean light-trace full graph demo，旧 UUID
`3fc9b8f4-901b-008f-8bc9-26ea3bf6f0c1` 的最小上板 `Finish()` timeout 结论只作为
历史记录，不对应当前 master-controller 文件。再上一版同名 `20260614` 15 路
light-trace full graph demo，旧 UUID
`ef3b1102-90ec-551a-d1e9-55fb6c023da5` 的 164 MHz timing-fail 结论只作为历史记录。
再上一版同名 `20260614` 7 路 light-trace full graph demo，旧 UUID
`6dfaf1e3-9707-7f46-b914-1f59ca240993` 的上板结果只作为历史记录，不对应当前文件。
再上一版 `20260613` no-debug full graph demo，旧 UUID
`b233c1af-6ba7-ebc5-8a5b-c56d348c53c7` 的构建结论只作为历史记录。

已同步的 `20260615-demo` light trace 接入 14 个关键 stream，跟随 master-controller
控制流记录 controller、loader、update 和写回可见性：

```text
controller, ptr_loader, vector_loader, coeff_loader,
pair_compute[0..7], pack_writer, x_hbm_writer
```

full isotope 47 路 trace 仍可通过 `JACOBI_TRACE_ISOTOPE=1` 打开，但它的
`Jacobi_DebugMonitor` 曾在 HLS 阶段消耗约 70GB 内存并长时间停留，不适合作为默认硬件
debug 构建。

服务器侧复测上一版 `20260614-demo` 7 路 light-trace xclbin 后，`thermal2_n16` /
`thermal2_n1024` 已通过，`thermal2_n65536` 仍卡在 `Finish()`。当前同步文件已经换成
light-trace xclbin，并让 host 在 trace ABI 下默认执行 60 次 pre-Finish 周期
BO sync；每 10 次采样会打印完整 Debug source 表。

2026-06-15 demo-only 上板日志位于
`logs/jacobi_full_graph_hw_20260615_223100_master_controller/`。该版已通过：

| 数据集 | 模式 | MAX_ITERS | CPU diff | FPGA kernel | 结果 |
| --- | --- | ---: | ---: | ---: | --- |
| `thermal2_n16` | 1iter | 1 | 1.17029 | 0.157074 ms | pass |
| `thermal2_n1024` | 1iter | 1 | 1.17326 | 0.144711 ms | pass |
| `thermal2_n4096` | 1iter | 1 | 1.29186 | 0.147567 ms | pass |
| `thermal2_n16384` | 1iter | 1 | 1.12363 | 0.194424 ms | pass |
| `thermal2_n65536` | 1iter | 1 | 1.11631 | 0.374841 ms | pass |
| `thermal2_n131072` | 1iter | 1 | 1.32690 | 0.646992 ms | pass |
| `thermal2_n262144` | 1iter | 1 | 1.41496 | 1.112300 ms | pass |
| `thermal2` | 1iter | 1 | 1.00000 | 4.806930 ms | pass |
| `thermal2_n1024` | 完整固定轮数 | 451 | 9.95398e-06 | 2.664400 ms | pass |
| `thermal2_n65536` | 完整固定轮数 | 743 | 9.95398e-06 | 182.212 ms | pass |
| `thermal2_n131072` | 完整固定轮数 | 842 | 9.89437e-06 | 411.684 ms | pass |
| `thermal2_n262144` | 完整固定轮数 | 900 | 9.95398e-06 | 882.205 ms | pass |
| `thermal2` | 完整固定轮数 | 24409 | 9.98378e-06 | 113035 ms | pass |

当前完整 `thermal2` 长运行不是死锁：60 次 pre-Finish 采样期间 Status/Metrics 仍保持
sentinel，但 `Finish()` 随后返回，最终 trace source 显示 controller 到
`done_round`，ptr/vector/coeff loader、8 路 pair compute、pack writer、X HBM writer
均进入 stop。需要注意，当前硬件仍是固定轮数；`Status=1` 表示到达 `MAX_ITERS`，
不是硬件内部收敛。

## 2026-06-16 wide-HBM artifact

| 项目 | 内容 |
| --- | --- |
| 文件 | `395bitstream/cuper-tapa-jacobi-u55c-20260616-demo.xclbin` |
| `.info` | `395bitstream/cuper-tapa-jacobi-u55c-20260616-demo.xclbin.info` |
| 构建目录 | `cuper-jacobi-wide-hbm-build/` |
| Kernel | `CuperJacobiIteration` |
| ABI | `JACOBI_WIDE_HBM=1`；`Matrix_data_0..23` HBM[0..23]，`SpElement_list_ptr/B/Diag_inv/X/Status` HBM[30]，`Metrics/Debug` HBM[31] |
| UUID | `9b42ccc8-7b2f-e182-cb77-317084abdca8` |
| SHA256 | `84f3926deca697975525ddff84800e1140cd83535a8e3fdf5d0ea19efff35afa` |
| DATA / KERNEL / HBM clock | `139 MHz` / `500 MHz` / `450 MHz` |
| 时序状态 | 未收敛，WNS `-0.501 ns`，TNS `-475.386 ns`，setup failing endpoints `2903` |

这版把 Cuper 主矩阵 HBM channel 从 16 扩到 24。更新路径仍沿用
`Jacobi_MasterController` 和 8 路 update pair lane；wide 模式下每个 pair lane 消费
3 路 accumulator 输出。生成前已做 wide-HBM host/software smoke，最终硬件 build
目录下 `thermal2_n16 MAX_ITERS=1` 通过，显示 `HBM_Channel Num: 24`、
`Slice Size: 96`、`Correctness Verification: Passed`。硬件 link 完成并生成
`.xclbin`，v++ 总耗时 `5h 42m 5s`。由于 timing 未收敛，当前只保存为性能方向
实验 artifact，尚未做上板验证，也不覆盖 `20260615` 已板测通过 demo 的结论。

上一版 mmap-only split-bank probe 的 2026-06-13 native XRT 上板 smoke 已通过：

```text
logs: logs/jacobi_mmap_probe_hw_20260613_214342/
ROW_NUM=16:   rc=0, wait_state=COMPLETED, Status[8..11]=1245921841,16,1,1
ROW_NUM=1024: rc=0, wait_state=COMPLETED, Status[8..11]=1245921841,1024,1,64
Debug[48..51]=1245921841,11,1,8192
```

这说明基本 launch、split-bank mmap 写回和 native XRT BO sync 是通的。该记录不代表
当前 full graph demo 已上板通过；当前 full graph 的失败边界仍需重新验证。

更早 finite-pair demo 上板 `thermal2_n16 MAX_ITERS=1` 仍 timeout，host 停在
`[tapa-invoke] after ReadFromDevice before Finish`。probe 期间 CU 已显示
`Status (IDLE)` 且 firewall GOOD，同时存在 `[CuperJacobiIter]` D 状态线程。
这说明当时优先怀疑 TAPA/FRT `Finish()` 或 XRT exec 清理路径，以及空 R 的
Vector_X drain 协议。随后 pre-Finish/empty-R 版本已把 host pre-Finish dump 和
`Batch_num==0` 空 R no-X-read/drain 修复打入源码和 `.xclbin`。

服务器侧随后复测上一版 UUID `5c9f0e72-5ea9-7142-1e90-690b72d30557`，`thermal2_n16`
和 `thermal2_n1024` 的 `MAX_ITERS=1` 均为 120s timeout，host 仍停在
`after ReadFromDevice before Finish`。pre-Finish dump 已经执行，但
Status/Metrics/Debug 全 0；probe 期间 CU 为 `IDLE`，firewall `GOOD`。随后曾追加
entry mmap probe 版完整 graph：

| 槽位 | 含义 |
| --- | --- |
| `Debug[0]` | DebugMonitor 入口 magic，阻塞写并等待 write response |
| `Debug[48..51]` | magic、debug stream 数、入口 phase、stop drain cycles |
| `Status[8..11]` | magic、`Row_num`、`Max_iters`、每轮 `float_v16` 包数 |
| `Metrics[8..11]` | 与 `Status[8..11]` 镜像的 double mmap probe |

这版 probe 源码当时通过 `thermal2_n16` / `thermal2_n1024` 的 debug ABI
software/TAPA simulation，并重新生成完整硬件 demo xclbin，同步到 Jacobi demo
槽。服务器侧复测该 UUID 后，`thermal2_n16` 与 `thermal2_n1024` 的
`MAX_ITERS=1` 均为 120s timeout，Status[8..11]、Metrics[8..11]、Debug[48..51]
入口 probe 全 0。下一步不再继续往完整 graph 里堆事件，而是先用
`CuperJacobiMmapProbeOnly` 和 native XRT runner 验证 mmap 写回边界。

当前源码已把完整 graph 的默认 debug 路径改为非阻塞：`JACOBI_DEADLOCK_DEBUG=1`
只启用 Debug buffer/event stream，不再默认入口阻塞写 Debug/Status/Metrics probe；
若要复现旧入口 probe，需要额外设置 `JACOBI_BLOCKING_ENTRY_PROBE=1`。host 也已把
Status/Metrics/Debug 改为 sentinel 初始化和 `read_write_mmap`，用于下一版 full graph
上板时判断 pre-Finish BO 是否被 kernel 覆盖。

2026-06-14 追加 `JACOBI_TRACE_ISOTOPE=1` 调试边界。trace 版会给完整 graph 的关键
task 分配 source 编号，并把每个 source 的最后 phase/lane/value/event 写到
`Debug[64 + source*4 ...]`。覆盖的 source 包括 dispatcher、ptr/vector loader、
16 路 matrix loader、16 路 accumulator、frame/coeff loader、8 路 pair compute、
pack writer 和 X HBM writer。业务 task 只非阻塞发事件，Debug BO 由单独 monitor
写回，host 在 `Finish()` 前先打印快照，因此下一轮上板即使继续卡在 `Finish()`，
也应能看到最后到达的数据流位置。trace connectivity 把 `Status/Metrics/Debug`
分到 `HBM[24]/HBM[25]/HBM[26]`。

## 当前 micro probe 工具

| 项目 | 内容 |
| --- | --- |
| Debug top | `CuperJacobiMmapProbeOnly` |
| Native runner | `cuper_jacobi_mmap_probe_xrt` |
| Same-bank link | `make cuper-jacobi-link-mmap-probe-xclbin` |
| Split-bank link | `make cuper-jacobi-link-mmap-probe-xclbin-split` |
| 当前同步版本 | 已被 full graph demo 覆盖；需要时重新构建或使用归档 xclbin 复测 |
| XO 验证 | 已生成 `cuper-jacobi-iteration-build/CuperJacobiMmapProbeOnly.xo` |

## 当前已记录测试

| 数据集 | 迭代 | 状态 | 关键输出 |
| --- | ---: | --- | --- |
| `cant.mtx` | 2 | 当前 deadlock-debug 单 `X` ABI 通过 | `Final buffer=0`, `Final diff=0`, `Error Num=0`, `spmv_update=103856 cycles` |
| `thermal2_n65536` | 1 | 当前 deadlock-debug 单 `X` ABI 通过 | `Final buffer=0`, `Final diff=0`, `Error Num=0`, `spmv_update=36081 cycles` |
| `thermal2_n262144` | 1 | 早期 software run 通过 | `Final diff=1.41496`, `Error Num=0`；需用当前 root target 补跑 |
| `thermal2_n16` | 1 | `JACOBI_TRACE_ISOTOPE=1` trace ABI software 通过 | `Error Num=0`, Debug[48..51]=`1245921841,47,1,8192` |
| `thermal2_n1024` | 1 | `JACOBI_TRACE_ISOTOPE=1` trace ABI software 通过 | `Error Num=0`, 47 个 source 槽位均可见 |
| `thermal2_n16` | 1 | `JACOBI_TRACE_LIGHT=1` trace ABI software 通过 | `Error Num=0`, Debug[48..51]=`1245921841,7,1,8192` |
| `thermal2_n1024` | 1 | `JACOBI_TRACE_LIGHT=1` trace ABI software 通过 | `Error Num=0`, 7 个关键 source 槽位均可见 |
| `thermal2_n1024` | 1 | 15 路 `JACOBI_TRACE_LIGHT=1` source software 通过 | `Error Num=0`, pair_compute[0..7] 槽位均可见 |
| `thermal2_n65536` | 1 | 15 路 `JACOBI_TRACE_LIGHT=1` source software 通过 | `Error Num=0`, pair_compute[0..7]、pack_writer、x_hbm_writer 均进入 stop |

详细命令和字段见 `testing.md`。

## 相关文档

```text
DLC/Cuper-jacobi-iteration/docs/jacobi_iteration.md
DLC/Cuper-jacobi-iteration/docs/jacobi_implementation_plan.md
DLC/Cuper-jacobi-iteration/docs/testing.md
docs/codex/coding.md
docs/codex/testing.md
395bitstream/README.md
```

## 待补

- 当前 demo 已完成 demo-only 上板，后续如果要晋级标准，需要决定固定轮数 ABI 是否可接受，
  或补硬件内部 diff/early-exit。
- 长运行时 Status/Metrics 仍主要在 kernel 结束后可见；若继续工程化，需要增加周期性进度写回。
- `source.diff` 暂不生成；当前结果证明功能边界打通，但仍是 debug demo，不是性能优化晋级候选。
