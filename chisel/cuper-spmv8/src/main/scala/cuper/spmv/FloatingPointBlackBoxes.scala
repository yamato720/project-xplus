package cuper.spmv

import chisel3._

// 复用 TAPA/HLS 生成的单精度浮点乘法 IP，保持 latency、模块名和现有 Verilog/Tcl
// artifact 一致，方便由 TAPA 打包流程继续接入同一套 Vivado IP。
class HlsFmul32 extends BlackBox(Map(
  "ID" -> 1,
  "NUM_STAGE" -> 8,
  "din0_WIDTH" -> 32,
  "din1_WIDTH" -> 32,
  "dout_WIDTH" -> 32
)) {
  val io = IO(new Bundle {
    val clk = Input(Clock())
    val reset = Input(Bool())
    val ce = Input(Bool())
    val din0 = Input(UInt(32.W))
    val din1 = Input(UInt(32.W))
    val dout = Output(UInt(32.W))
  })

  override def desiredName: String =
    "CuperSpmvOnly_CoreStrip_fmul_32ns_32ns_32_8_max_dsp_1"
}

// 复用原 RTL owner-bank accumulator 使用的单精度浮点加法 IP。这里的 Chisel 逻辑只负责
// 调度读写和相关性检查，真正的 IEEE-754 加法仍交给 HLS 浮点核。
class HlsFadd32 extends BlackBox(Map(
  "ID" -> 1,
  "NUM_STAGE" -> 12,
  "din0_WIDTH" -> 32,
  "din1_WIDTH" -> 32,
  "dout_WIDTH" -> 32
)) {
  val io = IO(new Bundle {
    val clk = Input(Clock())
    val reset = Input(Bool())
    val ce = Input(Bool())
    val din0 = Input(UInt(32.W))
    val din1 = Input(UInt(32.W))
    val dout = Output(UInt(32.W))
  })

  override def desiredName: String =
    "CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1"
}
