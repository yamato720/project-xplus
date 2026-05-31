# Bitstream Archive

这个目录用于保存从 `395bitstream/` 移出的历史 bitstream 本地留档，以及对应文字说明。
GitHub 只同步 Markdown 说明；本目录内的 `.xclbin` 受 `.gitignore` 保护，不进 Git。

## 目录约定

- `395bitstream/`: 放当前需要通过 GitHub 及时同步到服务器的可运行 bitstream 和直接配套文件。
- `bitstream_archive/`: 放历史 bitstream 的本地留档；Git 可跟踪 `.md`、`.info` 和源码快照，但不跟踪 `.xclbin`。
- `bitstream_archive/2026-05-24-current-spmv-bitstreams/`: 本地保存 2026-05-24 当前两版 SpMV bitstream 及对应源码快照；Git 同步源码和 `.xclbin.info`，只排除 `.xclbin` 大文件。
- `bitstream_archive/2026-05-29-tapa-pcg-spmv-demo-candidates/`: 本地保存
  2026-05-29 从 `395bitstream/` 移出的 `cuper-tapa-spmv` one-shot demo 和
  `cuper-tapa-pcg` full-PCG demo；两者均未晋级为标准版。
- `bitstream_archive/2026-05-31-tapa-pcg-controller-split-demo/`: 本地保存
  2026-05-31 从 `395bitstream/` 移出的 `cuper-tapa-pcg` controller-split
  full-PCG demo；该版上板测试通过到完整 `thermal2` 的 init/1iter，并 full-run
  到 `thermal2_n262144`，但未晋级为标准版。
- 可以在本目录保存 `.xclbin` 原文件，但不要 `git add -f`。
- 不在本目录放 `.xo`、`.log`、`.json`、Vivado/Vitis 临时目录或其它大型工具原始产物。
- `.xclbin.info` 可以跟随源码快照进入 Git，用于记录 xclbin 的 kernel、UUID、clock 和 memory topology。

## 更新流程

每次 `395bitstream/` 增加或替换 bitstream 后，同步更新一个新的留档 md：

```bash
find 395bitstream -maxdepth 1 -type f -printf '%f\t%s\t%TY-%Tm-%Td %TH:%TM:%TS %TZ\n' | sort
sha256sum 395bitstream/*
xclbinutil --info --input 395bitstream/<file>.xclbin
git check-ignore -v bitstream_archive/**/*.xclbin
```

最后一条命令应显示 `.xclbin` 被 `.gitignore` 忽略，表示历史 bitstream 不会传到 GitHub。
