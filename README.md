# Project-XPlus

`Project-XPlus` 是从 `Project-X` 内独立出来的 Jacobi-PCG 多 kernel HLS 子项目根目录。

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

## 用法

直接运行本地多-kernel 基线：

```bash
cd ~/ProjectFS/Project-X/Project-XPlus
make run-local
```

指定数据集和迭代参数：

```bash
cd ~/ProjectFS/Project-X/Project-XPlus
make run-local DATASET_DIR=data/generated/cgsolver/n512 TAU=1e-10 MAX_ITERS=0
```

构建 XRT host：

```bash
cd ~/ProjectFS/Project-X/Project-XPlus
make xrt-host
```

构建 `sw_emu` xclbin：

```bash
cd ~/ProjectFS/Project-X/Project-XPlus
make build-sw
```

运行 `sw_emu`：

```bash
cd ~/ProjectFS/Project-X/Project-XPlus
make run-xrt TARGET=sw_emu
```

运行 `sw_emu` 并生成 `txt/json/html` 报告：

```bash
cd ~/ProjectFS/Project-X/Project-XPlus
make run-sw-report REPORT_BASENAME=sw_emu_n512
```

会同时生成两份 HTML：

1. 交互分页版：适合 Chrome
2. 静态展开版：适合 VSCode HTML 预览

构建硬件 bitstream：

```bash
cd ~/ProjectFS/Project-X/Project-XPlus
make build-hw
```

也可以用脚本：

```bash
cd ~/ProjectFS/Project-X/Project-XPlus
scripts/build_hw.sh
scripts/build_hw_tmux.sh
```

硬件执行并生成 `hw` 报告：

```bash
cd ~/ProjectFS/Project-X/Project-XPlus
make run-hw-report REPORT_BASENAME_HW=hw_n512_report
```

或：

```bash
cd ~/ProjectFS/Project-X/Project-XPlus
scripts/run_hw_report.sh REPORT_BASENAME_HW=hw_n512_report
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
cd ~/ProjectFS/Project-X/Project-XPlus
python3 scripts/generate_cg_dataset.py
```

生成自定义规模：

```bash
cd ~/ProjectFS/Project-X/Project-XPlus
python3 scripts/generate_cg_dataset.py --size 1024 --output-dir data/generated/cgsolver/n1024
```
