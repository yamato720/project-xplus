package cuper.spmv

import chisel3._

final case class MatrixStreamInPorts(
  dout: UInt,
  emptyN: Bool,
  read: Bool,
  peekDout: UInt,
  peekEmptyN: Bool,
  peekRead: Bool
)

final case class TaggedStreamOutPorts(
  din: UInt,
  fullN: Bool,
  write: Bool,
  peek: UInt
)

// TAPA stream 端口按 HLS 命名展开。方法必须在 RawModule 上调用，才能生成顶层 IO。
trait CuperSpmvStreamPorts { this: RawModule =>
  // Matrix 输入流宽度为 513 bit，目前核心逻辑只消费低 512 bit。
  protected def matrixIn(prefix: String): MatrixStreamInPorts =
    MatrixStreamInPorts(
      IO(Input(UInt(513.W))).suggestName(s"${prefix}_s_dout"),
      IO(Input(Bool())).suggestName(s"${prefix}_s_empty_n"),
      IO(Output(Bool())).suggestName(s"${prefix}_s_read"),
      IO(Input(UInt(513.W))).suggestName(s"${prefix}_peek_dout"),
      IO(Input(Bool())).suggestName(s"${prefix}_peek_empty_n"),
      IO(Output(Bool())).suggestName(s"${prefix}_peek_read")
    )

  // 输出仍使用现有 writer 期待的 tagged float stream。
  protected def taggedOut(prefix: String): TaggedStreamOutPorts =
    TaggedStreamOutPorts(
      IO(Output(UInt(129.W))).suggestName(s"${prefix}_s_din"),
      IO(Input(Bool())).suggestName(s"${prefix}_s_full_n"),
      IO(Output(Bool())).suggestName(s"${prefix}_s_write"),
      IO(Input(UInt(129.W))).suggestName(s"${prefix}_peek")
    )
}
