# Entry mmap probe 失败分析与下一版修改建议

本文记录 2026-06-13 `entry mmap probe` 版 Jacobi demo 的上板结果和下一步修改建议。
这里的 `deadlock` 仍只是历史调试宏名，不表示已经证明根因是 PL dataflow 死锁。

## 本轮测试对象

```text
bitstream: 395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin
UUID:      7bf54cce-83a3-b7e7-97a9-719446658c03
SHA256:    775d1da4c1c2f51ec58e0569950f618eb159481bf3eddea4e27b8f6a4da9eb24
build dir: cuper-tapa-jacobi-u55c-20260613-entry-mmap-probe-debug-build/
logs:      logs/jacobi_entry_mmap_probe_hw_20260613_171648/
```

该 xclbin 不是 timing-clean artifact：

```text
DATA achieved: 175.9 MHz
KERNEL clock:  500 MHz
HBM clock:     450 MHz
WNS:           -2.350 ns
TNS:           -60974.352 ns
failing endpoints: 101235
```

因此当前结论只能作为调试线索，不能作为稳定功能结果。

## 实测结果

测试命令口径：

```bash
make cuper-jacobi-build-host JACOBI_DEADLOCK_DEBUG=1

timeout 120s env \
  BITFILE=/home/pyx/ProjectFS/Project-X/Project-XPlus/395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin \
  MAX_ITERS=1 JACOBI_DEADLOCK_DEBUG=1 \
  make cuper-jacobi-run-hw MATRIX=data/suitesparse/Schmid/csr/thermal2_n16 JACOBI_DEADLOCK_DEBUG=1

timeout 120s env \
  BITFILE=/home/pyx/ProjectFS/Project-X/Project-XPlus/395bitstream/cuper-tapa-jacobi-u55c-20260613-demo.xclbin \
  MAX_ITERS=1 JACOBI_DEADLOCK_DEBUG=1 \
  make cuper-jacobi-run-hw MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024 JACOBI_DEADLOCK_DEBUG=1
```

结果：

| 数据集 | R NNZ | Slice Num | MAX_ITERS | 结果 |
| --- | ---: | ---: | ---: | --- |
| `thermal2_n16` | 0 | 0 | 1 | `rc=124`，120s timeout |
| `thermal2_n1024` | 5338 | 172 | 1 | `rc=124`，120s timeout |

两个 case 都停在同一个 host 边界：

```text
[tapa-invoke] after Exec before ReadFromDevice
[tapa-invoke] after ReadFromDevice before Finish
```

`Finish()` 前打印到的 BO 快照也完全一致：

```text
[jacobi-prefinish] Status[0..2]=0,0,0 Metrics[0..7]=0,0,0,0,0,0,0,0
[jacobi-prefinish-probe] Status[8..11]=0,0,0,0 Metrics[8..11]=0,0,0,0
[jacobi-deadlock-debug] heartbeat=0 event_count=0 last_source=0 last_phase=0 last_lane=0 last_value=0 stop_marker=0
[jacobi-deadlock-probe] Debug[48..51]=0,0,0,0
```

上板 timeout 后检查到 CU 通常已经是 `IDLE`，firewall 为 `GOOD`。这和一个仍在
PL 内部持续运行的普通 dataflow deadlock 不完全匹配。

## 与软件仿真的差异

同一套 entry probe 源码在 software/TAPA simulation 中能看到 probe 写回：

```text
thermal2_n16:
  Status[8..11]=1245921841,16,1,1
  Metrics[8..11]=1245921841,16,1,1
  Debug[48..51]=1245921841,11,1,8192

thermal2_n1024:
  Status[8..11]=1245921841,1024,1,64
  Metrics[8..11]=1245921841,1024,1,64
  Debug[48..51]=1245921841,11,1,8192
```

`1245921841` 是 `0x4a434231`，即当前 probe magic。软件能通过而硬件全 0，说明问题
已经越过了普通 C++ 逻辑正确性范围，需要先拆 host/runtime/m_axi 写回边界。

## 当前判断

1. 这不是单纯 `R NNZ=0` 或 empty batch 特例。
   `thermal2_n1024` 是非空 R 路径，也到同一个 `Finish()` 不返回点。

2. 当前证据不能直接定性为 PL dataflow 死锁。
   timeout 后 CU 报 `IDLE`，firewall `GOOD`，更像 TAPA/FRT `Finish()`、kernel
   cleanup、device execution thread，或 BO 迁移/同步边界问题。也不能排除 routed
   timing violation 导致的不可重复硬件异常。

3. `ReadFromDevice()` 后 host 数组仍全 0 是强线索，但不是严格同步证明。
   当前 TAPA/FRT 调用顺序是：

   ```text
   WriteToDevice -> Exec -> ReadFromDevice -> Finish
   ```

   从 host 日志只能证明 `ReadFromDevice()` 函数返回了；不能仅凭这个日志证明所有
   D2H 数据已经在 kernel 完成前可见。因此 pre-Finish 全 0 要按“debug 仍未穿透
   Finish 边界”理解。

4. 当前 entry probe 自己也可能改变失败形态。
   `Jacobi_DebugMonitor` 和 `Jacobi_RoundDispatcher` 入口处都使用了阻塞式
   async mmap write 并等待 write response。如果 HBM[24] 写响应、connectivity 或
   timing 有问题，probe task 会在入口卡住，并让 graph 无法正常收尾。

## 下一版优先修改

### P0. 先做可返回的 mmap-only micro top

新增一个 debug-only top，例如：

```text
CuperJacobiMmapProbeOnly
```

行为只保留三件事：

```text
Status[8..11]  = magic, Row_num, Max_iters, packet_count
Metrics[8..11] = magic, Row_num, Max_iters, packet_count
Debug[48..51]  = magic, stream_count, phase, stop_drain_cycles
return
```

不要接入 Cuper SpMV service、Jacobi update、stage timer、debug event stream 或 feedback
token。这个 top 如果不能稳定返回，下一步就不要继续查 Jacobi dataflow，而应查：

```text
host/kernel ABI
TAPA/FRT set_arg 顺序
write_only_mmap 回读语义
HBM[24] bank 分配
XRT 2023.1 runtime 行为
timing violation
```

建议同时构建两版 connectivity：

```text
版本 A: Status/Metrics/Debug 仍都在 HBM[24]
版本 B: Status -> HBM[24], Metrics -> HBM[25], Debug -> HBM[26]
```

如果 A 失败、B 通过，优先怀疑同 bank 三个 m_axi master 的竞争或 routing/timing。
如果 A/B 都失败，优先看 ABI/runtime/timing。

当前实现状态：

```text
CuperJacobiMmapProbeOnly 已加入 kernels/detail/jacobi_mmap_probe_only.hpp
make cuper-jacobi-build-mmap-probe-xo 已生成 CuperJacobiMmapProbeOnly.xo
```

### P1. 写一个 native XRT debug runner

当前 `tapa::invoke` 把 start/wait/sync 边界藏在 FRT 里，不利于观察失败瞬间。建议新增一个
临时 XRT host，只服务 debug bitstream：

```text
load xclbin
create BOs
set kernel args
sync input BOs to device
start run
sleep/poll N seconds
sync Status/Metrics/Debug BOs from device
print snapshot
then wait with timeout or abort/reset
```

它不需要性能计时，也不需要完整 verification。目标是回答一个问题：

```text
kernel 未完成或 Finish 未返回时，Status/Metrics/Debug BO 里到底有没有写入痕迹？
```

如果 native XRT 能采样到 magic，而 TAPA/FRT pre-Finish 仍全 0，问题就在 FRT
调用顺序或 BO 迁移语义附近。如果 native XRT 也采不到 magic，再回到 kernel
entry/m_axi/timing。

当前实现状态：

```text
host/mmap_probe_xrt.cpp 已加入 native XRT runner
make cuper-jacobi-build-mmap-probe-xrt-host 已通过
```

### P2. 去掉 full graph 里的入口阻塞 mmap probe

在完整 Jacobi graph 里不要再把入口 debug 写做成必须等待 write response 的阻塞路径。
建议改成：

```text
issue write addr/data
记录 issued_count
非阻塞 drain write_resp
周期性更新 response_count
永不因 debug mmap response 阻塞业务控制流
```

尤其是这两处要改：

```text
DLC/Cuper-jacobi-iteration/kernels/detail/jacobi_deadlock_debug.hpp
DLC/Cuper-jacobi-iteration/kernels/detail/jacobi_controller.hpp
```

当前阻塞入口写适合独立 micro top，不适合放在还没跑通的完整 graph 里。

### P3. Host 侧把 Status/Metrics/Debug 初始化为非零 sentinel

现在 host buffer 初始值全是 0，因此无法区分：

```text
kernel 没写
D2H 没同步
kernel 写了 0
BO 被错误清零
```

下一版建议初始化为固定 sentinel：

```text
Status[i] = 0x51510000 + i
Debug[i]  = 0x53530000 + i
Metrics[i] = -1000000.0 - i
```

pre-Finish 和 final dump 都打印。这样至少能判断 D2H 迁移是否覆盖了 host buffer，以及
kernel 写入是否只改了部分槽位。

### P4. 准备一版不带 Debug buffer 的完整 graph

构建一个只保留正式 `Status/Metrics` 的完整 Jacobi graph，临时移除：

```text
Debug mmap
Jacobi_DebugMonitor
Debug_Event_Stream
Debug_Stop_Stream
业务 task 的 Debug_Event_out 参数
```

如果该版行为改变，说明 debug monitor 或 Debug m_axi 本身参与了失败。
如果仍停在 `Finish()` 且 Status/Metrics 仍全 0，再看 FRT/Status m_axi 或整体 graph
退出协议。

### P5. 降低时钟，先拿 timing-clean debug artifact

当前 WNS/TNS 太差，debug 结果有污染风险。下一轮 debug 不追性能，建议先把 clock 降到
更保守的目标，目标是得到 timing-clean 或至少显著收敛的 xclbin。否则即使加了更多
监测点，也很难判断是逻辑协议问题还是 timing 后硬件异常。

## full graph 继续调试的顺序

只有在 P0/P1 证明 mmap probe 可以可靠写回后，再回到完整 Jacobi dataflow。建议顺序：

1. `CuperJacobiOneRoundNoFeedback`
   固定 1 轮，不走 `Feedback_Token_Stream`，`Jacobi_XHbmWriter` 写完后直接发 final
   token 或写 Status 后返回。

2. `CuperJacobiUpdateOnlyDebug`
   输入模拟的 `-R*x`，只测 coeff loader、pair compute、pack writer、X HBM writer 和
   write response。

3. `CuperJacobiSpmvOnlyDebug`
   只跑 R 和 X 到 accumulator/update 输入边界，输出 packet count 到 HBM。

4. 恢复完整 graph，增加 per-stage balance counter：

   ```text
   commands_sent / commands_seen
   frames_sent / frames_seen
   vector_packets_read / forwarded / drained
   matrix_beats_read
   accumulator_outputs
   pair_inputs_consumed
   packets_packed
   x_hbm_writes_issued / responses_seen
   feedback_tokens_sent
   stop_tokens_seen / done_ack_count
   ```

这些 counter 的优先级低于 P0/P1。现在 entry probe 都不可见，继续往内部加 event
只会扩大面积，仍然很可能什么都读不到。

## 下一版验收口径

先跑最小集：

```text
mmap-only probe: thermal2_n16, thermal2_n1024
no-Debug full graph: thermal2_n16, thermal2_n1024
one-round no-feedback: thermal2_n1024
```

每个 case 必须记录：

```text
UUID / SHA256 / timing WNS/TNS
rc / timeout
host 停止位置
Status[0..15]
Metrics[0..15]
Debug[0..63] 或 no-Debug 标记
xbutil dynamic-regions CU 状态
firewall 状态
是否需要 reset 才能恢复
```

在上述最小集返回前，不要更新 HTML 成功数据，也不要把 Jacobi demo 晋级为标准
bitstream。
