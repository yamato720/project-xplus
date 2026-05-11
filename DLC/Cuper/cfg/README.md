# Cuper Config

这里放 `Cuper` 子项目自己的平台与 connectivity 配置。

建议后续放置：

- `connectivity_u55c.cfg`
- 平台特定的 bank / SLR / clock 配置
- 如有多板卡，按设备拆不同 cfg

当前阶段先不要直接复用上层 `Project-XPlus/cfg/` 作为默认入口，
除非你已经确认 `Cuper` 的 kernel 端口和 bank 规划完全一致。
