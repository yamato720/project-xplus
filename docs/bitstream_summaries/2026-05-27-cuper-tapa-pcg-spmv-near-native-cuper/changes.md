# 这一版改了什么

## 背景

当前 `cuper-tapa-pcg` full-PCG 版虽然复用了 TAPA Cuper 的 16 路 SpMV task graph，
但之前 controller 仍在 SpMV 热路径里做了两类串行化工作：

- 输入侧：从 `double X/P` 逐元素读取并临时打包成 `float_v16`；
- 输出侧：把 Cuper 产出的 `float_v16 AP` 再拆成 16 个 double 写入旧 `AP` HBM。

这会让 full-PCG 内嵌 SpMV 明显慢于 standalone `cuper-tapa-spmv`，也是当前优化
目标优先处理的周边路径。

当前后续方向再次调整：2026-05-29 one-shot `CuperPcgSpmv` single SpMV demo 已经
接近满血 `Cuper(...)` 并跑通完整 `thermal2`，后续不再把 single SpMV 本体作为主
优化目标。single SpMV demo 继续作为回归基线；full-PCG 性能优化转向
controller/dot/update 路径。

2026-05-29 已额外生成一个当前源码下的 full-PCG `CuperPcg` demo bitstream，
放入 `395bitstream/` 的第二个 demo 槽。它用于确认 full-PCG 路径仍可完成
`hw` 构建；同日已完成 demo-only 上板测试。结果显示完整 `thermal2` 的
init/1iter 仍能返回，但共同成功点的 1iter 性能没有优于标准版或上一 demo，
因此不改变下面 2026-05-27 packed feed/AP demo 的“性能未达标”结论。

2026-05-31 又按“把 controller 内部循环 II 尽量拉满”的实验方向生成了新的
full-PCG `CuperPcg` demo bitstream，并覆盖 `395bitstream/` 的同名 full-PCG
demo 槽。这个新文件已完成 demo-only 上板测试；2026-05-29 的 demo-only 测试结论
只作为历史记录保留，不再对应当前同名 `.xclbin`。

代码里仍要明确区分 single SpMV 基线和 full-PCG 性能路径：

| 形态 | 入口/文件 | 作用 |
| --- | --- | --- |
| 满血 Cuper SpMV | `Cuper(...)` / `detail/cuper_spmv_tasks.hpp` | 当前 standalone TAPA Cuper SpMV 标准基准 |
| full-PCG controller/update | `CuperPcg(...)` / `detail/pcg_controller.hpp` + `detail/pcg_spmv_service.hpp` | 当前性能优化对象，重点是 `dot_p_ap`、`update_xr`、`update_p` 和 service/timer 开销 |

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

### 2026-05-31 II=1 controller 实验

本轮只改 `detail/pcg_controller.hpp` 中 controller 自身的 lane/reduction
流水目标和手工 tick 估计：

- `init_r_lanes`
- `init_zp_lanes`
- `dot_p_ap_lanes`
- `update_xr_lanes`
- `update_z_reduce`
- `update_p_lanes`

这些 loop 的 `#pragma HLS pipeline` 目标被压到 `II=1`，对应手工 ticks 从原先
`Row_num * 4` 或 `Row_num * 2` 改成 `Row_num * 1`。这只是实验性目标值；
HLS 实际调度没有全部达到 II=1。

## 预期影响

当前后续预期改善：

- 降低 `1iter kernel_reported`；
- 降低 `controller_total`；
- 降低 `dot_p_ap`、`update_xr`、`update_p` 这几个大头阶段；
- 保持 `AP path = iter recv + dot_p_ap` 不恶化；
- 保持完整 `thermal2` 可返回和数值 diff 通过。

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
2. xclbin 已以 `-demo` 后缀放入 `395bitstream/`，当时覆盖 full-PCG demo 槽位。
3. `.xclbin.info`、UUID、SHA256、DATA/HBM clock 已记录到 `testing.md`。
4. 2026-05-28 按用户要求完成 demo-only 上板测试，并更新
   `testing.md` 与 `395bitstream/cuper_spmv_u55c_compare_20260524.html`。
5. 2026-05-29 同批源码下的 full-PCG demo 构建成功，并同步为
   `395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin`：
   UUID `086a3345-ddf0-ffdd-b260-16ca5fa5223a`，SHA256
   `83baded1910ecb2c9e662f9ff6920fd8a55dbd2898ae69629c862714e17cf7f1`，
   DATA/KERNEL/HBM clock `210/500/408 MHz`。
6. 2026-05-29 当前 full-PCG demo 已完成 demo-only 上板测试：
   `thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、
   `thermal2_n262144` 和完整 `thermal2` 的 init-only / 1iter 全部返回；
   但 `thermal2_n262144` 的 1iter `kernel_reported=426.7009 ms`，仍慢于
   标准版 `188.8202 ms` 和上一 demo `416.6492 ms`。
7. 该 2026-05-29 full-PCG demo 旧 UUID 未晋级标准版；2026-05-31 同名
   `395bitstream/` demo 槽已被新的 II=1 controller 实验构建覆盖。
8. 2026-05-31 II=1 controller 实验构建成功，并覆盖
   `395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin`：
   UUID `0170fa86-6e62-cfc9-aa66-2d330dd72cf2`，SHA256
   `ec3a98b09d662611ce50c4c484cb6b55ad2e7dbcd712a0b6d7833b38e4579fc8`，
   DATA/KERNEL/HBM clock `223/500/444 MHz`。
9. 2026-05-31 II=1 controller 实验 demo-only 上板测试完成：
   `thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、
   `thermal2_n262144` 和完整 `thermal2` 的 init-only / 1iter 全部返回。
   完整 `thermal2` 1iter `kernel_reported=1767.8254 ms`，比 2026-05-29
   旧 UUID 的 `1960.0357 ms` 改善；但 `thermal2_n262144` 1iter
   `385.1288 ms` 仍明显慢于标准版 `188.8202 ms` 和上一 demo
   `182.5644 ms`。

仍需完成：

1. 以 2026-05-29 one-shot single SpMV demo 作为回归基线，避免后续 full-PCG 改动
   破坏 SpMV 成功边界和 diff。
2. 直接分析 full `CuperPcg(...)` 的 `dot_p_ap`、`update_xr`、`update_p` 大规模
   退化，不再用 single SpMV 本体解释 full-PCG 1iter 倒挂。
3. 继续分析当前 II=1 demo 里 `update_xr`、`update_p`、`dot_p_ap` 的大规模瓶颈；
   只有后续实测证明共同成功点接近或优于标准版，才更新正式 `source.diff`。
