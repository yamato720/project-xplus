# SpMV accumulator 乱序方案 2：Router 用输入下标识别 Core 来源

本文记录当前正在实现和编译的方案 2。它和方案 1 的主要区别是：Core 输出包仍使用
现有 `Matrix_Mult_X`，不在数据包里显式增加 `core_id`；第一级 Router 根据输入
stream 下标判断来源 Core。

## 当前目标

目标是把 SpMV 后端 accumulator 从“固定接收某一路 Core 输出”改为“按最终输出
packet 拥有 partial sum”。这样不同 Core 产生的 row contribution 可以乱序到达，
只要 Router 把它们送到正确 owner accumulator 即可。

当前路径是：

```text
Core[0..15]
  -> RowRouterOoo
  -> OwnerAccumulatorOoo[0..15]
  -> TaggedScatterWriter
  -> Y
```

其中：

| 模块 | 作用 |
| --- | --- |
| `CuperSpmvOnly_RowRouterOoo` | 读取 16 路 Core 输出，用输入 stream 下标作为来源 Core id，把 beat 内有效 slot 转成 tagged scalar。 |
| `CuperSpmvOnly_OwnerAccumulatorOoo` | 按 `packet_idx % HBM_CHANNEL_NUM` 拥有 partial sum，接收任意来源 Core 的贡献并累加。 |
| `CuperSpmvOnly_TaggedScatterWriter` | 把 owner accumulator 输出的 tagged scalar 写回 host 侧 scalar `Y_out`。 |

当前宏组合：

```text
JACOBI_TOP=CuperSpmvServiceOnly
JACOBI_SPMV_ONLY=1
JACOBI_HBM_CHANNELS=16
JACOBI_SPMV_LANE_STATIC_REAL=1
JACOBI_SPMV_OOO_ACCUMULATE=1
```

## 当前边界

这版先验证 accumulator 入口乱序和 owner-bank partial sum，不同时重写 X 广播路径。
因此 X 仍沿用原 Cuper 风格：

```text
Vector_Loader -> Core0 -> Core1 -> ... -> Core15 -> drain
```

也就是 X 只从 HBM 读取一次，然后沿 Core 链转发。这个路径节省 HBM 读口，但长链会
带来传播延迟和背压风险；在 OOO accumulator 逐步解除后端顺序限制后，X 链可能变成
新的限制点。

## 方案 2 待办

1. 把中心 `RowRouterOoo` 拆成多级 RouterTree。

   第一版中心 Router 功能简单，但所有 Core 输出会汇聚到一个仲裁点。后续可按
   4 路或 8 路一组做 local router，再进入 owner bank，降低中心 fan-in 压力。

2. 增加 X 转发树。

   当前 X 是链式转发。后续计划保持“一次 HBM 读取 X”的原则，但把链式广播改成树状
   广播，例如：

   ```text
   Vector_Loader
     -> XRouter0 -> Core0..3
     -> XRouter1 -> Core4..7
     -> XRouter2 -> Core8..11
     -> XRouter3 -> Core12..15
   ```

   这个改动的目标不是增加 X 的 HBM 读取次数，而是缩短最长转发路径，减少后级 Core
   因等待 X 包传播而被动停顿。它应作为方案 2 的后续优化项单独做，不混进当前
   OOO accumulator bitstream。

3. 观察 owner accumulator 的 II、URAM 和时序。

   方案 2 把 partial sum 的所有权改为 output packet owner 后，性能上限取决于
   Router 吞吐、owner bank 累加 II、URAM 端口压力和 writer 出口。若 HLS 报告显示
   owner accumulator 因读改写冲突不能稳定 II=1，需要继续做 row locality cache 或
   更细粒度 bank 拆分。

4. 保留方案 1 作为长期可扩展备选。

   如果后续需要先 merge Core 输出再路由，方案 2 的“输入下标即来源”信息会在 merge
   后丢失；那时可切到方案 1，在 Core 输出包里显式携带 `core_id`。

## 当前验证状态

截至本文记录，当前方案 2 的 software/TAPA simulation 已通过：

```text
thermal2_n1024
thermal2_n65536
thermal2
```

## 2026-06-19 静态转置与 owner-lane accumulator 结果

本轮按“每 8 路一组”的方向，把中心 `RowRouterOoo` 改成静态 8-lane transpose：

```text
Core[source]
  -> SourceLaneSplitterOoo[source]
  -> Owner_Lane_Stream[owner * 8 + pair_lane]
  -> OwnerLaneAccumulatorOoo[owner, pair_lane]
  -> TaggedScatterWriterOoo
```

映射关系：

```text
group_size = HBM_CHANNEL_NUM / 8
owner      = lane * group_size + (source % group_size)
pair_lane  = source / group_size
```

这样仍保留“按最终输出 packet owner 持有 partial sum”的设计，但乱序强度从全通道
中心路由降低为固定 8-lane 分组，避免一个动态 router 同时面对所有 Core 输出。

software/TAPA simulation 已通过：

```text
thermal2_n1024
thermal2_n65536
thermal2
```

HLS 结果显示，转发层已经不是瓶颈：

```text
SourceLaneSplitterOoo / split_packets: Final II = 1
TaggedScatterWriterOoo / scatter:      Final II = 1
OwnerLaneAccumulatorOoo / writer:      Final II = 1
OwnerLaneAccumulatorOoo / consume:     Final II = 14
```

因此当前瓶颈已经锁定在 accumulator 对 partial-sum URAM 的 FP32 读-改-写依赖：
即使每个 `OwnerLaneAccumulatorOoo` 只接一条输入流，HLS 仍认为连续输入可能命中同一个
row 地址，必须等待 FP32 加法结果写回，最终 `consume` 仍为 II=14。

这说明“拆 router / 每 8 路一组”可以保留乱序结构，但单靠它不能恢复流水。下一步若
继续追求性能，需要在 accumulator 算法上处理同 row 连续贡献，例如：

1. 重新引入 host 侧 window/reorder，让同一 partial-sum 地址保持足够距离；
2. 在硬件里做 pending scoreboard / 多槽 row combiner，并保证 HLS 能识别独立槽位；
3. 改成先按 row 收集贡献再做局部 reduction tree，最后写 partial sum；
4. 使用更宽或不同形式的局部累加协议，而不是每个贡献都立即读改写 URAM。

当前不建议基于这版继续生成完整 bitstream；应先把 `OwnerLaneAccumulatorOoo / consume`
的 II 降下来，再进入 Vitis link。

## 2026-06-20 追加：16 owner-bank RTL demo 已生成

后续没有继续用 128 个 `OwnerLaneAccumulatorOoo` 直接进硬件，而是把每个 owner 的
8 条 pair-lane 收进一个 RTL owner-bank wrapper：

```text
SourceLaneSplitterOoo[source]
  -> Owner_Bank_Stream[owner][pair_lane]
  -> RtlOwnerBankAccumulatorOoo[owner]
  -> TaggedScatterWriterOoo
```

也就是说，乱序强度仍然是按最终 output packet owner 聚合，但硬件资源从
`owner * pair_lane` 数量级的独立 RTL 实例，收缩到 `owner` 数量级的 RTL wrapper。
每个 wrapper 内部实例化 8 个 lane accumulator，并在 bank 内做 round-robin 输出仲裁。

当前宏组合：

```text
JACOBI_TOP=CuperSpmvServiceOnly
JACOBI_SPMV_ONLY=1
JACOBI_HBM_CHANNELS=16
JACOBI_SPMV_LANE_STATIC_REAL=1
JACOBI_SPMV_OOO_ACCUMULATE=1
JACOBI_SPMV_OOO_ACCUMULATE_RTL=1
```

当前同步 artifact：

```text
395bitstream/cuper-tapa-spmv-u55c-20260620-ooobank16-demo.xclbin
```

构建结果：

```text
UUID: 22b0a282-c282-cfaf-e45a-f8bebf4cc644
SHA256: a5ab4ba8a601bb12c3b737e318da28c29a3e4bdd2c037a9e670ac31a5a9f51b4
DATA/KERNEL/HBM clock: 149 / 500 / 450 MHz
Timing: WNS -0.005 ns, TNS -0.017 ns, setup failing endpoints 9
```

这说明 16 owner-bank RTL 方案已经越过了 128 owner-lane 版本的资源/布局硬失败点。
不过它仍需服务器侧 `thermal2` sweep 验证性能和稳定性；在上板数据回来前，不能把它
当作优于 `strip16` 或 `lanereal16` 的性能结论。
