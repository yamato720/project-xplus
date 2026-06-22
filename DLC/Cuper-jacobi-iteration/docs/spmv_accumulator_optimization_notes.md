# Cuper SpMV accumulator optimization notes

本文记录 `CuperSpmvServiceOnly` strip16/window 系列之后，对 accumulator 继续优化的候选方向。
当前问题的核心不是加法器数量不足，而是同一个 `local_part_Y_ping/pong[addr]` 被短距离重复
读改写时，URAM read-modify-write recurrence 限制 HLS pipeline II。`JACOBI_SPMV_ACC_WINDOW`
能把 `Accumulator/Pipeline_cuper_acc_accumulate` 从 II=2 压到 II=1，但 host 侧为了拉开同 row
距离会引入额外 padding/重排工作量，因此不能直接等价为整图 2x。

## 已观察到的基线

2026-06-22 的 strip16 window 实验中：

- old strip16：`Accumulator/cuper_acc_accumulate` 为 II=2，`VectorWriter/write_Y` 为 II=1。
- window14/window16：accumulator 主循环达到 II=1，但旧 progress 版本把
  `VectorWriter/write_Y` 退成 II=2。
- 删除 `write_Y` 内部的 `ScatterFirstWrite` / `ScatterFirstResp` progress 事件后，
  `VectorWriter/write_Y` 回到 II=1；这两个事件只用于 host pre-Finish progress snapshot，
  不参与计算或最终状态写回。

2026-06-22 的 lanereal16 非 RTL 分段累加实验：

- 宏：`JACOBI_SPMV_SEGMENTED_ACCUMULATE=1`，隐式打开
  `JACOBI_SPMV_LANE_STATIC_REAL=1` 和 `JACOBI_SPMV_OOO_ACCUMULATE=1`。
- 接线：仍走 16 个 `CuperSpmvOnly_OwnerAccumulatorTransposeOoo` owner-bank task，
  没有再切成 128 个 owner-lane output task。
- 软件仿真：`thermal2_n1024`、`thermal2_n65536` 均通过，`Error Num=0`。
- HLS 结果：`OwnerAccumulatorTransposeOoo/Pipeline_consume` 仍为 II=98，日志显示
  仍卡在 `local_part_Y_ping` 的 URAM load/store carried dependence；顺序 flush
  中连续 FP32 add 还拉长了 critical path。
- 结论：这个“段满/逐出时在 consume 内 flush”的实现功能正确，但 HLS 方向不成立，
  不应启动硬件构建。下一步如果继续分段累加，需要把 flush/RMW 从 consume 主循环
  拆出去，或改成真正独立的写回状态机/任务，而不是内联在每拍消费路径里。

后续优化应避免只追求单个 loop 的 II 好看，而要同时关注：

- accumulator 输入工作量是否增加；
- `Y_out` 写回和 progress/debug 路径是否重新串行化；
- routed DATA clock 是否能维持；
- full `thermal2` 等效 cycle 是否真正下降。

## 候选方案评价

| 方法 | 在本项目中的核心目标 | 预期收益 | 主要代价 | 建议优先级 |
| --- | --- | --- | --- | --- |
| 分段累加 / per-row local buffer | 在 accumulator 内先吸收短距离同 row_group 更新，减少直接 URAM RMW | 高 | 比较器、寄存器/CAM、flush 逻辑增加 | 最高 |
| 乱序执行 / 输入预排序 | host 侧拉开热点 row_group 的相邻更新时间，同时减少纯 padding | 中到高 | host 预处理复杂，元数据/顺序约束更难维护 | 高 |
| 小窗口局部归约 | 在小范围内合并同 row 更新，再写 URAM | 中到高 | 方向做错会只增加逻辑，不降 cycle | 中高 |
| 分块 CSR/ELL hybrid | 规则区用更密集格式，不规则区保留 CSR-like stream | 中到高 | 转换逻辑、元数据、双路径验证增加 | 中高 |
| 泛化归约树 / 分层归约 | 先在局部窗口内合并，再进入 accumulator | 中 | 若不按 row_group 命中合并，会白加加法树 | 中 |
| 稀疏到密集混合策略 | 对近 dense 局部块改走 block/dense kernel | 中到高 | 需要阈值、双路径、host 分区 | 中 |
| 原子加 / 原子合并 | 软件或 GPU 原型思路；FPGA lane-owned HLS 主线收益有限 | 低到中 | 热点串行化，时序风险 | 低 |
| GPU shared memory / warp primitive 类比 | FPGA 对应物是 BRAM/register bank 和 lane 局部合并 | 中 | 不能直接同构到当前 TAPA/HLS 代码 | 中 |
| 软件事务 / 锁 | correctness debug 或极小原型 | 很低 | II 和时序都会变差 | 最低 |

这里把“乱序执行”和“预排序/重排输入”视为同一类 host-side scheduling 问题。区别只是策略强弱：
前者偏动态/贪心，后者偏静态分桶/排序。它们的共同目标都是降低 accumulator 近距离同地址碰撞。

## 推荐落地路线

### V1: 小型 per-row cache

在每个 accumulator 内维护 2 到 4 个 cache entry：

```text
{valid, addr, ping_sum, pong_sum}
```

处理流程：

1. 新输入命中 cache addr：直接在寄存器里累加。
2. miss 且有空 entry：分配新 entry。
3. miss 且 cache 满：选择 victim，flush 到 `local_part_Y_ping/pong`，再装入新 addr。
4. batch/iteration 结束前 flush 所有 valid entry。

目标是先验证短距离碰撞能否被硬件吸收，从而降低对 host padding/window 的依赖。V1 不追求完全消除
冲突，只追求以小逻辑代价减少最密集的局部重复更新。

### V2: 8 到 16 entry CAM

如果 V1 有效，再扩大 entry 数，吸收更长距离的热点 row_group。风险是比较器扇出和 mux 变大，
可能把 DATA clock 或 accumulator 内部 critical path 拉坏。需要每一步都查 HLS II、estimated period、
post-route WNS 和 full `thermal2` cycle。

### V3: host 乱序 + 小 cache

host 不再靠固定 window 插 padding，而是做较弱的 row_group aware scheduling：

- 优先把相同 row_group 的更新打散；
- 不强行追求完全无碰撞；
- 让硬件小 cache 吸收剩余局部冲突；
- 以减少输入 beat/padding 为主要收益指标。

这可能是最实际的性能路径：host 降低无效工作量，accumulator 降低 recurrence 敏感度。

### V4: 分块 CSR/ELL hybrid

把规则区域改为 block/ELL-like 格式，不规则区域保留当前 CSR-like packet。该方向改动较大，应放在
V1/V3 证明 accumulator 局部合并有效之后。

## 不建议优先做的方向

- 单纯增加加法树：当前卡点是同地址状态更新，不是浮点加法器数量。
- 在 URAM 上模拟原子加或锁：会把热点串行化，通常比现在更差。
- 直接照搬 GPU shared memory/warp primitive：概念有参考价值，但 FPGA 实现应落到 register/BRAM
  bank 和 lane-local combine，而不是照搬线程模型。

## 判断一次实验是否有效

每轮实验至少记录：

| 指标 | 目的 |
| --- | --- |
| `Accumulator/Pipeline_cuper_acc_accumulate` II/depth/estimated period | 确认 accumulator 没退化 |
| `VectorWriter/Pipeline_write_Y` II | 防止 debug/progress 或写回路径重新变成瓶颈 |
| matrix read beats / padding beats | 判断是否只是把 stall 变成了额外输入工作 |
| DATA/KERNEL/HBM clock 和 routed WNS/TNS | 判断高层收益是否被降频吃掉 |
| `thermal2_n262144` 和 full `thermal2` FPGA ms | 过滤小规模固定开销噪声 |
| 等效 cycles | 区分频率提升和真实工作量下降 |

短期目标不是“所有 report 都 II=1”，而是让 full `thermal2` 等效 cycle 低于 old strip16。
如果 cycle 没降，只是靠更高频率变快，那么该方向仍然有价值，但还没有解决 accumulator 工作量问题。
