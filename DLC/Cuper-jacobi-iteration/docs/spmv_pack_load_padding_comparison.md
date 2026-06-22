# SpMV 16 路打包负载与 padding 对比

记录日期：2026-06-22

本文只看 host 打包后的数据量、padding 和 HBM 负载均衡，不评价上板运行时间。

## 口径

- 工具：`cuper-jacobi-iteration-build/pack_profile`
- 数据：完整 `A`，不是 Jacobi `R=A-D`
- 参数：`HBM=16`，`kPeNum=8`
- 数据集：`thermal2_n16384` 及以上
- `原格式 w10`：旧全 HBM 共享 batch 长度，window=10
- `strip10/14/16`：per-HBM strip 后的动态读 beat，window 分别为 10/14/16
- `lanereal`：当前 lane-static real/batch 打包口径，对应工具里的
  `lane_static_real_batch`

复现命令示例：

```bash
make cuper-jacobi-pack-profile
make cuper-jacobi-run-pack-profile \
  MATRIX=data/suitesparse/Schmid/csr/thermal2_n65536 \
  HBM=16 WINDOW=10 DROP_DIAG=0 CSV=1
```

如果要看 Jacobi 迭代里的 `R=A-D` 打包，去掉 `DROP_DIAG=0` 或显式使用
`DROP_DIAG=1`。

## 读 Beat

越小越好。每个 beat 是 512-bit，含 8 个 `SpElement` slot。

| 数据集 | nnz | 原格式 w10 beats | strip10 beats | strip14 beats | strip16 beats | lanereal beats |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `thermal2_n16384` | 107,908 | 16,960 | 16,128 | 17,468 | 17,283 | 14,062 |
| `thermal2_n65536` | 437,000 | 68,464 | 65,252 | 69,598 | 70,852 | 57,472 |
| `thermal2_n131072` | 866,060 | 137,152 | 130,317 | 138,975 | 142,375 | 114,874 |
| `thermal2_n262144` | 1,748,980 | 280,880 | 264,875 | 282,415 | 290,341 | 234,721 |
| `thermal2` | 8,580,313 | 1,373,440 | 1,292,261 | 1,373,472 | 1,414,739 | 1,151,370 |

## Beat 减少率

相对 `原格式 w10`。负数表示比原格式还多读。

| 数据集 | strip10 beat 减少 | strip14 beat 减少 | strip16 beat 减少 | lanereal beat 减少 |
| --- | ---: | ---: | ---: | ---: |
| `thermal2_n16384` | 4.91% | -3.00% | -1.90% | 17.09% |
| `thermal2_n65536` | 4.69% | -1.66% | -3.49% | 16.06% |
| `thermal2_n131072` | 4.98% | -1.33% | -3.81% | 16.24% |
| `thermal2_n262144` | 5.70% | -0.55% | -3.37% | 16.43% |
| `thermal2` | 5.91% | -0.00% | -3.01% | 16.17% |

## 有效密度

有效密度 = `nnz / read_slots`。越高表示 padding 越少。

| 数据集 | 原格式 w10 | strip10 | strip14 | strip16 | lanereal |
| --- | ---: | ---: | ---: | ---: | ---: |
| `thermal2_n16384` | 79.53% | 83.63% | 77.22% | 78.04% | 95.92% |
| `thermal2_n65536` | 79.79% | 83.71% | 78.49% | 77.10% | 95.05% |
| `thermal2_n131072` | 78.93% | 83.07% | 77.90% | 76.04% | 94.24% |
| `thermal2_n262144` | 77.83% | 82.54% | 77.41% | 75.30% | 93.14% |
| `thermal2` | 78.09% | 83.00% | 78.09% | 75.81% | 93.15% |

## Padding 比例

padding 比例 = `1 - 有效密度`。

| 数据集 | 原格式 w10 pad | strip10 pad | strip14 pad | strip16 pad | lanereal pad |
| --- | ---: | ---: | ---: | ---: | ---: |
| `thermal2_n16384` | 20.47% | 16.37% | 22.78% | 21.96% | 4.08% |
| `thermal2_n65536` | 20.21% | 16.29% | 21.51% | 22.90% | 4.95% |
| `thermal2_n131072` | 21.07% | 16.93% | 22.10% | 23.96% | 5.76% |
| `thermal2_n262144` | 22.17% | 17.46% | 22.59% | 24.70% | 6.86% |
| `thermal2` | 21.91% | 17.00% | 21.91% | 24.19% | 6.85% |

## HBM 负载均衡

`read eff = total_beats / (16 * max_hbm_beats)`。越接近 100%，越说明最慢 HBM
和平均 HBM 接近。

原格式的读长是全 HBM 强制对齐的，所以读长本身天然 100%；这里的
`HBM real nnz eff` 是真实非零元素按 HBM 分配后的均衡度。

| 数据集 | HBM real nnz eff | strip10 read eff | strip14 read eff | strip16 read eff | lanereal read eff |
| --- | ---: | ---: | ---: | ---: | ---: |
| `thermal2_n16384` | 99.28% | 95.73% | 95.85% | 95.85% | 98.20% |
| `thermal2_n65536` | 99.52% | 98.48% | 97.93% | 97.45% | 98.46% |
| `thermal2_n131072` | 99.72% | 98.55% | 98.73% | 98.54% | 98.47% |
| `thermal2_n262144` | 99.80% | 98.53% | 98.77% | 98.83% | 99.01% |
| `thermal2` | 99.93% | 99.50% | 99.49% | 99.57% | 99.77% |

## HBM 瓶颈

格式为 `ideal/max/extra`，单位是 beat。`extra = max_hbm_beats - ideal`。

| 数据集 | strip10 ideal/max/extra | strip14 ideal/max/extra | strip16 ideal/max/extra | lanereal ideal/max/extra |
| --- | ---: | ---: | ---: | ---: |
| `thermal2_n16384` | 1,008/1,053/45 | 1,092/1,139/47 | 1,081/1,127/46 | 879/895/16 |
| `thermal2_n65536` | 4,079/4,141/62 | 4,350/4,442/92 | 4,429/4,544/115 | 3,592/3,648/56 |
| `thermal2_n131072` | 8,145/8,265/120 | 8,686/8,798/112 | 8,899/9,030/131 | 7,180/7,291/111 |
| `thermal2_n262144` | 16,555/16,802/247 | 17,651/17,870/219 | 18,147/18,361/214 | 14,671/14,817/146 |
| `thermal2` | 80,767/81,175/408 | 85,842/86,282/440 | 88,422/88,800/378 | 71,961/72,128/167 |

## 结论

- 从纯打包角度，`lanereal` 最干净：大数据集读 beat 约少 16%，有效密度约
  93%-96%，HBM 读负载也更均衡。
- `strip10` 是保守收益：读 beat 约少 4.7%-5.9%，有效密度从约 78%-80% 提到
  82%-84%。
- `strip14` / `strip16` 虽然能帮助 accumulator HLS 达到更好的 II，但在完整 A
  的打包层面会引入更多 window padding；大多数大数据集读 beat 不降反升。
- 因此 window14/16 的上板性能没有按 II=1 提升，并不奇怪：打包端已经把 strip
  的读量收益吃掉了，后端还要承担更长的流。
