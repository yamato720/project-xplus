# 代码阅读指南

## 入口

- `chisel/cuper-spmv8/src/main/scala/cuper/spmv/CuperSpmvChisel8.scala`：
  独立 Vitis RTL kernel 顶层、AXI loaders、internal queues、scalar writer 和
  Status/Metrics writer。
- `chisel/cuper-spmv8/src/main/scala/cuper/spmv/CuperSpmvOnlyChiselDataPath8.scala`：
  复用的 8-HBM Chisel SpMV datapath。
- `host/cuper_spmv_chisel_xrt.cpp`：ownerbank8 host packing、XRT launch 和 baseline
  status/Y 校验。
- `chisel/cuper-spmv8/scripts/package_kernel_xo.tcl`：standalone XO packaging，包含
  fmul/fadd IP generation。

## ABI

ABI 与 drain-probe 保持一致：

```text
0  SpElement_list_ptr -> HBM[8]
1  Matrix_data_0      -> HBM[0]
...
8  Matrix_data_7      -> HBM[7]
9  X                  -> HBM[9]
10 Y_out              -> HBM[10]
11 Status             -> HBM[30]
12 Metrics            -> HBM[31]
13 Batch_num
14 Matrix_len
15 Row_num
16 Column_num
17 Iteration_num
```

当前 standalone loaders 只提供一轮 SpMV 数据，因此 datapath 内部固定按一轮运行；
`Iteration_num` 仍保留在 AXI-Lite/Status ABI 中。

## 数据流

`CuperSpmvChisel8` 先读取 `ptr[0..7]` 得到每路 matrix 长度。随后启动 datapath，并行
运行：

```text
ptr boundary loader -> PE_Param FIFO
X loader            -> Vector_X FIFO
Matrix loaders      -> Matrix_A FIFO[0..7]
```

当前同步 demo 的 `CuperSpmvOnly_ChiselDataPath8` 使用 owner-step8 phase-1。它仍实例化 8x8
`StripCoreLane` / `StripAccumLane`，但不再把 `xMem` 做成 16 写/64 读的
`Reg(Vec(8192))`，也不再使用旧 low-memory serial issue 的单 source/单 owner 路径：

```text
Vector_X_Stream_in packet -> loadX/loadXWrite -> 8 份单读/单写 SyncReadMem xMemCopies
Matrix_A_Stream_i beat    -> consumeBatch     -> 每 source 预取一个 pending beat
same owner slot           -> issueSlotRead    -> 跨 active source 解码 row/col/value
                           -> issueSlotWait    -> 每 source 等待各自 xMem 同步读
                           -> issueSlotSend    -> 所有 active source ready 后一起送 Core
```

这个改动只影响 datapath 内部发射粒度和综合内存压力，不改变 host ABI、HBM mapping、
ptr boundary 语义或 tagged Y 输出格式。若任一 active source 的对应 Core lane 因
RAW/backpressure 不 ready，整个 owner step 停住，保持 fmul/fadd RAW 顺序正确。

PE 参数流格式：

```text
4 个 header token
boundary 0 的 8 路 start offset
boundary 1..Batch_num 的 8 路 end offset
```

datapath 输出 8 路 129-bit tagged token：

```text
[31:0]    packet index
[63:32]   pair lane
[95:64]   Y even row bits
[127:96]  Y odd row bits
```

scalar writer 地址：

```text
Y[(packet << 4) + (pair << 1) + 0] = tagged[95:64]
Y[(packet << 4) + (pair << 1) + 1] = tagged[127:96]
```

## Status/Metrics

仍保留 drain-probe 计数：

```text
Status[7]  ptr words expected
Status[8]  ptr words read
Status[9]  X packets expected
Status[10] X packets read
Status[11] matrix done mask
Status[12] R response error mask
Status[13] B response error mask
Status[16..23] ptr[0..7] matrix length
```

baseline 新增：

```text
Status[31] 0x53504d56 ("SPMV")
Status[32] tagged pairs expected
Status[33] tagged pairs read
Status[34] scalar writes expected
Status[35] scalar write responses
Status[36] datapath done
Status[37] writer done
Status[39] scalar writes issued
Metrics[43] packed tagged expected/read
Metrics[44] packed scalar expected/responses
Metrics[45] done flags
Metrics[46] scalar writes issued
```

correctness-debug 追加的观测槽位：

```text
Status[40] valid slots
Status[41] nonzero X reads
Status[42] nonzero products
Status[43] accumulator accepts
Status[44] nonzero tagged writes
Status[45] nonzero scalar Y writes
Status[46] first nonzero scalar Y addr
Status[47] first nonzero scalar Y data bits
Status[48..51] datapath first nonzero tagged packet/pair/ping/pong
Status[52..55] writer first nonzero tagged packet/pair/ping/pong
Metrics[47] valid slots
Metrics[48] padding slots
Metrics[49] nonzero X reads
Metrics[50] nonzero products
Metrics[51] accumulator accepts
Metrics[52] tagged writes
Metrics[53] nonzero tagged writes
Metrics[54..55] datapath first nonzero tagged sample
Metrics[56] first nonzero scalar Y data/addr packed
Metrics[57] nonzero scalar Y writes
Metrics[58] raw stall cycles
Metrics[59] writer backpressure cycles
Metrics[60..61] writer first nonzero tagged sample
Status[56] core_nonzero_out
Status[57] fadd_nonzero_out
Status[58] partial_read_nonzero
Status[59..61] first nonzero core/fadd/partial sample bits
Metrics[62] packed {fadd_nonzero_out[31:0], core_nonzero_out[31:0]}
Metrics[63] packed {partial_read_nonzero[31:0], 32'h0}
```

当前同步到 `395bitstream/` 的 slim/no-debug xclbin 使用
`CUPER_SPMV_CHISEL8_SLIM_DEBUG=1` 生成，保留上述槽位 ABI，但关闭重 debug fanout，
因此这些 debug counters 预期为 0。服务器侧反馈显示该 slim/no-debug xclbin 已经
`CHECK_Y=1` 通过 listed `thermal2*`，但完整 `thermal2` 为 `459.425 ms`，远慢于
strip8 的 `2.71420 ms`。需要按下面断点口径定位时，应使用 full-debug 构建；
slim/no-debug 版的 no-check 只采信 magic、count、done mask 和 error mask。

上板 debug 判断口径：

```text
valid/product 为 0              -> 先看 matrix slot decode、padding/reuse、X read 地址
nonzero_products 非零但 core_nonzero_out 为 0
                                -> 先看 fmul wrapper / latency / valid-data 对齐
core 非零但 fadd_nonzero_out 为 0
                                -> 先看 fadd wrapper 或 accumulator 输入/partial read
fadd 非零但 partial_read_nonzero 为 0
                                -> 先看 partial SRAM write/read/init 覆盖
partial 非零但 tagged/Y 为 0/错位
                                -> 先看 tagged latch、scalar writer handshake/address/data 或 host BO sync/索引
```

Host 侧 `--check-y` 失败会打印首批 mismatch、最大 diff、上述 debug 摘要和首批非零
Y，no-check 仍只校验 magic/count/done/error mask。

注意：旧 `nonzero_products` 是 issue 阶段的输入侧计数，只说明本次送入 fmul 的
`value` 和 `X` 非零；它不能证明 Vivado fmul IP 的输出非零。新版
`core_nonzero_out` 才是在 fmul 输出被 accumulator 接收时计数。
