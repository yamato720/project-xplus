package cuper.spmv

import chisel3._

// Core 和 Accumulator 之间的中间乘积包。
// 后续要加 scoreboard/乱序调度时，插入点就是这条 product stream。
class StripProduct(groupBits: Int) extends Bundle {
  val group = UInt(groupBits.W)
  val pong = Bool()
  val value = UInt(32.W)
}
