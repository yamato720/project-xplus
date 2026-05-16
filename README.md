# Project-XPlus

`Project-XPlus` 是一个面向 Jacobi-PCG 多 kernel HLS/XRT 的独立子项目。

当前目标：

1. 以 `Project-XPlus` 为新的工程根组织 Jacobi-PCG solver
2. 让 `Project-XPlus` 自己拥有数据集生成、加载和 golden 参考
3. 同时维护本地多-kernel 基线和 XRT/Vitis 多 kernel 子工程入口

## 目录

```text
Project-XPlus/
  Makefile
  README.md
  cfg/
    connectivity_u55c.cfg
  docs/
    design/
      hls.md
  DLC/
    Cuper/
      README.md
      Makefile
      cfg/
      data/
      docs/
      host/
      include/
      kernels/
      logs/
      reports/
      scripts/
      src/
      xrt.ini
  include/
    cg_common.hpp
    cg_kernels.hpp
  src/
    CsrDataset.hpp
    CgSolverGolden.hpp
  host/
    main.cpp
    cpu_reference.hpp
    dataset_bridge.hpp
    multi_kernel_solver.hpp
    xrt_host.cpp
  kernels/
    cg_kernels.cpp
    spmv_csr_kernel.cpp
    init_pcg_kernel.cpp
    dot_kernel.cpp
    update_xrz_kernel.cpp
    update_p_kernel.cpp
  scripts/
    generate_cg_dataset.py
    run_sw_emu.sh
    run_hw_emu.sh
    run_hw.sh
  xrt.ini
```

## 当前阶段

当前版本先完成：

1. 新项目根目录搭建
2. 设计文档落地
3. 本地可运行的 5-kernel Jacobi-PCG 基线
4. `Project-XPlus` 自有数据集生成、加载和 golden 对照
5. XRT/Vitis 子工程的 host、kernel、connectivity 和运行脚本骨架
6. `sw_emu` / `hw` 报告输出链，包括 txt/json/interactive html/static html
7. 关键 host 与 kernel 顶层中文注释，便于顺着看硬件执行流程
8. `DLC/Cuper` 独立 HLS 子项目骨架，便于作为迁移分支单独演进

## DLC

当前在 `DLC/` 下预留独立子项目区：

- [DLC/Cuper/README.md](/home/pyx/ProjectFS/Project-X/Project-XPlus/DLC/Cuper/README.md)

`Cuper` 当前按独立 HLS 子项目管理：

- 先不接入根 `Makefile`
- 自己维护 `host/kernel/include/cfg/scripts`
- 自己维护 `build/logs/reports`
- 等结构稳定后，再决定是否与上层工程共享更多公共部分

## 文档

- 设计基线：[docs/design/hls.md](/home/pyx/ProjectFS/Project-X/Project-XPlus/docs/design/hls.md)
- HLS 源码解析：[docs/design/hls_source_walkthrough_zh.md](/home/pyx/ProjectFS/Project-X/Project-XPlus/docs/design/hls_source_walkthrough_zh.md)

## 用法

直接运行本地多-kernel 基线：

```bash
make run-local
```

默认数据集和迭代参数在源码中指定：

- [host/run_defaults.hpp](/home/pyx/ProjectFS/Project-X/Project-XPlus/host/run_defaults.hpp)

当前默认使用 `data/suitesparse/Schmid/csr/thermal2_n1024`。

SuiteSparse 实际数据文件不提交到 Git。首次 clone 后先下载并转换默认数据：

```bash
make download-suitesparse-data
```

需要复现全部本地登记的数据集：

```bash
make download-suitesparse-data DATASETS=all
```

需要从完整 `thermal2` 生成其他尺寸的主子矩阵：

```bash
make download-suitesparse-data DATASETS=thermal2_n2048
```

数据来源、尺寸和 checksum 记录在 [data/suitesparse/SOURCES.md](/home/pyx/ProjectFS/Project-X/Project-XPlus/data/suitesparse/SOURCES.md)。

交互式选择数据集和运行方式：

```bash
make launcher
```

launcher 首页也提供 `d. 数据集下载/生成`，可以直接按大小从完整 `thermal2` 生成 `thermal2_n<N>`。

构建 XRT host：

```bash
make xrt-host
```

构建 `sw_emu` xclbin：

```bash
make build-sw
```

运行 `sw_emu`：

```bash
make run-xrt TARGET=sw_emu
```

运行 `sw_emu` 并生成 `txt/json/html` 报告：

```bash
make run-sw-report
```

终端默认只打印关键摘要，详细运行输出会写入同名 `.log` 文件。

如果已经有编好的 `sw_emu` 产物，优先直接运行，不存在时再自动补编：

```bash
make run-sw-report-existing
```

软件报告实际文件名前会自动加 `SW_` 前缀。

会同时生成两份 HTML：

1. 交互分页版：适合 Chrome
2. 静态展开版：适合 VSCode HTML 预览

构建硬件 bitstream：

```bash
make build-hw
```

默认硬件 link 会额外要求 Vivado 导出一组实现阶段分析报告：

1. routed timing summary
2. post-route physical optimization timing summary
3. hierarchical utilization
4. power estimate
5. methodology
6. DRC

这些配置在 [cfg/vivado_analysis_reports.cfg](/home/pyx/ProjectFS/Project-X/Project-XPlus/cfg/vivado_analysis_reports.cfg)。如果只想按最简方式生成 bitstream，可以临时关闭：

```bash
make build-hw ENABLE_VIVADO_ANALYSIS=0
```

也可以用脚本：

```bash
scripts/build_hw.sh
scripts/build_hw_tmux.sh
```

如果已有 `build/hw` 并且 Vivado implemented run 还在，可以不重新生成 bitstream，尝试补导出功耗报告：

```bash
make vivado-power-report
```

导出所有可选 Vivado 分析报告：

```bash
make vivado-analysis
```

默认输出目录：

```text
build/hw/_x_temp/reports/analysis/
```

`vivado-analysis` 会逐项尝试导出：

```text
power
utilization
timing
methodology
drc
route_status
clock_utilization
clock_interaction
cdc
design_analysis
qor_suggestions
high_fanout
control_sets
ram_utilization
```

可以只导出其中一部分：

```bash
make vivado-analysis VIVADO_ANALYSIS_REPORTS="power timing utilization"
```

如果要采样板卡运行时电源/功耗传感器，而不是 Vivado 静态估计：

```bash
make xrt-power-snapshot
```

如果需要打包完整的 Vivado `vpl` 目录：

```bash
make vivado-package-full
```

硬件执行并生成 `hw` 报告：

```bash
make run-hw-report
```

如果已经有编好的硬件 bitstream，优先直接运行，不存在时再自动补编：

```bash
make run-hw-report-existing
```

硬件报告实际文件名前会自动加 `HW_` 前缀。

或：

```bash
scripts/run_hw_report.sh
```

## 数据来源

当前版本已经在 `Project-XPlus` 内自带：

1. 数据集生成脚本
2. CSR 数据加载器
3. CPU golden Jacobi-PCG 参考实现

当前默认数据集目录：

```text
data/generated/cgsolver/n512
```

当前默认 golden 参考来源：

```text
src/CsrDataset.hpp
src/CgSolverGolden.hpp
```

## 报告文件

每次 `run-sw-report*` / `run-hw-report*` 默认会输出：

```text
SW_<basename>.json
SW_<basename>.txt
SW_<basename>.html
SW_<basename>_static.html
SW_<basename>.log

HW_<basename>.json
HW_<basename>.txt
HW_<basename>.html
HW_<basename>_static.html
HW_<basename>.log
```

其中：

1. 终端只保留关键摘要
2. 详细构建/运行日志进入 `.log`
3. 结构化结果进入 `json/txt/html`

## 后续实现边界

## 执行流程

当前硬件执行流程是：

1. host 读取 CSR 数据集并构造 `m_inv`
2. host 通过 XRT 下载 `xclbin`
3. host 分配 BO，并把 CSR / 向量同步到 device
4. host 依次启动 5 个 kernel：
   `spmv_csr_kernel`
   `init_pcg_kernel`
   `dot_kernel`
   `update_xrz_kernel`
   `update_p_kernel`
5. host 每轮只回读必要标量，计算 `alpha / beta / 收敛判断`
6. 全部迭代结束后再回读最终 `x`
7. host 用 CPU golden 和残差重新校验，并生成 txt/json/html 报告

正式 HLS 多 kernel 方案以：

- `spmv_csr_kernel`
- `init_pcg_kernel`
- `dot_kernel`
- `update_xrz_kernel`
- `update_p_kernel`

为正式计算阶段。
生成默认 `n512` 数据集：

```bash
python3 scripts/generate_cg_dataset.py
```

生成自定义规模：

```bash
python3 scripts/generate_cg_dataset.py --size 1024 --output-dir data/generated/cgsolver/n1024
```
