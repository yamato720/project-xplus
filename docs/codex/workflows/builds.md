# 构建目录与 tmux 纪律

## 1. 构建目录

硬件构建不要使用裸 `build/` 混放。

规则：

1. 构建目录必须使用当前主线或当前 bitstream 名加 `-build` 后缀。
2. 典型形式：

```text
cuper-tapa-spmv-build/
cuper-notapa-spmv-build/
cuper-notapa-fpga-pcg-build/
cuper-tapa-pcg-fpga-u55c-YYYYMMDD-build/
```

3. 所有 build 目录必须被 `.gitignore` 忽略。
4. 如果新增 build 目录，要确认 `*-build/` 或显式条目已经覆盖。
5. 不要把临时 build 产物、`.xo`、`.link_summary`、`.compile_summary`、Vivado
   中间目录提交进仓库。
6. 只把需要同步的 `.xclbin` 放到 `395bitstream/`，只把需要归档的文件放到
   `bitstream_archive/`。

## 2. 硬件 bitstream 前置验证

每次准备生成 `TARGET=hw` bitstream 前，先做同一 top/ABI 的软件级验证。目标是把
C++、task graph、host 参数、ABI、数据准备和明显死锁尽量挡在长时间实现之前。

优先级：

1. 能跑 `sw_emu` 的路径，先跑对应 `build-*-sw` 和最小数据集 smoke。
2. TAPA 路径若 `sw_emu` 不适合或当前目标只需要 XO，至少先跑 TAPA software
   simulation 或对应 host 的空 bitstream/software-sim smoke。
3. 如果工具链、平台或数据暂时不支持软件仿真，必须在版本记录或回复里写清楚：
   没跑哪一步、为什么没跑、用什么 smoke 代替。
4. 软件级验证失败时，不启动 `*-hw-tmux`；先修到软件级验证返回。

常见口径示例：

```bash
make build-cuper-control-sw
make run-cuper-control-xrt TARGET=sw_emu DATASET=data/suitesparse/Schmid/csr/thermal2_n16

make build-cuper-tapa-pcg-spmv-sw
make run-cuper-tapa-pcg-spmv TARGET=sw_emu DATASET=data/suitesparse/Schmid/csr/thermal2_n16

make cuper-tapa-pcg-host
make run-cuper-tapa-spmv DATASET=data/suitesparse/Schmid/csr/thermal2_n16 SPMV_REPEATS=1
```

具体命令以当前 Makefile 实际存在的 target 为准；没有 target 时先补 target 或记录
替代验证，不要直接跳到硬件实现。

## 3. tmux 和长时间构建

1. 长时间硬件构建用 tmux，不在普通前台会话里跑。
2. tmux 命名要能看出主线，例如：

```text
project-xplus-cuper-tapa-pcg-hw
project-xplus-cuper-notapa-spmv-hw
```

3. tmux 命令结束后保留 shell，不要自动退出，方便回看日志。
4. 构建日志写入 `logs/`，文件名包含主线和时间戳。
5. 不要随意杀正在跑的实现。需要停止时先确认阶段、日志和是否有并行构建。
6. 如果构建进入 `vpl` / `impl` / routing，后续源码注释或重构不会影响这次结果；
   新源码只影响下一次构建。

## 4. tmux 启动后的守候点

启动 `*-hw-tmux` 后不要立刻结束本轮工作。比较安全的守候点分两类：

1. 本轮目标是完整 `.xclbin`：
   - 守到 TAPA/Vitis 前端已经完成当前 top 的 XO 生成；
   - 必要的 XO patch 已通过；
   - Vitis link 已开始，并在日志里看到 `Running VPL`、`Run vpl`、
     `Starting logic synthesis`、`Starting logic optimization`、`Starting
     implementation` 或等价信息之一。
2. 本轮目标只是 XO：
   - 守到 `.xo` 文件存在；
   - 必要的 XO patch 已通过；
   - `make -q <path/to/kernel.xo>` 返回 `0`，确认 Makefile 也认为 XO target
     已经完成。

选择这个守候点的原因：

- TAPA 分析、HLS、RTL packaging、XO patch、connectivity/top 名等错误通常会在
  这个点之前暴露，人工可立即修；
- 进入 VPL synthesis/implementation 后，剩余风险主要是资源、时序、routing 或
  platform 约束，通常需要长时间等待，不适合一直阻塞当前对话；
- 如果还没有进入这个点就失败，必须先看日志定位并修复，不能把失败的 tmux 留给用户
  自己处理。

回复里至少给出：

```text
tmux session
log path
build dir
当前日志阶段
是否已过 XO/patch
如果是完整 xclbin，是否已进入 VPL/synthesis/implementation
```
