package cuper.spmv

import chisel3._
import chisel3.util._

// Standalone fixed 8-HBM Vitis RTL kernel entry point.
//
// This top keeps the public ABI introduced by the entry/drain probes, but now
// drives the real Chisel SpMV datapath:
//   ptr HBM -> internal PE param stream
//   X HBM -> internal float_v16 stream
//   Matrix_data_0..7 -> internal matrix streams
//   tagged datapath output -> scalar Y_out writer
// Status/Metrics remain mmap writeback buffers for host-side bring-up checks.
class CuperSpmvChisel8 extends RawModule {
  override def desiredName: String = "CuperSpmvChisel8"

  private val hbmChannels = 8
  private val pointerArgs = 13
  private val statusWords = 64
  private val metricWords = 64
  private val magic32 = "h43535056".U(32.W) // "CSPV"
  private val spmvMagic32 = "h53504d56".U(32.W) // "SPMV"
  private val magic64 = "h4353504d56384348".U(64.W) // "CSPMV8CH"

  val ap_clk = IO(Input(Clock())).suggestName("ap_clk")
  val ap_rst_n = IO(Input(Bool())).suggestName("ap_rst_n")
  val interrupt = IO(Output(Bool())).suggestName("interrupt")

  val s_axi_control = IO(new AxiLiteSlavePort()).suggestName("s_axi_control")

  val m_axi_SpElement_list_ptr =
    IO(new AxiMasterPort(32)).suggestName("m_axi_SpElement_list_ptr")
  val m_axi_Matrix_data_0 =
    IO(new AxiMasterPort(512)).suggestName("m_axi_Matrix_data_0")
  val m_axi_Matrix_data_1 =
    IO(new AxiMasterPort(512)).suggestName("m_axi_Matrix_data_1")
  val m_axi_Matrix_data_2 =
    IO(new AxiMasterPort(512)).suggestName("m_axi_Matrix_data_2")
  val m_axi_Matrix_data_3 =
    IO(new AxiMasterPort(512)).suggestName("m_axi_Matrix_data_3")
  val m_axi_Matrix_data_4 =
    IO(new AxiMasterPort(512)).suggestName("m_axi_Matrix_data_4")
  val m_axi_Matrix_data_5 =
    IO(new AxiMasterPort(512)).suggestName("m_axi_Matrix_data_5")
  val m_axi_Matrix_data_6 =
    IO(new AxiMasterPort(512)).suggestName("m_axi_Matrix_data_6")
  val m_axi_Matrix_data_7 =
    IO(new AxiMasterPort(512)).suggestName("m_axi_Matrix_data_7")
  val m_axi_X = IO(new AxiMasterPort(512)).suggestName("m_axi_X")
  val m_axi_Y_out = IO(new AxiMasterPort(32)).suggestName("m_axi_Y_out")
  val m_axi_Status = IO(new AxiMasterPort(32)).suggestName("m_axi_Status")
  val m_axi_Metrics = IO(new AxiMasterPort(64)).suggestName("m_axi_Metrics")

  private def driveAxiIdle(port: AxiMasterPort, sizeLog2: Int): Unit = {
    port.AWADDR := 0.U
    port.AWBURST := 1.U
    port.AWCACHE := 3.U
    port.AWID := 0.U
    port.AWLEN := 0.U
    port.AWLOCK := false.B
    port.AWPROT := 0.U
    port.AWQOS := 0.U
    port.AWSIZE := sizeLog2.U
    port.AWVALID := false.B
    port.BREADY := false.B
    port.ARADDR := 0.U
    port.ARBURST := 1.U
    port.ARCACHE := 3.U
    port.ARID := 0.U
    port.ARLEN := 0.U
    port.ARLOCK := false.B
    port.ARPROT := 0.U
    port.ARQOS := 0.U
    port.ARSIZE := sizeLog2.U
    port.ARVALID := false.B
    port.RREADY := false.B
    port.WDATA := 0.U
    port.WLAST := true.B
    port.WSTRB := ((BigInt(1) << port.WSTRB.getWidth) - 1).U
    port.WVALID := false.B
  }

  private val resetBool = !ap_rst_n

  withClockAndReset(ap_clk, resetBool) {
    val matrix = Seq(
      m_axi_Matrix_data_0,
      m_axi_Matrix_data_1,
      m_axi_Matrix_data_2,
      m_axi_Matrix_data_3,
      m_axi_Matrix_data_4,
      m_axi_Matrix_data_5,
      m_axi_Matrix_data_6,
      m_axi_Matrix_data_7)
    val allMasters = Seq(
      m_axi_SpElement_list_ptr) ++ matrix ++ Seq(
      m_axi_X,
      m_axi_Y_out,
      m_axi_Status,
      m_axi_Metrics)

    driveAxiIdle(m_axi_SpElement_list_ptr, 2)
    matrix.foreach(driveAxiIdle(_, 6))
    driveAxiIdle(m_axi_X, 6)
    driveAxiIdle(m_axi_Y_out, 2)
    driveAxiIdle(m_axi_Status, 2)
    driveAxiIdle(m_axi_Metrics, 3)

    val argPtr = RegInit(VecInit(Seq.fill(pointerArgs)(0.U(64.W))))
    val batchNum = RegInit(0.U(32.W))
    val matrixLen = RegInit(0.U(32.W))
    val rowNum = RegInit(0.U(32.W))
    val columnNum = RegInit(0.U(32.W))
    val iterationNum = RegInit(0.U(32.W))
    val startPending = RegInit(false.B)
    val autoRestart = RegInit(false.B)
    val doneSticky = RegInit(false.B)

    val batchNumWire = WireDefault(batchNum)
    val matrixLenWire = WireDefault(matrixLen)
    val rowNumWire = WireDefault(rowNum)
    val columnNumWire = WireDefault(columnNum)
    val iterationNumWire = WireDefault(iterationNum)

    val datapath = Module(new CuperSpmvOnlyChiselDataPath8)
    val datapathStart = RegInit(false.B)
    datapathStart := false.B

    val peParamQ = Module(new Queue(UInt(33.W), 64, pipe = true, flow = false))
    val xQ = Module(new Queue(UInt(513.W), 64, pipe = true, flow = false))
    val matrixQ = Seq.fill(hbmChannels)(
      Module(new Queue(UInt(513.W), 64, pipe = true, flow = false)))
    val taggedQ = Seq.fill(hbmChannels)(
      Module(new Queue(UInt(129.W), 64, pipe = true, flow = false)))

    peParamQ.io.enq.valid := false.B
    peParamQ.io.enq.bits := 0.U
    xQ.io.enq.valid := false.B
    xQ.io.enq.bits := 0.U
    for (i <- 0 until hbmChannels) {
      matrixQ(i).io.enq.valid := false.B
      matrixQ(i).io.enq.bits := 0.U
      taggedQ(i).io.deq.ready := false.B
    }

    datapath.ap_clk := ap_clk
    datapath.ap_rst_n := ap_rst_n
    datapath.ap_start := datapathStart
    datapath.Iteration_num := 1.U
    datapath.Row_num := rowNumWire
    datapath.Batch_num := batchNumWire
    datapath.Matrix_len := matrixLenWire
    datapath.Column_num := columnNumWire

    datapath.PE_Param_in_s_dout := peParamQ.io.deq.bits
    datapath.PE_Param_in_s_empty_n := peParamQ.io.deq.valid
    peParamQ.io.deq.ready := datapath.PE_Param_in_s_read
    datapath.PE_Param_in_peek_dout := peParamQ.io.deq.bits
    datapath.PE_Param_in_peek_empty_n := peParamQ.io.deq.valid

    datapath.Vector_X_Stream_in_s_dout := xQ.io.deq.bits
    datapath.Vector_X_Stream_in_s_empty_n := xQ.io.deq.valid
    xQ.io.deq.ready := datapath.Vector_X_Stream_in_s_read
    datapath.Vector_X_Stream_in_peek_dout := xQ.io.deq.bits
    datapath.Vector_X_Stream_in_peek_empty_n := xQ.io.deq.valid

    val matrixPorts = Seq(datapath.m0, datapath.m1, datapath.m2, datapath.m3,
      datapath.m4, datapath.m5, datapath.m6, datapath.m7)
    for (i <- 0 until hbmChannels) {
      matrixPorts(i).dout := matrixQ(i).io.deq.bits
      matrixPorts(i).emptyN := matrixQ(i).io.deq.valid
      matrixQ(i).io.deq.ready := matrixPorts(i).read
      matrixPorts(i).peekDout := matrixQ(i).io.deq.bits
      matrixPorts(i).peekEmptyN := matrixQ(i).io.deq.valid
    }

    val taggedPorts = Seq(datapath.y0, datapath.y1, datapath.y2, datapath.y3,
      datapath.y4, datapath.y5, datapath.y6, datapath.y7)
    for (i <- 0 until hbmChannels) {
      taggedQ(i).io.enq.valid := taggedPorts(i).write
      taggedQ(i).io.enq.bits := taggedPorts(i).din
      taggedPorts(i).fullN := taggedQ(i).io.enq.ready
      taggedPorts(i).peek := taggedQ(i).io.deq.bits
    }

    // AXI-Lite write channel.  AW and W are accepted independently; the register
    // write is committed only after both halves are captured.
    val awValid = RegInit(false.B)
    val awAddr = Reg(UInt(12.W))
    val wValid = RegInit(false.B)
    val wData = Reg(UInt(32.W))
    val wStrb = Reg(UInt(4.W))
    val bValid = RegInit(false.B)

    s_axi_control.AWREADY := !awValid
    s_axi_control.WREADY := !wValid
    s_axi_control.BVALID := bValid
    s_axi_control.BRESP := 0.U

    when(!awValid && s_axi_control.AWVALID) {
      awValid := true.B
      awAddr := s_axi_control.AWADDR
    }
    when(!wValid && s_axi_control.WVALID) {
      wValid := true.B
      wData := s_axi_control.WDATA
      wStrb := s_axi_control.WSTRB
    }

    val commitWrite = awValid && wValid && !bValid
    when(commitWrite) {
      val addr = awAddr
      switch(addr) {
        is("h000".U) {
          when(wData(0)) {
            startPending := true.B
            doneSticky := false.B
          }
          autoRestart := wData(7)
        }
        is("h010".U) { argPtr(0) := Cat(argPtr(0)(63, 32), wData) }
        is("h014".U) { argPtr(0) := Cat(wData, argPtr(0)(31, 0)) }
        is("h01c".U) { argPtr(1) := Cat(argPtr(1)(63, 32), wData) }
        is("h020".U) { argPtr(1) := Cat(wData, argPtr(1)(31, 0)) }
        is("h028".U) { argPtr(2) := Cat(argPtr(2)(63, 32), wData) }
        is("h02c".U) { argPtr(2) := Cat(wData, argPtr(2)(31, 0)) }
        is("h034".U) { argPtr(3) := Cat(argPtr(3)(63, 32), wData) }
        is("h038".U) { argPtr(3) := Cat(wData, argPtr(3)(31, 0)) }
        is("h040".U) { argPtr(4) := Cat(argPtr(4)(63, 32), wData) }
        is("h044".U) { argPtr(4) := Cat(wData, argPtr(4)(31, 0)) }
        is("h04c".U) { argPtr(5) := Cat(argPtr(5)(63, 32), wData) }
        is("h050".U) { argPtr(5) := Cat(wData, argPtr(5)(31, 0)) }
        is("h058".U) { argPtr(6) := Cat(argPtr(6)(63, 32), wData) }
        is("h05c".U) { argPtr(6) := Cat(wData, argPtr(6)(31, 0)) }
        is("h064".U) { argPtr(7) := Cat(argPtr(7)(63, 32), wData) }
        is("h068".U) { argPtr(7) := Cat(wData, argPtr(7)(31, 0)) }
        is("h070".U) { argPtr(8) := Cat(argPtr(8)(63, 32), wData) }
        is("h074".U) { argPtr(8) := Cat(wData, argPtr(8)(31, 0)) }
        is("h07c".U) { argPtr(9) := Cat(argPtr(9)(63, 32), wData) }
        is("h080".U) { argPtr(9) := Cat(wData, argPtr(9)(31, 0)) }
        is("h088".U) { argPtr(10) := Cat(argPtr(10)(63, 32), wData) }
        is("h08c".U) { argPtr(10) := Cat(wData, argPtr(10)(31, 0)) }
        is("h094".U) { argPtr(11) := Cat(argPtr(11)(63, 32), wData) }
        is("h098".U) { argPtr(11) := Cat(wData, argPtr(11)(31, 0)) }
        is("h0a0".U) { argPtr(12) := Cat(argPtr(12)(63, 32), wData) }
        is("h0a4".U) { argPtr(12) := Cat(wData, argPtr(12)(31, 0)) }
        is("h0ac".U) { batchNum := wData }
        is("h0b4".U) { matrixLen := wData }
        is("h0bc".U) { rowNum := wData }
        is("h0c4".U) { columnNum := wData }
        is("h0cc".U) { iterationNum := wData }
      }
      awValid := false.B
      wValid := false.B
      bValid := true.B
    }
    when(bValid && s_axi_control.BREADY) {
      bValid := false.B
    }

    val states = Enum(10)
    val idle = states(0)
    val readLenAddr = states(1)
    val readLenData = states(2)
    val runDataPath = states(3)
    val writeStatusAddr = states(4)
    val writeStatusData = states(5)
    val writeStatusResp = states(6)
    val writeMetricsAddr = states(7)
    val writeMetricsData = states(8)
    val writeMetricsResp = states(9)

    val state = RegInit(idle)
    val lenIndex = RegInit(0.U(3.W))
    val statusIndex = RegInit(0.U(6.W))
    val metricIndex = RegInit(0.U(6.W))

    val ptrWordsRead = RegInit(0.U(32.W))
    val xPacketsRead = RegInit(0.U(32.W))
    val matrixLenPerChannel = RegInit(VecInit(Seq.fill(hbmChannels)(0.U(32.W))))
    val matrixBeatsRead = RegInit(VecInit(Seq.fill(hbmChannels)(0.U(32.W))))
    val matrixDoneMask = RegInit(0.U(32.W))
    val rErrorMask = RegInit(0.U(32.W))
    val bErrorMask = RegInit(0.U(32.W))
    val firstPtr = RegInit(0.U(32.W))
    val lastPtr = RegInit(0.U(32.W))
    val firstXLow64 = RegInit(0.U(64.W))
    val lastXLow64 = RegInit(0.U(64.W))
    val matrixFirstLow64 = RegInit(VecInit(Seq.fill(hbmChannels)(0.U(64.W))))
    val matrixLastLow64 = RegInit(VecInit(Seq.fill(hbmChannels)(0.U(64.W))))
    val cycleCounter = RegInit(0.U(64.W))
    val lastBresp = RegInit(0.U(2.W))
    val lastRresp = RegInit(0.U(2.W))

    val ptrWordsExpected = Wire(UInt(32.W))
    val boundaryWordsExpected = Wire(UInt(32.W))
    val xPacketsExpected = Wire(UInt(32.W))
    val yPacketsExpected = Wire(UInt(32.W))
    val taggedPairsExpected = Wire(UInt(32.W))
    val scalarWritesExpected = Wire(UInt(32.W))
    ptrWordsExpected := (batchNum +& 2.U(32.W)) << 3
    boundaryWordsExpected := (batchNum +& 1.U(32.W)) << 3
    xPacketsExpected := (columnNum +& 15.U(32.W)) >> 4
    yPacketsExpected := (rowNum +& 15.U(32.W)) >> 4
    taggedPairsExpected := yPacketsExpected << 3
    scalarWritesExpected := taggedPairsExpected << 1

    val ptrStates = Enum(4)
    val ptrIdle = ptrStates(0)
    val ptrHeader = ptrStates(1)
    val ptrBoundaryAddr = ptrStates(2)
    val ptrBoundaryData = ptrStates(3)
    val ptrState = RegInit(ptrIdle)
    val ptrHeaderIndex = RegInit(0.U(2.W))
    val ptrBoundaryIndex = RegInit(0.U(32.W))

    val xStates = Enum(4)
    val xIdle = xStates(0)
    val xAddr = xStates(1)
    val xData = xStates(2)
    val xDone = xStates(3)
    val xState = RegInit(xIdle)
    val xPacketIndex = RegInit(0.U(32.W))

    val matrixStates = Enum(4)
    val matrixIdle = matrixStates(0)
    val matrixAddr = matrixStates(1)
    val matrixData = matrixStates(2)
    val matrixDone = matrixStates(3)
    val matrixState = RegInit(VecInit(Seq.fill(hbmChannels)(matrixIdle)))
    val matrixBeatIndex = RegInit(VecInit(Seq.fill(hbmChannels)(0.U(32.W))))
    val matrixDoneNow = Wire(Vec(hbmChannels, Bool()))
    matrixDoneNow := VecInit(Seq.fill(hbmChannels)(false.B))

    val writerStates = Enum(4)
    val writerSelect = writerStates(0)
    val writerAddr = writerStates(1)
    val writerData = writerStates(2)
    val writerResp = writerStates(3)
    val writerState = RegInit(writerSelect)
    val writerCursor = RegInit(0.U(3.W))
    val taggedPairsRead = RegInit(0.U(32.W))
    val yWritesIssued = RegInit(0.U(32.W))
    val yWriteResponses = RegInit(0.U(32.W))
    val pendingYAddr = RegInit(0.U(32.W))
    val pendingYData = RegInit(0.U(32.W))
    val nextYValid = RegInit(false.B)
    val nextYAddr = RegInit(0.U(32.W))
    val nextYData = RegInit(0.U(32.W))
    val datapathDoneSeen = RegInit(false.B)

    val ptrLoaderDone = ptrState === ptrIdle && state =/= idle
    val xLoaderDone = xState === xDone || xPacketsExpected === 0.U
    val allMatrixLoadersDone = matrixState.map(_ === matrixDone).reduce(_ && _)
    val writerDone =
      taggedPairsRead >= taggedPairsExpected &&
        yWriteResponses >= scalarWritesExpected &&
        writerState === writerSelect &&
        !nextYValid

    val running = state =/= idle
    when(running) {
      cycleCounter := cycleCounter + 1.U
    }
    when(datapath.ap_done) {
      datapathDoneSeen := true.B
    }

    // AXI-Lite read channel.
    val rValid = RegInit(false.B)
    val rData = RegInit(0.U(32.W))
    val rIsCtrl = RegInit(false.B)
    s_axi_control.ARREADY := !rValid
    s_axi_control.RVALID := rValid
    s_axi_control.RDATA := rData
    s_axi_control.RRESP := 0.U

    def ctrlWord: UInt =
      Cat(0.U(24.W), autoRestart, 0.U(3.W), doneSticky, state === idle, doneSticky, startPending)

    def readArgLowHigh(index: Int, high: Boolean): UInt =
      if (high) argPtr(index)(63, 32) else argPtr(index)(31, 0)

    when(!rValid && s_axi_control.ARVALID) {
      val addr = s_axi_control.ARADDR
      rIsCtrl := addr === 0.U
      rData := 0.U
      switch(addr) {
        is("h000".U) { rData := ctrlWord }
        is("h010".U) { rData := readArgLowHigh(0, high = false) }
        is("h014".U) { rData := readArgLowHigh(0, high = true) }
        is("h01c".U) { rData := readArgLowHigh(1, high = false) }
        is("h020".U) { rData := readArgLowHigh(1, high = true) }
        is("h028".U) { rData := readArgLowHigh(2, high = false) }
        is("h02c".U) { rData := readArgLowHigh(2, high = true) }
        is("h034".U) { rData := readArgLowHigh(3, high = false) }
        is("h038".U) { rData := readArgLowHigh(3, high = true) }
        is("h040".U) { rData := readArgLowHigh(4, high = false) }
        is("h044".U) { rData := readArgLowHigh(4, high = true) }
        is("h04c".U) { rData := readArgLowHigh(5, high = false) }
        is("h050".U) { rData := readArgLowHigh(5, high = true) }
        is("h058".U) { rData := readArgLowHigh(6, high = false) }
        is("h05c".U) { rData := readArgLowHigh(6, high = true) }
        is("h064".U) { rData := readArgLowHigh(7, high = false) }
        is("h068".U) { rData := readArgLowHigh(7, high = true) }
        is("h070".U) { rData := readArgLowHigh(8, high = false) }
        is("h074".U) { rData := readArgLowHigh(8, high = true) }
        is("h07c".U) { rData := readArgLowHigh(9, high = false) }
        is("h080".U) { rData := readArgLowHigh(9, high = true) }
        is("h088".U) { rData := readArgLowHigh(10, high = false) }
        is("h08c".U) { rData := readArgLowHigh(10, high = true) }
        is("h094".U) { rData := readArgLowHigh(11, high = false) }
        is("h098".U) { rData := readArgLowHigh(11, high = true) }
        is("h0a0".U) { rData := readArgLowHigh(12, high = false) }
        is("h0a4".U) { rData := readArgLowHigh(12, high = true) }
        is("h0ac".U) { rData := batchNum }
        is("h0b4".U) { rData := matrixLen }
        is("h0bc".U) { rData := rowNum }
        is("h0c4".U) { rData := columnNum }
        is("h0cc".U) { rData := iterationNum }
      }
      rValid := true.B
    }
    when(rValid && s_axi_control.RREADY) {
      when(rIsCtrl) {
        doneSticky := false.B
      }
      rValid := false.B
    }

    val statusValue = Wire(UInt(32.W))
    statusValue := 0.U
    switch(statusIndex) {
      is(0.U) { statusValue := 1.U }
      is(1.U) { statusValue := magic32 }
      is(2.U) { statusValue := rowNum }
      is(3.U) { statusValue := columnNum }
      is(4.U) { statusValue := batchNum }
      is(5.U) { statusValue := matrixLen }
      is(6.U) { statusValue := iterationNum }
      is(7.U) { statusValue := ptrWordsExpected }
      is(8.U) { statusValue := ptrWordsRead }
      is(9.U) { statusValue := xPacketsExpected }
      is(10.U) { statusValue := xPacketsRead }
      is(11.U) { statusValue := matrixDoneMask }
      is(12.U) { statusValue := rErrorMask }
      is(13.U) { statusValue := bErrorMask }
      is(14.U) { statusValue := Cat(0.U(30.W), lastRresp) }
      is(15.U) { statusValue := Cat(0.U(30.W), lastBresp) }
      is(16.U) { statusValue := matrixLenPerChannel(0) }
      is(17.U) { statusValue := matrixLenPerChannel(1) }
      is(18.U) { statusValue := matrixLenPerChannel(2) }
      is(19.U) { statusValue := matrixLenPerChannel(3) }
      is(20.U) { statusValue := matrixLenPerChannel(4) }
      is(21.U) { statusValue := matrixLenPerChannel(5) }
      is(22.U) { statusValue := matrixLenPerChannel(6) }
      is(23.U) { statusValue := matrixLenPerChannel(7) }
      is(24.U) { statusValue := Cat(0.U(28.W), state.asUInt) }
      is(25.U) { statusValue := Cat(0.U(27.W), writerCursor, writerState.asUInt) }
      is(26.U) { statusValue := matrixBeatIndex(0) }
      is(27.U) { statusValue := ptrBoundaryIndex }
      is(28.U) { statusValue := xPacketIndex }
      is(29.U) { statusValue := firstPtr }
      is(30.U) { statusValue := lastPtr }
      is(31.U) { statusValue := spmvMagic32 }
      is(32.U) { statusValue := taggedPairsExpected }
      is(33.U) { statusValue := taggedPairsRead }
      is(34.U) { statusValue := scalarWritesExpected }
      is(35.U) { statusValue := yWriteResponses }
      is(36.U) { statusValue := Cat(0.U(31.W), datapathDoneSeen) }
      is(37.U) { statusValue := Cat(0.U(31.W), writerDone) }
      is(38.U) { statusValue := Cat(0.U(29.W), ptrLoaderDone, xLoaderDone, allMatrixLoadersDone) }
      is(39.U) { statusValue := yWritesIssued }
    }

    val metricValue = Wire(UInt(64.W))
    metricValue := 0.U
    switch(metricIndex) {
      is(0.U) { metricValue := magic64 }
      is(1.U) { metricValue := cycleCounter }
      is(2.U) { metricValue := Cat(ptrWordsExpected, ptrWordsRead) }
      is(3.U) { metricValue := Cat(xPacketsExpected, xPacketsRead) }
      is(4.U) { metricValue := Cat(bErrorMask, rErrorMask) }
      is(5.U) { metricValue := Cat(0.U(32.W), matrixDoneMask) }
      is(6.U) { metricValue := firstXLow64 }
      is(7.U) { metricValue := lastXLow64 }
      is(8.U) { metricValue := Cat(0.U(32.W), matrixBeatsRead(0)) }
      is(9.U) { metricValue := Cat(0.U(32.W), matrixBeatsRead(1)) }
      is(10.U) { metricValue := Cat(0.U(32.W), matrixBeatsRead(2)) }
      is(11.U) { metricValue := Cat(0.U(32.W), matrixBeatsRead(3)) }
      is(12.U) { metricValue := Cat(0.U(32.W), matrixBeatsRead(4)) }
      is(13.U) { metricValue := Cat(0.U(32.W), matrixBeatsRead(5)) }
      is(14.U) { metricValue := Cat(0.U(32.W), matrixBeatsRead(6)) }
      is(15.U) { metricValue := Cat(0.U(32.W), matrixBeatsRead(7)) }
      is(16.U) { metricValue := matrixFirstLow64(0) }
      is(17.U) { metricValue := matrixFirstLow64(1) }
      is(18.U) { metricValue := matrixFirstLow64(2) }
      is(19.U) { metricValue := matrixFirstLow64(3) }
      is(20.U) { metricValue := matrixFirstLow64(4) }
      is(21.U) { metricValue := matrixFirstLow64(5) }
      is(22.U) { metricValue := matrixFirstLow64(6) }
      is(23.U) { metricValue := matrixFirstLow64(7) }
      is(24.U) { metricValue := matrixLastLow64(0) }
      is(25.U) { metricValue := matrixLastLow64(1) }
      is(26.U) { metricValue := matrixLastLow64(2) }
      is(27.U) { metricValue := matrixLastLow64(3) }
      is(28.U) { metricValue := matrixLastLow64(4) }
      is(29.U) { metricValue := matrixLastLow64(5) }
      is(30.U) { metricValue := matrixLastLow64(6) }
      is(31.U) { metricValue := matrixLastLow64(7) }
      is(32.U) { metricValue := argPtr(0) }
      is(33.U) { metricValue := argPtr(9) }
      is(34.U) { metricValue := argPtr(10) }
      is(35.U) { metricValue := argPtr(11) }
      is(36.U) { metricValue := argPtr(12) }
      is(37.U) { metricValue := Cat(rowNum, columnNum) }
      is(38.U) { metricValue := Cat(batchNum, matrixLen) }
      is(39.U) { metricValue := Cat(0.U(32.W), iterationNum) }
      is(40.U) { metricValue := Cat(ptrBoundaryIndex, xPacketIndex) }
      is(41.U) { metricValue := Cat(0.U(32.W), matrixBeatIndex(0)) }
      is(42.U) { metricValue := Cat(firstPtr, lastPtr) }
      is(43.U) { metricValue := Cat(taggedPairsExpected, taggedPairsRead) }
      is(44.U) { metricValue := Cat(scalarWritesExpected, yWriteResponses) }
      is(45.U) { metricValue := Cat(0.U(61.W), datapathDoneSeen, writerDone, xLoaderDone) }
      is(46.U) { metricValue := Cat(0.U(32.W), yWritesIssued) }
    }

    // Ptr stream loader: first four internal headers, then boundary-major
    // ptr[8 .. 8 + (Batch_num + 1) * 8 - 1].
    switch(ptrState) {
      is(ptrHeader) {
        peParamQ.io.enq.valid := true.B
        peParamQ.io.enq.bits := MuxLookup(ptrHeaderIndex, 0.U(33.W))(Seq(
          0.U -> Cat(0.U(1.W), batchNum),
          1.U -> Cat(0.U(1.W), matrixLen),
          2.U -> Cat(0.U(1.W), rowNum),
          3.U -> Cat(0.U(1.W), columnNum)))
        when(peParamQ.io.enq.ready) {
          when(ptrHeaderIndex === 3.U) {
            ptrBoundaryIndex := 0.U
            ptrState := ptrBoundaryAddr
          }.otherwise {
            ptrHeaderIndex := ptrHeaderIndex + 1.U
          }
        }
      }

      is(ptrBoundaryAddr) {
        when(ptrBoundaryIndex >= boundaryWordsExpected) {
          ptrState := ptrIdle
        }.otherwise {
          m_axi_SpElement_list_ptr.ARADDR := argPtr(0) + ((8.U(32.W) + ptrBoundaryIndex) << 2)
          m_axi_SpElement_list_ptr.ARVALID := true.B
          when(m_axi_SpElement_list_ptr.ARREADY) {
            ptrState := ptrBoundaryData
          }
        }
      }

      is(ptrBoundaryData) {
        m_axi_SpElement_list_ptr.RREADY := peParamQ.io.enq.ready
        peParamQ.io.enq.valid := m_axi_SpElement_list_ptr.RVALID && peParamQ.io.enq.ready
        peParamQ.io.enq.bits := Cat(0.U(1.W), m_axi_SpElement_list_ptr.RDATA)
        when(m_axi_SpElement_list_ptr.RVALID && peParamQ.io.enq.ready) {
          ptrWordsRead := ptrWordsRead + 1.U
          lastPtr := m_axi_SpElement_list_ptr.RDATA
          lastRresp := m_axi_SpElement_list_ptr.RRESP
          when(m_axi_SpElement_list_ptr.RRESP =/= 0.U) {
            rErrorMask := rErrorMask | 1.U
          }
          ptrBoundaryIndex := ptrBoundaryIndex + 1.U
          ptrState := ptrBoundaryAddr
        }
      }
    }

    // X loader.
    switch(xState) {
      is(xAddr) {
        when(xPacketIndex >= xPacketsExpected) {
          xState := xDone
        }.otherwise {
          m_axi_X.ARADDR := argPtr(9) + (xPacketIndex << 6)
          m_axi_X.ARVALID := true.B
          when(m_axi_X.ARREADY) {
            xState := xData
          }
        }
      }

      is(xData) {
        m_axi_X.RREADY := xQ.io.enq.ready
        xQ.io.enq.valid := m_axi_X.RVALID && xQ.io.enq.ready
        xQ.io.enq.bits := Cat(0.U(1.W), m_axi_X.RDATA)
        when(m_axi_X.RVALID && xQ.io.enq.ready) {
          xPacketsRead := xPacketsRead + 1.U
          when(xPacketIndex === 0.U) {
            firstXLow64 := m_axi_X.RDATA(63, 0)
          }
          lastXLow64 := m_axi_X.RDATA(63, 0)
          lastRresp := m_axi_X.RRESP
          when(m_axi_X.RRESP =/= 0.U) {
            rErrorMask := rErrorMask | (BigInt(1) << 9).U(32.W)
          }
          xPacketIndex := xPacketIndex + 1.U
          xState := xAddr
        }
      }
    }

    // Eight independent single-outstanding matrix loaders.
    for (i <- 0 until hbmChannels) {
      switch(matrixState(i)) {
        is(matrixAddr) {
          when(matrixBeatIndex(i) >= matrixLenPerChannel(i)) {
            matrixDoneNow(i) := true.B
            matrixState(i) := matrixDone
          }.otherwise {
            matrix(i).ARADDR := argPtr(1 + i) + (matrixBeatIndex(i) << 6)
            matrix(i).ARVALID := true.B
            when(matrix(i).ARREADY) {
              matrixState(i) := matrixData
            }
          }
        }

        is(matrixData) {
          matrix(i).RREADY := matrixQ(i).io.enq.ready
          matrixQ(i).io.enq.valid := matrix(i).RVALID && matrixQ(i).io.enq.ready
          matrixQ(i).io.enq.bits := Cat(0.U(1.W), matrix(i).RDATA)
          when(matrix(i).RVALID && matrixQ(i).io.enq.ready) {
            matrixBeatsRead(i) := matrixBeatsRead(i) + 1.U
            when(matrixBeatIndex(i) === 0.U) {
              matrixFirstLow64(i) := matrix(i).RDATA(63, 0)
            }
            matrixLastLow64(i) := matrix(i).RDATA(63, 0)
            lastRresp := matrix(i).RRESP
            when(matrix(i).RRESP =/= 0.U) {
              rErrorMask := rErrorMask | (BigInt(1) << (i + 1)).U(32.W)
            }
            matrixBeatIndex(i) := matrixBeatIndex(i) + 1.U
            matrixState(i) := matrixAddr
          }
        }
      }
    }
    when(matrixDoneNow.asUInt.orR) {
      matrixDoneMask := matrixDoneMask | matrixDoneNow.asUInt.pad(32)
    }

    // Tagged output writer.  It drains one tagged token per cycle attempt and
    // emits the two scalar Y writes using one AXI write transaction at a time.
    val selectedTaggedValid = Mux1H((0 until hbmChannels).map { i =>
      (writerCursor === i.U) -> taggedQ(i).io.deq.valid
    })
    val selectedTaggedBits = Mux1H((0 until hbmChannels).map { i =>
      (writerCursor === i.U) -> taggedQ(i).io.deq.bits
    })
    val selectedTaggedRead =
      state === runDataPath &&
        writerState === writerSelect &&
        taggedPairsRead < taggedPairsExpected &&
        selectedTaggedValid
    for (i <- 0 until hbmChannels) {
      taggedQ(i).io.deq.ready := selectedTaggedRead && writerCursor === i.U
    }

    switch(writerState) {
      is(writerSelect) {
        when(state === runDataPath && taggedPairsRead < taggedPairsExpected) {
          when(selectedTaggedValid) {
            val packet = selectedTaggedBits(31, 0)
            val pair = selectedTaggedBits(63, 32)
            val base = (packet << 4) + (pair << 1)
            pendingYAddr := base
            pendingYData := selectedTaggedBits(95, 64)
            nextYAddr := base + 1.U
            nextYData := selectedTaggedBits(127, 96)
            nextYValid := true.B
            taggedPairsRead := taggedPairsRead + 1.U
            writerCursor := writerCursor + 1.U
            writerState := writerAddr
          }.otherwise {
            writerCursor := writerCursor + 1.U
          }
        }
      }

      is(writerAddr) {
        m_axi_Y_out.AWADDR := argPtr(10) + (pendingYAddr << 2)
        m_axi_Y_out.AWVALID := true.B
        when(m_axi_Y_out.AWREADY) {
          writerState := writerData
        }
      }

      is(writerData) {
        m_axi_Y_out.WDATA := pendingYData
        m_axi_Y_out.WSTRB := "hf".U
        m_axi_Y_out.WVALID := true.B
        m_axi_Y_out.WLAST := true.B
        when(m_axi_Y_out.WREADY) {
          yWritesIssued := yWritesIssued + 1.U
          writerState := writerResp
        }
      }

      is(writerResp) {
        m_axi_Y_out.BREADY := true.B
        when(m_axi_Y_out.BVALID) {
          lastBresp := m_axi_Y_out.BRESP
          yWriteResponses := yWriteResponses + 1.U
          when(m_axi_Y_out.BRESP =/= 0.U) {
            bErrorMask := bErrorMask | (BigInt(1) << 10).U(32.W)
          }
          when(nextYValid) {
            pendingYAddr := nextYAddr
            pendingYData := nextYData
            nextYValid := false.B
            writerState := writerAddr
          }.otherwise {
            writerState := writerSelect
          }
        }
      }
    }

    val runComplete =
      ptrState === ptrIdle &&
        xLoaderDone &&
        allMatrixLoadersDone &&
        datapathDoneSeen &&
        writerDone

    switch(state) {
      is(idle) {
        when(startPending) {
          startPending := false.B
          doneSticky := false.B
          cycleCounter := 0.U
          lenIndex := 0.U
          statusIndex := 0.U
          metricIndex := 0.U
          ptrWordsRead := 0.U
          xPacketsRead := 0.U
          xPacketIndex := 0.U
          ptrHeaderIndex := 0.U
          ptrBoundaryIndex := 0.U
          ptrState := ptrIdle
          xState := xIdle
          writerState := writerSelect
          writerCursor := 0.U
          taggedPairsRead := 0.U
          yWritesIssued := 0.U
          yWriteResponses := 0.U
          nextYValid := false.B
          datapathDoneSeen := false.B
          matrixDoneMask := 0.U
          rErrorMask := 0.U
          bErrorMask := 0.U
          firstPtr := 0.U
          lastPtr := 0.U
          firstXLow64 := 0.U
          lastXLow64 := 0.U
          lastBresp := 0.U
          lastRresp := 0.U
          for (i <- 0 until hbmChannels) {
            matrixLenPerChannel(i) := 0.U
            matrixBeatsRead(i) := 0.U
            matrixBeatIndex(i) := 0.U
            matrixState(i) := matrixIdle
            matrixFirstLow64(i) := 0.U
            matrixLastLow64(i) := 0.U
          }
          state := readLenAddr
        }
      }

      is(readLenAddr) {
        m_axi_SpElement_list_ptr.ARADDR := argPtr(0) + (lenIndex << 2)
        m_axi_SpElement_list_ptr.ARVALID := true.B
        when(m_axi_SpElement_list_ptr.ARREADY) {
          state := readLenData
        }
      }

      is(readLenData) {
        m_axi_SpElement_list_ptr.RREADY := true.B
        when(m_axi_SpElement_list_ptr.RVALID) {
          val ptrData = m_axi_SpElement_list_ptr.RDATA
          ptrWordsRead := ptrWordsRead + 1.U
          lastPtr := ptrData
          when(lenIndex === 0.U) {
            firstPtr := ptrData
          }
          for (i <- 0 until hbmChannels) {
            when(lenIndex === i.U) {
              matrixLenPerChannel(i) := ptrData
            }
          }
          lastRresp := m_axi_SpElement_list_ptr.RRESP
          when(m_axi_SpElement_list_ptr.RRESP =/= 0.U) {
            rErrorMask := rErrorMask | 1.U
          }
          when(lenIndex === 7.U) {
            datapathStart := true.B
            ptrState := ptrHeader
            ptrHeaderIndex := 0.U
            ptrBoundaryIndex := 0.U
            xState := xAddr
            xPacketIndex := 0.U
            writerState := writerSelect
            for (i <- 0 until hbmChannels) {
              matrixBeatIndex(i) := 0.U
              matrixState(i) := matrixAddr
            }
            state := runDataPath
          }.otherwise {
            lenIndex := lenIndex + 1.U
            state := readLenAddr
          }
        }
      }

      is(runDataPath) {
        when(runComplete) {
          statusIndex := 0.U
          state := writeStatusAddr
        }
      }

      is(writeStatusAddr) {
        m_axi_Status.AWADDR := argPtr(11) + (statusIndex << 2)
        m_axi_Status.AWVALID := true.B
        when(m_axi_Status.AWREADY) {
          state := writeStatusData
        }
      }

      is(writeStatusData) {
        m_axi_Status.WDATA := statusValue
        m_axi_Status.WSTRB := "hf".U
        m_axi_Status.WVALID := true.B
        m_axi_Status.WLAST := true.B
        when(m_axi_Status.WREADY) {
          state := writeStatusResp
        }
      }

      is(writeStatusResp) {
        m_axi_Status.BREADY := true.B
        when(m_axi_Status.BVALID) {
          lastBresp := m_axi_Status.BRESP
          when(m_axi_Status.BRESP =/= 0.U) {
            bErrorMask := bErrorMask | (BigInt(1) << 11).U(32.W)
          }
          when(statusIndex === (statusWords - 1).U) {
            metricIndex := 0.U
            state := writeMetricsAddr
          }.otherwise {
            statusIndex := statusIndex + 1.U
            state := writeStatusAddr
          }
        }
      }

      is(writeMetricsAddr) {
        m_axi_Metrics.AWADDR := argPtr(12) + (metricIndex << 3)
        m_axi_Metrics.AWVALID := true.B
        when(m_axi_Metrics.AWREADY) {
          state := writeMetricsData
        }
      }

      is(writeMetricsData) {
        m_axi_Metrics.WDATA := metricValue
        m_axi_Metrics.WSTRB := "hff".U
        m_axi_Metrics.WVALID := true.B
        m_axi_Metrics.WLAST := true.B
        when(m_axi_Metrics.WREADY) {
          state := writeMetricsResp
        }
      }

      is(writeMetricsResp) {
        m_axi_Metrics.BREADY := true.B
        when(m_axi_Metrics.BVALID) {
          lastBresp := m_axi_Metrics.BRESP
          when(m_axi_Metrics.BRESP =/= 0.U) {
            bErrorMask := bErrorMask | (BigInt(1) << 12).U(32.W)
          }
          when(metricIndex === (metricWords - 1).U) {
            doneSticky := true.B
            state := idle
            when(autoRestart) {
              startPending := true.B
            }
          }.otherwise {
            metricIndex := metricIndex + 1.U
            state := writeMetricsAddr
          }
        }
      }
    }

    interrupt := doneSticky

    // Keep otherwise-unused response fields visible to synthesis and lint.  They
    // are part of the Vitis AXI ABI even when the kernel uses only one ID.
    val unusedRespIds = allMasters.map(port => Cat(port.RID, port.BID, port.RLAST)).reduce(_ ^ _)
    dontTouch(unusedRespIds)
    dontTouch(wStrb)
  }
}
