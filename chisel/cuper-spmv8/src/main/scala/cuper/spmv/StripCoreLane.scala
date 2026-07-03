package cuper.spmv

import chisel3._
import chisel3.util._

// 单个 source-HBM/owner slot 的 Core lane：只负责 value * X[col]。
//
// 这一级对应原 TAPA/HLS 路径中的 CoreStrip 乘法部分。它不读写 partial sum，
// 输出 StripProduct 给后级 accumulator。中间用 Decoupled 边界，方便后续插入
// scoreboard 做乱序调度。
class StripCoreLane(groupBits: Int) extends Module {
  private val fmulLatency = 8

  val io = IO(new Bundle {
    val inValid = Input(Bool())
    val inReady = Output(Bool())
    val inGroup = Input(UInt(groupBits.W))
    val inPong = Input(Bool())
    val inValue = Input(UInt(32.W))
    val inX = Input(UInt(32.W))

    val out = Decoupled(new StripProduct(groupBits))

    val busy = Output(Bool())
    val rawStall = Output(Bool())
    val accept = Output(Bool())
  })

  val mulValid = RegInit(VecInit(Seq.fill(fmulLatency)(false.B)))
  val mulGroup = Reg(Vec(fmulLatency, UInt(groupBits.W)))
  val mulPong = Reg(Vec(fmulLatency, Bool()))

  val fmul = Module(new HlsFmul32)
  fmul.io.clk := clock
  fmul.io.reset := reset.asBool

  val outValid = mulValid(fmulLatency - 1)
  val canAdvance = !outValid || io.out.ready

  // fmul blackbox 有 ce 端口；当后级不接收当前输出时，整条乘法流水保持不动。
  fmul.io.ce := canAdvance

  // 保守检查同一个 group/ping-pong 是否仍在乘法流水里，避免同地址乘积过早进入后级。
  val mulHazard = (0 until fmulLatency).map { i =>
    mulValid(i) && mulGroup(i) === io.inGroup && mulPong(i) === io.inPong
  }.reduce(_ || _)
  val fire = io.inValid && canAdvance && !mulHazard

  io.inReady := canAdvance && !mulHazard
  io.rawStall := io.inValid && !io.inReady
  io.accept := fire
  io.busy := mulValid.asUInt.orR

  fmul.io.din0 := Mux(fire, io.inValue, 0.U)
  fmul.io.din1 := Mux(fire, io.inX, 0.U)

  when(canAdvance) {
    for (i <- (fmulLatency - 1) to 1 by -1) {
      mulValid(i) := mulValid(i - 1)
      mulGroup(i) := mulGroup(i - 1)
      mulPong(i) := mulPong(i - 1)
    }
    mulValid(0) := fire
    mulGroup(0) := io.inGroup
    mulPong(0) := io.inPong
  }

  io.out.valid := outValid
  io.out.bits.group := mulGroup(fmulLatency - 1)
  io.out.bits.pong := mulPong(fmulLatency - 1)
  io.out.bits.value := fmul.io.dout
}
