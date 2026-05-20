# Project-XPlus HLS 路径源码解析

这份文档专门回答一个问题：

**如果从 `Project-XPlus` 的源码本身出发，HLS/XRT 这条链路到底是怎么串起来的。**

它把“命令入口 -> 构建脚本 -> XRT host -> 单顶层 PCG 控制 kernel -> 连接配置 -> 报告输出”按源码路径讲清楚。

当前默认 XRT 路径已经从“host 串行调度 5 个 kernel”切换为“host 启动一次 `pcg_control_kernel`”。原来的 5 个拆分 kernel 仍保留在源码中，用于本地多-kernel 基线和拆分实现参考。

如果要先看 Jacobi-PCG 的数学递推、为什么初始化阶段要先 `spmv(x0)`、以及数学变量如何映射到 BO/kernel，请先看：

- [jacobi_pcg_algorithm_flow_zh.md](jacobi_pcg_algorithm_flow_zh.md)

如果要看 `nasa2910` 上的滑动窗口 SpMV 和局部 dataflow 实验，请看：

- [spmv_windowed_dataflow_zh.md](spmv_windowed_dataflow_zh.md)

---

## 1. 先看总路径

从使用者视角，当前最关键的几条命令是：

```bash
make build-sw
make run-sw-report
make build-hw
make run-hw-report
```

这几条命令最终会走到下面这组核心文件：

```text
Makefile
host/xrt_host.cpp
kernels/pcg_control_kernel.cpp
kernels/spmv_csr_kernel.cpp
kernels/init_pcg_kernel.cpp
kernels/dot_kernel.cpp
kernels/update_xrz_kernel.cpp
kernels/update_p_kernel.cpp
cfg/connectivity_u55c.cfg
scripts/render_report.py
```

可以把它理解成 6 层：

1. `Makefile`
   负责决定当前是 `sw_emu` 还是 `hw`
2. `xrt_host.cpp`
   负责 XRT 交互、BO 管理和一次性启动 kernel
3. `pcg_control_kernel.cpp`
   负责真正的 FPGA 计算和 PCG 主循环控制
4. `connectivity_u55c.cfg`
   负责 kernel memory port 到 HBM bank 的绑定
5. `src/*.hpp`
   负责数据集加载和 CPU golden
6. `render_report.py`
   负责把运行结果转成 HTML 报告

---

## 2. 命令入口在 `Makefile`

文件：

```text
Makefile
```

### 2.1 构建入口

软件仿真和硬件构建的入口是：

```make
build-sw:
	$(MAKE) build TARGET=sw_emu

build-hw:
	$(MAKE) build TARGET=hw
```

这里的关键是：

```text
TARGET=sw_emu / hw
```

它决定了：

1. `v++` 的 `-t` 参数
2. 构建输出目录
3. 后续 `run-xrt` 是走仿真环境还是直接走板卡

### 2.2 输出目录

当前构建目录通过：

```make
TARGET_BUILD_DIR := $(BUILD_DIR)/$(TARGET)
```

隔离成：

```text
build/sw_emu/
build/hw/
```

这样 `sw_emu` 和 `hw` 的 `xo/xclbin/emconfig` 不会互相覆盖。

### 2.3 `xo` 与 `xclbin`

当前默认只编译一个控制 kernel `xo`：

```text
pcg_control_kernel.xo
```

对应规则在 `Makefile` 里：

```make
$(XO_PCG_CONTROL): kernels/pcg_control_kernel.cpp ...
	$(VPP) -c ... -k pcg_control_kernel ...
```

最后再统一 link：

```make
$(XCLBIN): $(XOS) cfg/connectivity_u55c.cfg
	$(VPP) -l ...
```

也就是说，当前默认 `xclbin` 是一个 **单 kernel 控制流 xclbin**，`alpha / beta / rr<=tau / breakdown` 都在 FPGA kernel 内决定。

---

## 3. Host 入口在 `host/xrt_host.cpp`

文件：

```text
host/xrt_host.cpp
```

这个文件是整条硬件执行路径的中心。

### 3.1 它的职责

它负责：

1. 解析命令行
2. 读取数据集
3. 构造 Jacobi 预条件器 `m_inv`
4. 下载 `xclbin`
5. 创建 `pcg_control_kernel` XRT kernel 对象
6. 分配 BO 并同步数据
7. 启动一次 kernel
8. 回读 `status / metrics / x`
9. 用 CPU golden 做校验
10. 输出 txt/json/html 报告

### 3.2 数据加载

这里走的是：

```cpp
const Dataset dataset = Dataset::load(options.dataset_dir);
```

而 `Dataset` 实际来自：

```text
host/dataset_bridge.hpp
src/CsrDataset.hpp
```

也就是说，当前 `XPlus` 已经使用自己的数据加载器，不再从 `XS` 取。

### 3.3 Jacobi 预条件器

host 不会真的构造完整对角矩阵 `M`，而是只做：

```cpp
m_inv[i] = 1.0 / diag(A)[i]
```

对应函数：

```cpp
build_jacobi_inverse(...)
```

这样和 kernel 侧的 `z = M^{-1} r` 是逐元素乘法关系。

---

## 4. XRT kernel 对象的创建

在 `xrt_host.cpp` 中，下载完 `xclbin` 后只创建一个 kernel：

```cpp
auto pcg_kernel = xrt::kernel(device, uuid.get(), "pcg_control_kernel");
```

这里的名字必须和 HLS 顶层函数名完全一致。

如果顶层函数改名，host 这边也必须一起改。

---

## 5. BO 分配与数据同步

`xrt_host.cpp` 中有两类 helper：

```cpp
make_input_bo(...)
make_inout_bo(...)
```

区别是：

1. `make_input_bo`
   用于只读输入，例如 `a_win_row_ptr / a_win_col_idx / a_win_blocks / b / m_inv`
2. `make_inout_bo`
   用于 device 侧会被 kernel 改写的缓冲，例如 `x / r / z / p / ap / metrics / status`

这一步完成后，host 拿到的不是裸指针，而是：

```text
xrt::bo
```

之后把 `bo` 作为实参塞给 `pcg_run.set_arg(...)`。

---

## 6. 单 kernel 的硬件执行顺序

`pcg_control_kernel.cpp` 内部完成完整 Jacobi-PCG：

```text
1. x/r/z/p/ap 等向量状态常驻 HBM
2. 用 4x4 block/bitmap + x-window + row-tile SpMV 计算 ax0 = A*x0，并写入 ap BO
3. 生成 r0 / z0 / p0 / rz0 / rr0，写入 r/z/p BO
4. 循环：
   ap = A*p    # 同样走滑动窗口 SpMV
   pAp = p^T ap
   alpha = rz / pAp
   x = x + alpha*p
   r = r - alpha*ap
   z = M^{-1}r
   rz_new = r^T z
   rr_new = r^T r
   如果 rr_new <= tau，直接在 kernel 内退出
   beta = rz_new / rz_old
   p = z + beta*p
5. 写回最终 x、metrics 和 status
```

这里最关键的变化是：`alpha / beta / rr<=tau / breakdown` 不再回到 host 判断，而是在 FPGA kernel 内判断。host 只在 kernel 结束后读一次状态。

---

## 7. `cfg/connectivity_u55c.cfg` 在哪一层生效

文件：

```text
cfg/connectivity_u55c.cfg
```

它不是算法逻辑文件，而是 `v++ -l` 阶段使用的连接配置。

它负责定义：

1. 当前有几个 kernel 实例
2. 每个 kernel 参数绑到哪个 HBM bank

例如：

```ini
nk=pcg_control_kernel:1:pcg_control_kernel_1
```

表示这个 `xclbin` 里有一个 `pcg_control_kernel` 实例。

而类似：

```ini
sp=pcg_control_kernel_1.a_win_blocks:HBM[1]
```

表示 `a_win_blocks` 这个 memory port 接到 `HBM[1]`。

这一步不改变算法，只改变 kernel 访问 device memory 的物理映射。

---

## 8. 本地基线代码与 XRT 代码的关系

除了 `xrt_host.cpp`，当前还有一套“本地直接调用同名 kernel 函数”的基线：

```text
host/main.cpp
host/multi_kernel_solver.hpp
kernels/cg_kernels.cpp
```

这套路径的意义是：

1. 不依赖 XRT / xclbin
2. 保持与硬件版相同的阶段拆分
3. 用来先验证多 kernel 拆分本身是否数值正确

也就是说：

```text
cg_kernels.cpp
```

不是给 `v++` 编的 HLS 顶层文件，而是本地调试用的“同名 CPU 版本”。

---

## 9. 报告链路在什么地方闭环

运行结束后：

1. `xrt_host.cpp`
   输出终端摘要
2. `xrt_host.cpp`
   写 `txt/json`
3. `scripts/render_report.py`
   读 JSON 生成两个 HTML

当前有两份 HTML：

1. interactive
   适合浏览器
2. static
   适合 VSCode 预览

也就是说，报告不是 `vitis_analyzer` 生成的，而是 `Project-XPlus` 自己的结果可视化链。

---

## 10. 阅读顺序建议

如果你想最快看懂 HLS 路径源码，建议按这个顺序：

1. `Makefile`
   看命令入口和 `sw_emu/hw` 产物路径
2. `host/xrt_host.cpp`
   看 host 怎么启动一次 `pcg_control_kernel`
3. `cfg/connectivity_u55c.cfg`
   看端口和 HBM 映射
4. `kernels/pcg_control_kernel.cpp`
   看 FPGA 内完整 PCG 控制流
5. `scripts/render_report.py`
   看报告是怎么从 JSON 变成 HTML 的

如果是先想确认算法逻辑，再切到硬件实现，则建议先看：

1. `host/multi_kernel_solver.hpp`
2. `kernels/cg_kernels.cpp`
3. `kernels/pcg_control_kernel.cpp`
4. `host/xrt_host.cpp`

这样会更容易把本地拆分逻辑和单 kernel 控制流对应起来。
