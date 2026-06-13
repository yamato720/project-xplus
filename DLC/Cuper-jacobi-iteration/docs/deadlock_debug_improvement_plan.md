# Finish 不返回问题的 Debug 改进方案

## 现状结论

本轮 finish hang 上板测试对象是当时的
`395bitstream/cuper-tapa-jacobi-u55c-20260611-demo.xclbin`，它已经是
`JACOBI_DEADLOCK_DEBUG=1` 的 debug ABI。这里的 `DEADLOCK` 是当前调试宏和
artifact 的历史命名，不表示已经证明根因一定是死锁：

```text
UUID:   b4664f5e-8cd6-0f7d-56ae-28384fce6400
SHA256: 1113701276f09545b2407d16823e5649d6e017a9fcef63a014838106612e8eb5
ABI:    single X buffer + Status + Metrics + Debug buffer
```

2026-06-12 用 debug host 上板复测：

```text
logdir: logs/jacobi_deadlock_debug_hw_20260612_141845/
thermal2_n16    MAX_ITERS=1: rc=124, 240s timeout
thermal2_n1024  MAX_ITERS=1: reached same Finish non-return point, then interrupted
```

两个点都停在同一位置：

```text
[tapa-invoke] after Exec before ReadFromDevice
[tapa-invoke] after ReadFromDevice before Finish
```

`thermal2_n16` 是 `R NNZ=0` 的 diagonal-only case；`thermal2_n1024` 是非空
`R NNZ=5338`。因此这轮不能再只按 `R NNZ=0` 特例解释，至少说明正常非空 SpMV
路径也可能无法完成 TAPA graph 退出。但这仍只是现象，不足以证明一定是数据流死锁。

当前可能原因包括：

- 真正的 stream/dataflow deadlock：某条 stream 少消费或少生产一个包。
- 某个常驻或 detached task 没有退出，TAPA `Finish` 一直等 task done。
- AXI mmap 写请求和写响应数量不平衡，例如 X/Status/Metrics/Debug 任一路响应没收齐。
- debug monitor 自己等待 stop/drain，导致 debug 路径也无法完整结束。
- timing 未收敛导致硬件进入异常状态；当前 bitstream WNS/TNS 都很差，不能排除。
- host/kernel ABI 或 runtime 交互仍有隐藏问题；本轮已用 debug host 对齐 ABI，但仍需
  native XRT runner 进一步排除 runtime 封装影响。

## 为什么当前 debug 还不能定位原因

当前 debug buffer 的设计能证明 ABI 接上了，但不适合定位 `Finish` 不返回问题：

1. Host 只有在 `tapa::invoke(...)` 返回后才调用 `PrintJacobiDebugBuffer()`。现在挂在
   `Finish`，所以 host 无法打印 `Debug` BO 的内容。
2. `Debug` 是 kernel 内的普通 mmap 写回路径。TAPA runtime 日志显示已经走过
   `ReadFromDevice`，但 `tapa::invoke` 没返回，host 代码仍拿不到读回后的数据。
3. `Jacobi_DebugMonitor` 自己也是 graph 内 task。它依赖 `Debug_Stop_Stream` 和
   `kJacobiDebugStopDrainCycles` 正常收尾；如果问题正好发生在 stop/drain 链路，
   monitor 也可能无法最终写出“最后状态”。
4. 业务 task 用 `try_write` 写 debug event，这避免 debug 反压主数据流，但也意味着
   debug stream 满时事件会丢。它适合做 heartbeat，不适合做严格的每阶段完成证明。
5. 当前 debug 记录的是事件流的“最后可见进度”，不是每条关键 stream 的 credit/balance。
   对于 stream 少读一个包、某个 detached task 不退出这类问题，事件式 debug 仍可能看不准。

所以当前应按“debug 暂时还盯不到根因”处理：它确认了新 ABI 仍停在 `Finish`，但还不能
区分具体原因，更不能把结论收敛成“已经确认是死锁”。

## 下一版 debug 应该怎么改

### 1. 加硬件 watchdog，让 kernel 能主动返回

最优先做一个 debug-only watchdog，而不是继续依赖 host timeout 杀进程。

建议增加编译宏：

```text
JACOBI_DEADLOCK_WATCHDOG=1
```

行为：

- 在 dispatcher 或独立 `Jacobi_Watchdog` task 中维护全局 progress heartbeat。
- 任何关键阶段推进时更新 progress counter。
- 如果超过 `N` cycles 没有 progress，watchdog 写入 `Status/Debug`：
  - `Status[0] = kJacobiStatusWatchdogTimeout`
  - `Status[2] = 已完成迭代数`
  - `Debug[0] = heartbeat`
  - `Debug[1] = watchdog timeout code`
  - `Debug[2..] = 各阶段最后计数`
- watchdog 发一个 stop/abort token，让 graph 尽量走可返回路径。

目标不是直接修复根因，而是让 `tapa::invoke` 返回，使 host 能打印 debug dump。

### 2. 从事件 debug 改成计数 debug

保留 event stream，但新增一组更可靠的 per-stage counters。每个 task 只写自己的槽位，
避免多生产者竞争：

```text
Debug[16 + source] = phase
Debug[64 + source] = progress_count
Debug[96 + source] = wait_code
Debug[128 + source] = last_iter
Debug[160 + source] = last_packet
```

至少覆盖这些 source：

```text
dispatcher
ptr_loader
vector_loader
matrix_loader[0..15]  可先汇总 count
core[0..15]           可先汇总 count
accumulator[0..15]    可先汇总 count
coeff_loader
pair_compute[0..7]
pack_writer
hbm_writer
stage_timer
debug_monitor
```

需要记录的计数：

```text
commands_sent / commands_seen
matrix_commands_sent / matrix_commands_seen
frames_sent / frames_seen
vector_packets_read
core_vector_packets_forwarded
matrix_beats_read
accumulator_outputs
pair_inputs_consumed
coeff_pairs_consumed
packets_packed
hbm_writes_issued
hbm_write_responses
feedback_tokens_sent
stop_tokens_seen
```

原因定位时先看 balance：

```text
packets_packed < frame.packet_count       -> pair_compute 或 accumulator 输出不足
hbm_writes_issued > hbm_write_responses   -> AXI write response 卡住
feedback_tokens_sent == 0                 -> writer 没完成，本轮没反馈
stop_tokens_seen 不全                     -> stop/drain 链路不闭合
```

### 3. 做可返回的分段 micro-kernel

现在 graph 太大，`Finish` 不返回时范围很宽。建议拆 3 个 debug-only top：

1. `CuperJacobiSpmvOnlyDebug`
   - 输入 `R` 和 `X`。
   - 输出 accumulator 后的 raw `float_v2` 或 pack 后 `float_v16` 到 HBM。
   - 不做 Jacobi update，不做 feedback token。

2. `CuperJacobiUpdateOnlyDebug`
   - 输入模拟的 `-Rx`、`B`、`Diag_inv`。
   - 只跑 coeff loader、pair compute、pack writer、X HBM writer。
   - 验证 update/write response/feedback token。

3. `CuperJacobiOneRoundDebug`
   - 固定只跑 1 轮，不做 next-token feedback。
   - 由一个 final writer 写 Status 后直接停机。

这三步比在完整 graph 里加更多 printf 式事件更有效，能把问题缩到 SpMV service、
update path 或 feedback/stop path。

### 4. Host 侧要支持失败后采样

当前 `tapa::invoke` 封装隐藏了 run/wait/sync 的边界。debug 阶段建议新增一个
native XRT runner 或 TAPA runtime debug wrapper：

```text
start kernel
sleep/poll N seconds
sync Debug BO from device
print Debug snapshot
then wait or abort
```

如果直接用 TAPA API 做不到，就给 debug xclbin 写一个临时 XRT host。这个 host 不需要
完整性能计时，只要能在 kernel 未完成时把 Debug BO 采样出来。

## 功能修复路线

debug 抓不到具体点之前，功能修复仍建议按下面顺序收敛。

### 1. 先砍掉 feedback token 环

当前完整 graph 的轮次控制是：

```text
RoundTokenSource -> RoundTokenMux -> RoundDispatcher
                                  -> XHbmWriter -> FeedbackToken -> RoundTokenMux
```

这对长期设计是合理的，但对 `Finish` 不返回排查不友好。先做 debug-only one-round：

```text
RoundDispatcher 发 1 轮 command/frame
XHbmWriter 写完 frame.packet_count 后直接写 Status/Metrics
不再生成下一轮 token
```

`MAX_ITERS=1` 能返回后，再恢复 feedback token。这样能判断问题是否在反馈 token /
stop token 环上。

### 2. 给每条 stop 链路做 ack

目前 stop 是广播出去，但 controller/dispatcher 不知道谁真的退出了。建议每个常驻 task
增加 debug-only ack：

```text
ptr_loader_done
vector_loader_done
matrix_loader_done_count
core_done_count
accumulator_done_count
pair_compute_done_count
pack_writer_done
hbm_writer_done
debug_monitor_done
```

dispatcher 等 ack 或 watchdog timeout 后再写最终 Status。这样即使不能完全修掉根因，
也能知道最后缺哪个 ack。

### 3. 明确 detached task 的退出协议

当前 `Jacobi_UpdatePairCompute` 是 `detach` 且无限循环，没有 frame/stop 输入。它只能靠
顶层 graph 结束时被动结束。对 `Finish` 类问题，这很危险。

建议改成：

```text
Jacobi_UpdatePairCompute(
  Frame_in_for_pair,
  Neg_Rx_in_0,
  Neg_Rx_in_1,
  Coeff_in,
  Updated_out,
  Done_out
)
```

每轮按 frame 消费，收到 stop frame 后写 done 并 return。这样 pair compute 不再是
不可观察、不可确认退出的 detached 常驻 task。

### 4. 非空 R 和空 R 分开修

本轮 `thermal2_n1024` 非空 R 也到同一 `Finish` 卡点，所以 diagonal-only bypass 只能解决
`thermal2_n16`，不能解决完整问题。建议顺序：

1. 用 one-round debug 让 `thermal2_n1024` 先返回。
2. 再加 diagonal-only bypass 修 `thermal2_n16` 的空 R 特例。
3. 最后恢复 `MAX_ITERS>1` 的 feedback token。

## 下一轮验收顺序

不要直接扫全套 thermal2。建议每个新 xclbin 只跑：

```text
thermal2_n1024 MAX_ITERS=1  timeout 120s
thermal2_n16   MAX_ITERS=1  timeout 120s
thermal2_n1024 MAX_ITERS=2  timeout 120s
```

判定：

- 任一点仍卡 `after ReadFromDevice before Finish`：先看 watchdog/debug snapshot，不跑更大数据。
- `n1024 MAX_ITERS=1` 返回但 `n16` 不返回：处理 diagonal-only bypass。
- `MAX_ITERS=1` 返回但 `MAX_ITERS=2` 不返回：处理 feedback token / 单 X 写回后读依赖。
- 三项都返回后，再跑 `thermal2_n65536` 和统一 thermal2 集合。

## 当前记录

本轮日志：

```text
logs/jacobi_deadlock_debug_hw_20260612_141845/thermal2_n16_max1.log
logs/jacobi_deadlock_debug_hw_20260612_141845/thermal2_n1024_max1.log
```

当前结论：

```text
debug ABI 已启用，但仍无法在 Finish 不返回时把 Debug buffer 打印出来。
thermal2_n16 和 thermal2_n1024 都到达 after ReadFromDevice before Finish。
当前 debug 只能确认 Finish 不返回仍存在；死锁只是候选原因之一，具体 source 未定位。
```

## 2026-06-12 代码排查新增结论

已发现一个明确的退出协议 bug：

```text
SpmvService_DestroyFloatV16
```

旧逻辑是“优先读 Vector_X_Stream；如果这一拍 Vector_X_Stream 为空且 stop stream
有数据，就读 stop 并 return”。这个判断在硬件上有竞态：`Jacobi_RoundDispatcher`
收到最终 stop token 后会立刻向链尾 drain 发 stop，但 Core15 可能还有本轮残余
`Vector_X_Stream[16]` 包正在路上。如果 drain 在尾流暂时为空的一拍提前退出，后续
Core15 再转发 X 包时无人消费，Core 链可能卡住。此时 dispatcher 已经能写
`Status/Metrics`，host 可能走到 `ReadFromDevice`，但 TAPA `Finish` 仍在等未完成的
task，于是表现为当前的 finish hang。

已改成按 `Column_num` 和 `Max_iters` 计算链尾应收到的总 X 包数：

```text
expected_packets = ceil(Column_num / 16) * Max_iters
```

`SpmvService_DestroyFloatV16` 现在必须 drain 完 `expected_packets` 个尾端 X 包，并且
看到 stop 后才 return。这样 stop 不能再抢在迟到的尾流前面让 drain 早退。

本地已做验证：

```text
make cuper-jacobi-build-host
make cuper-jacobi-regression-sw MODE=quick NO_BUILD=1 ALLOW_MISSING=1
JACOBI_DEADLOCK_DEBUG=1 make cuper-jacobi-build-host
JACOBI_DEADLOCK_DEBUG=1 MAX_ITERS=1 make cuper-jacobi-run-sw MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024
```

结果：quick regression 通过；debug ABI 下 `thermal2_n1024 MAX_ITERS=1` software/TAPA
simulation 返回，`Error Num=0`。

2026-06-12 已重新生成包含该 tail-drain 修复的硬件 bitstream：

```text
build dir: cuper-tapa-jacobi-u55c-20260612-tail-drain-debug-build/
xclbin:    CuperJacobiIteration.xclbin
UUID:      401e53eb-a68f-55fb-78f8-5553f14edcd2
SHA256:    46272395b4f4cef1a977767225080dfe2194fed3cf55baccbb5e4eec68e82e2f
timing:    not met, WNS -2.842 ns, TNS -74910.742 ns
```

这版曾同步到 Jacobi demo 槽：

```text
395bitstream/cuper-tapa-jacobi-u55c-20260612-demo.xclbin
395bitstream/cuper-tapa-jacobi-u55c-20260612-demo.xclbin.info
```

它上板后仍出现 Finish 不返回，当前已经被 2026-06-13 pre-Finish/empty-R debug demo 覆盖：

```text
395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin
395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin.info
```

2026-06-11 和 2026-06-12 demo 的 finish hang 日志和结论只作为旧 artifact 历史记录。
