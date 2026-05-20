# Cuper Scripts

这里放 `Cuper` 独立构建、运行、打包、报告转换脚本。

当前 U55C 环境脚本：

- `launcher.py`：交互式/命令行构建与运行入口
- `env_u55c.sh`：加载 TAPA、Vitis 2022.2、XRT、U55C platform 路径
- `build_host.sh`：用 CMake 编译 `build/cuper_host`
- `build_xo_u55c.sh`：用 `tapa compile` 生成 `Cuper_2022.xo`
- `link_xclbin_u55c.sh`：用 `v++ --link` 生成 `Cuper_2022.xclbin`
- `run_hw.sh`：设置 `BITFILE` 后运行 host

推荐顺序：

```bash
cd /home/pyx/ProjectFS/Project-X/Project-XPlus/DLC/Cuper

scripts/build_host.sh
scripts/build_xo_u55c.sh
scripts/link_xclbin_u55c.sh
scripts/run_hw.sh data/matrices/sit100/sit100.mtx
```

后台生成硬件 bitstream：

```bash
scripts/launcher.py hw-tmux
tmux attach -t cuper_hw_build
tail -f logs/build_hw_tmux.log
```

如果要重启同名 tmux 任务：

```bash
scripts/launcher.py hw-tmux --force
```

如果已经有现成 bitstream，可以跳过 `build_xo_u55c.sh` 和
`link_xclbin_u55c.sh`，直接指定：

```bash
BITFILE=/path/to/Cuper_2022.xclbin scripts/run_hw.sh data/matrices/sit100/sit100.mtx
```
