package cuper.spmv

import chisel3._
import chisel3.util._

// Standalone fixed 8-HBM Vitis RTL kernel entry point.
//
// This top keeps the final public ABI while acting as an HBM drain-probe: it
// reads the complete ptr table, drains all X float_v16 packets, drains every
// Matrix_data_i beat described by ptr[i], writes Y_out[0], then publishes raw
// Status/Metrics words.  The full SpMV datapath can replace the drain FSM
// behind the same AXI-Lite register map and m_axi ports.
class CuperSpmvChisel8 extends RawModule {
  override def desiredName: String = "CuperSpmvChisel8"

  private val hbmChannels = 8
  private val pointerArgs = 13
  private val statusWords = 64
  private val metricWords = 64
  private val magic32 = "h43535056".U(32.W) // "CSPV"
  private val drainMagic32 = "h44525042".U(32.W) // "DRPB"
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
    port.WSTRB := ((BigInt(1) << (port.WSTRB.getWidth)) - 1).U
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

    val states = Enum(16)
    val idle = states(0)
    val readPtrAddr = states(1)
    val readPtrData = states(2)
    val readXAddr = states(3)
    val readXData = states(4)
    val readMatrixAddr = states(5)
    val readMatrixData = states(6)
    val writeYAddr = states(7)
    val writeYData = states(8)
    val writeYResp = states(9)
    val writeStatusAddr = states(10)
    val writeStatusData = states(11)
    val writeStatusResp = states(12)
    val writeMetricsAddr = states(13)
    val writeMetricsData = states(14)
    val writeMetricsResp = states(15)

    val state = RegInit(idle)
    val ptrIndex = RegInit(0.U(32.W))
    val xPacketIndex = RegInit(0.U(32.W))
    val matrixIndex = RegInit(0.U(3.W))
    val matrixBeatIndex = RegInit(0.U(32.W))
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
    val xPacketsExpected = Wire(UInt(32.W))
    ptrWordsExpected := (batchNum +& 2.U(32.W)) << 3
    xPacketsExpected := (columnNum +& 15.U(32.W)) >> 4

    val running = state =/= idle
    when(running) {
      cycleCounter := cycleCounter + 1.U
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

    val selectedMatrixArReady = Mux1H((0 until hbmChannels).map { i =>
      (matrixIndex === i.U) -> matrix(i).ARREADY
    })
    val selectedMatrixRValid = Mux1H((0 until hbmChannels).map { i =>
      (matrixIndex === i.U) -> matrix(i).RVALID
    })
    val selectedMatrixRData = Mux1H((0 until hbmChannels).map { i =>
      (matrixIndex === i.U) -> matrix(i).RDATA
    })
    val selectedMatrixRResp = Mux1H((0 until hbmChannels).map { i =>
      (matrixIndex === i.U) -> matrix(i).RRESP
    })
    val selectedMatrixExpected = Mux1H((0 until hbmChannels).map { i =>
      (matrixIndex === i.U) -> matrixLenPerChannel(i)
    })
    val selectedMatrixMask = Mux1H((0 until hbmChannels).map { i =>
      (matrixIndex === i.U) -> (BigInt(1) << i).U(32.W)
    })
    val selectedMatrixRMask = Mux1H((0 until hbmChannels).map { i =>
      (matrixIndex === i.U) -> (BigInt(1) << (i + 1)).U(32.W)
    })

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
      is(25.U) { statusValue := Cat(0.U(29.W), matrixIndex) }
      is(26.U) { statusValue := matrixBeatIndex }
      is(27.U) { statusValue := ptrIndex }
      is(28.U) { statusValue := xPacketIndex }
      is(29.U) { statusValue := firstPtr }
      is(30.U) { statusValue := lastPtr }
      is(31.U) { statusValue := drainMagic32 }
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
      is(40.U) { metricValue := Cat(ptrIndex, xPacketIndex) }
      is(41.U) { metricValue := Cat(0.U(32.W), matrixBeatIndex) }
      is(42.U) { metricValue := Cat(firstPtr, lastPtr) }
    }

    switch(state) {
      is(idle) {
        when(startPending) {
          startPending := false.B
          doneSticky := false.B
          cycleCounter := 0.U
          ptrIndex := 0.U
          xPacketIndex := 0.U
          matrixIndex := 0.U
          matrixBeatIndex := 0.U
          statusIndex := 0.U
          metricIndex := 0.U
          ptrWordsRead := 0.U
          xPacketsRead := 0.U
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
            matrixFirstLow64(i) := 0.U
            matrixLastLow64(i) := 0.U
          }
          state := readPtrAddr
        }
      }

      is(readPtrAddr) {
        when(ptrWordsExpected === 0.U) {
          state := readXAddr
        }.otherwise {
          m_axi_SpElement_list_ptr.ARADDR := argPtr(0) + (ptrIndex << 2)
          m_axi_SpElement_list_ptr.ARVALID := true.B
          when(m_axi_SpElement_list_ptr.ARREADY) {
            state := readPtrData
          }
        }
      }

      is(readPtrData) {
        m_axi_SpElement_list_ptr.RREADY := true.B
        when(m_axi_SpElement_list_ptr.RVALID) {
          val ptrData = m_axi_SpElement_list_ptr.RDATA
          ptrWordsRead := ptrWordsRead + 1.U
          lastPtr := ptrData
          when(ptrIndex === 0.U) {
            firstPtr := ptrData
          }
          for (i <- 0 until hbmChannels) {
            when(ptrIndex === i.U) {
              matrixLenPerChannel(i) := ptrData
            }
          }
          lastRresp := m_axi_SpElement_list_ptr.RRESP
          when(m_axi_SpElement_list_ptr.RRESP =/= 0.U) {
            rErrorMask := rErrorMask | 1.U
          }
          when(ptrIndex === (ptrWordsExpected - 1.U)) {
            xPacketIndex := 0.U
            state := readXAddr
          }.otherwise {
            ptrIndex := ptrIndex + 1.U
            state := readPtrAddr
          }
        }
      }

      is(readXAddr) {
        when(xPacketsExpected === 0.U) {
          matrixIndex := 0.U
          matrixBeatIndex := 0.U
          state := readMatrixAddr
        }.otherwise {
          m_axi_X.ARADDR := argPtr(9) + (xPacketIndex << 6)
          m_axi_X.ARVALID := true.B
          when(m_axi_X.ARREADY) {
            state := readXData
          }
        }
      }

      is(readXData) {
        m_axi_X.RREADY := true.B
        when(m_axi_X.RVALID) {
          xPacketsRead := xPacketsRead + 1.U
          when(xPacketIndex === 0.U) {
            firstXLow64 := m_axi_X.RDATA(63, 0)
          }
          lastXLow64 := m_axi_X.RDATA(63, 0)
          lastRresp := m_axi_X.RRESP
          when(m_axi_X.RRESP =/= 0.U) {
            rErrorMask := rErrorMask | (BigInt(1) << 9).U(32.W)
          }
          when(xPacketIndex === (xPacketsExpected - 1.U)) {
            matrixIndex := 0.U
            matrixBeatIndex := 0.U
            state := readMatrixAddr
          }.otherwise {
            xPacketIndex := xPacketIndex + 1.U
            state := readXAddr
          }
        }
      }

      is(readMatrixAddr) {
        when(selectedMatrixExpected === 0.U || matrixBeatIndex >= selectedMatrixExpected) {
          matrixDoneMask := matrixDoneMask | selectedMatrixMask
          when(matrixIndex === 7.U) {
            state := writeYAddr
          }.otherwise {
            matrixIndex := matrixIndex + 1.U
            matrixBeatIndex := 0.U
          }
        }.otherwise {
          for (i <- 0 until hbmChannels) {
            when(matrixIndex === i.U) {
              matrix(i).ARADDR := argPtr(1 + i) + (matrixBeatIndex << 6)
              matrix(i).ARVALID := true.B
            }
          }
          when(selectedMatrixArReady) {
            state := readMatrixData
          }
        }
      }

      is(readMatrixData) {
        for (i <- 0 until hbmChannels) {
          when(matrixIndex === i.U) {
            matrix(i).RREADY := true.B
          }
        }
        when(selectedMatrixRValid) {
          matrixBeatsRead(matrixIndex) := matrixBeatsRead(matrixIndex) + 1.U
          when(matrixBeatIndex === 0.U) {
            matrixFirstLow64(matrixIndex) := selectedMatrixRData(63, 0)
          }
          matrixLastLow64(matrixIndex) := selectedMatrixRData(63, 0)
          lastRresp := selectedMatrixRResp
          when(selectedMatrixRResp =/= 0.U) {
            rErrorMask := rErrorMask | selectedMatrixRMask
          }
          when(matrixBeatIndex === (selectedMatrixExpected - 1.U)) {
            matrixDoneMask := matrixDoneMask | selectedMatrixMask
            when(matrixIndex === 7.U) {
              state := writeYAddr
            }.otherwise {
              matrixIndex := matrixIndex + 1.U
              matrixBeatIndex := 0.U
              state := readMatrixAddr
            }
          }.otherwise {
            matrixBeatIndex := matrixBeatIndex + 1.U
            state := readMatrixAddr
          }
        }
      }

      is(writeYAddr) {
        m_axi_Y_out.AWADDR := argPtr(10)
        m_axi_Y_out.AWVALID := true.B
        when(m_axi_Y_out.AWREADY) {
          state := writeYData
        }
      }

      is(writeYData) {
        m_axi_Y_out.WDATA := 0.U
        m_axi_Y_out.WSTRB := "hf".U
        m_axi_Y_out.WVALID := true.B
        m_axi_Y_out.WLAST := true.B
        when(m_axi_Y_out.WREADY) {
          state := writeYResp
        }
      }

      is(writeYResp) {
        m_axi_Y_out.BREADY := true.B
        when(m_axi_Y_out.BVALID) {
          lastBresp := m_axi_Y_out.BRESP
          when(m_axi_Y_out.BRESP =/= 0.U) {
            bErrorMask := bErrorMask | (BigInt(1) << 10).U(32.W)
          }
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
    // are part of the Vitis AXI ABI even when the probe uses only one ID.
    val unusedRespIds = allMasters.map(port => Cat(port.RID, port.BID, port.RLAST)).reduce(_ ^ _)
    dontTouch(unusedRespIds)
    dontTouch(wStrb)
  }
}
