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

随后又生成了 controller-split 实验 demo，并再次覆盖同名 full-PCG demo 槽。该
新文件已完成软件级验证、XO/HLS、完整 `hw` bitstream 构建和 demo-only 上板测试；
旧 II=1 demo 的测试结论只作为历史对照，不能套用到当前 UUID。2026-05-31 又补跑
一组当前 UUID 的 full-run 完整 PCG，不传 `MAX_ITERS=1`，并用
`KERNEL_TIMEOUT_SEC=0` 禁用 host 默认 60 秒超时；该组已确认到
`thermal2_n262144` 多轮收敛。随后该 controller-split demo 已按用户要求作为新存档点
移入 `bitstream_archive/2026-05-31-tapa-pcg-controller-split-demo/`，不再保留在
`395bitstream/` 同步目录。

2026-05-31 又在 `0940e6c` 的 packed timing 版本上生成新的 full-PCG
`CuperPcg` demo bitstream，当前同步槽为
`395bitstream/cuper-tapa-pcg-fpga-u55c-20260531-demo.xclbin`。这版把 PCG 主状态改为
packed `double_v8`，并把 `Metrics[5..15]` 明确为 packed memory packet work，
分段实测继续看 `[stage-cycles]` / `[stage-ms]`。它已完成 demo-only init-only 与
1iter 上板测试；完整 `thermal2` 1iter 比归档 controller-split demo 略快，但共同成功点
仍未超过标准版。

代码里仍要明确区分 single SpMV 基线和 full-PCG 性能路径：

| 形态 | 入口/文件 | 作用 |
| --- | --- | --- |
| 满血 Cuper SpMV | `Cuper(...)` / `detail/cuper_spmv_tasks.hpp` | 当前 standalone TAPA Cuper SpMV 标准基准 |
| full-PCG controller/update | `CuperPcg(...)` / `detail/pcg_controller.hpp` + `detail/pcg_spmv_service.hpp` | 当前性能优化对象，重点是 fused `iter_spmv_recv_dot`、`init_zp/update_rz_reduce` reduction、`update_x/update_p` 和 service/timer 开销 |

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
- 早期 packed feed/AP 版中，`dot_p_ap` 和 `update_xr` 改成按包读取
  `AP_spmv`，lane 内转 double 参与 FP64 计算。
- 当前 packed timing 版中，`p^T AP` 已合入 `iter_spmv_stream` 接收路径；
  `AP_spmv` 仍写入 HBM，供 `update_xr` 读取。
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

### 2026-05-31 controller-split 实验

本轮继续只改 `detail/pcg_controller.hpp`，目标是缓解上一版
`update_xr_lanes` / `update_p_lanes` 的大 II：

- `dot_p_ap` 合入 `iter_spmv_stream`，controller 接收 `A*p` 时同步计算
  `p^T AP`，避免后续再完整扫描一遍 `P` 和 `AP_spmv`。
- `update_xr` 拆成 `update_xr_compute_lanes` 与 `update_xr_store_lanes`，
  用本地 fully partitioned lane 数组隔离 FP64 计算和 HBM store。
- `update_p` 拆成 `update_p_compute_lanes` 与 `update_p_store_lanes`，再单独
  pack `P_spmv`。
- `init_r` / `init_zp` / `iter_spmv` / `update_xr` 内的 `float_v16` lane 访问先
  unpack 到本地数组，避免直接在重计算路径上反复访问 packed 类型。

HLS 报告显示：

| loop | 新结果 |
| --- | ---: |
| `update_xr_compute_lanes` | II=1 |
| `update_xr_store_lanes` | II=1 |
| `update_p_compute_lanes` | II=1 |
| `update_p_store_lanes` | II=1 |
| `iter_dot_p_ap_lanes` | II=5 |
| `update_z_reduce` | II=5 |

`Pcg_Controller` 顶层 HLS 估计 latency 从上一版约 `451580129243` 降到
`173580117235`，LUT 从 `72458` 降到 `51280`。这只是 HLS 层面的改善，是否转化
为板上收益需要当前 UUID 的 demo-only 测试确认。

### 2026-05-31 packed timing 实验

本轮继续围绕 `detail/pcg_controller.hpp` 和 host metrics 口径调整：

- `B/M_inv/X/R/Z/P` 主状态从标量 `double` mmap 改为 512-bit packed
  `double_v8` mmap，降低 controller 对 FP64 状态的逐元素 HBM 访问压力。
- `init_spmv_recv_r`、`init_zp_reduce_pack`、`update_xr`、`update_z_reduce`、
  `update_p_pack` 等阶段按 double-v8 packet 读写，`X` 最终仍作为 FP64 状态写回。
- `Metrics[5..15]` 改为 packed memory packet work，避免把它误认为 cycle；
  `[stage-cycles]` 和 `[stage-ms]` 才是分段实测时间。
- host 的 `MAX_ITERS=0` 语义改为 `effective_max_iters=max(4*N,1000)`，
  不再代表 init-only。init-only 测试仍使用
  `TAU=1e100 MAX_ITERS=1 DIFF_TOL=1e-1`。

这版生成的 bitstream UUID 为 `f5b4fb4b-d7cc-f559-b5ba-29e2e6a88668`，
DATA/KERNEL/HBM clock 为 `172/500/405 MHz`。它比归档 controller-split demo
频率低，但完整 `thermal2` 1iter 仍从 `954.0779 ms` 降到 `944.1232 ms`。

完整 `thermal2` 1iter 的关键拆分为：

| 项 | ms | 占 controller |
| --- | ---: | ---: |
| `controller_total` | 920.2593 | 100.0% |
| `init_spmv + iter_spmv_recv_dot` | 189.3382 | 20.6% |
| `pcg_vector_total` | 730.9200 | 79.4% |
| `init_zp` | 197.2362 | 21.4% |
| `update_xr` | 211.3258 | 23.0% |
| `update_z` | 169.0716 | 18.4% |
| `update_p` | 153.2865 | 16.7% |
| `kernel_minus_controller` | 23.8639 | 不属于 controller |

因此当前结论是：端口宽度已经从“每次一个 double”改成 512-bit packed
`double_v8`，但有效瓶颈没有转成 raw SpMV 本体。剩余大头仍是 controller 内阶段
串行、FP64 reduction recurrence、HBM 往返和 lane-wise compute/store 组合。

### 2026-05-31 main reduction 实验

本轮在 `main` 上继续做一个较小范围的 controller 修改，`2.1` 只作为存档/对照分支：

- 给 `init_zp` 和 `update_z` 的 `rz/rr` FP64 reduction 增加
  `kPcgReductionBanks=8` 的分银行累加器；
- packet loop 改成按 lane unroll、按 bank 累加，loop 尾部再归约到标量；
- 不保留更激进的 `update_xr/update_p` 外层全展开/外层 pipeline 实验，因为此前
  HLS 报告显示它会把 `iter_spmv_stream/update_xr/update_p` 拉差；
- 试过给 `p^T AP` 也做同样的 banked reduction，但 `iter_spmv_stream` 会变成
  II=11，接收 AP 流本身被拖慢，因此撤回。

当前保留版本的软件仿真通过 `thermal2_n16` 和 `thermal2_n1024`
`MAX_ITERS=1 TAU=1e-100 DIFF_TOL=1e-3`。XO/HLS 报告入口：
`cuper-tapa-pcg-main-reduction-xo-build/hw_emu/tapa_CuperPcg/report/Pcg_Controller/csynth.rpt`。

关键 HLS 变化：

| loop | packed timing 基线 | 当前 main reduction |
| --- | ---: | ---: |
| `init_zp` packet loop | II=5 | II=4 |
| `update_z` packet loop | II=5 | II=2 |
| `iter_dot_p_ap_lanes` | II=5 | II=5 |
| `update_xr_compute_lanes` | II=1 | II=1 |
| `update_p_compute_lanes` | II=1 | II=1 |

结论：这个 patch 是保守的 reduction 局部改善，不是全局控制器拆分。下一步如果继续
提速，主力仍应放在把 `update_xr/update_p` 从单 controller 串行阶段里拆出去，或把
AP stream 直接喂给后续 dot/update 流程，减少 `AP_spmv` 写回再读。

### 2026-07-05 Callipepla 式 update 语义拆分

本轮只改 full `CuperPcg(...)` 实际使用的 `detail/pcg_controller.hpp` 和
host metrics 标签，不改 `Cuper(...)`、`CuperPcgSpmv(...)` single-SpMV demo、
顶层 ABI、connectivity 或 Cuper 矩阵格式。

核心变化：

- 保留 `iter_spmv_stream`：继续接收 `A*p`、写 `AP_spmv`，并融合计算
  `p_ap = p^T AP`；
- 原 `update_xr` 改为 `update_x`，只读 `X/P` 并写回 `X`；
- 原 `update_z_reduce` 改为 `update_rz_reduce`，读取旧 `R`、`AP_spmv` 和
  `M_inv`，同时生成新 `R`、新 `Z` 和 `rz_new/rr_new`；
- `AP_spmv` 仍是必要 HBM 断点，因为 `alpha` 只有在 `p^T AP` 全量归约后才知道，
  不能直接把 SpMV stream 旁路给 residual update；
- `update_p` 保持当前读 `Z/P`、写 `P/P_spmv` 的结构，暂不做跨迭代 `P` stream
  旁路。

工作量口径随语义调整：

| Metrics | 旧标签 | 新标签 | 新含义 |
| --- | --- | --- | --- |
| `[8]` / `[20]` | `update_xr` | `update_x` | `X/P` 读和 `X` 写 |
| `[9]` / `[21]` | `update_z_reduce` | `update_rz_reduce` | `AP_spmv/R/M_inv` 读和 `R/Z` 写，含 `rz/rr` 归约 |

`pcg_vector_total` 仍统计 `init_zp + update_x + update_rz_reduce + update_p`。
这轮源码改动尚未生成新 demo bitstream，也不更新正式 `source.diff`。

### 2026-07-07 PCG 向量阶段 worker 拆分

本轮继续只改 full `CuperPcg(...)` 实际使用的 PCG 内部 task graph，不改顶层
ABI、HBM connectivity、Cuper SpMV matrix/vector 数据格式、`Cuper(...)` 或
`CuperPcgSpmv(...)`。

核心变化：

- 新增 `detail/pcg_vector_phases.hpp`，定义 `PcgVectorCommand`、
  `PcgVectorResult` 和常驻 `Pcg_Vector_Phases` task。
- `Pcg_Controller` 移除大段 HBM 向量 mmap 端口和 `Spmv_in`，改为只连接
  `Vector_Command_Stream` / `Vector_Result_Stream`。
- `Pcg_Vector_Phases` 持有 `Spmv_in` 和 `B/M_inv/X/R/Z/P/AP_spmv/P_spmv` mmap，
  接管 `init_spmv`、`init_zp`、`iter_dot`、`update_x`、
  `update_rz_reduce` 和 `update_p`。
- controller 现在只负责 SpMV command、vector command/result 边界、alpha/beta、
  convergence/breakdown、stage timer 和 Metrics/Status 写回，更接近 Callipepla
  的标量调度器。
- `AP_spmv` 仍是 HBM 断点，因为 `alpha` 必须等 `p^T AP` 全量归约后才知道；
  `P_spmv` 也仍保留为下一轮 SpMV 输入，本轮不改 Cuper vector loader 流控。

Metrics/host 口径保持兼容：

| Metrics | 当前含义 |
| --- | --- |
| `[5..15]` | packed memory packet work / packet 数，不是实测 cycle |
| `[16]` | `init_spmv` stage cycle，边界包住 vector phase command/result |
| `[17]` | `init_zp` stage cycle |
| `[18]` | `iter_spmv_recv_dot` stage cycle，仍包含 `p^T AP` |
| `[20]` | `update_x` stage cycle |
| `[21]` | `update_rz_reduce` stage cycle |
| `[22]` | `update_p` stage cycle |
| `[23]` | `controller_total` |

这轮已生成新的 `cuper-tapa-pcg` demo xclbin：

```text
395bitstream/cuper-tapa-pcg-fpga-u55c-20260707-demo.xclbin
UUID: 1de9a25a-0257-8c9d-e39d-a470554d0f20
SHA256: 4b2ab1b8b10b27917947b044511da73812ddf688145719146780d21ad60baf25
INFO SHA256: fb4f0c8c09eb43c0738f420bc0c35a1c4f4a1f63b308ea6577b458b2ffbcb9a1
DATA/KERNEL/HBM: 228/500/422 MHz
Routed timing: WNS -1.043 ns, TNS -24489.869 ns
```

该 artifact 尚未上板，且 timing 未收敛，因此仍不更新正式 `source.diff`。

## 预期影响

当前后续预期改善：

- 降低 `1iter kernel_reported`；
- 降低 `controller_total`；
- 降低 `iter_spmv_recv_dot`、`init_zp/update_rz_reduce`、`update_x/update_p`
  这几个大头阶段；
- 保持 `iter recv + dot` 不恶化；
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
10. 2026-05-31 controller-split 实验构建成功，并覆盖
    `395bitstream/cuper-tapa-pcg-fpga-u55c-20260529-demo.xclbin`：
    UUID `1d536c39-f561-340b-7efc-ac2c8440543d`，SHA256
    `bc58605b36c98b29d84ce14939b95f8fc6b84bb7a505007fda95458545a349b8`，
    DATA/KERNEL/HBM clock `211/500/450 MHz`。
11. 2026-05-31 controller-split 实验 demo-only 上板测试完成：
    `thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、
    `thermal2_n262144` 和完整 `thermal2` 的 init-only / 1iter 全部返回。
    完整 `thermal2` 1iter `kernel_reported=954.0779 ms`，比上一 II=1 demo 的
    `1767.8254 ms` 明显改善；`thermal2_n262144` 1iter `211.3790 ms`，
    仍略慢于标准版 `188.8202 ms` 和上一 demo `182.5644 ms`。本版
    `dot_p_ap` 已合入 `iter_spmv` 计时，分段数据不能和旧 `dot_p_ap` 独立阶段
    直接逐列比较。
12. 2026-05-31 controller-split 实验 full-run 完整 PCG 补测完成到
    `thermal2_n262144`：`thermal2_n16` 到 `thermal2_n262144` 分别跑到
    `1/60/81/96/104/113/120` 次并收敛；`thermal2_n262144`
    `kernel_reported=15263.805830 ms`，明显快于 2026-05-29 旧 demo 的
    `39491.638 ms`，接近 TAPA 标准版旧记录 `14418.306 ms`。完整 `thermal2`
    禁用 host 60 秒超时后约 490 秒仍为 `ctrl=0x0`，按用户要求停止，记录为未完成。
13. 2026-05-31 已把 controller-split demo 作为新存档点移入
    `bitstream_archive/2026-05-31-tapa-pcg-controller-split-demo/`；`.xclbin` 只做本地
    留档，`.xclbin.info` 和 README 记录归档信息。
14. 2026-05-31 packed timing 实验构建成功，并同步为
    `395bitstream/cuper-tapa-pcg-fpga-u55c-20260531-demo.xclbin`：
    UUID `f5b4fb4b-d7cc-f559-b5ba-29e2e6a88668`，SHA256
    `a8df40e1bf21774c7608c329fd591012b84744a18dcf4e8b0dd36672d64ccf72`，
    DATA/KERNEL/HBM clock `172/500/405 MHz`。
15. 2026-05-31 packed timing demo-only 上板测试完成：
    `thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、
    `thermal2_n262144` 和完整 `thermal2` 的 init-only / 1iter 全部返回。
    完整 `thermal2` 1iter `kernel_reported=944.1232 ms`，比归档
    controller-split demo 的 `954.0779 ms` 略快；`thermal2_n262144` 1iter
    `210.3193 ms`，仍慢于标准版 `188.8202 ms` 和上一 demo `182.5644 ms`。
    本版不更新正式 `source.diff`。
16. 2026-07-07 vector phase worker 拆分完成软件级验证：
    `make cuper-tapa-pcg-fpga-host`、`make cuper-tapa-pcg-host`、`n512 MAX_ITERS=1`
    和 `thermal2_n16 MAX_ITERS=1` local smoke 均通过；`sw_emu` link 因 XO target
    只支持 `hw_emu/hw` 而失败。
17. 2026-07-07 `make cuper-tapa-pcg-hw-tmux` 完整 `hw` 构建成功，并同步为
    `395bitstream/cuper-tapa-pcg-fpga-u55c-20260707-demo.xclbin`。该 demo
    `impl Complete`，但 routed timing 未收敛，尚未上板，不更新正式 `source.diff`。

仍需完成：

1. 以 2026-05-29 one-shot single SpMV demo 作为回归基线，避免后续 full-PCG 改动
   破坏 SpMV 成功边界和 diff。
2. 直接分析 full `CuperPcg(...)` 的 `iter_spmv_recv_dot`、
   `init_zp/update_rz_reduce`、`update_x/update_p` 大规模退化，不再用 single SpMV
   本体解释 full-PCG 1iter 倒挂。
3. 对当前 2026-07-07 demo 补跑 full-PCG demo-only 上板测试，至少覆盖
   `thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、
   `thermal2_n262144` 和完整 `thermal2` 的 init-only / `MAX_ITERS=1`。
4. 继续分析当前 controller/vector worker 路径里 FP64 reduction recurrence、stage
   串行化、task 同步和 packed FP64 HBM 访问的大规模瓶颈；
   只有后续实测证明共同成功点接近或优于标准版，才更新正式 `source.diff`。
5. 如后续仍要验证完整 `thermal2` full-run，需要按长跑任务单独安排，不再用 host
   默认 60 秒超时判断；本轮只确认禁用 host 超时后长时间仍未返回。
