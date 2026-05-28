# 2026-05-28 TAPA full-PCG packed feed/AP demo 留档

本目录保存从 `395bitstream/` 当前 demo 槽移出的旧候选 bitstream。它被新的
single-SpMV demo 替换，但仍保留本地二进制和 `.info` 方便回查。

## 归档对象

```text
cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin
cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin.info
```

旧 demo 主线是 `cuper-tapa-pcg`，kernel 为 `CuperPcg`。它来自 2026-05-27 的
packed feed/AP full-PCG 构建，只作为 demo 候选保留过，未晋级为标准版。

## 关键参数

| 项目 | 值 |
| --- | --- |
| UUID | `cc61e044-06f7-4726-8f18-773ac52ab1b2` |
| SHA256 | `add20fec0352d83c2b8cc8161d78d7f989124e11278ca553fe94a8cd231309bb` |
| DATA clock | 216 MHz |
| KERNEL clock | 500 MHz |
| HBM clock | 450 MHz |
| 构建日志 | `logs/cuper_tapa_pcg_packed_ap_hw_20260527_191340.log` |
| 版本记录 | `docs/bitstream_summaries/2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/` |

## 替换关系

2026-05-28 起，`395bitstream/` 的当前 demo 槽改为：

```text
395bitstream/cuper-tapa-spmv-u55c-20260528-demo.xclbin
```

新 demo 是 `CuperPcgSpmv`，属于 `cuper-tapa-spmv` 主线，用于单独测试
`CuperPcg` 内部 PCG service SpMV 链的性能和边界。

## Git 状态

本目录中的 `.xclbin` 受 `.gitignore` 保护，默认不进 Git；`.info` 和本 README
可以同步，用于记录旧 demo 的身份和归档位置。
