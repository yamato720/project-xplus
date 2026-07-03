package cuper.spmv

import chisel3._

// Minimal AXI4 master port shape used by Vitis RTL kernels.  The signal names
// intentionally match the HLS/TAPA-generated m_axi_* ports in existing xclbins.
class AxiMasterPort(dataBits: Int) extends Bundle {
  val AWADDR = Output(UInt(64.W))
  val AWBURST = Output(UInt(2.W))
  val AWCACHE = Output(UInt(4.W))
  val AWID = Output(UInt(1.W))
  val AWLEN = Output(UInt(8.W))
  val AWLOCK = Output(Bool())
  val AWPROT = Output(UInt(3.W))
  val AWQOS = Output(UInt(4.W))
  val AWREADY = Input(Bool())
  val AWSIZE = Output(UInt(3.W))
  val AWVALID = Output(Bool())

  val BID = Input(UInt(1.W))
  val BREADY = Output(Bool())
  val BRESP = Input(UInt(2.W))
  val BVALID = Input(Bool())

  val ARADDR = Output(UInt(64.W))
  val ARBURST = Output(UInt(2.W))
  val ARCACHE = Output(UInt(4.W))
  val ARID = Output(UInt(1.W))
  val ARLEN = Output(UInt(8.W))
  val ARLOCK = Output(Bool())
  val ARPROT = Output(UInt(3.W))
  val ARQOS = Output(UInt(4.W))
  val ARREADY = Input(Bool())
  val ARSIZE = Output(UInt(3.W))
  val ARVALID = Output(Bool())

  val RDATA = Input(UInt(dataBits.W))
  val RID = Input(UInt(1.W))
  val RLAST = Input(Bool())
  val RREADY = Output(Bool())
  val RRESP = Input(UInt(2.W))
  val RVALID = Input(Bool())

  val WDATA = Output(UInt(dataBits.W))
  val WLAST = Output(Bool())
  val WREADY = Input(Bool())
  val WSTRB = Output(UInt((dataBits / 8).W))
  val WVALID = Output(Bool())
}

class AxiLiteSlavePort(addrBits: Int = 12) extends Bundle {
  val AWVALID = Input(Bool())
  val AWREADY = Output(Bool())
  val AWADDR = Input(UInt(addrBits.W))

  val WVALID = Input(Bool())
  val WREADY = Output(Bool())
  val WDATA = Input(UInt(32.W))
  val WSTRB = Input(UInt(4.W))

  val ARVALID = Input(Bool())
  val ARREADY = Output(Bool())
  val ARADDR = Input(UInt(addrBits.W))

  val RVALID = Output(Bool())
  val RREADY = Input(Bool())
  val RDATA = Output(UInt(32.W))
  val RRESP = Output(UInt(2.W))

  val BVALID = Output(Bool())
  val BREADY = Input(Bool())
  val BRESP = Output(UInt(2.W))
}
