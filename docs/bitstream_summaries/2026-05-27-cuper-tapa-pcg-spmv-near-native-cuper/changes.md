# 这一版改了什么

## 背景

当前 `cuper-tapa-pcg` full-PCG 版虽然复用了 TAPA Cuper 的 16 路 SpMV task graph，
但之前 controller 仍在 SpMV 热路径里做了两类串行化工作：

- 输入侧：从 `double X/P` 逐元素读取并临时打包成 `float_v16`；
- 输出侧：把 Cuper 产出的 `float_v16 AP` 再拆成 16 个 double 写入旧 `AP` HBM。

这会让 full-PCG 内嵌 SpMV 明显慢于 standalone `cuper-tapa-spmv`，也是当前优化
目标优先处理的周边路径。

## 代码改动

### packed 输入向量

- `CuperPcg` 顶层新增 `X_spmv` 和 `P_spmv` 两个 memory port。
- `CuperSpmvCommand` 新增 `vector_source` 字段，用来区分本次 SpMV 读取 `x0`
  还是读取当前搜索方向 `p`。
- `Pcg_Vector_Loader` 直接从 packed HBM 读 `float_v16`，不再消费 controller
  写出的临时 `Pcg_X_Stream`。
- `Pcg_Controller` 在 `init_zp` 和 `update_p` 阶段同步维护 `P_spmv`。
- host 在启动新 ABI 时预先把 `x0` 打包到 `X_spmv`，并为 `P_spmv` 准备 packed
  初始缓冲。

### packed AP 输出

- `CuperPcg` 顶层把旧 `double* AP` 替换为 `float_v16* AP_spmv`。
- `iter_spmv` 收到 `Spmv_in` 后直接写 `AP_spmv[packet]`。
- `dot_p_ap` 和 `update_xr` 改成按包读取 `AP_spmv`，lane 内转 double 参与
  FP64 计算。
- connectivity 中 `CuperPcg_1.AP` 改为 `CuperPcg_1.AP_spmv`，仍放在 HBM[22]。

### host ABI

- 新 ABI memory arg 数量从 26 变为 28：
  - `AP_spmv`: HBM[22]
  - `X_spmv`: HBM[24]
  - `P_spmv`: HBM[25]
  - `Metrics/Status`: HBM[26]
- direct register offset 已同步后移。
- `--legacy-abi` / `LEGACY_ABI=1` 保留旧 26-arg 布局，用于跑
  `cuper-tapa-pcg-fpga-u55c-20260525.xclbin` 等旧标准 bitstream。

## 预期影响

预期改善：

- 降低 `iter_spmv` 的 AP 回收和 P 向量 feed 开销；
- 减少 controller 在 SpMV 热路径上的逐元素 HBM 访问；
- 让 full-PCG 内嵌 SpMV 更接近 standalone TAPA Cuper 的 `float_v16` 数据粒度。

潜在代价：

- `Pcg_Controller` 额外维护 `P_spmv` 和读取 `AP_spmv`，资源略涨；
- 新 ABI 与旧 xclbin 不兼容，必须用 `--legacy-abi` 跑旧 bitstream；
- full-PCG 总时间仍可能受 FP64 dot/reduction、向量更新和 task graph 同步限制。

## TAPA/HLS 资源快照

`cuper-tapa-pcg-packed-ap-build/hw_emu/tapa_CuperPcg/report.json`：

| 指标 | 数值 |
| --- | ---: |
| HLS clock period | 4.382 ns |
| Total LUT | 164,299 |
| Total FF | 226,454 |
| Total DSP | 907 |
| Total BRAM_18K | 1,094 |
| Total URAM | 512 |
| Pcg_Controller LUT | 37,381 |
| Pcg_Controller FF | 41,742 |
| Pcg_Controller BRAM_18K | 70 |

资源相比前一个软件生成报告有上涨，主要来自 packed 缓冲路径和 controller 内
按包处理 AP/P 的额外逻辑。最终是否可接受要以 `hw` impl 和板上动态数据为准。

## 后续动作

已完成：

1. tmux 中 `hw` 构建成功结束。
2. xclbin 已以 `-demo` 后缀放入 `395bitstream/`，覆盖当前 demo 槽位。
3. `.xclbin.info`、UUID、SHA256、DATA/HBM clock 已记录到 `testing.md`。

仍需完成：

1. 按 `testing.md` 对比当前标准版并更新本目录 `testing.md`。
2. 同步更新 `395bitstream/cuper_spmv_u55c_compare_20260524.html` 或新的 HTML 报告。
