# 2026-05-29 TAPA PCG / single SpMV demo 候选归档

本目录保存 2026-05-29 两个 demo 候选的本地备份。两者都已完成 demo-only
上板测试，但都没有按性能目标晋级为标准 bitstream；`395bitstream/` 中仍可保留
这两个 demo 作为同步/对比槽位，四个标准成品不受影响。

## 归档对象

```text
cuper-tapa-spmv-u55c-20260528-demo.xclbin
cuper-tapa-spmv-u55c-20260528-demo.xclbin.info
cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin
cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin.info
```

## 关键参数

| 文件 | 主线 | Kernel | UUID | SHA256 | DATA/KERNEL/HBM |
| --- | --- | --- | --- | --- | --- |
| `cuper-tapa-spmv-u55c-20260528-demo.xclbin` | `cuper-tapa-spmv` | `CuperPcgSpmv` | `c95c1dfc-20ca-9152-279e-bafdf35fdc3d` | `19d227179db7f22adfd12e78da119a99d102c59ebe25df686a652c6715ea95f2` | 147 / 500 / 418 MHz |
| `cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin` | `cuper-tapa-pcg` | `CuperPcg` | `086a3345-ddf0-ffdd-b260-16ca5fa5223a` | `83baded1910ecb2c9e662f9ff6920fd8a55dbd2898ae69629c862714e17cf7f1` | 210 / 500 / 408 MHz |

## 测试状态

测试日志：

```text
logs/codex_two_demo_test_20260529_1300/
```

相关版本记录：

```text
docs/bitstream_summaries/2026-05-28-cuper-tapa-spmv-single-optimization/
docs/bitstream_summaries/2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/
```

single SpMV one-shot demo 从 `thermal2_n16` 到完整 `thermal2` 全部返回，完整规模
`spmv_avg=1.781541 ms`。它改善了成功边界，但共同成功点比 standalone TAPA Cuper
SpMV 标准略慢，后续只作为 single-SpMV 回归基线和边界记录。

full-PCG demo 的 init-only 和 `MAX_ITERS=1` 从 `thermal2_n16` 到完整 `thermal2`
全部返回，direct ctrl 均为 `0x4 -> 0xe`。它保留了完整规模功能边界，但共同成功点
1iter 明显慢于当前 TAPA full-PCG 标准版和上一 demo，因此不建议按性能目标晋级。

## Git 状态

本目录中的 `.xclbin` 受 `.gitignore` 保护，只做本地留档；`.xclbin.info` 和本
README 进入 Git，用来记录归档位置、UUID、clock 和测试结论。
