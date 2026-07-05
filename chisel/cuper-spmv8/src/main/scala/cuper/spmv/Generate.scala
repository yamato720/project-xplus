package cuper.spmv

import chisel3.RawModule
import _root_.circt.stage.ChiselStage

import java.nio.charset.StandardCharsets
import java.nio.file.{Files, Path}

object CuperSpmv8BuildConfig {
  private def truthy(value: String): Boolean = {
    val normalized = value.trim.toLowerCase
    normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on"
  }

  val slimDebug: Boolean =
    sys.env.get("CUPER_SPMV_CHISEL8_SLIM_DEBUG").exists(truthy) ||
      sys.props.get("spmv.chisel8.slimDebug").exists(truthy)

  val enableDebug: Boolean = !slimDebug
}

object CuperSpmv8Emitter {
  // 生成给 Vivado/TAPA 打包使用的 Verilog。关闭随机初始化和调试信息，避免生成网表中
  // 出现 TAPA 流程不需要的随机化逻辑或大量局部调试符号。
  private val firtoolOpts = Array(
    "-disable-all-randomization",
    "-strip-debug-info",
    "--lowering-options=disallowLocalVariables,disallowPackedArrays",
    "-default-layer-specialization=enable"
  )

  def emitVerilog(gen: => RawModule, targetDir: String, fileName: String): Unit = {
    val outDir = Path.of(targetDir)
    Files.createDirectories(outDir)
    val systemVerilog = ChiselStage.emitSystemVerilog(
      gen = gen,
      firtoolOpts = firtoolOpts
    )
    // Vivado 2022.2 的部分 RTL/IP 打包路径对 always_comb 兼容性不好，这里只做文本级
    // 兼容替换，不改变 Chisel 生成逻辑。
    val vivadoVerilog = systemVerilog
      .replace("always_comb begin", "always @(*) begin")
      .replace("end // always_comb", "end // always")
    Files.write(outDir.resolve(fileName), vivadoVerilog.getBytes(StandardCharsets.UTF_8))
  }
}

object GenerateCuperSpmvOnlyChiselDataPath8 extends App {
  // 默认输出到子工程内 generated/，仓库脚本会传入 verilog/tapa 作为实际集成目录。
  val targetDir = args.headOption.getOrElse("generated")
  CuperSpmv8Emitter.emitVerilog(
    gen = new CuperSpmvOnlyChiselDataPath8(enableDebug = CuperSpmv8BuildConfig.enableDebug),
    targetDir = targetDir,
    fileName = "CuperSpmvOnly_ChiselDataPath8.v"
  )
}

object GenerateCuperSpmvChisel8 extends App {
  val targetDir = args.headOption.getOrElse("generated")
  CuperSpmv8Emitter.emitVerilog(
    gen = new CuperSpmvChisel8(enableDebug = CuperSpmv8BuildConfig.enableDebug),
    targetDir = targetDir,
    fileName = "CuperSpmvChisel8.sv"
  )
}
