 可以做，而且这是比全 RTL 后端更稳的中间分支。核心思路是：

  现有：
  8 条 Owner_Lane_Stream
   -> HLS OwnerAccumulatorTransposeOoo 自己轮询/try_read/累加

  弱 RTL 分支：
  8 条 Owner_Lane_Stream
   -> RTL ScoreboardScheduler 只负责选 lane + pop
   -> 1 条 Scheduled_Tagged_Stream
   -> HLS ScheduledOwnerAccumulator 负责累加/写回

  也就是说，RTL 只做“调度和防冲突”，FP32 加法、URAM partial sum、writer 仍留在 HLS。

  为什么这样合理

  全 RTL owner-bank 出不来，主要难点是 RTL 里同时做：

  - 8 路 stream handshake
  - scoreboard/hazard
  - URAM ping/pong
  - FP32 fadd IP pipeline
  - writer 输出
  - done/drain 状态机

  现在只抽 scoreboard，就把 RTL 难度压到：

  - 看 8 条输入 FIFO head
  - 比较 {lane, addr, pingpong}
  - 选一个可发 lane
  - pop 该 lane
  - 输出 {lane + TaggedScalar} 到 HLS

  这部分不碰 FP32，不碰 URAM，不碰最终写回，时序和调试都简单很多。

  关键接口

  需要一个新的 scheduled token 类型，比如：

  struct CuperSpmvOnly_ScheduledTaggedScalar {
    ap_uint<3> lane;
    CuperSpmvOnly_TaggedScalar tagged;
  };

  RTL scoreboard 输出一条 stream：

  Scheduled_Tagged_Stream
    lane
    done
    packet_idx
    pair_lane
    scalar_lane
    value

  HLS accumulator 用 lane 选择 local_part_Y_ping[lane][addr] / pong[lane][addr]。

  HLS 累加器怎么改

  新建一个函数，例如：

  CuperSpmvOnly_OwnerAccumulatorScoreboardHls(...)

  输入从 8 条变成 1 条 scheduled stream：

  tapa::istream<CuperSpmvOnly_ScheduledTaggedScalar>& Scheduled_Stream

  内部保留现有 HLS owner transpose 的数组：

  local_part_Y_ping[8][URAM_DEPTH]
  local_part_Y_pong[8][URAM_DEPTH]
  done[8]

  consume 循环：

  scheduled = Scheduled_Stream.read();
  lane = scheduled.lane;
  tagged = scheduled.tagged;

  if (tagged.done) done[lane] = true;
  else Adder_p(addr, value, local_part_Y_ping/lane...)

  writer 部分基本沿用当前 OwnerAccumulatorTransposeOoo：每个 owner_group 输出 8 个 pair_lane 的 TaggedFloatV2。

  最重要的风险

  如果 HLS 这边仍然看到“动态 lane 下标访问 local_part_Y_ping[lane][addr]”，它可能把 8 个 lane 的 URAM bank 推断得不够干净，导致 II 或资源不理想。要
  尽量加：

  #pragma HLS array_partition complete variable=local_part_Y_ping dim=1
  #pragma HLS array_partition complete variable=local_part_Y_pong dim=1

  然后看 HLS report。理论上 lane 维完全 partition 后，动态 lane 只是 8:1 mux，不应该产生跨 lane memory conflict。

  scoreboard RTL 插入位置

  在当前宏区附近加一个新宏，比如：

  JACOBI_SPMV_OOO_SCOREBOARD_RTL

  然后 graph 变成：

  SourceLaneSplitterOoo x16
   -> Owner_Lane_Stream[128]
   -> RtlOwnerScoreboardOoo x16
   -> Scheduled_Owner_Stream[16]
   -> HlsOwnerAccumulatorScheduledOoo x16
   -> Vector_Y_Tagged_Stream[16]

  不要复用 JACOBI_SPMV_OOO_ACCUMULATE_RTL，因为那个代表全 RTL owner-bank。新宏单独隔离，避免影响已知版本。

  目前是不是“发空包”

  不是。这个新 RTL scoreboard 也应该保持“不发就不 pop”。如果 8 个 head 都冲突：

  issue_valid=0
  8 条 input stream 都不 read
  scheduled output 不 write

  预期收益

  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   HLS consume 控制逻辑简化                           中等
  ─────────────────────────────────────────────────  ─────────────────────────────────
   用 scoreboard 避免 HLS accumulator 内部冲突保护    中到高
  ─────────────────────────────────────────────────  ─────────────────────────────────
   FP32/URAM 仍是 HLS                                 风险比全 RTL 小很多
  ─────────────────────────────────────────────────  ─────────────────────────────────
   前端 beat 耦合                                     不解决，需要 FIFO/slot 解耦另做

  性能上它不一定超过全 RTL 理想版，但比当前纯 HLS owner transpose 更有希望接近 II≈1，并且比全 RTL owner-bank 容易上板。

  建议实施顺序

  这个分支我建议命名成：

  JACOBI_SPMV_OOO_SCOREBOARD_RTL

  它的定位很清楚：RTL scheduler + HLS accumulator，不是 RTL 替代整个后端。