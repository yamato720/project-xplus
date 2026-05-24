# Bitstream Archive

这个目录用于保存从 `395bitstream/` 移出的历史 bitstream 本地留档，以及对应文字说明。
GitHub 只同步 Markdown 说明；本目录内的 `.xclbin` 受 `.gitignore` 保护，不进 Git。

## 目录约定

- `395bitstream/`: 放当前需要通过 GitHub 及时同步到服务器的可运行 bitstream 和直接配套文件。
- `bitstream_archive/`: 放历史 bitstream 的本地留档；Git 只跟踪 `.md` 说明。
- `bitstream_archive/2026-05-24-current-spmv-bitstreams/`: 本地保存 2026-05-24 当前两版 SpMV bitstream 及对应源码快照；Git 只同步该目录的 `README.md`。
- 可以在本目录保存 `.xclbin` 原文件，但不要 `git add -f`。
- 不在本目录放 `.xo`、`.log`、`.info`、`.json`、Vivado/Vitis 临时目录或其它工具原始产物。
- 如果需要留存 `.xclbin.info` 里的内容，把关键字段整理进 `.md`，不要把 `.info` 原文件复制进来。

## 更新流程

每次 `395bitstream/` 增加或替换 bitstream 后，同步更新一个新的留档 md：

```bash
find 395bitstream -maxdepth 1 -type f -printf '%f\t%s\t%TY-%Tm-%Td %TH:%TM:%TS %TZ\n' | sort
sha256sum 395bitstream/*
xclbinutil --info --input 395bitstream/<file>.xclbin
git check-ignore -v bitstream_archive/**/*.xclbin
```

最后一条命令应显示 `.xclbin` 被 `.gitignore` 忽略，表示历史 bitstream 不会传到 GitHub。
