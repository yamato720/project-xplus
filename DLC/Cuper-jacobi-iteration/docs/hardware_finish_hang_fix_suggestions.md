# Jacobi Hardware Finish Hang 修改建议

## 当前结论

2026-06-11 新构建的 `CuperJacobiIteration.xclbin` 能完成 Vitis link 和 bitstream
生成，但上板运行仍没有通过。最小数据集 `thermal2_n16` 在 host 已经进入 TAPA
runtime 收尾阶段后卡住：

```text
[tapa-invoke] before WriteToDevice
[tapa-invoke] after WriteToDevice before Exec
[tapa-invoke] after Exec before ReadFromDevice
[tapa-invoke] after ReadFromDevice before Finish
```

这说明 host 侧输入、BO 创建、kernel 启动和 device-to-host read 都已经走到后段；
真正的问题更像是 TAPA graph 里仍有 task/stream 没有干净结束，导致 `Finish` 等不到
kernel 完成。它不再像最早那次是 `CL_INVALID_BUFFER_SIZE`，也不像单纯数值校验失败。

本轮测试对象：

```text
bitstream: cuper-jacobi-iteration-build/CuperJacobiIteration.xclbin
UUID:      585d847d-bde9-9dfc-8718-eb8f474e948d
SHA256:    e0d644c0fe3e48d88c3c001e2419a6bf8531c6543837c13b31d18748e225f635
clock:     HBM 450 MHz, KERNEL 500 MHz, DATA 220 MHz
timing:    routed WNS -1.205 ns, TNS -18023.475 ns
log:       logs/jacobi_hw_20260611_153318/thermal2_n16_max1.log
```

已做过的源码修正：

- `host/main.cpp`：CPU reference 改成固定跑满 `MAX_ITERS`，不再按 `tau` 提前退出。
- `kernels/detail/jacobi_controller.hpp`：硬件 controller 保留 `Tau` ABI，但不再执行
  `diff <= Tau` 的收敛 break。
- `include/Cuper_common.h`：`R NNZ=0` 时仍给 XRT 分配一个 padded 512-bit beat，避免
  zero-byte BO。

这些修改之后，`thermal2_n16` 已经能越过 BO 创建和 kernel 启动，但仍停在 `Finish`。

## 最优先修改建议

### 1. 不要让 `R NNZ=0` 产生 `Batch_num=0`

`thermal2_n16` 的 Jacobi 拆分结果是：

```text
N = 16
NNZ = 16
R NNZ = 0
Matrix_len = 0
```

当前 host 只修了矩阵 BO 的分配大小，但 kernel 看到的 `Batch_num` 仍可能是 0。这个状态
对 service 化 Cuper SpMV 不安全：

- `Jacobi_Vector_Loader` 仍会按 `Column_num` 输出 1 个 `float_v16` 的 `-x_old` 包；
- `SpmvService_Core` 在 `Batch_num=0` 时不会进入 batch loop，因此不会消费/转发这包
  vector；
- controller 后面广播 stop，但 `Vector_X_Stream` 本身没有 in-band stop token；
- 结果可能留下未消费的 stream 数据或未退出的 task，最终表现为 TAPA `Finish` 挂住。

建议先在 host 侧把空 `R` 改成一个空 batch：

```text
Batch_num = 1
SpElement_list_ptr = [0, 0]
Matrix_len = 0
Matrix_data 每路仍只需要 padded allocation，不读有效 beat
```

这样 16 级 Core 至少会执行一次 batch 逻辑，消费并转发 vector 包，但 matrix decode
区间是 `[0, 0)`，不会读矩阵数据；Accumulator 仍会输出全 0 的 `-R*x_old`，Jacobi
update 可以正常得到 `x_next = b * diag_inv`。这是改动最小、最贴近现有 Cuper service
协议的修法。

改动位置优先看：

```text
DLC/Cuper-jacobi-iteration/include/Cuper_common.h
DLC/Cuper-jacobi-iteration/host/main.cpp
```

目标不是把 kernel `Matrix_len` 改成 1；`Matrix_len` 应继续保持 0，避免 HBM loader 读
不存在的矩阵 beat。需要改的是 batch 边界表，让 task graph 的控制流完整走一轮空 batch。

### 2. 给 vector stream 也设计可证明的停止语义

当前 stop 主要分两类：

- command stop：给 ptr loader、vector loader、matrix loader；
- sideband stop：给 checker、sort tree、链尾 `DestroyFloatV16`。

但 `Vector_X_Stream` 链路本身没有 in-band stop。只要某一轮 Core 没有按预期消费所有
vector 包，链尾 sideband stop 并不能清掉前级 stream。建议把这个协议补硬：

1. 让 `SpmvService_Core` 每收到一条正常 command，都必须消费/转发本轮完整的
   `Cuper_NumFloatV16Packets(Column_num)` vector 包，即使 `Batch_num=0` 或某些 batch
   没有 matrix beat。
2. 或者给 `Vector_X_Stream` 包一层带 `last/stop` 的结构，使 stop 可以沿同一条数据链
   从 `Jacobi_Vector_Loader` 一直传播到 `DestroyFloatV16`。
3. 不建议长期依赖“链尾收到 sideband stop 就退出”，因为它不能证明前级 stream 已经空。

如果先做第 1 条的空 batch 修法，这一条可以作为下一步加固；如果 `thermal2_n1024`
这类非空 `R` 之后仍挂，优先排查这里。

### 3. controller 不应在下游完全排空前发送 sideband stop

`Jacobi_Controller` 现在收到 `Jacobi_Update_Service` 的 result 后，就立即广播 stop。
这只能证明 update 已经消费了需要的 `float_v16` 输出包，不能严格证明 checker 已经
丢弃完所有 padding 输出、sort tree 输入侧也完全排空。

建议增加一个轻量的 `SpmvRoundDone` ack：

```text
SpmvService_MultSortTree / checker-drain wrapper
  -> round_done stream
  -> Jacobi_Controller
```

controller 每轮等待两个条件：

```text
update_done && spmv_backend_drained
```

之后再进入下一轮或发 stop。这样可以把“数学结果够了”和“service 后端排空了”分开，
避免 stop 把仍在排 padding 的 task 提前截断。

## 建议验证顺序

每次改完不要直接扫全套 thermal2，先按下面顺序缩小问题：

1. `MAX_ITERS=0 thermal2_n16`
   - 只验证 controller 发 stop 后整个 graph 能否退出。
   - 如果这个都挂，说明 stop path 本身不闭合。

2. `MAX_ITERS=1 thermal2_n16`
   - 验证 `R NNZ=0` / 空 batch / diagonal-only Jacobi 路径。
   - 期望能打印 `Status`、`Final buffer`、`Iterations`、`Final diff` 和 `Error Num=0`。

3. `MAX_ITERS=1 thermal2_n1024`
   - 验证非空 `R` 的正常 Cuper SpMV service 路径。
   - 如果 n16 过而 n1024 挂，问题大概率不在空 batch，而在 service backend drain/stop。

4. 通过上述三项后再跑统一集合：

```text
thermal2_n16
thermal2_n1024
thermal2_n4096
thermal2_n16384
thermal2_n65536
thermal2_n131072
thermal2_n262144
thermal2
```

## 建议保留的调试字段

现有 `Metrics[4..7]` 只有 kernel 正常返回后才能由 host 打印。对 `Finish` hang 来说，
建议临时加更粗的 progress counter，写在 `Status` 或一个独立 debug BO 中：

```text
controller_sent_command
controller_got_update_result
controller_sent_stop
ptr_loader_seen_stop
vector_loader_seen_stop
matrix_loader_seen_stop_count
core_stop_forwarded_count
accumulator_seen_stop_count
checker_seen_stop_count
sort_seen_stop
update_seen_stop
```

如果 TAPA `Finish` 仍挂，至少能从已读回的 BO 判断最后卡在哪个 stop 边界。当前日志只能
看到 host 卡在 `Finish`，无法区分是哪一个 task 没退出。

## 时序备注

这版虽然生成了 xclbin，但 routed timing 未收敛：

```text
Setup failing endpoints: 45465
Worst slack: -1.205 ns
Total violation: -18023.475 ns
```

因此即使下一版功能上能返回，也不能直接晋级标准 bitstream。建议功能问题修完后，再考虑：

- 降低 DATA clock 或调整 Vitis clock 配置；
- 给 update/service control path 加 pipeline；
- 减少全图常驻 task 的组合 fanout；
- 再生成 timing-clean 或至少 WNS 显著改善的 demo。

## 是否应该写入 HTML

当前没有成功硬件数据点，不建议把这版 Jacobi 作为第 5 个成功基准写入
`395bitstream/cuper_spmv_u55c_compare_20260524.html`。可以记录为失败候选：

```text
thermal2_n16 / MAX_ITERS=1 / timeout or stopped
卡点: [tapa-invoke] after ReadFromDevice before Finish
```

等 `thermal2_n16` 和 `thermal2_n1024` 都能返回，再把 Jacobi 与四个标准主线并列展示。
