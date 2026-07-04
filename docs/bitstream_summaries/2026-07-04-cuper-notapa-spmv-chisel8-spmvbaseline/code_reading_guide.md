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

`CuperSpmvOnly_ChiselDataPath8` 当前是 low-memory serial issue 版。它仍实例化 8x8
`StripCoreLane` / `StripAccumLane`，但不再把 `xMem` 做成 16 写/64 读的
`Reg(Vec(8192))`：

```text
Vector_X_Stream_in packet -> loadX/loadXWrite -> 单读/单写 SyncReadMem xMem
Matrix_A_Stream_i beat    -> consumeBatch     -> 选择一路 source
selected beat slot        -> issueSlotRead    -> 解码 row/col/value
                           -> issueSlotWait    -> 等待 xMem 同步读
                           -> issueSlotSend    -> 发送到对应 source/owner Core lane
```

这个改动只影响 datapath 内部发射粒度和综合内存压力，不改变 host ABI、HBM mapping、
ptr boundary 语义或 tagged Y 输出格式。

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
