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

## 2. tmux 和长时间构建

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
