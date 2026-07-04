package cuper.spmv

import chisel3._
import chisel3.util._

// 固定 8-HBM 的 Chisel SpMV datapath，用来替换 CuperSpmvServiceOnly 中原 HLS/RTL
// 数据通路。外部端口名保持 HLS stream/ap_ctrl_hs 风格，便于被现有 TAPA kernel 例化。
class CuperSpmvOnlyChiselDataPath8 extends RawModule with CuperSpmvStreamPorts {
  override def desiredName: String = "CuperSpmvOnly_ChiselDataPath8"

  // 8 个 Matrix_A_Stream 输入。每个 512-bit beat 内有 8 个 64-bit slot，因此 owner 维度
  // 也固定为 8。xMem 缓存当前 batch 的 X 向量窗口。
  private val hbmChannels = 8
  private val xWords = 8192
  private val xAddrBits = log2Ceil(xWords)
  private val ownerGroups = 12288
  private val groupBits = log2Ceil(ownerGroups)

  // 与 Vivado HLS ap_ctrl_hs 兼容的启动/完成握手。
  val ap_clk = IO(Input(Clock())).suggestName("ap_clk")
  val ap_rst_n = IO(Input(Bool())).suggestName("ap_rst_n")
  val ap_start = IO(Input(Bool())).suggestName("ap_start")
  val ap_done = IO(Output(Bool())).suggestName("ap_done")
  val ap_idle = IO(Output(Bool())).suggestName("ap_idle")
  val ap_ready = IO(Output(Bool())).suggestName("ap_ready")

  // 控制参数沿用 CuperSpmvServiceOnly 的 ABI。Matrix_len 当前仅保留为未使用输入，
  // 真正的 per-channel 起止边界来自 PE_Param_in 流。
  val Iteration_num = IO(Input(UInt(32.W))).suggestName("Iteration_num")
  val Row_num = IO(Input(UInt(32.W))).suggestName("Row_num")
  val Batch_num = IO(Input(UInt(32.W))).suggestName("Batch_num")
  val Matrix_len = IO(Input(UInt(32.W))).suggestName("Matrix_len")
  val Column_num = IO(Input(UInt(32.W))).suggestName("Column_num")

  // PE_Param_in 流格式沿用 strip-padding 路径：前 4 个 header 丢弃，然后每个 batch
  // 读取 8 个 start 和 8 个 end，得到各 HBM channel 本 batch 的 matrix beat 范围。
  val PE_Param_in_s_dout = IO(Input(UInt(33.W))).suggestName("PE_Param_in_s_dout")
  val PE_Param_in_s_empty_n = IO(Input(Bool())).suggestName("PE_Param_in_s_empty_n")
  val PE_Param_in_s_read = IO(Output(Bool())).suggestName("PE_Param_in_s_read")
  val PE_Param_in_peek_dout = IO(Input(UInt(33.W))).suggestName("PE_Param_in_peek_dout")
  val PE_Param_in_peek_empty_n = IO(Input(Bool())).suggestName("PE_Param_in_peek_empty_n")
  val PE_Param_in_peek_read = IO(Output(Bool())).suggestName("PE_Param_in_peek_read")

  // X 向量流每个 513-bit word 只消费低 512 bit，拆成 16 个 float32 缓存在 xMem。
  val Vector_X_Stream_in_s_dout = IO(Input(UInt(513.W))).suggestName("Vector_X_Stream_in_s_dout")
  val Vector_X_Stream_in_s_empty_n = IO(Input(Bool())).suggestName("Vector_X_Stream_in_s_empty_n")
  val Vector_X_Stream_in_s_read = IO(Output(Bool())).suggestName("Vector_X_Stream_in_s_read")
  val Vector_X_Stream_in_peek_dout = IO(Input(UInt(513.W))).suggestName("Vector_X_Stream_in_peek_dout")
  val Vector_X_Stream_in_peek_empty_n = IO(Input(Bool())).suggestName("Vector_X_Stream_in_peek_empty_n")
  val Vector_X_Stream_in_peek_read = IO(Output(Bool())).suggestName("Vector_X_Stream_in_peek_read")

  val m0 = matrixIn("Matrix_A_Stream_0")
  val m1 = matrixIn("Matrix_A_Stream_1")
  val m2 = matrixIn("Matrix_A_Stream_2")
  val m3 = matrixIn("Matrix_A_Stream_3")
  val m4 = matrixIn("Matrix_A_Stream_4")
  val m5 = matrixIn("Matrix_A_Stream_5")
  val m6 = matrixIn("Matrix_A_Stream_6")
  val m7 = matrixIn("Matrix_A_Stream_7")
  private val matrix = Seq(m0, m1, m2, m3, m4, m5, m6, m7)

  val y0 = taggedOut("Vector_Y_Tagged_Stream_0")
  val y1 = taggedOut("Vector_Y_Tagged_Stream_1")
  val y2 = taggedOut("Vector_Y_Tagged_Stream_2")
  val y3 = taggedOut("Vector_Y_Tagged_Stream_3")
  val y4 = taggedOut("Vector_Y_Tagged_Stream_4")
  val y5 = taggedOut("Vector_Y_Tagged_Stream_5")
  val y6 = taggedOut("Vector_Y_Tagged_Stream_6")
  val y7 = taggedOut("Vector_Y_Tagged_Stream_7")
  private val tagged = Seq(y0, y1, y2, y3, y4, y5, y6, y7)

  private val resetBool = !ap_rst_n

  withClockAndReset(ap_clk, resetBool) {
    // 顶层顺序控制：
    // idle -> 读 header -> 清零 partial -> 对每个 batch 读 start/X/end/矩阵 ->
    // 等待累加器 drain -> 逐 source-pair/group 输出 tagged Y -> 下一轮或 done。
    val states = Enum(18)
    val idle = states(0)
    val readHeader = states(1)
    val initPartial = states(2)
    val readStart = states(3)
    val loadX = states(4)
    val loadXWrite = states(5)
    val readEnd = states(6)
    val consumeBatch = states(7)
    val issueSlotRead = states(8)
    val issueSlotWait = states(9)
    val issueSlotSend = states(10)
    val drainAccum = states(11)
    val outputRead = states(12)
    val outputWait = states(13)
    val outputEmit = states(14)
    val nextOutput = states(15)
    val nextIter = states(16)
    val doneState = states(17)

    val state = RegInit(idle)
    val donePulse = RegInit(false.B)
    // iterationTime 为 0 时按 1 轮处理，保持 host 传 0 不会直接空跑。
    val iterationTime = Reg(UInt(32.W))
    val iterIdx = RegInit(0.U(32.W))
    val numOutPackets = Reg(UInt(32.W))
    val numOwnerGroups = Reg(UInt(32.W))
    val totalVectorPackets = Reg(UInt(32.W))

    val headerCount = RegInit(0.U(3.W))
    val initGroup = RegInit(0.U(groupBits.W))
    val boundaryCount = RegInit(0.U(4.W))
    val batchIdx = RegInit(0.U(32.W))
    val xPacketIdx = RegInit(0.U(10.W))
    val xPacketLimit = Reg(UInt(10.W))
    val xPacketBase = Reg(UInt(32.W))
    val xWriteWord = Reg(UInt(512.W))
    val xWriteLane = RegInit(0.U(4.W))

    // 每个 HBM channel 当前 batch 的 [start, end) matrix beat 范围。
    val start = Reg(Vec(hbmChannels, UInt(32.W)))
    val end = Reg(Vec(hbmChannels, UInt(32.W)))
    val remaining = Reg(Vec(hbmChannels, UInt(32.W)))
    val issueSource = RegInit(0.U(3.W))
    val issueOwner = RegInit(0.U(3.W))
    val issueWord = Reg(UInt(512.W))
    val issuePrevCol = RegInit("h3fff".U(14.W))
    val issuePrevVal = RegInit(0.U(32.W))
    val issueRow = Reg(UInt(18.W))
    val issueValue = Reg(UInt(32.W))
    val issueX = Reg(UInt(32.W))

    // 输出扫描坐标：outPair 选择 source-HBM，outGroup/owner 合成最终 output packet id。
    val outGroup = RegInit(0.U(groupBits.W))
    val outPair = RegInit(0.U(3.W))
    val outPing = Reg(Vec(hbmChannels, UInt(32.W)))
    val outPong = Reg(Vec(hbmChannels, UInt(32.W)))
    val outValid = RegInit(VecInit(Seq.fill(hbmChannels)(false.B)))
    val outPacket = Reg(Vec(hbmChannels, UInt(32.W)))

    val xMem = SyncReadMem(xWords, UInt(32.W))
    val xReadEn = WireDefault(false.B)
    val xReadAddr = WireDefault(0.U(xAddrBits.W))
    val xReadData = xMem.read(xReadAddr, xReadEn)
    val xWriteEn = WireDefault(false.B)
    val xWriteAddr = WireDefault(0.U(xAddrBits.W))
    val xWriteData = WireDefault(0.U(32.W))
    when(xWriteEn) {
      xMem.write(xWriteAddr, xWriteData)
    }
    // 8x8 Core/Accumulator lane 矩阵：source 维度来自 Matrix_A_Stream_i，
    // owner 维度来自 beat 内 slot。Core 和 Accumulator 之间的 Decoupled product
    // stream 是后续插入 scoreboard/乱序调度的位置。
    val coreLanes = Seq.tabulate(hbmChannels, hbmChannels) { (_, _) =>
      Module(new StripCoreLane(groupBits))
    }
    val accumLanes = Seq.tabulate(hbmChannels, hbmChannels) { (_, _) =>
      Module(new StripAccumLane(groupBits, ownerGroups))
    }

    // 这些计数器当前不作为端口输出，dontTouch 便于综合后在调试网表/波形中观察。
    val counterXPackets = RegInit(0.U(64.W))
    val counterMatrixBeats = RegInit(VecInit(Seq.fill(hbmChannels)(0.U(64.W))))
    val counterValidSlots = RegInit(0.U(64.W))
    val counterPaddingSlots = RegInit(0.U(64.W))
    val counterRawStall = RegInit(0.U(64.W))
    val counterOutputWrites = RegInit(0.U(64.W))
    val counterWriterBackpressure = RegInit(0.U(64.W))
    dontTouch(counterXPackets)
    dontTouch(counterMatrixBeats)
    dontTouch(counterValidSlots)
    dontTouch(counterPaddingSlots)
    dontTouch(counterRawStall)
    dontTouch(counterOutputWrites)
    dontTouch(counterWriterBackpressure)

    // 行/列数换算：
    // - Vector X 每包 16 个 float32
    // - Vector Y 每 tagged packet 覆盖 2 行，8 个 owner 合成一个 group
    // - 每个 batch 最多预取 512 个 X packet，对应原 Cuper batch 窗口
    def packetCount(count: UInt): UInt = (count + 15.U) >> 4
    def ownerGroupCount(count: UInt): UInt = (count + 7.U) >> 3
    def minUInt(a: UInt, b: UInt): UInt = Mux(a < b, a, b)
    def vectorPacketStart(batch: UInt): UInt = batch << 9
    def vectorPacketLimit(base: UInt): UInt =
      Mux(base >= totalVectorPackets, 0.U, minUInt(512.U, totalVectorPackets - base)(9, 0))
    def xBits(word: UInt, lane: Int): UInt = word(31 + lane * 32, lane * 32)
    def slotBits(word: UInt, lane: Int): UInt = word(63 + lane * 64, lane * 64)
    def selectSlot(word: UInt, lane: UInt): UInt =
      Mux1H((0 until hbmChannels).map(i => (lane === i.U) -> slotBits(word, i)))

    // 默认所有 stream 不读不写，只在具体状态中拉高握手信号。
    PE_Param_in_s_read := false.B
    PE_Param_in_peek_read := false.B
    Vector_X_Stream_in_s_read := false.B
    Vector_X_Stream_in_peek_read := false.B

    val matrixRead = Wire(Vec(hbmChannels, Bool()))
    matrixRead := VecInit(Seq.fill(hbmChannels)(false.B))
    for ((ports, idx) <- matrix.zipWithIndex) {
      ports.read := matrixRead(idx)
      ports.peekRead := false.B
    }

    val taggedDin = Wire(Vec(hbmChannels, UInt(129.W)))
    val taggedWrite = Wire(Vec(hbmChannels, Bool()))
    taggedDin := VecInit(Seq.fill(hbmChannels)(0.U(129.W)))
    taggedWrite := VecInit(Seq.fill(hbmChannels)(false.B))
    for ((ports, idx) <- tagged.zipWithIndex) {
      ports.din := taggedDin(idx)
      ports.write := taggedWrite(idx)
    }

    ap_done := donePulse
    ap_ready := donePulse
    ap_idle := state === idle && !donePulse

    val matrixValid = VecInit(matrix.map(_.emptyN))
    val matrixDout = VecInit(matrix.map(_.dout))
    val yFullN = VecInit(tagged.map(_.fullN))

    // 每个 lane 先给默认空输入；后面的 slot 解码逻辑只覆盖本周期实际发射的 lane。
    val laneRawStall = Wire(Vec(hbmChannels, Vec(hbmChannels, Bool())))
    val laneAccept = Wire(Vec(hbmChannels, Vec(hbmChannels, Bool())))
    val laneBusy = Wire(Vec(hbmChannels, Vec(hbmChannels, Bool())))
    for (source <- 0 until hbmChannels) {
      for (owner <- 0 until hbmChannels) {
        val core = coreLanes(source)(owner)
        val accum = accumLanes(source)(owner)

        core.io.inValid := false.B
        core.io.inGroup := 0.U
        core.io.inPong := false.B
        core.io.inValue := 0.U
        core.io.inX := 0.U

        // 当前是直连；scoreboard 后续可以替换这三行之间的 product stream。
        accum.io.in <> core.io.out

        accum.io.initValid := state === initPartial
        accum.io.initAddr := initGroup
        accum.io.outRead := state === outputRead && outPair === source.U
        accum.io.outAddr := outGroup

        laneRawStall(source)(owner) := core.io.rawStall || accum.io.rawStall
        laneAccept(source)(owner) := core.io.accept
        laneBusy(source)(owner) := core.io.busy || accum.io.busy
      }
    }

    val issueSlot = selectSlot(issueWord, issueOwner)
    val issueSlotRow = issueSlot(49, 32)
    val issueSlotCol = issueSlot(63, 50)
    val issueSlotRawVal = issueSlot(31, 0)
    val issueSlotReuse = (issuePrevCol & issueSlotCol) === "h3fff".U
    val issueSlotValue = Mux(issueSlotReuse, issuePrevVal, issueSlotRawVal)
    val issueSlotValid = !issueSlotRow(17)

    val sourceCanRead = VecInit((0 until hbmChannels).map { source =>
      remaining(source) =/= 0.U && matrixValid(source)
    })
    val anySourceCanRead = sourceCanRead.asUInt.orR
    val selectedSource = PriorityEncoder(sourceCanRead)
    val selectedMatrixWord = Mux1H((0 until hbmChannels).map { source =>
      (selectedSource === source.U) -> matrixDout(source)(511, 0)
    })
    for (source <- 0 until hbmChannels) {
      matrixRead(source) := state === consumeBatch && anySourceCanRead && selectedSource === source.U
    }

    val activeCoreReady = Mux1H((0 until hbmChannels).flatMap { source =>
      (0 until hbmChannels).map { owner =>
        (issueSource === source.U && issueOwner === owner.U) -> coreLanes(source)(owner).io.inReady
      }
    })
    for (source <- 0 until hbmChannels) {
      for (owner <- 0 until hbmChannels) {
        when(state === issueSlotSend && issueSource === source.U && issueOwner === owner.U) {
          coreLanes(source)(owner).io.inValid := true.B
          coreLanes(source)(owner).io.inGroup := issueRow(14, 1)
          coreLanes(source)(owner).io.inPong := issueRow(0)
          coreLanes(source)(owner).io.inValue := issueValue
          coreLanes(source)(owner).io.inX := issueX
        }
      }
    }

    // 汇总状态用于 batch 完成、累加器 drain、输出背压和调试计数。
    val anyRemaining = remaining.map(_ =/= 0.U).reduce(_ || _)
    val anyLaneBusy = laneBusy.asUInt.orR
    val anyRawStall = laneRawStall.asUInt.orR
    val totalOutputWrites = PopCount(taggedWrite.asUInt)
    val anyOutputBlocked =
      outValid.zip(yFullN).map { case (valid, ready) => valid && !ready }.reduce(_ || _)

    val selectedPing = Wire(Vec(hbmChannels, UInt(32.W)))
    val selectedPong = Wire(Vec(hbmChannels, UInt(32.W)))
    // 输出时 outPair 选择一个 source-HBM，把该 source 对所有 owner 的 partial sum 一次读出。
    for (owner <- 0 until hbmChannels) {
      selectedPing(owner) := Mux1H((0 until hbmChannels).map { source =>
        (outPair === source.U) -> accumLanes(source)(owner).io.outPing
      })
      selectedPong(owner) := Mux1H((0 until hbmChannels).map { source =>
        (outPair === source.U) -> accumLanes(source)(owner).io.outPong
      })
    }

    donePulse := false.B

    // 性能/健康计数：有效 slot、padding slot、RAW 停顿、写出数量和 writer 背压。
    when(anyRawStall) {
      counterRawStall := counterRawStall + 1.U
    }
    when(totalOutputWrites =/= 0.U) {
      counterOutputWrites := counterOutputWrites + totalOutputWrites
    }
    when(state === outputEmit && anyOutputBlocked) {
      counterWriterBackpressure := counterWriterBackpressure + 1.U
    }

    switch(state) {
      is(idle) {
        when(ap_start) {
          // 新一次 ap_start 清空运行计数，并根据当前输入规模计算输出和 X 预取边界。
          iterationTime := Mux(Iteration_num === 0.U, 1.U, Iteration_num)
          iterIdx := 0.U
          numOutPackets := packetCount(Row_num)
          numOwnerGroups := ownerGroupCount(packetCount(Row_num))
          totalVectorPackets := packetCount(Column_num)
          headerCount := 0.U
          counterXPackets := 0.U
          counterMatrixBeats := VecInit(Seq.fill(hbmChannels)(0.U(64.W)))
          counterValidSlots := 0.U
          counterPaddingSlots := 0.U
          counterRawStall := 0.U
          counterOutputWrites := 0.U
          counterWriterBackpressure := 0.U
          state := readHeader
        }
      }

      is(readHeader) {
        // 丢弃 PE_Param_in 前 4 个 header token；后续内容才是 per-HBM start/end。
        when(PE_Param_in_s_empty_n) {
          PE_Param_in_s_read := true.B
          headerCount := headerCount + 1.U
          when(headerCount === 3.U) {
            initGroup := 0.U
            state := initPartial
          }
        }
      }

      is(initPartial) {
        // 每轮开始前清零所有 owner group 的 ping/pong partial sum。
        when(initGroup + 1.U >= numOwnerGroups(groupBits - 1, 0)) {
          boundaryCount := 0.U
          state := readStart
        }.otherwise {
          initGroup := initGroup + 1.U
        }
      }

      is(readStart) {
        // 读取当前 batch 的 8 个 per-channel 起始 beat offset。
        when(PE_Param_in_s_empty_n) {
          PE_Param_in_s_read := true.B
          start(boundaryCount(2, 0)) := PE_Param_in_s_dout(31, 0)
          boundaryCount := boundaryCount + 1.U
          when(boundaryCount === 7.U) {
            batchIdx := 0.U
            xPacketBase := 0.U
            xPacketLimit := vectorPacketLimit(0.U)
            xPacketIdx := 0.U
            state := loadX
          }
        }
      }

      is(loadX) {
        // 只加载当前 batch 需要的 X packet 窗口。为避免 16 写/64 读的超大
        // 多端口寄存器阵列，每个 float_v16 packet 分 16 拍写入单端口 SRAM。
        when(xPacketIdx >= xPacketLimit) {
          boundaryCount := 0.U
          state := readEnd
        }.elsewhen(Vector_X_Stream_in_s_empty_n) {
          Vector_X_Stream_in_s_read := true.B
          xWriteWord := Vector_X_Stream_in_s_dout(511, 0)
          xWriteLane := 0.U
          state := loadXWrite
        }
      }

      is(loadXWrite) {
        xWriteEn := true.B
        xWriteAddr := ((xPacketIdx << 4) + xWriteLane)(xAddrBits - 1, 0)
        xWriteData := Mux1H((0 until 16).map { lane =>
          (xWriteLane === lane.U) -> xBits(xWriteWord, lane)
        })
        when(xWriteLane === 15.U) {
          xPacketIdx := xPacketIdx + 1.U
          counterXPackets := counterXPackets + 1.U
          state := loadX
        }.otherwise {
          xWriteLane := xWriteLane + 1.U
        }
      }

      is(readEnd) {
        // 读取当前 batch 的 8 个结束 offset，同时得到各 channel 剩余 beat 数。
        when(PE_Param_in_s_empty_n) {
          val channel = boundaryCount(2, 0)
          PE_Param_in_s_read := true.B
          end(channel) := PE_Param_in_s_dout(31, 0)
          remaining(channel) := PE_Param_in_s_dout(31, 0) - start(channel)
          boundaryCount := boundaryCount + 1.U
          when(boundaryCount === 7.U) {
            state := consumeBatch
          }
        }
      }

      is(consumeBatch) {
        // 每次只取一路 Matrix beat，然后按 8 个 owner slot 串行读 X/送 Core。
        // 这保留 8-HBM ABI，但避免对 X SRAM 建 64 个组合读端口。
        when(!anyRemaining) {
          when(batchIdx + 1.U >= Batch_num) {
            state := drainAccum
          }.otherwise {
            for (source <- 0 until hbmChannels) {
              start(source) := end(source)
            }
            // 下一 batch 复用上一批 end 作为 start，再加载对应 X 窗口。
            batchIdx := batchIdx + 1.U
            val nextBatch = batchIdx + 1.U
            val nextBase = vectorPacketStart(nextBatch)
            xPacketBase := nextBase
            xPacketLimit := vectorPacketLimit(nextBase)
            xPacketIdx := 0.U
            state := loadX
          }
        }.elsewhen(anySourceCanRead) {
          issueSource := selectedSource
          issueOwner := 0.U
          issueWord := selectedMatrixWord
          issuePrevCol := "h3fff".U
          issuePrevVal := 0.U
          state := issueSlotRead
        }
      }

      is(issueSlotRead) {
        issuePrevCol := issueSlotCol
        issuePrevVal := issueSlotValue
        when(issueSlotValid) {
          issueRow := issueSlotRow
          issueValue := issueSlotValue
          xReadEn := true.B
          xReadAddr := issueSlotCol(xAddrBits - 1, 0)
          state := issueSlotWait
        }.otherwise {
          counterPaddingSlots := counterPaddingSlots + 1.U
          when(issueOwner === 7.U) {
            remaining(issueSource) := remaining(issueSource) - 1.U
            counterMatrixBeats(issueSource) := counterMatrixBeats(issueSource) + 1.U
            state := consumeBatch
          }.otherwise {
            issueOwner := issueOwner + 1.U
          }
        }
      }

      is(issueSlotWait) {
        issueX := xReadData
        state := issueSlotSend
      }

      is(issueSlotSend) {
        when(activeCoreReady) {
          counterValidSlots := counterValidSlots + 1.U
          when(issueOwner === 7.U) {
            remaining(issueSource) := remaining(issueSource) - 1.U
            counterMatrixBeats(issueSource) := counterMatrixBeats(issueSource) + 1.U
            state := consumeBatch
          }.otherwise {
            issueOwner := issueOwner + 1.U
            state := issueSlotRead
          }
        }
      }

      is(drainAccum) {
        // 等所有 Core fmul 和 Accumulator read/fadd 流水写回后，再开始读 partial sum 输出。
        when(!anyLaneBusy) {
          outGroup := 0.U
          outPair := 0.U
          state := outputRead
        }
      }

      is(outputRead) {
        // 同步 SRAM 读请求阶段：为当前 source-pair 和 owner group 读 ping/pong。
        for (owner <- 0 until hbmChannels) {
          val packet = (outGroup << 3) + owner.U
          outPacket(owner) := packet
          outValid(owner) := packet < numOutPackets
        }
        state := outputWait
      }

      is(outputWait) {
        // 等待 SyncReadMem 读数据返回，并锁存到输出寄存器。
        for (owner <- 0 until hbmChannels) {
          outPing(owner) := selectedPing(owner)
          outPong(owner) := selectedPong(owner)
        }
        state := outputEmit
      }

      is(outputEmit) {
        // 若下游 writer 某个 owner stream 满，则保留该 owner 的 outValid，下周期继续尝试。
        for (owner <- 0 until hbmChannels) {
          val payload = Cat(0.U(1.W), outPong(owner), outPing(owner), outPair.pad(32), outPacket(owner))
          taggedDin(owner) := payload
          taggedWrite(owner) := outValid(owner) && yFullN(owner)
          when(outValid(owner) && yFullN(owner)) {
            outValid(owner) := false.B
          }
        }
        val willRemain = outValid.zip(yFullN).map { case (valid, ready) => valid && !ready }.reduce(_ || _)
        when(!willRemain) {
          state := nextOutput
        }
      }

      is(nextOutput) {
        // 先遍历 8 个 source-pair，再进入下一个 owner group。
        when(outPair =/= 7.U) {
          outPair := outPair + 1.U
          state := outputRead
        }.elsewhen(outGroup + 1.U < numOwnerGroups(groupBits - 1, 0)) {
          outPair := 0.U
          outGroup := outGroup + 1.U
          state := outputRead
        }.otherwise {
          state := nextIter
        }
      }

      is(nextIter) {
        // 多 iteration 复用相同矩阵/参数流约定，每轮重新清零 partial sum 并重新读 batch 边界。
        when(iterIdx + 1.U >= iterationTime) {
          state := doneState
        }.otherwise {
          iterIdx := iterIdx + 1.U
          initGroup := 0.U
          boundaryCount := 0.U
          state := initPartial
        }
      }

      is(doneState) {
        // ap_done/ap_ready 只打一拍，然后回到 idle 等待下一次启动。
        donePulse := true.B
        state := idle
      }
    }

    // peek 端口和 Matrix_len/xPacketBase 当前只为 ABI 兼容保留，dontTouch 防止被完全裁掉导致
    // 顶层端口或调试观测点意外变化。
    val unused = PE_Param_in_peek_dout ^ Vector_X_Stream_in_peek_dout ^
      Cat(matrix.map(_.peekDout(0)).reverse) ^ Cat(tagged.map(_.peek(0)).reverse) ^
      Cat(PE_Param_in_peek_empty_n, Vector_X_Stream_in_peek_empty_n) ^
      Matrix_len ^ xPacketBase
    dontTouch(unused)
  }
}
