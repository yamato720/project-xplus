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

## 修正后的最优先修改建议

### 1. 首选：给 `R NNZ=0` 做 diagonal-only bypass

`thermal2_n16` 的 Jacobi 拆分结果是：

```text
N = 16
NNZ = 16
R NNZ = 0
Matrix_len = 0
```

当前 host 只修了矩阵 BO 的分配大小，但 kernel 看到的 `Batch_num` 仍可能是 0。这个
状态对 service 化 Cuper SpMV 不安全：

- `Jacobi_Vector_Loader` 仍会按 `Column_num` 输出 1 个 `float_v16` 的 `-x_old` 包；
- `SpmvService_Core` 在 `Batch_num=0` 时不会进入 batch loop，因此不会消费/转发这包
  vector；
- controller 后面广播 stop，但 `Vector_X_Stream` 本身没有 in-band stop token；
- 结果可能留下未消费的 stream 数据或未退出的 task，最终表现为 TAPA `Finish` 挂住。

但不要把“强行制造空 batch”作为首选修法。当前 `Jacobi_Update_Service` 对每个正常
`JacobiFrame` 都会等待 `frame.packet_count` 个 `Spmv_in` 包；如果空 batch 没有严格地产生
对应数量的全 0 `-R*x_old` 包，controller 就会一直等不到 `Update_Result_in`。这在硬件侧
很容易从“空矩阵特例”变成另一个死锁点。

更稳的首选方案是给 `R NNZ=0` / `Matrix_len=0` 做 diagonal-only bypass：

1. host 检测 `R NNZ == 0` 或 `Matrix_len == 0`，把这个状态作为 scalar 参数传给
   `CuperJacobiIteration`，或等价地写入 controller 可读的配置。
2. `Jacobi_Controller` 在 diagonal-only 模式下不发正常 SpMV command，只给 update 侧发
   “本轮 `neg_rx` 恒为 0” 的 frame。
3. `Jacobi_Update_Service` 增加一个 `rx_is_zero` / `bypass_spmv` 标志；该标志为真时，
   不读 `Spmv_in`，直接计算 `x_next = b * diag_inv`，同时继续读取 `x_old` 计算 diff。
4. 每轮完成后仍由 update 返回 `JacobiUpdateResult`，controller 按原来的双缓冲规则切换
   `X0/X1`。
5. 退出时 controller 仍发送一次 stop command 给 SpMV service，让 ptr/vector/matrix
   loader、core、accumulator、checker、sort、drain 都有明确退出路径。

这个方案的好处是：diagonal-only 数据集不再依赖 Cuper SpMV service 为空矩阵“合成全 0
输出包”，也不会让 `Jacobi_Vector_Loader` 先写出无人消费的 `Vector_X_Stream`。

### 2. 备选：如果坚持复用 Cuper service，空 batch 必须满足全部条件

也可以继续走 Cuper service，但这不是“只把 BO padding 补上”就够了。必须同时满足：

```text
Batch_num = 1
SpElement_list_ptr = [0, 0]
Matrix_len = 0
Matrix_data 每路仍只需要 padded allocation，不读有效 beat
```

也就是说，host 必须真实构造长度为 2 的边界表，传给 kernel 的 `Batch_num` 必须是 1；
不能只让 `SpElement_list_ptr_fpga` 分配出 padded buffer 后仍传 `Batch_num=0`。

在这个条件下，16 级 Core 才会执行一次 batch 逻辑，消费并转发 vector 包；matrix decode
区间是 `[0, 0)`，所以不会读矩阵数据。Accumulator 理论上会清零本地累加器并输出全 0
结果，checker/sort 再拼出 `frame.packet_count` 个全 0 `float_v16` 包，供
`Jacobi_Update_Service` 消费。

这条路上线前必须用软件仿真或硬件 debug counter 证明下面三件事：

- `Jacobi_Vector_Loader` 输出的所有 X 包都被 16 级 Core 链消费到链尾；
- `SpmvService_MultSortTree` 对每轮输出了 `spmv_service_num_float_v16_packets(Row_num)` 个包；
- `Jacobi_Update_Service` 收齐这些包并返回 `JacobiUpdateResult`。

如果这三点任一项无法证明，优先回到第 1 条 diagonal-only bypass。

改动位置优先看：

```text
DLC/Cuper-jacobi-iteration/include/Cuper_common.h
DLC/Cuper-jacobi-iteration/host/main.cpp
DLC/Cuper-jacobi-iteration/kernels/detail/jacobi_common.hpp
DLC/Cuper-jacobi-iteration/kernels/detail/jacobi_controller.hpp
DLC/Cuper-jacobi-iteration/kernels/detail/jacobi_update_service.hpp
```

无论用第 1 条还是第 2 条，都不要把 kernel `Matrix_len` 改成 1；`Matrix_len` 应继续
保持 0，避免 HBM loader 读不存在的矩阵 beat。

### 3. 给 vector stream 也设计可证明的停止语义

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

如果先做 diagonal-only bypass，`thermal2_n16` 可以绕开这条风险；但 `thermal2_n1024`
这类非空 `R` 仍会完整走 Cuper service，所以后续仍要排查这里。

### 4. controller 不应在下游完全排空前发送 sideband stop

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

每次改完不要直接扫全套 thermal2，先按下面顺序缩小问题。不要先用当前实现跑
`MAX_ITERS=0` 当 stop-path smoke：现在 controller 会跳过正常 SpMV/update 轮次，然后直接
发 stop；这只能覆盖“零迭代退出”，不能证明 Cuper service 的正常轮次能 drain，反而容易把
sort/checker/vector drain 的停止顺序测成另一个特例。

1. `MAX_ITERS=1 thermal2_n16`
   - 验证 `R NNZ=0` / 空 batch / diagonal-only Jacobi 路径。
   - 期望能打印 `Status`、`Final buffer`、`Iterations`、`Final diff` 和 `Error Num=0`。
   - 如果采用首选 diagonal-only bypass，确认 update 没有读取 `Spmv_in`。
   - 如果采用空 batch 备选，确认 sort 输出包数等于 `frame.packet_count`。

2. `MAX_ITERS=1 thermal2_n1024`
   - 验证非空 `R` 的正常 Cuper SpMV service 路径。
   - 如果 n16 过而 n1024 挂，问题大概率不在 diagonal-only 特例，而在 service backend
     drain/stop 或 checker/sort 输出协议。

3. `MAX_ITERS=2 thermal2_n16`
   - 验证双缓冲 `X0/X1` 往返和 `final_buffer` 奇偶。
   - 对 diagonal-only 矩阵，两轮结果仍应和 CPU fixed-count Jacobi 对齐。

4. 只有明确实现了 `Max_iters == 0` 的返回协议后，再补 `MAX_ITERS=0 thermal2_n16`
   - 这时应约定 `Status[2]=0`、`Final buffer=0`、不发正常 update frame。
   - 这个测试只用于零迭代 ABI，不用于证明 SpMV service 正常轮次。

5. 通过上述项目后再跑统一集合：

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
建议临时加更粗的 progress counter，写在 `Status` 或一个独立 debug BO 中。注意这些
counter 只能作为“最后可见进度”的线索：如果 kernel 最终卡在 `Finish`，host 侧在
`ReadFromDevice` 前后看到的 debug BO 可能不是完整最终状态，不能把它当作严格同步点。

```text
controller_sent_command
controller_sent_diag_bypass_frame
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
update_bypass_packets
sort_output_packets
```

如果 TAPA `Finish` 仍挂，至少能从已读回的 BO 粗略判断最后卡在哪个 stop 边界。当前日志
只能看到 host 卡在 `Finish`，无法区分是哪一个 task 没退出。

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
