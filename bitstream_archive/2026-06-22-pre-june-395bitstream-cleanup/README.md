# 2026-06-22 Pre-June 395bitstream Cleanup

这个目录保存从 `395bitstream/` 移出的 2026-06-01 以前 bitstream。移动目的只是缩小
同步目录，让 `395bitstream/` 只保留当前 6 月以来仍在测试窗口内的 Jacobi/SpMV
demo 和实验 artifact；这些归档文件没有在本次重新测试。

## 归档文件

| 文件 | 原路径 | SHA256 | 说明 |
| --- | --- | --- | --- |
| `cuper-tapa-spmv-u55c-20260522.xclbin` | `395bitstream/cuper-tapa-spmv-u55c-20260522.xclbin` | `ec4bfcaf02463592e3c6732b05de86c6e0e264494e6c73b120a5751d789bd327` | TAPA Cuper single SpMV 旧可用 bitstream；原同步目录中没有配套 `.info` |
| `cuper-tapa-pcg-fpga-u55c-20260525.xclbin` | `395bitstream/cuper-tapa-pcg-fpga-u55c-20260525.xclbin` | `8733b618312d1d17bee8123e512eec14f0ca831b6eca1372b3c22e6be11ae301` | TAPA Cuper FPGA-PCG 旧标准/对照文件，配套 `.info` 一并归档 |
| `cuper-notapa-spmv-u55c-20260524.xclbin` | `395bitstream/cuper-notapa-spmv-u55c-20260524.xclbin` | `02bbb30e0d7f4262e4f86d210fbe978b55a772b39e0c47462510868323c8cb98` | no-TAPA single SpMV 旧标准/对照文件，配套 `.info` 一并归档 |
| `cuper-notapa-pcg-fpga-u55c-20260522.xclbin` | `395bitstream/cuper-notapa-pcg-fpga-u55c-20260522.xclbin` | `b75529ce602a0eca0adfd8f6a97e6eb57aefef6cc14cfb663b445533c58222d0` | no-TAPA FPGA-PCG 旧标准/对照文件，配套 `.info` 一并归档 |
| `cuper-tapa-spmv-u55c-20260528-demo.xclbin` | `395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin` | `19d227179db7f22adfd12e78da119a99d102c59ebe25df686a652c6715ea95f2` | TAPA Cuper `CuperPcgSpmv` single SpMV demo，未晋级标准，配套 `.info` 一并归档 |
| `cuper-tapa-pcg-fpga-u55c-20260531-demo.xclbin` | `395bitstream/cuper-tapa-pcg-fpga-u55c-20260531-demo.xclbin` | `a8df40e1bf21774c7608c329fd591012b84744a18dcf4e8b0dd36672d64ccf72` | TAPA Cuper full-PCG packed timing demo，未晋级标准，配套 `.info` 一并归档 |

## 配套信息

除 `cuper-tapa-spmv-u55c-20260522.xclbin` 外，其余归档 bitstream 均带有原
`.xclbin.info`。这些文件记录 kernel、UUID、clock 和 memory topology，便于后续回查。
