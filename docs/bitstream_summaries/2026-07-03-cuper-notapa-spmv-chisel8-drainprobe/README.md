# CuperSpmvChisel8 HBM Drain-Probe

## 状态

这版是 `cuper-notapa-spmv` 主线下的独立 Chisel RTL kernel HBM drain-probe demo：

```text
kernel: CuperSpmvChisel8
source: chisel/cuper-spmv8/src/main/scala/cuper/spmv/CuperSpmvChisel8.scala
generated RTL: verilog/chisel/CuperSpmvChisel8.sv
build dir: cuper-spmv-chisel8-build/
bitfile: 395bitstream/cuper-notapa-spmv-u55c-20260703-chisel8-drainprobe-demo.xclbin
build log: logs/cuper_spmv_chisel8_drainprobe_hw_20260703_213712.log
```

它保持 entry-probe 版的 kernel 名、host ABI、AXI-Lite offset、13 路 `m_axi_*`
端口和 HBM mapping 不变，但把 FSM 从 first-read entry-probe 替换成 HBM
drain-probe。当前仍不计算 SpMV，也不写完整 `Y_out`；只写 `Y_out[0]=0` 保持返回链路。

## Drain 范围

RTL 按单 ID、单 outstanding 口径顺序读取：

1. `ptr[0..7]`，得到每路 `Matrix_data_i` beat 数。
2. 完整 boundary table：`ptr[8 + boundary * 8 + channel]`，`boundary=0..Batch_num`。
3. `ceil(Column_num/16)` 个 512-bit X packets。
4. `Matrix_data_0..7`，每路读取 `ptr[channel]` 个 512-bit beats。
5. `Y_out[0]=0`。
6. `Status[0..63]` 和 `Metrics[0..63]`。

## Status/Metrics

保留 magic：

```text
Status[1]  = 0x43535056
Metrics[0] = 0x4353504d56384348
```

关键口径：

```text
Status[7]  ptr words expected
Status[8]  ptr words read
Status[9]  X packets expected
Status[10] X packets read
Status[11] matrix done mask, 8 路完成时为 0xff
Status[12] R response error mask
Status[13] B response error mask
Status[16..23] ptr[0..7] matrix length
Status[31] 0x44525042 ("DRPB")

Metrics[8..15]  每路 matrix beats read
Metrics[16..23] 每路 first matrix low64
Metrics[24..31] 每路 last matrix low64
Metrics[6..7]   first/last X low64
```

host 侧新增 drain-probe 摘要校验：no-check 模式下也会检查 magic、ptr/X/matrix 计数、
done mask 和 response error mask。`CHECK_Y=1` 仍预期失败，因为 RTL 不计算 SpMV。

## 同步信息

```text
UUID: 3ea13c75-0ba3-5dbe-0d58-4778e489313b
SHA256: 3783f92f2dd0000d009ea5ff98e7be61157fbeb948bfd846ee975bbdc191e80f
DATA/KERNEL/HBM clock: 150 / 500 / 450 MHz
Routed timing: WNS 0.003 ns, TNS 0.000 ns, setup failing endpoints 0, hold WHS 0.009 ns
Vitis result: Run completed, VPL impl Complete
Vitis elapsed: 1h 56m 41s
```

## 当前建议

继续作为 debug/demo 候选，不晋级标准 bitstream。本轮已经完成 RTL 生成、Verilator
lint、host 编译、XO 打包、完整 xclbin 构建和 `395bitstream/` 同步：

```text
tmux: project-xplus-cuper-spmv-chisel8-drainprobe-hw
log: logs/cuper_spmv_chisel8_drainprobe_hw_20260703_213712.log
result: Run completed, VPL impl Complete
```

本机没有 U55C/XRT device，尚未上板。服务器侧上板验收只采信 no-check drain 计数和
response mask，不采信 Y correctness。
