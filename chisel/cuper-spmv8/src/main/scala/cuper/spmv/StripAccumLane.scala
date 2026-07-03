package cuper.spmv

import chisel3._
import chisel3.util._

// 单个 source-HBM 到 owner-bank 的局部累加 lane。
//
// 这一级只接收 Core lane 已经算好的 value * X[col] 乘积，维护 ping/pong 两套
// partial sum SRAM，并执行 partial += product。乘法已经从这里拆出，后续 scoreboard
// 可以插在 Core lane 和 Accumulator lane 之间。
class StripAccumLane(groupBits: Int, depth: Int) extends Module {
  private val faddLatency = 13

  val io = IO(new Bundle {
    // 每轮开始时逐地址清零 partial sum。
    val initValid = Input(Bool())
    val initAddr = Input(UInt(groupBits.W))

    // Core/scoreboard 输入的中间乘积。
    val in = Flipped(Decoupled(new StripProduct(groupBits)))

    // 输出阶段按 group 读出 ping/pong partial sum。
    val outRead = Input(Bool())
    val outAddr = Input(UInt(groupBits.W))
    val outPing = Output(UInt(32.W))
    val outPong = Output(UInt(32.W))

    // 调试/性能计数信号：busy 用于 drain，rawStall 用于统计同地址相关性阻塞。
    val busy = Output(Bool())
    val rawStall = Output(Bool())
    val accept = Output(Bool())
  })

  // ping/pong 与原 SpMV tagged 输出格式对应：一个 tagged packet 携带同 owner 的两行。
  val pingMem = SyncReadMem(depth, UInt(32.W))
  val pongMem = SyncReadMem(depth, UInt(32.W))

  val rdValid = RegInit(false.B)
  val rdGroup = Reg(UInt(groupBits.W))
  val rdPong = Reg(Bool())
  val rdValue = Reg(UInt(32.W))

  // 用显式 valid/group/pong shift register 对齐 HLS fadd latency。
  val addValid = RegInit(VecInit(Seq.fill(faddLatency)(false.B)))
  val addGroup = Reg(Vec(faddLatency, UInt(groupBits.W)))
  val addPong = Reg(Vec(faddLatency, Bool()))

  val fadd = Module(new HlsFadd32)
  fadd.io.clk := clock
  fadd.io.reset := reset.asBool
  fadd.io.ce := true.B

  // 同一个 group/ping-pong 在读旧值或加法写回阶段中不能再次进入。
  val rdHazard = rdValid && rdGroup === io.in.bits.group && rdPong === io.in.bits.pong
  val addHazard = (0 until faddLatency).map { i =>
    addValid(i) && addGroup(i) === io.in.bits.group && addPong(i) === io.in.bits.pong
  }.reduce(_ || _)
  val hazard = rdHazard || addHazard
  val fire = io.in.valid && !hazard

  io.in.ready := !hazard
  io.rawStall := io.in.valid && hazard
  io.accept := fire
  io.busy := rdValid || addValid.asUInt.orR

  val pingReadForAccum = fire && !io.in.bits.pong
  val pongReadForAccum = fire && io.in.bits.pong
  val pingReadEn = io.outRead || pingReadForAccum
  val pongReadEn = io.outRead || pongReadForAccum
  val pingReadAddr = Mux(io.outRead, io.outAddr, io.in.bits.group)
  val pongReadAddr = Mux(io.outRead, io.outAddr, io.in.bits.group)
  val pingReadData = pingMem.read(pingReadAddr, pingReadEn)
  val pongReadData = pongMem.read(pongReadAddr, pongReadEn)

  io.outPing := pingReadData
  io.outPong := pongReadData

  // SyncReadMem 读延迟一拍，rd* 寄存器把乘积和读出的旧 partial sum 对齐到 fadd。
  rdValid := fire
  rdGroup := io.in.bits.group
  rdPong := io.in.bits.pong
  rdValue := io.in.bits.value

  fadd.io.din0 := Mux(rdValid, Mux(rdPong, pongReadData, pingReadData), 0.U)
  fadd.io.din1 := Mux(rdValid, rdValue, 0.U)

  // 跟踪 fadd 输出归属，最终写回 pingMem/pongMem。
  for (i <- (faddLatency - 1) to 1 by -1) {
    addValid(i) := addValid(i - 1)
    addGroup(i) := addGroup(i - 1)
    addPong(i) := addPong(i - 1)
  }
  addValid(0) := rdValid
  addGroup(0) := rdGroup
  addPong(0) := rdPong

  val addOutValid = addValid(faddLatency - 1)
  val addOutGroup = addGroup(faddLatency - 1)
  val addOutPong = addPong(faddLatency - 1)

  // initValid 优先用于整轮清零；正常路径在 fadd 结束后写回新的 partial sum。
  val pingWriteEn = io.initValid || (addOutValid && !addOutPong)
  val pingWriteAddr = Mux(io.initValid, io.initAddr, addOutGroup)
  val pingWriteData = Mux(io.initValid, 0.U, fadd.io.dout)
  when(pingWriteEn) {
    pingMem.write(pingWriteAddr, pingWriteData)
  }

  val pongWriteEn = io.initValid || (addOutValid && addOutPong)
  val pongWriteAddr = Mux(io.initValid, io.initAddr, addOutGroup)
  val pongWriteData = Mux(io.initValid, 0.U, fadd.io.dout)
  when(pongWriteEn) {
    pongMem.write(pongWriteAddr, pongWriteData)
  }
}
