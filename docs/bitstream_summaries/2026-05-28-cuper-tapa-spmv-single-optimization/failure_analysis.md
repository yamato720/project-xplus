# CuperPcgSpmv 上板 timeout 分析

## 现象

当前 demo：

```text
395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin
kernel: CuperPcgSpmv
UUID: 08f1f2dc-8c44-007f-a0a5-4dce1236ddd9
```

demo-only smoke 在最小数据集 `thermal2_n16` 上两次 180s timeout。日志目录：

```text
logs/codex_spmv_demo_only_test_20260528_143556/
```

两次日志都停在：

```text
[tapa-invoke] after Exec before ReadFromDevice
[tapa-invoke] after ReadFromDevice before Finish
```

这说明 host 已经启动 kernel，并且 `Y_out` 的 D2H 读取阶段返回；卡住点在
`Finish`，也就是 TAPA/XRT 等待 kernel 完整结束、收尾和释放执行状态时没有看到
所有 task 正常退出。它不像是单纯的输出 BO 读不回来，也不像是 CPU diff 失败；
本轮没有走到 `spmv_avg` 和 diff 打印。

## 为什么从 TAPA PCG 拆出来会单独跑不了

`CuperPcg` 里的 SpMV 链不是原始一次性 `Cuper(...)`。它是“常驻服务”模型：

```text
controller -> command -> ptr/vector/matrix loader -> 16-core chain
           -> accumulator -> checker -> sort tree -> controller/consumer
```

full-PCG 里，controller 在同一个 kernel 内既是命令源，也是 SpMV 输出消费者：

- 初始化时消费 `A*x0`；
- 迭代时消费 `A*p`；
- 后续 dot/update/controller 阶段自然给服务链留出 drain 时间；
- kernel 结束时由 PCG controller 按原协议关闭服务 task。

`CuperPcgSpmv` 抽出来后，拓扑变成：

```text
Pcg_SingleSpmv_Controller
  -> Pcg_* service SpMV chain
  -> Pcg_Mult_Sort_Tree
  -> Pcg_Single_Vector_Writer -> Y_out
  -> Writer_Done_Stream -> controller -> stop
```

关键变化是：原来 SpMV 输出被 PCG controller 在片上消费；现在变成异步 HBM writer
写 `Y_out` 后再通知 controller 发 stop。这个 stop/drain 协议是新拼出来的，不是
full-PCG 中已经验证过的退出路径。只要 writer done、stop token、尾部 destroy、
checker 或 sort tree 中任一环节存在 race 或漏 drain，板上就可能表现为
`ReadFromDevice` 已经能读到 BO，但 kernel 自身仍未 done，因此 `Finish` 卡住。

## 为什么软件仿真通过仍不能证明硬件会退出

软件仿真能证明 C/C++ 级数据流在理想调度下可以产生正确输出，但不能等价证明板上
AP_CTRL_HS done 一定会拉起。这里尤其不等价，原因有三类：

1. `try_read` / `try_write` / `empty` / `full` 的调度在硬件里是真实 backpressure，
   软件仿真很难覆盖所有 FIFO 满/空、AXI write response 延迟和 task 退出 interleaving。
2. `Pcg_Single_Vector_Writer` 依赖 `Y_out.write_resp` 收齐后才写
   `Writer_Done_Stream`；板上 AXI 写响应、TAPA async mmap drain 和 task done 的时序
   比软件仿真严格。
3. full-PCG 原路径有额外 controller 计算阶段，可能无意中给上游 stream drain 留了
   时间；单 SpMV 抽出版在 writer done 后立刻发 stop，更容易暴露 stop token 抢跑、
   padding 输出未排空或尾部 task 未退出的问题。

因此，“软件仿真通过”只能说明功能路径大概率能算出结果，不能说明硬件 service task
的有限退出协议正确。

## 当前最可疑路径

按代码和日志，优先怀疑下面几处。

1. writer done 与全链路 drain 的边界过早

`Pcg_Single_Vector_Writer` 在收到 `num_ite_y = ceil(Row_num/16)` 个 `float_v16`
并收到 AXI write response 后，就向 controller 写 `Writer_Done_Stream`。但这只证明
writer 需要的有效输出包已经写入 `Y_out`，不证明上游 `Pcg_Vector_Checker`、
`Pcg_Mult_Sort_Tree`、16 路 accumulator/core 和尾部 destroy 都已经完成一次命令的
所有 padding/残余 token drain。

`thermal2_n16` 很小，writer 只需要 1 个 `float_v16`。如果上游仍有为对齐产生的
padding、PE 参数或向量转发残留，controller 过早发 stop 会使部分 task 退出，而另一些
task 仍阻塞在旧命令的读写上。

2. stop token 和数据 token 共用/相邻 stream，容易形成抢跑

`Pcg_Vector_Checker` 和 `Pcg_Mult_Sort_Tree` 通过独立 stop stream 退出；core/accumulator
通过 `PE_Param` / `Vector_Y_Param` 中的 `kPcgStopToken` 退出；vector destroy 又通过
另一路 stop 退出。这套协议跨多条 stream，没有单一的全局 drain barrier。

full-PCG 里这套协议是 controller 的自然生命周期一部分；single SpMV demo 中 stop
由 writer done 触发，语义更弱。

3. `Pcg_Destroy_float_v16` 可能先看到 stop，未完全 drain `Vector_X_Stream[16]`

`Pcg_Destroy_float_v16` 当前循环中优先检查 `Stop_in`，看到 stop 后直接 return。
如果 vector packet 还在 core 链尾传播，destroy 先退出会导致
`Vector_X_Stream[16]` 后续没人消费。软件仿真可能因为调度顺序没有触发这个 interleaving；
板上真实流水里这个 race 更现实。

4. full-PCG 输出消费口径和 single writer 口径不完全等价

上一轮 full-PCG demo 的 raw `iter recv` 只是 packed AP 接收/缓存段，不等价于完整
standalone SpMV。`CuperPcgSpmv` 直接把 sort tree 输出写回 `Y_out`，它验证的是另一个
消费端：HBM writer + host D2H + kernel finish。这个出口在 full-PCG 中并没有同形态
验证过。

## 当前判断

这不是“从 PCG 里拆出来所以一定应该能跑”的问题。拆出来复用了计算链，但改变了
生命周期边界：

- full-PCG 验证的是服务链在 PCG controller 消费下能工作；
- single SpMV demo 要额外验证服务链能独立写 HBM、发 done、全 task 有限退出；
- 当前失败点在 `Finish`，更像有限退出/drain 协议问题，而不是矩阵计算本身或
  软件仿真覆盖过的数值路径问题。

## 下一步修复建议

优先按“让 kernel 有限退出”调试，而不是先看性能：

1. 给 `CuperPcgSpmv` 加硬件可读的阶段/心跳计数，至少区分：
   - controller 已发 command；
   - writer 收到第一个/最后一个 output packet；
   - writer 收到所有 AXI write response；
   - controller 已发 stop；
   - destroy/checker/sort 是否收到 stop。
2. 改 `Pcg_Destroy_float_v16` 的退出策略：不要一看到 stop 就 return，先在 stop 后继续
   drain 到一个明确边界，或让 stop 跟随 vector stream 数据链尾传播。
3. 在 single SpMV demo 中增加一个显式 drain barrier：writer done 只表示 `Y_out`
   写完，controller 发 stop 前还需要等待上游完成本次命令的可证明信号。
4. 暂时把 `Pcg_Single_Vector_Writer` 改成片上消费输出并计数，不写 HBM；若这样能
   finish，再单独定位 async mmap writer/AXI response；若仍不能 finish，则问题在
   service stop/drain。
5. 用 `thermal2_n16` 保持最小复现，不扩大 sweep。修到 n16 能稳定返回并有 diff 后，
   再跑 `thermal2_n65536`、`thermal2_n131072` 等性能点。

本轮失败 demo 不更新正式 `source.diff`。后续只有修复后 demo-only 上板确认能返回并
有性能/边界收益，才更新该目标目录的正式补丁。
