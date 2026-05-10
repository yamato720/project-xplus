# Project-XPlus HLS 路径源码解析

这份文档专门回答一个问题：

**如果从 `Project-XPlus` 的源码本身出发，HLS/XRT 这条链路到底是怎么串起来的。**

它不重复算法原理，而是把“命令入口 -> 构建脚本 -> XRT host -> 5 个 kernel -> 连接配置 -> 报告输出”按源码路径讲清楚。

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
   负责 host 侧调度和 XRT 交互
3. `5 个 HLS kernel 顶层`
   负责真正的 FPGA 计算
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

每个 kernel 会单独编译成一个 `xo`：

```text
spmv_csr_kernel.xo
init_pcg_kernel.xo
dot_kernel.xo
update_xrz_kernel.xo
update_p_kernel.xo
```

对应规则都在 `Makefile` 里，例如：

```make
$(XO_SP_MV): kernels/spmv_csr_kernel.cpp ...
	$(VPP) -c ... -k spmv_csr_kernel ...
```

最后再统一 link：

```make
$(XCLBIN): $(XOS) cfg/connectivity_u55c.cfg
	$(VPP) -l ...
```

也就是说，当前 `Project-XPlus` 是一个真正的 **多 kernel xclbin**，不是把所有逻辑塞成一个单顶层 kernel。

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
5. 创建 XRT kernel 对象
6. 分配 BO 并同步数据
7. 串行调度 5 个 kernel
8. 回读标量和最终解向量
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

在 `xrt_host.cpp` 中，下载完 `xclbin` 后会创建 5 个 kernel：

```cpp
auto spmv_kernel = xrt::kernel(device, uuid.get(), "spmv_csr_kernel");
auto init_kernel = xrt::kernel(device, uuid.get(), "init_pcg_kernel");
auto dot_kernel = xrt::kernel(device, uuid.get(), "dot_kernel");
auto update_xrz_kernel = xrt::kernel(device, uuid.get(), "update_xrz_kernel");
auto update_p_kernel = xrt::kernel(device, uuid.get(), "update_p_kernel");
```

这里的名字必须和各个 HLS 顶层函数名完全一致。

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
   用于只读输入，例如 `row_ptr / col_idx / values / b / m_inv`
2. `make_inout_bo`
   用于 device 侧会被 kernel 改写的缓冲，例如 `x / r / z / p / ap / metrics`

这一步完成后，host 拿到的不是裸指针，而是：

```text
xrt::bo
```

之后每个 kernel 的参数设置，都是把 `bo` 作为实参塞给 `run.set_arg(...)`。

---

## 6. 初始化阶段的硬件执行顺序

当前初始化阶段是两步：

### 6.1 `spmv_csr_kernel`

先算：

```text
ax = A * x0
```

对应代码：

```cpp
xrt::run init_spmv_run(spmv_kernel);
...
init_spmv_run.start();
init_spmv_run.wait();
```

### 6.2 `init_pcg_kernel`

再算：

```text
r  = b - ax
z  = M^{-1} r
p  = z
rz = r^T z
rr = r^T r
```

对应代码：

```cpp
xrt::run init_run(init_kernel);
...
init_run.start();
init_run.wait();
```

执行完后，host 从 `metrics_bo` 回读：

```cpp
rz = metrics_mapped[0];
rr = metrics_mapped[1];
```

然后才进入主循环。

---

## 7. 主循环的硬件执行顺序

当前每轮都是 host 串行调度 4 个阶段：

### 7.1 `spmv_csr_kernel`

算：

```text
ap = A * p
```

### 7.2 `dot_kernel`

算：

```text
pAp = p^T ap
```

host 回读 `dot_out[0]` 后自行计算：

```text
alpha = rz / pAp
```

### 7.3 `update_xrz_kernel`

算：

```text
x      = x + alpha p
r      = r - alpha ap
z      = M^{-1} r
rz_new = r^T z
rr     = r^T r
```

host 再从 `metrics_bo` 回读：

```text
rz_new
rr
```

然后计算：

```text
beta = rz_new / rz_old
```

### 7.4 `update_p_kernel`

算：

```text
p = z + beta p
```

### 7.5 为什么 host 保留控制权

因为这一版强调：

1. 多 kernel 架构清晰
2. 每轮只回读少量标量
3. breakdown / 收敛判断更容易调试

所以 `alpha / beta / rr<=tau` 没有塞进某个大顶层 solver kernel 里，而是留在 host 侧。

---

## 8. 5 个 HLS kernel 顶层分别做什么

### 8.1 `kernels/spmv_csr_kernel.cpp`

功能：

```text
y = A x
```

实现特点：

1. `x` 先缓存到 `x_local`
2. 外层按 row 遍历
3. 内层按 CSR 区间 `row_ptr[row] ~ row_ptr[row+1]` 聚合

### 8.2 `kernels/init_pcg_kernel.cpp`

功能：

```text
r / z / p / rz / rr
```

实现特点：

1. 向量更新与 reduction 合并
2. 输出标量统一写进 `metrics`

### 8.3 `kernels/dot_kernel.cpp`

功能：

```text
out[0] = a^T b
```

当前只服务于：

```text
pAp = p^T ap
```

### 8.4 `kernels/update_xrz_kernel.cpp`

功能：

```text
x / r / z / rz_new / rr
```

实现特点：

1. 对同一个 `alpha` 相关的步骤做融合
2. host 每轮只回读两个标量

### 8.5 `kernels/update_p_kernel.cpp`

功能：

```text
p = z + beta p
```

它是当前最轻量的一个 kernel。

---

## 9. `cfg/connectivity_u55c.cfg` 在哪一层生效

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
nk=spmv_csr_kernel:1:spmv_csr_kernel_1
```

表示这个 `xclbin` 里有一个 `spmv_csr_kernel` 实例。

而类似：

```ini
sp=spmv_csr_kernel_1.values:HBM[1]
```

表示 `values` 这个 memory port 接到 `HBM[1]`。

这一步不改变算法，只改变 kernel 访问 device memory 的物理映射。

---

## 10. 本地基线代码与 XRT 代码的关系

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

## 11. 报告链路在什么地方闭环

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

## 12. 阅读顺序建议

如果你想最快看懂 HLS 路径源码，建议按这个顺序：

1. `Makefile`
   看命令入口和 `sw_emu/hw` 产物路径
2. `host/xrt_host.cpp`
   看 host 怎么调度 5 个 kernel
3. `cfg/connectivity_u55c.cfg`
   看端口和 HBM 映射
4. `kernels/spmv_csr_kernel.cpp`
5. `kernels/init_pcg_kernel.cpp`
6. `kernels/dot_kernel.cpp`
7. `kernels/update_xrz_kernel.cpp`
8. `kernels/update_p_kernel.cpp`
9. `scripts/render_report.py`
   看报告是怎么从 JSON 变成 HTML 的

如果是先想确认算法逻辑，再切到硬件实现，则建议先看：

1. `host/multi_kernel_solver.hpp`
2. `kernels/cg_kernels.cpp`
3. `host/xrt_host.cpp`

这样会更容易把本地逻辑和硬件调度对应起来。
