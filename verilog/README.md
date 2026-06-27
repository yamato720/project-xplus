# Cuper SpMV RTL 文件索引

这个目录保存 `DLC/Cuper-jacobi-iteration` 的 SpMV-only RTL 实验文件。先按
编译宏区分，不要只看文件名里的 `Scoreboard` 或 `OwnerBank`。

## 先看这里

| 你要找的东西 | 编译宏 | 主要文件 | 接线位置 |
| --- | --- | --- | --- |
| 全 RTL owner-bank 后端 | `JACOBI_SPMV_OOO_ACCUMULATE_RTL=1` | `tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v` | `cuper_spmv_service_only_top_graphs.hpp` 里的 `CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_BANK_ACC` |
| 单记分板 RTL 调度器 | `JACOBI_SPMV_OOO_SCOREBOARD_RTL=1` | `tapa/CuperSpmvOnly_RtlOwnerScoreboardOoo.v` | `cuper_spmv_service_only_top_graphs.hpp` 里的 `CUPER_SPMV_ONLY_INVOKE_RTL_OWNER_SCOREBOARD` |
| 8-lane 记分板 primitive | 被单记分板调度器 include | `tapa/CuperSpmvOnly_RtlIssueScoreboard8.v` | `CuperSpmvOnly_RtlOwnerScoreboardOoo.v` 内部 |
| owner-lane 累加器 | 被全后端 include | `tapa/CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo.v` | `CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v` 内部 |
| scatter 仿真模型 | 只用于本目录仿真 | `tapa/CuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel.v` | 不 hotpatch 到 XO |

两个 RTL 分支互斥。`build_xo_u55c.sh` 会拒绝同时打开
`JACOBI_SPMV_OOO_ACCUMULATE_RTL` 和 `JACOBI_SPMV_OOO_SCOREBOARD_RTL`。

## 两条 RTL 分支

### 全 RTL owner-bank 后端

宏：

```bash
JACOBI_TOP=CuperSpmvServiceOnly
JACOBI_SPMV_ONLY=1
JACOBI_SPMV_LANE_STATIC_REAL=1
JACOBI_SPMV_OOO_ACCUMULATE=1
JACOBI_SPMV_OOO_ACCUMULATE_RTL=1
```

数据路：

```text
SourceLaneSplitterOoo[source]
  -> Owner_Lane_Stream[owner][pair_lane]
  -> RtlOwnerBankAccumulatorOoo[owner]
  -> Vector_Y_Tagged_Stream[owner]
  -> TaggedScatterWriterOoo
```

这里的“全后端”指 owner-bank accumulator 这一段由 RTL 接管：8 路 owner-lane
输入、lane-local RAW scoreboard、URAM partial sum、FP32 fadd pipeline 和 bank 内
输出仲裁都在 RTL 里。`TaggedScatterWriterOoo` 仍是 HLS/TAPA 生成路径。

hotpatch 到 TAPA work dir 的文件：

```text
tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v
tapa/CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo.v
tapa/CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo_support.vh
tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1.v
tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1_ip.tcl
```

快速验证入口：

```bash
make -C verilog tapa-bank-sim
make -C verilog tapa-bank-scatter-cpp-sim
make -C verilog tapa-splitter-bank-scatter-cpp-sim
make -C verilog tapa-splitter16-bank16-dataset-cpp-sim
```

### 单记分板 RTL 调度器

宏：

```bash
JACOBI_TOP=CuperSpmvServiceOnly
JACOBI_SPMV_ONLY=1
JACOBI_SPMV_LANE_STATIC_REAL=1
JACOBI_SPMV_OOO_ACCUMULATE=1
JACOBI_SPMV_OOO_SCOREBOARD_RTL=1
```

数据路：

```text
SourceLaneSplitterOoo[source]
  -> Owner_Lane_Stream[owner][pair_lane]
  -> RtlOwnerScoreboardOoo[owner]
  -> Scheduled_Owner_Stream[owner]
  -> OwnerAccumulatorScheduledOoo[owner]
  -> Vector_Y_Tagged_Stream[owner]
  -> TaggedScatterWriterOoo
```

这条是弱 RTL 分支：RTL 只做 8 路 FIFO head 选择和 RAW hazard scoreboard。
FP32 加法、URAM partial sum 和最终 tagged 输出仍在 HLS
`OwnerAccumulatorScheduledOoo` 里。

hotpatch 到 TAPA work dir 的文件：

```text
tapa/CuperSpmvOnly_RtlOwnerScoreboardOoo.v
tapa/CuperSpmvOnly_RtlIssueScoreboard8.v
```

`JACOBI_SPMV_SCOREBOARD_DEBUG=1` 只适用于这条分支，并且要求
`JACOBI_SPMV_OOO_SCOREBOARD_RTL=1`。

快速验证入口：

```bash
make -C verilog tapa-vector-scoreboard-sim
make -C verilog tapa-scoreboard-dataset-cpp-sim
```

## 目录分组

### `tapa/`

这些文件面向 TAPA custom RTL 或配套仿真。

| 文件 | 角色 | 是否 hotpatch 到 XO |
| --- | --- | --- |
| `CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v` | 全 RTL owner-bank wrapper，实例化 8 个 lane accumulator 并仲裁输出 | 是，仅全后端分支 |
| `CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo.v` | 单 lane RTL accumulator，含 RAW scoreboard、URAM、FP32 fadd pipeline | 是，仅全后端分支 |
| `CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo_support.vh` | 给非 Verilator/TAPA pack 用的 include 边界 | 是，仅全后端分支 |
| `CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1.v` | 全后端 FP32 fadd wrapper | 是，仅全后端分支 |
| `CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1_ip.tcl` | 生成 Vivado floating_point IP | 是，仅全后端分支 |
| `CuperSpmvOnly_RtlOwnerScoreboardOoo.v` | 单记分板 wrapper，8 lane 输入到 scheduled vector 输出 | 是，仅单记分板分支 |
| `CuperSpmvOnly_RtlIssueScoreboard8.v` | 单记分板 primitive，决定每拍哪些 lane 可 issue | 是，仅单记分板分支 |
| `CuperSpmvOnly_RtlOwnerLanePassThrough.v` | 早期 custom RTL handshake smoke，不是当前主分支 | 否 |
| `CuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel.v` | scatter pipeline 仿真模型，替代生成 RTL 做本地验证 | 否 |
| `CuperSpmvOnly_CoreStrip_fmul_32ns_32ns_32_8_max_dsp_1_ip_sim.v` | CoreStrip xsim 用 fmul 仿真 wrapper | 否 |

### `vsrc/`

SystemVerilog testbench 和小型模型。

| 文件 | 用途 |
| --- | --- |
| `tb_tapa_owner_lane_accumulator.sv` | 单 lane RTL accumulator testbench |
| `tb_tapa_owner_bank_accumulator.sv` | 全 RTL owner-bank testbench |
| `tb_tapa_owner_bank_to_scatter.sv` | owner-bank 到 scatter 的联调 testbench |
| `tb_tapa_vector_issue_scoreboard8.sv` | 8-lane issue scoreboard testbench |
| `tb_tapa_backend_dataset_xsim.sv` | Vivado xsim 后端数据集 testbench |
| `tb_tapa_corestrip_xsim.sv` | CoreStrip xsim testbench |
| `tb_tapa_scatter_pipeline.sv` | scatter pipeline 单元 testbench |
| `ooo_accumulator.sv` / `tb_ooo_accumulator.sv` | 更早的独立 OOO accumulator 原型 |
| `axi_mem_model.sv` | top/backend xsim 使用的 AXI memory model |

### `csrc/`

C/C++ 数据生成器和 Verilator 驱动。

| 文件 | 用途 |
| --- | --- |
| `gen_tapa_backend_xsim_vectors.c` | 生成 backend xsim 数据 |
| `gen_tapa_top_xsim_vectors.cpp` | 生成 CuperSpmvServiceOnly top xsim 数据 |
| `gen_tapa_top_xsim_tb.py` | 从生成 RTL 自动生成 top xsim testbench |
| `sim_tapa_scoreboard_dataset.cpp` | 用真实矩阵数据驱动 `RtlIssueScoreboard8` |
| `sim_tapa_splitter16_bank16_dataset.cpp` | 16 splitter + 16 bank + scatter 数据集仿真 |
| `sim_tapa_splitter_bank_scatter.cpp` | splitter/bank/scatter 小联调 |
| `sim_tapa_bank_to_scatter.cpp` | bank/scatter 小联调 |
| `sim_tapa_scatter_pipeline.cpp` | scatter pipeline 仿真 |
| `sim_ooo_accumulator.cpp` / `gen_ooo_accum_vectors.c` | 早期 OOO accumulator 原型 |

### `scripts/`

Vivado xsim tcl 脚本。

| 文件 | 用途 |
| --- | --- |
| `run_tapa_corestrip_xsim.tcl` | 单独跑 generated `CuperSpmvOnly_CoreStrip` |
| `run_tapa_backend_dataset_xsim.tcl` | 跑 backend 数据集 xsim |
| `run_cuper_spmv_service_only_top_xsim.tcl` | 跑完整 `CuperSpmvServiceOnly` top xsim |

## XO hotpatch 位置

`DLC/Cuper-jacobi-iteration/scripts/build_xo_u55c.sh` 在打开任一 RTL 宏时，不走
单步 `tapa compile`，而是执行：

```text
tapa analyze
tapa synth
复制 verilog/tapa 里的 custom RTL 到 $WORK_DIR/hdl
tapa pack
```

全后端分支替换 `CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v`；单记分板分支替换
`CuperSpmvOnly_RtlOwnerScoreboardOoo.v`。因此要确认 XO 实际用了哪个 RTL，看
build log 里的：

```text
Cuper SpMV OOO accumulator RTL boundary: ...
Replaced generated RTL wrapper with custom RTL: .../CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v

Cuper SpMV OOO scoreboard RTL boundary: ...
Replaced generated scoreboard wrapper with custom RTL: .../CuperSpmvOnly_RtlOwnerScoreboardOoo.v
```

## 速查命令

```bash
# 单记分板 primitive
make -C verilog tapa-vector-scoreboard-sim

# 单记分板真实矩阵 trace
make -C verilog tapa-scoreboard-dataset-cpp-sim \
  CSR_MATRIX=data/suitesparse/Schmid/csr/thermal2_n1024

# 全 RTL owner-bank 单元
make -C verilog tapa-bank-sim

# 全 RTL owner-bank + scatter
make -C verilog tapa-bank-scatter-cpp-sim

# generated splitter + 全 RTL owner-bank + generated/model scatter
make -C verilog tapa-splitter-bank-scatter-cpp-sim
make -C verilog tapa-splitter-bank-generated-scatter-cpp-sim
```
