# Jacobi Finish 不返回的候选原因与细粒度监测点

## 当前现象

2026-06-12 复测当前 Jacobi demo：

```text
bitstream: 395bitstream/cuper-tapa-jacobi-u55c-20260612-demo.xclbin
UUID:      401e53eb-a68f-55fb-78f8-5553f14edcd2
SHA256:    46272395b4f4cef1a977767225080dfe2194fed3cf55baccbb5e4eec68e82e2f
log:       logs/jacobi_tail_drain_hw_20260612_201117/thermal2_n1024_max1.log
case:      thermal2_n1024, MAX_ITERS=1, JACOBI_DEADLOCK_DEBUG=1
result:    timeout 120s, rc=124
```

Host 日志已经进入 TAPA runtime 收尾阶段：

```text
[tapa-invoke] after Exec before ReadFromDevice
[tapa-invoke] after ReadFromDevice before Finish
```

然后没有等到 `Finish` 返回。该 case 的矩阵为 `1024 x 1024`，`NNZ=6362`，
拆分后的 `R NNZ=5338`，CPU reference 已先跑完。因此这次不是单纯的
diagonal-only / `R NNZ=0` 特例。

另外，测试结束后系统里还能看到一个类似内核线程形态的 `[CuperJacobiIter]`
进程，`PPID=2`，用户态 cmdline/fd 为空。这说明用户态 timeout 已经终止 host，
但 device/runtime 侧可能仍有未清掉的执行上下文。它支持“runtime/device 收尾没有
完成”这个判断，但仍不能单独证明业务数据流已经死锁。

当前准确结论应写成：

```text
CuperJacobiIteration 在新版 tail-drain debug bitstream 上仍然 Finish 不返回；
根因未定，不能只定性为死锁。
```

## 还不能直接定性为死锁

现象上像 hang，但候选原因至少有六类：

1. 真正的 stream/dataflow deadlock：某条业务 stream 少生产或少消费一个包。
2. 常驻 task / detached task 未退出：TAPA `Finish` 等所有 task done，某个 task
   一直活着会表现成 Finish 不返回。
3. AXI mmap 请求和响应不平衡：`X`、`Status`、`Metrics`、`Debug` 任一路
   write response 或 read data 没收齐。
4. stop/drain 协议不闭合：stop token 发出后，某条链路没有 drain 到预期包数，
   或者缺少 done ack，controller 不知道谁退出了。
5. debug monitor 自己卡住：当前 debug task 也在 graph 内，依赖 `Debug_Stop_Stream`
   和 Debug mmap 写响应完成收尾。
6. timing 或板卡/runtime 状态：当前 20260612 demo routed timing 未收敛
   `WNS=-2.842 ns`、`TNS=-74910.742 ns`，不能排除硬件异常状态。

所以目前建议把问题命名为 `Finish non-return` 或 `runtime finalization hang`，
而不是在证据不足时写成 confirmed deadlock。

## 当前 debug 盯不到的原因

`host/main.cpp` 里的 `PrintJacobiDebugBuffer()` 在 `tapa::invoke(...)` 返回后才打印。
现在卡在 `Finish` 返回前，所以 host 拿不到 debug dump。

现有 kernel debug 也只是一条事件流：

- 覆盖 dispatcher、8 个 update pair、pack writer、X HBM writer。
- 事件用 `try_write`，不会反压业务流，但 stream 满时事件会丢。
- Debug monitor 通过普通 mmap 写 Debug BO，自己也要等 write response。
- 最终只能看到“最后收到的事件”，不是所有 stream 的 credit/balance。

因此下一版不能继续只加 printf 式事件。需要变成“每个 task 固定槽位计数 +
等待原因 + AXI request/response balance + stop/done ack”。

## 推荐 Debug Buffer 布局

建议新增 debug-only ABI，保留 `JACOBI_DEADLOCK_DEBUG=1`，但把 Debug BO 扩到
至少 1024 个 `INDEX_TYPE`。每个 source 使用固定 16 个槽位，便于 host 在 kernel
未正常完成时也能解析快照。

```text
base = 32 + source * 16

Debug[base + 0]  phase
Debug[base + 1]  progress
Debug[base + 2]  wait_code
Debug[base + 3]  last_iter
Debug[base + 4]  expected_packets
Debug[base + 5]  produced_or_sent
Debug[base + 6]  consumed_or_received
Debug[base + 7]  stop_seen
Debug[base + 8]  done_seen_or_returned
Debug[base + 9]  mmap_read_req
Debug[base + 10] mmap_read_resp
Debug[base + 11] mmap_write_req
Debug[base + 12] mmap_write_resp
Debug[base + 13] last_token_or_frame
Debug[base + 14] last_error
Debug[base + 15] heartbeat
```

写法上有两个选择：

1. 每个 task 只写自己的固定 Debug mmap 槽位。实现简单，但会增加多个 mmap
   writer source，可能引入新的资源和响应路径。
2. 每个 task 把 counter sample 发到单一 debug writer。业务 task 本地累加计数，
   sample 用非阻塞方式发送，debug writer 单独写 mmap。

优先推荐第 2 种。无论哪种，都要避免 debug 路径阻塞主数据流。

## 细粒度监测点

### 1. Round token / dispatcher

对应 `Jacobi_RoundTokenSource`、`Jacobi_RoundTokenMux`、`Jacobi_RoundDispatcher`。

需要记录：

- 初始 token 是否发出：`initial_token_sent`。
- feedback token 是否收到：`feedback_token_seen`。
- dispatcher 读到的 `token.iter`、`token.max_iters`、`token.packet_count`。
- compute command 发送数：`ptr_command_sent`、`vector_command_sent`。
- matrix prefetch command 发送数：每个 HBM channel 的 `matrix_command_sent[ch]`。
- update frame 发送数：`update_frame_sent`。
- stop 发送数：`compute_stop_sent`、`matrix_stop_sent[ch]`、
  `update_stop_sent`、`vector_destroy_stop_sent`、`debug_stop_sent`。
- `Status` / `Metrics` mmap 写请求和响应数。

关键判断：

```text
feedback_token_seen == 0
  -> XHbmWriter 可能没完成本轮写回，或 Feedback_Token_Stream 没送到 mux。

compute_stop_sent == 1 但下游 stop_seen 不全
  -> stop/drain 链路不闭合。

Status.write_req > Status.write_resp 或 Metrics.write_req > Metrics.write_resp
  -> controller 卡在最终 mmap 写响应。
```

### 2. SpElement ptr loader

对应 `SpmvService_SpElementPtrLoader`。

需要记录：

- compute command seen 数。
- stop command seen。
- 每轮 `Batch_num`、`Row_num`、`Column_num`。
- `SpElement_list_ptr` read request / read response 数。
- `PE_Param` 输出数。
- wait reason：`wait_command`、`wait_ptr_read_data`、`wait_pe_param_full`。

关键判断：

```text
ptr_command_seen < dispatcher.ptr_command_sent
  -> Command_Stream[0] 卡在 dispatcher 到 ptr loader 之间。

ptr_read_req > ptr_read_resp
  -> 边界表 HBM read response 未收齐。

PE_Param_sent 少于预期
  -> Core 链启动参数不足。
```

### 3. Vector loader 与 X 链尾 drain

对应 `Jacobi_Vector_Loader` 和 `SpmvService_DestroyFloatV16`。

需要记录：

- vector command seen / stop seen。
- `X` read request / read response 数。
- `Vector_X_Stream[0]` 发送包数。
- 链尾 drain 的 `expected_packets`、`drained_packets`、`stop_seen`、`returned`。
- wait reason：`wait_x_read_data`、`wait_vector_out_full`、`wait_chain_tail_packet`、
  `wait_destroy_stop`。

关键判断：

```text
Vector_X_Stream[0].sent == packet_count 但 tail.drained < expected_packets
  -> Core 链中间或尾部没有转发完 X。

tail.stop_seen == 1 且 tail.drained == expected_packets 但 returned == 0
  -> drain task 自身退出条件或综合后状态异常。
```

### 4. Matrix loader[0..15]

对应 16 路 `SpmvService_MatrixLoader`。

每一路都需要记录：

- matrix command seen / stop seen。
- `Matrix_len`。
- `Matrix_data[ch]` read request / read response 数。
- `Matrix_A_Stream[ch]` 输出 beat 数。
- wait reason：`wait_matrix_command`、`wait_matrix_read_data`、
  `wait_matrix_out_full`。

关键判断：

```text
matrix_command_seen[ch] < dispatcher.matrix_command_sent[ch]
  -> 第 ch 路 prefetch command 没到。

matrix_read_req[ch] > matrix_read_resp[ch]
  -> 第 ch 路 HBM read response 没收齐。

matrix_out_sent[ch] < Matrix_len
  -> 第 ch 路没有完成矩阵预取。
```

### 5. Core[0..15]

对应 16 级 `SpmvService_Core`。

每级需要记录：

- `PE_Param_in` 读数、`PE_Param_out` 写数。
- `Vector_X_Stream_in` 读数、`Vector_X_Stream_out` 写数。
- `Matrix_A_Stream[ch]` 消费 beat 数。
- `Vector_Y_Param` 输出数。
- `Matrix_Mult_Vector_Stream` 输出数。
- stop token seen / forwarded。
- wait reason：`wait_pe_param`、`wait_matrix_a`、`wait_vector_x`、
  `wait_pe_param_out_full`、`wait_vector_x_out_full`、`wait_y_param_full`、
  `wait_mult_out_full`。

关键判断：

```text
core[k].vector_in > core[k].vector_out
  -> 第 k 级或后一级 X 转发可能断住。

core[k].matrix_consumed < Matrix_len
  -> 第 k 路 SpMV 未消费完矩阵。

core[k].stop_seen == 1 但 core[k].stop_forwarded == 0
  -> stop token 没继续传到链尾。
```

### 6. Accumulator[0..15]

对应 16 路 `SpmvService_Accumulator`。

每路需要记录：

- `Vector_Y_Param` command seen / stop seen。
- `Matrix_Mult_Vector_Stream` 消费数。
- `Vector_Y_Stream[ch]` 输出 `float_v2` 数。
- 本轮预期 `num_pe_output`。
- wait reason：`wait_y_param`、`wait_mult_input`、`wait_vector_y_out_full`。

关键判断：

```text
accum[ch].out_float_v2 < num_pe_output
  -> 后面的 pair compute 一定拿不齐输入。

accum[ch].out_full wait 长时间累加
  -> update pair 或 pack writer 反压。
```

### 7. Update frame fork

对应 `Jacobi_UpdateFrameFork`。

需要记录：

- frame in 数。
- coeff frame / pack frame / hbm frame 输出数。
- stop frame seen / forwarded。
- wait reason：`wait_coeff_frame_full`、`wait_pack_frame_full`、`wait_hbm_frame_full`。

关键判断：

```text
frame_in == 1 但三路 frame_out 不全
  -> update 子图没有统一启动。

stop frame forwarded 不全
  -> update 子图退出链路不闭合。
```

### 8. UpdateCoeffLoader

对应 `Jacobi_UpdateCoeffLoader`。

需要记录：

- frame seen / stop seen。
- `B` read request / response 数。
- `Diag_inv` read request / response 数。
- 每个 lane pair 的 coeff 输出数。
- wait reason：`wait_b_read_data`、`wait_diag_read_data`、`wait_coeff_out_full_mask`。

关键判断：

```text
B.read_req > B.read_resp 或 Diag.read_req > Diag.read_resp
  -> 系数 HBM read response 未收齐。

coeff_out[lane] < frame.packet_count
  -> 第 lane 个 pair compute 缺 coeff。
```

### 9. UpdatePairCompute[0..7]

对应 8 个 `Jacobi_UpdatePairCompute`，当前是 `tapa::detach` 且 `for(;;)` 常驻。
这是最需要细化的点。

每个 pair 需要记录：

- `Neg_Rx_in_0` consumed。
- `Neg_Rx_in_1` consumed。
- `Coeff_in` consumed。
- `Updated_out` produced。
- 当前 `i`、`c_idx`、`o_idx`。
- `num_pe_output`、`num_out`。
- wait reason：
  - `wait_neg_rx0`
  - `wait_neg_rx1`
  - `wait_coeff`
  - `wait_updated_out_full`
- done round count。

关键判断：

```text
neg_rx0 + neg_rx1 < num_pe_output
  -> accumulator 对应两路输出不足，或 pair 读入顺序卡住。

coeff_consumed < num_out
  -> coeff loader 没供够，或 coeff stream 被反压。

updated_produced < num_out
  -> pack writer 拿不到完整 8 lane 更新。

done_round 已出现，但 Finish 仍不返回
  -> pair 本轮算完，问题更可能在 stop/常驻 task/下游 writer。
```

这里还建议做一个 debug-only 改造：给 pair compute 加 frame/stop 输入和 done 输出，
不要在 debug bitstream 中继续依赖 detached infinite loop 的隐式退出语义。

### 10. Pack writer

对应 `Jacobi_UpdatePackWriter`。

需要记录：

- frame seen / stop seen。
- 每个 `Updated_in[lane]` consumed 数。
- `X_Write_out` produced 数。
- 当前 `packet`。
- wait reason：
  - `wait_updated_lane_mask`
  - `wait_x_write_out_full`
- done round count。

关键判断：

```text
Updated_in[lane].consumed < frame.packet_count
  -> 第 lane 个 pair compute 没产够。

X_Write_out.produced == frame.packet_count
  -> update 拼包已完成，后面看 XHbmWriter。
```

### 11. X HBM writer

对应 `Jacobi_XHbmWriter`。

需要记录：

- frame seen / stop seen。
- `X_Write_in` consumed。
- `X.write_addr` / `X.write_data` request issued。
- `X.write_resp` received。
- feedback token sent。
- wait reason：
  - `wait_x_write_in`
  - `wait_x_addr_full`
  - `wait_x_data_full`
  - `wait_x_write_resp`
  - `wait_feedback_token_full`

关键判断：

```text
X.write_req == frame.packet_count 且 X.write_resp == frame.packet_count
  -> X 写回已经完成。

X.write_req > X.write_resp
  -> 卡在 X mmap write response。

feedback_token_sent == 0
  -> RoundTokenMux 收不到停止 token，dispatcher 不会进入最终 Status/Metrics 写回。
```

### 12. Status / Metrics / Debug mmap

对应 `Jacobi_WriteStatus`、`Jacobi_WriteMetrics`、`Jacobi_DebugMonitor`。

需要记录：

- `Status.write_req`、`Status.write_resp`。
- `Metrics.write_req`、`Metrics.write_resp`。
- `Debug.write_req`、`Debug.write_resp`。
- Debug monitor 的 `event_count`、`dropped_event_count`、`stop_seen`、`returned`。
- Debug monitor 当前是否在 `stop_drain` 或 `write_resp` 等待。

关键判断：

```text
业务数据流都 done，但 Status/Metrics response 不齐
  -> controller 最终 mmap 写回卡住。

Debug monitor 未 returned
  -> debug task 自己可能让 Finish 等不到 graph done。
```

## 需要新增的 done ack

仅靠 stop token 不够。debug 版应给每个常驻 task 增加 done ack：

```text
ptr_loader_done
vector_loader_done
matrix_loader_done_count
core_done_count
pe_param_destroy_done
vector_tail_destroy_done
accumulator_done_count
frame_fork_done
coeff_loader_done
pair_compute_done_mask
pack_writer_done
x_hbm_writer_done
stage_timer_done
debug_monitor_done
```

dispatcher 或 debug aggregator 不一定要等待所有 ack 才退出，但必须把 ack mask 写入
Debug BO。这样下一次 timeout 后能直接看到缺哪一段。

## 建议实现顺序

### 第一步：先让失败能返回

增加 debug-only watchdog。watchdog 不负责证明根因，只负责在长时间无 progress 时
写 `Status` / `Debug`，并尽量发 abort/stop token，让 `tapa::invoke` 返回。

建议状态码：

```text
kJacobiStatusWatchdogTimeout = 0xdead001
kJacobiStatusDebugAbort      = 0xdead002
```

如果 TAPA graph 很难被内部 watchdog 强行收掉，就优先做 native XRT debug host：

```text
start kernel
sleep/poll N seconds
sync Debug BO from device while kernel is still running
print snapshot
then wait / abort / reset
```

当前 `tapa::invoke` 封装只在返回后给 host 打印 Debug BO，不适合排查 Finish
不返回。

### 第二步：去掉最可疑的隐式退出点

debug 版优先处理 `Jacobi_UpdatePairCompute`：

- 去掉 `tapa::detach` infinite loop 的不可观察退出。
- 增加 frame 输入，按 frame 消费固定数量。
- 收到 stop frame 后写 done ack 并 return。

如果这样后 `thermal2_n1024 MAX_ITERS=1` 能返回，说明之前至少有一部分问题在
detached 常驻 task 的退出语义上。

### 第三步：做 one-round debug top

增加 `CuperJacobiOneRoundDebug`：

- 固定只跑一轮。
- `XHbmWriter` 写完 `frame.packet_count` 并收齐 write response 后直接写
  Status/Metrics。
- 不经过 feedback token 环。

这样能把问题拆成：

```text
one-round 能返回，完整 MAX_ITERS=1 不能返回
  -> feedback token / stop token / finalization 路径有问题。

one-round 也不能返回
  -> SpMV/update/write response 主数据路径仍有问题。
```

### 第四步：拆 micro-kernel

如果 one-round 仍不返回，再拆：

1. `CuperJacobiSpmvOnlyDebug`
   - 只跑 `R * -X` 到 accumulator 或 pack 后写 HBM。
   - 不做 Jacobi update，不做 feedback token。
2. `CuperJacobiUpdateOnlyDebug`
   - 输入模拟 `-Rx`、`B`、`Diag_inv`。
   - 只跑 coeff loader、pair compute、pack writer、X writer。
3. `CuperJacobiMmapOnlyDebug`
   - 只测 `X` / `Status` / `Metrics` / `Debug` mmap read/write response。

拆完后再决定是修 SpMV service、update path、feedback/stop path，还是先处理
timing 和 runtime。

## 下一轮上板验收

建议按这个顺序跑，不要直接上完整 `thermal2`：

```bash
timeout 120s env BITFILE=395bitstream/cuper-tapa-jacobi-u55c-20260612-demo.xclbin \
  MAX_ITERS=1 JACOBI_DEADLOCK_DEBUG=1 \
  make cuper-jacobi-run-hw MATRIX=data/suitesparse/Schmid/csr/thermal2_n16 \
  JACOBI_DEADLOCK_DEBUG=1

timeout 120s env BITFILE=395bitstream/cuper-tapa-jacobi-u55c-20260612-demo.xclbin \
  MAX_ITERS=1 JACOBI_DEADLOCK_DEBUG=1 \
  make cuper-jacobi-run-hw MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024 \
  JACOBI_DEADLOCK_DEBUG=1
```

判定标准：

- 如果 `thermal2_n1024 MAX_ITERS=1` 返回并打印 Status/Metrics/Debug，再继续跑
  `thermal2_n65536`。
- 如果仍 timeout，必须能从 Debug BO 或 native XRT snapshot 看到：
  - 哪个 task 的 `done` 没到；
  - 哪条 stream 的 produced/consumed 不平衡；
  - 哪一路 mmap request/response 不平衡；
  - 最后 wait reason 是什么。
- 如果 timeout 后仍没有可读快照，说明 debug 机制仍不够，优先做 native XRT
  running-kernel snapshot，而不是继续猜业务逻辑。

## 当前优先级判断

最优先排查顺序：

1. `Jacobi_XHbmWriter` 是否收齐 `X.write_resp` 并发出 stop feedback token。
2. `Jacobi_RoundTokenMux` / `Jacobi_RoundDispatcher` 是否收到 stop token 并写
   Status/Metrics。
3. `Jacobi_UpdatePairCompute[0..7]` 是否因为 detached infinite loop 让 Finish
   等不到明确退出。
4. `SpmvService_DestroyFloatV16` 是否准确 drain 完链尾 X 包并 return。
5. Debug monitor 是否因等待 stop/drain 或 Debug write response 反过来拖住 Finish。
6. 在上述 balance 都正常后，再把 routed timing 未收敛作为硬件不稳定方向单独处理。

在拿到这些计数前，继续修改收敛判断或 diagonal-only bypass 都只能解决局部特例；
它们不能解释 `thermal2_n1024` 非空 `R` 仍然 `Finish` 不返回。
