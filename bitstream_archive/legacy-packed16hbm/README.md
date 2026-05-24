# Legacy Packed16HBM Bitstream

这个目录保存 legacy packed16hbm bitstream 的本地留档。

注意：`Project-XPlus/.gitignore` 默认忽略 `*.xclbin`，只有 `395bitstream/*.xclbin`
被显式允许进 Git。因此本目录里的 `.xclbin` 会留在本机/U55C 服务器工作区，
但不会随 GitHub 同步。

## 文件

原同步路径：

```text
395bitstream/cuper-notapa-pcg-fpga-legacy-packed16hbm-u55c-20260522.xclbin
```

当前处理：

- 已从 `395bitstream/` 同步目录移出。
- 二进制文件保存在本目录，用于必要时做历史对照。
- GitHub 只同步本 Markdown 说明；本目录内的 `.xclbin` 受 `.gitignore` 保护，不进 Git。

## 元数据

| 项 | 值 |
| --- | --- |
| 文件名 | `cuper-notapa-pcg-fpga-legacy-packed16hbm-u55c-20260522.xclbin` |
| 大小 | 47124730 bytes |
| 本地修改时间 | 2026-05-24 21:14:47 CST |
| SHA256 | `8b67f0603e6df09f1837cecc12659670fd24524496aa297c58254a96bd71773e` |
| xclbin UUID | `aa6efd65-d5b8-4405-e19b-cef5e83d84b3` |
| Kernel | `cuper_pcg_control_kernel` |
| Platform | `xilinx_u55c_gen3x16_xdma_3_202210_1` |
| hbm_aclk | 430 MHz |
| KERNEL_CLK | 500 MHz |
| DATA_CLK | 281 MHz |

## 口径

这是旧的 no-TAPA FPGA-PCG packed16hbm control-kernel 对照版。当前 `395bitstream/`
只保留主线 bitstream；这个 legacy 版本只做本地留档，不再通过 GitHub 跟随同步。

如需重新对比，使用本目录下同 SHA256 的 `.xclbin`。
