# Cuper Jacobi Iteration Scripts

这里放 `Cuper-jacobi-iteration` 独立构建、运行、打包、报告转换脚本。

当前 U55C 环境脚本：

- `launcher.py`：交互式/命令行构建与运行入口
- `env_u55c.sh`：加载 TAPA、Vitis 2022.2、XRT、U55C platform 路径
- `build_host.sh`：用 CMake 编译 `cuper_jacobi_host`
- `build_xo_u55c.sh`：用 `tapa compile` 生成 `CuperJacobiIteration.xo`；可用
  `JACOBI_TOP=CuperJacobiMmapProbeOnly` 切到 mmap-only micro top
- `link_xclbin_u55c.sh`：用 `v++ --link` 生成 xclbin；micro top 支持 same-bank 与
  split-bank connectivity
- `run_hw.sh`：设置 `BITFILE` 后运行 host
- `run_mmap_probe_xrt.sh`：用 native XRT runner 运行 `CuperJacobiMmapProbeOnly`
- `regression_sw.py`：一键 software/TAPA simulation 回归，终端只输出摘要，详细输出写日志

推荐顺序：

```bash
cd /home/pyx/project-x/Project-XPlus/DLC/Cuper-jacobi-iteration

scripts/build_host.sh
MAX_ITERS=1 make run-sw MATRIX=data/matrices/cant.mtx
MAX_ITERS=1 make run-sw MATRIX=../../data/suitesparse/Schmid/csr/thermal2_n262144
make regression-sw MODE=quick
scripts/build_xo_u55c.sh
scripts/link_xclbin_u55c.sh
scripts/run_hw.sh data/matrices/cant.mtx
```

`run-sw` 不需要 `BITFILE`。它会运行 TAPA software simulation，并把 kernel 输出和
host 侧 CPU Jacobi reference 对比。

一键 software regression：

```bash
make regression-sw MODE=quick
make regression-sw MODE=full
make regression-sw CASE=thermal2_n65536 NO_BUILD=1
```

`MODE=quick` 跑 `cant.mtx` 和 `thermal2_n65536`；`MODE=full` 额外跑
`thermal2_n262144`。每个 case 的完整输出写到
`../../cuper-jacobi-iteration-build/regression/<timestamp>_<mode>/`，终端只打印
PASS/FAIL、`final_diff`、`Error Num` 和关键 cycle，避免把长日志塞进上下文。

后台生成硬件 bitstream：

```bash
scripts/launcher.py hw-tmux
tmux attach -t cuper_jacobi_iteration_hw_build
tail -f ../../cuper-jacobi-iteration-build/logs/build_hw_tmux.log
```

如果要重启同名 tmux 任务：

```bash
scripts/launcher.py hw-tmux --force
```

如果已经有现成 bitstream，可以跳过 `build_xo_u55c.sh` 和
`link_xclbin_u55c.sh`，直接指定：

```bash
BITFILE=/path/to/CuperJacobiIteration.xclbin scripts/run_hw.sh data/matrices/cant.mtx
```

## mmap-only micro probe

`CuperJacobiMmapProbeOnly` 只写 Status/Metrics/Debug 固定槽位并返回，不接入完整
Jacobi dataflow。它用于排查 mmap 写回、HBM bank、native XRT sync 和 TAPA/FRT
`Finish()` 边界。

从根目录运行：

```bash
make cuper-jacobi-build-mmap-probe-xrt-host
make cuper-jacobi-build-mmap-probe-xo
make cuper-jacobi-link-mmap-probe-xclbin
make cuper-jacobi-link-mmap-probe-xclbin-split
```

`link-mmap-probe-xclbin` 把 Status/Metrics/Debug 都放在 HBM[24]；
`link-mmap-probe-xclbin-split` 使用 HBM[24]/HBM[25]/HBM[26]。

上板运行：

```bash
BITFILE=/path/to/CuperJacobiMmapProbeOnly.xclbin \
  ROW_NUM=16 MAX_ITERS=1 make cuper-jacobi-run-mmap-probe-xrt
BITFILE=/path/to/CuperJacobiMmapProbeOnly.xclbin \
  ROW_NUM=1024 MAX_ITERS=1 make cuper-jacobi-run-mmap-probe-xrt
```
