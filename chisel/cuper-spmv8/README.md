# Cuper SpMV 8-HBM Chisel 数据通路

这个子工程现在有两条 Chisel 生成路径。

第一条是独立 Vitis RTL kernel：

```text
CuperSpmvChisel8
```

它使用固定 8-HBM ownerbank ABI，直接暴露 AXI4 master、AXI-Lite control 和
`Status`/`Metrics`。当前源码版本是 HBM drain-probe：读取完整 ptr table、
`ceil(Column_num/16)` 个 X packet，并按 `ptr[channel]` drain
`Matrix_data_0..7`，最后写 `Y_out[0]`、`Status[0..63]` 和 `Metrics[0..63]`。
它不依赖 `CuperSpmvServiceOnly` 的 TAPA graph。

常用命令：

```bash
make cuper-spmv-chisel8-generate
make cuper-spmv-chisel8-xrt-host
make build-cuper-spmv-chisel8-xo
make cuper-spmv-chisel8-hw-tmux
make run-cuper-spmv-chisel8-xrt TARGET=hw DATASET=data/suitesparse/Schmid/csr/thermal2_n16
```

第二条是原有固定 8-HBM 的 SpMV-only RTL 数据通路模块：

```text
CuperSpmvOnly_ChiselDataPath8
```

它用于替换 `CuperSpmvServiceOnly` 中原来的 stream datapath。当前目标是单独验证
8 路 HBM 的 Cuper SpMV service 数据通路，不是完整 Jacobi iteration，也不是 full-PCG。

适用配置：

```text
JACOBI_TOP=CuperSpmvServiceOnly
JACOBI_HBM_CHANNELS=8
JACOBI_SPMV_CHISEL_DATAPATH=1
```

模块位于现有 TAPA ptr/vector/matrix loader 之后，消费已有的 TAPA/HLS stream，
并向当前 scalar Y writer 输出原有格式的 `CuperSpmvOnly_TaggedFloatV2` stream。

## 目录结构

```text
chisel/cuper-spmv8/
  README.md
  build.sbt
  project/build.properties
  packaging/CuperSpmvChisel8.kernel.xml
  scripts/generate.sh
  scripts/generate_kernel.sh
  scripts/package_kernel_xo.sh
  scripts/package_kernel_xo.tcl
  scripts/link_kernel_xclbin.sh
  src/main/scala/cuper/spmv/
    AxiPorts.scala
    CuperSpmvChisel8.scala
    CuperSpmvOnlyChiselDataPath8.scala
    CuperSpmvStreamPorts.scala
    FloatingPointBlackBoxes.scala
    Generate.scala
    StripAccumLane.scala
    StripCoreLane.scala
    StripProduct.scala
```

其中 `target/` 是 sbt/Scala 编译缓存，不是源码入口；阅读和修改时优先看
`src/main/scala/cuper/spmv/`。

## 关键文件

`CuperSpmvChisel8.scala` 是独立 Vitis RTL kernel 顶层。当前只做 HBM drain-probe，
不计算 SpMV；后续 full SpMV baseline 会在同一 ABI 下接入真正数据通路。

`CuperSpmvOnlyChiselDataPath8.scala` 是 stream 数据通路模块，保留 8 路 matrix stream 的控制、
slot 解码、batch 调度、状态机和 partial sum 输出。

`CuperSpmvStreamPorts.scala` 放顶层 stream 端口的小结构和工厂方法，把原来
`_1/_2/_3` 风格的 tuple 端口访问改成具名字段，方便阅读。

`FloatingPointBlackBoxes.scala` 放 `HlsFmul32` / `HlsFadd32`，复用已有 HLS
浮点乘法、加法 IP 的 blackbox。

`StripCoreLane.scala` 放单个 source-HBM/owner slot 的乘法 core lane，只做
`value * X[col]`。

`StripProduct.scala` 定义 Core 和 Accumulator 之间的中间乘积包。这条
`Decoupled[StripProduct]` 边界就是后续插入 scoreboard/乱序调度的位置。

`StripAccumLane.scala` 放单个 source-HBM 到 owner-bank 的局部累加 lane，只接收
已经算好的中间乘积并执行 partial sum 累加。

`Generate.scala` 是 Verilog 生成入口。它调用 Chisel/CIRCT 生成 SystemVerilog，
并把部分 `always_comb` 文本替换成 Vivado 2022.2 更稳的 `always @(*)` 形式。

`scripts/generate.sh` 是仓库根目录使用的生成脚本，会把 Verilog 写到：

```text
verilog/tapa/CuperSpmvOnly_ChiselDataPath8.v
```

## 总体数据流

整体数据路径可以按下面的顺序理解：

```text
PE_Param_in
  -> 读取 header、每个 batch 的 8 路 start/end 边界

Vector_X_Stream_in
  -> 当前 batch 的 X 向量窗口
  -> xMem

Matrix_A_Stream_0..7
  -> 每路 matrix beat 拆成 8 个 slot
  -> 8 x 8 StripCoreLane 做 value * X[col]
  -> Decoupled StripProduct stream
  -> 后续可插 scoreboard / 乱序调度
  -> 8 x 8 StripAccumLane 做 partial sum 累加
  -> ping/pong partial sum

Vector_Y_Tagged_Stream_0..7
  <- 按 source-pair / owner-group 读出 partial sum
  <- 输出给已有 scalar Y writer
```

这里有两个维度：

- `source`：matrix beat 来自哪个 `Matrix_A_Stream_i`，也就是哪个 HBM channel。
- `owner`：一个 512-bit matrix beat 内的第几个 64-bit slot，对应输出 owner bank。

所以顶层会实例化 `8 x 8` 个 `StripCoreLane` 和 `8 x 8` 个 `StripAccumLane`。
每个 lane 只处理一个 `source -> owner` 组合。当前 Core 和 Accumulator 之间先直连；
后续如果要做乱序，可以在 `StripProduct` stream 上插入 scoreboard。

## Matrix Slot 格式

每个 matrix stream 输入宽度是 513 bit，目前只消费低 512 bit。低 512 bit 拆成
8 个 64-bit slot：

```text
slot[31:0]   : float32 value
slot[49:32]  : row/tag
slot[63:50]  : col
```

`row/tag` 内部继续拆分：

```text
row(17)    : padding 标记，1 表示该 slot 无效
row(14,1) : owner group
row(0)    : ping/pong 选择
```

`col` 用来索引当前 batch 已缓存的 `xMem`。`value` 支持现有 strip 数据格式里的复用编码：
当 reuse 条件命中时，当前 slot 沿用前一个 slot 的 value。

## Core 与 Accumulator 边界

原 TAPA/HLS 路径里，`CoreStrip` 负责解 matrix slot、读 X 并计算 `value * X[col]`，
Accumulator 只消费局部乘积并累加。Chisel 版本现在也按这个语义拆开：

```text
StripCoreLane
  input : value, X[col], group, ping/pong
  output: StripProduct(group, ping/pong, value * X[col])

StripAccumLane
  input : StripProduct
  action: partial[group][ping/pong] += product.value
```

`StripProduct` 使用 `Decoupled` 握手。当前顶层直接连接：

```text
core.io.out <> accum.io.in
```

后续 scoreboard 应该替换这条直连，放在 `StripCoreLane` 和 `StripAccumLane` 中间。

## StripCoreLane 做什么

`StripCoreLane` 是最小乘法单元。它只实例化 `HlsFmul32`，固定 8 拍 latency。
当后级 product stream 不 ready 时，它会通过 fmul 的 `ce` 暂停整条乘法流水，避免
中间乘积丢失。

为了避免同一个 `group + ping/pong` 的乘积在 Core 乘法流水内堆叠得太近，Core 仍会
做本地保守 hazard 检查；更复杂的跨 lane/跨 owner 调度应放到后续 scoreboard。

## StripAccumLane 做什么

`StripAccumLane` 是最小累加单元。它的逻辑是：

```text
partial[group][ping_or_pong] += product.value
```

实现上分成两段：

1. 从 ping/pong SRAM 读旧 partial sum。
2. `HlsFadd32` 计算旧 partial sum 加中间乘积，固定 13 拍 latency，再写回 SRAM。

为了避免同一个 `group + ping/pong` 在累加流水线里被重复读写，Accumulator 会检查
读旧值阶段和加法流水中的地址。如果发现相关性冲突，就拉低 `in.ready`，让前面的
Core 或 scoreboard 暂停发送该 product。

## 顶层状态机

顶层状态机在 `CuperSpmvOnlyChiselDataPath8` 的 `switch(state)` 中。建议按状态顺序读：

```text
idle
  等待 ap_start。

readHeader
  丢弃 PE_Param_in 前 4 个 header token。

initPartial
  清零本轮要用到的 ping/pong partial sum。

readStart
  读取当前 batch 的 8 个 per-HBM start offset。

loadX
  从 Vector_X_Stream_in 读取当前 batch 的 X 向量窗口，写入 xMem。

readEnd
  读取当前 batch 的 8 个 per-HBM end offset，并计算 remaining beat 数。

consumeBatch
  对 8 路 Matrix_A_Stream 独立发射 matrix beat。
  只有某一路 source 的 8 个 owner lane 都 ready 时，才真正读取该 source beat。

drainAccum
  等待所有 Core fmul 和 Accumulator read/fadd 流水线写回完成。

outputRead
  对当前 source-pair 和 owner group 发起 partial sum 读请求。

outputWait
  等待 SyncReadMem 的读数据返回。

outputEmit
  组装 129-bit tagged payload，并写入 Vector_Y_Tagged_Stream_0..7。

nextOutput
  先遍历 8 个 source-pair，再进入下一个 owner group。

nextIter
  多 iteration 时回到 initPartial；否则进入 done。

doneState
  拉高 ap_done/ap_ready 一拍，然后回到 idle。
```

## 输出格式

输出仍保持已有 writer 期望的 129-bit tagged payload：

```text
Cat(0.U(1.W), outPong, outPing, outPair.pad(32), outPacket)
```

含义：

- 最高 1 bit 当前保留为 0。
- `outPong` 和 `outPing` 是同一个 output packet 中两行的 partial sum。
- `outPair` 表示当前输出来自哪个 source-HBM pair。
- `outPacket` 是最终 Y writer 使用的 packet 编号。

下游 `Vector_Y_Tagged_Stream_i` 如果满了，顶层会保留对应 owner 的 `outValid`，
下一拍继续尝试写出，不会丢数据。

## 生成 Verilog

从仓库根目录执行：

```bash
./chisel/cuper-spmv8/scripts/generate.sh
```

默认输出：

```text
verilog/tapa/CuperSpmvOnly_ChiselDataPath8.v
```

也可以指定输出目录：

```bash
./chisel/cuper-spmv8/scripts/generate.sh /tmp/chisel-spmv8
```

脚本内部会进入 `chisel/cuper-spmv8/`，执行：

```bash
sbt "runMain cuper.spmv.GenerateCuperSpmvOnlyChiselDataPath8 <输出目录>"
```

## 本地编译检查

只检查 Scala/Chisel 源码是否能编译：

```bash
cd chisel/cuper-spmv8
sbt compile
```

重新生成 Verilog 后，建议再检查生成文件是否只包含预期变化：

```bash
git status --short
git diff -- verilog/tapa/CuperSpmvOnly_ChiselDataPath8.v
```

注意：如果 `chisel/` 目录仍是未跟踪状态，普通 `git diff` 不会显示 README 或 Scala
源码的变化，需要先 `git status --short` 确认未跟踪文件列表。

## 阅读顺序

如果第一次读这份实现，建议不要从 lane 内部开始钻，按下面顺序更容易：

1. 先看本 README，明确这个模块只负责 8-HBM SpMV-only datapath。
2. 看 `Generate.scala`，理解 Verilog 怎么生成。
3. 看 `CuperSpmvStreamPorts.scala`，理解 matrix/tagged stream 端口命名。
4. 看 `CuperSpmvOnlyChiselDataPath8` 顶层端口，确认它如何接 TAPA stream。
5. 看顶层状态机，从 `readHeader` 一路读到 `outputEmit`。
6. 看 matrix slot 解码，理解 `source`、`owner`、`group`、`ping/pong`。
7. 看 `StripCoreLane.scala`，理解单 lane 内部的 `value * X[col]` 乘法流水。
8. 看 `StripProduct.scala`，确认 Core 和 Accumulator 之间的 scoreboard 插入点。
9. 看 `StripAccumLane.scala`，理解 `partial += product` 的累加流水线。
10. 最后看 `FloatingPointBlackBoxes.scala`，确认浮点 IP 名字和 latency 没变。

## 当前限制

- HBM channel 数固定为 8，不是参数化通用版本。
- `Matrix_len` 和 peek stream 端口主要为 ABI 兼容保留，当前核心控制使用
  `PE_Param_in` 中的 per-channel start/end 边界。
- 浮点乘法和加法依赖外部 HLS/Vivado IP，Chisel 源码只描述调度、握手、SRAM 和相关性控制。
- 目前 Core 和 Accumulator 之间只是直连，还没有真正实现 scoreboard/乱序调度。
- 这个模块只输出 SpMV partial tagged stream，不负责 Jacobi update、PCG controller
  或 host 侧收敛判断。
