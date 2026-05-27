# 这一版改了什么

## 背景

上一标准版 `cuper-tapa-pcg-fpga-u55c-20260525.xclbin` 是 timed-debug 版
TAPA full-PCG。它能跑到 `thermal2_n262144`，但 full-PCG 内部的 SpMV
服务化路径明显慢于 standalone TAPA SpMV，且完整 `thermal2` 仍在
direct-register 运行后进入 `ctrl=0x0`。

## 代码方向

2026-05-27 demo 版来自 TAPA full-PCG 接收路径优化，目标是降低第二轮
`A*p` 的 SpMV 数据接收和归并开销。

关键方向：

- 优化 TAPA full-PCG 内嵌 SpMV 的 receive path。
- 保留 timed-debug 分段计时，继续输出 `init_spmv`、`iter_spmv`、
  `dot_p_ap`、`update_xr`、`update_z`、`update_p` 等 stage 字段。
- 不改变四条主线命名；新 bitstream 先作为 `-demo` 放入 `395bitstream/`。

对应源码补丁见本目录 `receive_path_source.diff`。该补丁来自提交
`debb634 Optimize TAPA PCG SpMV receive path`，需要回退源码时可以运行：

```bash
git apply --unidiff-zero -R docs/bitstream_summaries/2026-05-27-cuper-tapa-pcg-spmv-near-native-cuper/receive_path_source.diff
```

## 构建产物

- 构建输出：`cuper-tapa-pcg-fpga-u55c-20260525-build/hw/CuperPcg.xclbin`
- demo 同步文件：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin`
- demo info：`395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin.info`
- UUID：`9474ef8e-571b-ae13-f898-890e3af8ae5e`
- SHA256：`440d6969ff869d47aae5b12ab6d86d51b80794c07445799599ae13594a9166c5`
- DATA/HBM：216/450 MHz

## 预期收益

预期主要改善 1iter 中第二轮 SpMV，即 `iter_spmv` 分段；不预期直接解决完整
`thermal2` 的 `ctrl=0x0` 失败。

## 实际结果

`thermal2_n262144`：

| 指标 | 标准版 | demo | 变化 |
| --- | ---: | ---: | ---: |
| init kernel | 68.8579 ms | 66.3774 ms | -2.4805 ms |
| 1iter kernel | 188.7094 ms | 182.5644 ms | -6.1449 ms |
| init SpMV | 35.4271 ms | 35.1318 ms | -0.2953 ms |
| iter SpMV | 32.9576 ms | 27.1300 ms | -5.8276 ms |
| controller total, 1iter | 122.5991 ms | 121.4210 ms | -1.1781 ms |

实际收益符合“打到 iter SpMV”的预期，但整体 kernel 只快约 3.3%，说明总时间
仍主要受非 SpMV 路径影响。

## 未解决问题

- 完整 `thermal2` 仍失败，标准版和 demo 都是 `ctrl=0x0`。
- `stage-ms` 对 `dot_p_ap` 和向量更新阶段的拆分仍不够可信，多个字段接近
  1 cycle，不能解释 controller 总时间。
- demo 不适合作为“规模修复版”宣传，只能作为“小幅性能候选版”保留。

## 后续建议

1. 先补准非 SpMV 的 stage 计时，把 `dot_p_ap`、AP 写回、`x/r/z/p` 更新、
   `rz/rr` 归约和 service/drain 时间拆清楚。
2. 再决定优化 controller、service 化 SpMV 周边路径，还是优先查完整
   `thermal2` 的 direct-register `ctrl=0x0`。
3. 新 demo 测试时旧基线默认复用本目录和 HTML 中的数据；只需动态跑 demo
   与当前标准版。
