# Cuper

`Cuper` 是放在 `Project-XPlus/DLC/` 下的独立 HLS 子项目。

当前阶段先把它当成一个自洽的小工程看待，不直接接入 `Project-XPlus` 根 `Makefile`。

## 目录

```text
Cuper/
  Makefile
  README.md
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
```

## 当前目标

1. 让 `Cuper` 自己拥有独立的 host / kernel / cfg 入口
2. 让 `Cuper` 可以独立维护 HLS kernel 迁移与验证
3. 在结构稳定前，不和 `Project-XPlus` 根构建流程强耦合
4. 后续若需要，再决定哪些公共部分抽回上层共享

## 与上层工程的关系

- `Project-XPlus` 目前是母工程
- `DLC/Cuper` 当前按独立子项目管理
- 可以临时复用上层的公共头文件、数据集或脚本思路
- 但默认不共享 build / reports / logs / Makefile 产物路径

## 当前约定

- `host/` 放 `Cuper` 自己的运行入口和 host glue code
- `kernels/` 放 `Cuper` 自己的 HLS 顶层 kernel 与 helper
- `include/` 放 host/kernel 共享头
- `cfg/` 放 connectivity 和平台相关配置
- `scripts/` 放独立构建/运行脚本
- `src/` 放数据结构、golden、适配层等非顶层源码

## 下一步建议

1. 先把 `Cuper` 现有 kernel 顶层和 host 入口落到本目录
2. 先形成最小可编译/可运行链路
3. 再决定哪些类型定义和工具脚本值得和上层共享
