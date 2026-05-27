# Project-XPlus Codex 测试纪律

这份文件给新对话里的 Codex 或服务器侧 AI 用。测试口径来自
`395bitstream/cuper_spmv_u55c_compare_20260524.html`，目标是复现并核实
当前 `395bitstream/` 里几版 Cuper bitstream 的功能边界和性能量级。

## 0. 先读

1. `docs/codex/coding.md`
2. `395bitstream/README.md`
3. `395bitstream/cuper_spmv_u55c_compare_20260524.html`
4. `data/README.md`
5. `Makefile` 中 `run-cuper-*` 目标

如果只看本文件，至少要记住：

- 这里测的是 Cuper 路线，不是默认 CSR/control-kernel 路线。
- 有些 timeout / failed 是当前已知边界，不一定是新问题。
- 测试新 demo 时，默认只跑新 demo 本身。四条标准 bitstream 和旧基线数据默认
  复用当前 HTML、`docs/bitstream_summaries/` 和已归档日志；不要每次新 demo
  都重跑四大标准版本。
- demo 的测试数据和结论必须写回 `395bitstream/` 下的 HTML 报告；不能只留在
  `logs/` 目录里。
- `Project-XS/data/README.md` 当前只说明 generated 数据目录；真正用于本轮对比的
  `thermal2` / `thermal2_n<N>` 数据按 `Project-XPlus/data/README.md` 和
  `scripts/download_suitesparse_data.py` 准备。

## 1. 测试对象

四个主要 bitstream 路径固定从 `395bitstream/` 读取：

| 简称 | bitstream | kernel | 用途 |
| --- | --- | --- | --- |
| TAPA SpMV | `395bitstream/cuper-tapa-spmv-u55c-20260522.xclbin` | `Cuper` | 单次 TAPA Cuper SpMV |
| no-TAPA SpMV | `395bitstream/cuper-notapa-spmv-u55c-20260524.xclbin` | `cuper_packed_spmv_4ch_kernel` | 4ch no-TAPA single SpMV baseline |
| no-TAPA full-PCG | `395bitstream/cuper-notapa-pcg-fpga-u55c-20260522.xclbin` | `cuper_pcg_control_kernel` | no-TAPA 全流程 FPGA-PCG 代理 |
| TAPA full-PCG | `395bitstream/cuper-tapa-pcg-fpga-u55c-20260525.xclbin` | `CuperPcg` | TAPA Cuper + FPGA 内 PCG timed-debug 版 |

检查 bitstream 基本信息：

```bash
sha256sum 395bitstream/*.xclbin
rg -n "UUID|Frequency|Achieved Freq|Kernel:" 395bitstream/*.xclbin.info
```

当前 HTML 记录的关键信息：

| bitstream | UUID | DATA/HBM |
| --- | --- | --- |
| TAPA SpMV | `428b48ff-ec3b-e2d4-536b-97a8e654fea3` | DATA 174 MHz, HBM 448 MHz |
| no-TAPA SpMV 4ch | `f55d7632-a7dd-6b99-8692-dec5b0467a55` | DATA 166 MHz, HBM 450 MHz |
| no-TAPA full-PCG | `76c8fc93-2d50-f3b1-6dd1-951a43f475ce` | DATA 215 MHz, HBM 450 MHz |
| TAPA full-PCG timed-debug | `51132100-b217-df93-f4dd-05bfc169f820` | DATA 213 MHz, HBM 437 MHz |

如果当前 `395bitstream/README.md` 和 `.xclbin.info` 显示的 UUID/频率更新了，
以当前文件为准，并在测试记录里写明。

## 1.1 demo bitstream 对比规则

新生成的 bitstream 不能直接成为标准版。它必须先作为 demo 版放在
`395bitstream/`，用 `-demo` 后缀命名。测试时默认采用 demo-only 上板实测，
再用已有标准/基线记录做静态对照；不要自动重跑四大标准版本。

命名规则：

```text
395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin
395bitstream/cuper-tapa-pcg-fpga-u55c-20260527-demo.xclbin.info
395bitstream/cuper-tapa-spmv-u55c-20260527-demo.xclbin
395bitstream/cuper-tapa-spmv-u55c-20260527-demo.xclbin.info
```

四条基础版本仍然是唯一分类。任何 demo 都必须属于其中之一：

```text
cuper-tapa-pcg
cuper-tapa-spmv
cuper-notapa-pcg
cuper-notapa-spmv
```

对比规则：

1. 默认只跑新 demo：例如新的 `cuper-tapa-pcg-...-demo.xclbin` 只对该 demo
   跑本节规定的数据集和模式。
2. 同主线当前标准 bitstream、其它三条标准 bitstream、以及旧跨主线基线默认
   复用当前 HTML、`docs/bitstream_summaries/` 和已归档日志里的数据，只做静态对照，
   不做自动重跑。
3. 只有下面情况才重跑标准/旧基线：
   - 用户明确要求重跑；
   - 当前标准 bitstream 或 host 代码发生变化；
   - demo 结果和旧基线预期矛盾，需要排除板卡/XRT/数据集状态问题；
   - 要判断失败边界是否移动，且旧基线没有同口径记录。
4. 当前 SpMV 优化阶段优先做 `cuper-tapa-spmv` demo：把 `CuperPcg` 里的 PCG
   服务化 SpMV 路径抠出来，以单 SpMV kernel 形式测试。它属于
   `cuper-tapa-spmv` 主线 demo，默认只跑该 demo 的 single SpMV 数据集；标准基准
   复用满血 standalone TAPA Cuper SpMV 记录
   `395bitstream/cuper-tapa-spmv-u55c-20260522.xclbin`。
5. demo 的最低动态实测范围只针对 demo 本身；对 PCG 抽出版 `cuper-tapa-spmv`
   demo，至少跑 `thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、
   `thermal2_n262144` 和完整 `thermal2` 的 single SpMV，记录 `spmv_avg`、
   diff、rc、timeout 边界。对 TAPA full-PCG 候选，至少跑
   `thermal2_n16`、`thermal2_n65536`、`thermal2_n131072`、`thermal2_n262144`、
   完整 `thermal2` 的 init-only 与 1iter。若数据异常，再补中间规模。
6. demo 的测试报告必须写清楚：
   - demo 路径、UUID、SHA256
   - 本轮是否 demo-only；如果复用旧标准数据，要写明旧标准日志/HTML来源
   - 相同数据集、相同阈值、相同 timeout 下 demo 本身的退出状态
   - 相对既有标准/基线记录，哪些指标看起来更好，哪些退化，哪些失败边界变化
7. demo 数据必须写入 `395bitstream/cuper_spmv_u55c_compare_20260524.html`
   或当前对应 HTML 报告；HTML 视图细则见
   `docs/codex/workflows/reports.md` 的 “HTML 视图口径”。至少包含：
   - demo bitstream 信息；
   - demo-only 的退出边界；
   - 运行类型。single SpMV demo 写 `spmv_avg`、diff、rc、timeout；full-PCG demo
     写 init / 1iter 关键时间。
   - 简短结论；
   - 表格中的 demo 数据点。若趋势图仍使用历史标准/基线数据，必须在图注中明确
     写明“不包含当前 demo-only 新数据”。
   - 对 TAPA full-PCG demo，HTML 里的 `TAPA PCG 分段时间` 和
     `Init 与 1iter 差值` 必须优先展示本轮 demo-only 实测数据；标准/上一 demo/本 demo
     的对比另起一块展示，不要把这两个当前 demo 诊断表继续填成旧标准数据。
   - 对 TAPA full-PCG demo 的 HTML 三方对比，SpMV 组里的“标准”必须使用
     standalone TAPA Cuper SpMV 标准曲线
     `395bitstream/cuper-tapa-spmv-u55c-20260522.xclbin`，不是
     TAPA full-PCG 标准版里的内嵌 `iter_spmv` 分段；一次迭代组才使用
     TAPA full-PCG 标准版的 `1iter kernel_reported`。
   - 对 PCG 抽出版 `cuper-tapa-spmv` demo，HTML 里应把它标成“PCG SpMV 抽出版”
     或等价名称，直接和满血 standalone TAPA Cuper SpMV 的 `spmv_avg` 曲线/表格
     对比；不要把它写成 full-PCG 1iter demo。PCG 相关区域保留原数据但必须标明
     “本轮未跑 PCG，无 init/1iter 过程/无一次迭代新数据”；不要删除或用 single
     SpMV 数据覆盖 PCG 分段、Init 与 1iter 差值、一次迭代表格。
   - 如果 demo 改了分段语义，不能沿用旧表头。比如 raw `iter_spmv` 只覆盖
     packed AP 接收/缓存时，HTML 应标成 `iter recv`，并显式写出
     `AP path = iter recv + dot_p_ap` 后再拿去做 SpMV/AP 路径对比。
8. demo 的详细 Markdown 总结放入 `docs/bitstream_summaries/<版本目录>/`，
   版本目录建议使用 `YYYY-MM-DD-<主线>-<简短说明>/`。目录内必须维护
   `README.md`、`changes.md` 和 `testing.md`；其中 `testing.md` 记录测试命令、
   关键输出、失败边界和待补项目。正式 `source.diff` 只在测试确认性能提升，
   或用户明确要求保留功能边界修复补丁后更新；性能退步或失败的 demo 不覆盖上一份
   已验证有效的 `source.diff`。
9. 只有用户明确表示结果满意，demo 才能按 `docs/codex/coding.md` 的归档流程
   晋级并替换标准版。否则 demo 保持 demo 后缀，不能覆盖标准文件。

## 2. 数据集准备

本轮对比使用 `Schmid/thermal2` 及其前导主子矩阵：

```text
thermal2_n16
thermal2_n1024
thermal2_n4096
thermal2_n16384
thermal2_n65536
thermal2_n131072
thermal2_n262144
thermal2
```

期望目录：

```text
data/suitesparse/Schmid/csr/thermal2_n16
data/suitesparse/Schmid/csr/thermal2_n1024
...
data/suitesparse/Schmid/csr/thermal2
```

在 Project-XPlus 内可用下面命令下载/生成：

```bash
make download-suitesparse-data DATASETS="thermal2_n16 thermal2_n1024 thermal2_n4096 thermal2_n16384 thermal2_n65536 thermal2_n131072 thermal2_n262144 thermal2"
```

或先查看脚本支持项：

```bash
make list-suitesparse-data
```

说明：

- `thermal2_n<N>` 由完整 `thermal2` 的前 `N x N` 主子矩阵生成。
- `thermal2` 完整规模是 `N=1,228,045`，`nnz=8,580,313`。
- 如果服务器侧从 `Project-XS/data/` 上传数据，只要最终 CSR 数据目录满足
  Project-XPlus host 的 `CsrDataset::load()` 格式即可；测试命令可以把
  `DATASET=` 指向上传后的实际目录。
- `Project-XPlus/data/README.md` 里关于 `kMaxN=1024` 的限制主要针对默认
  cg_common 硬件路径；本文件的 Cuper bitstream 对比使用的是专门的 Cuper host/kernel。

数据存在性检查：

```bash
for d in thermal2_n16 thermal2_n1024 thermal2_n4096 thermal2_n16384 thermal2_n65536 thermal2_n131072 thermal2_n262144 thermal2; do
  test -d "data/suitesparse/Schmid/csr/$d" || echo "missing: $d"
done
```

## 3. Host 准备

先构建运行所需 host：

```bash
make cuper-tapa-pcg-host
make cuper-tapa-pcg-fpga-host
make cuper-notapa-pcg-xrt-host
make cuper-control-xrt-host
```

如果机器刚重启，先确认环境：

```bash
which xbutil || true
xbutil examine | sed -n '1,80p'
```

运行硬件前确认 U55C 空闲，尤其不要和另一个 `hw` run 同时抢同一张板。

## 4. 单点 smoke test

先用最小数据确认 bitstream 能加载、host 参数能传进去：

```bash
make run-cuper-tapa-spmv TARGET=hw \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  BITFILE=395bitstream/cuper-tapa-spmv-u55c-20260522.xclbin \
  SPMV_REPEATS=3 DIFF_TOL=1e-1

make run-cuper-notapa-spmv-4ch-xrt TARGET=hw \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  BITFILE=395bitstream/cuper-notapa-spmv-u55c-20260524.xclbin \
  SPMV_REPEATS=3 DIFF_TOL=1e-1

make run-cuper-pcg-tapa-fpga \
  DATASET=data/suitesparse/Schmid/csr/thermal2_n16 \
  BITFILE=395bitstream/cuper-tapa-pcg-fpga-u55c-20260525.xclbin \
  MAX_ITERS=1 DIFF_TOL=1e-4
```

smoke test 预期：

- single SpMV 能返回且 diff 在阈值内。
- TAPA full-PCG `MAX_ITERS=1` 能返回，控制寄存器应出现启动前 `0x4`、
  完成后 `0xe` 或 host 输出里的等价完成状态。

## 5. 完整对比矩阵

建议把日志按日期放到单独目录，避免覆盖旧结果：

```bash
stamp=$(date +%Y%m%d_%H%M%S)
logdir="logs/codex_bitstream_test_${stamp}"
mkdir -p "$logdir"
```

### 5.1 TAPA single SpMV

口径：

- `--spmv-only`
- 输入向量固定使用数据集 `b.txt`
- CPU `Dataset::spmv` 校验
- `SPMV_REPEATS=3`
- 大数据失败点外层用 180s timeout
- 对 PCG 抽出版 `cuper-tapa-spmv` demo，把 `BITFILE` 换成 demo xclbin；
  不需要重跑满血标准版，除非用户要求或旧记录不足。

命令模板：

```bash
for d in thermal2_n16 thermal2_n1024 thermal2_n4096 thermal2_n16384 thermal2_n65536 thermal2_n131072 thermal2_n262144 thermal2; do
  timeout 180s make run-cuper-tapa-spmv TARGET=hw \
    DATASET="data/suitesparse/Schmid/csr/$d" \
    BITFILE=395bitstream/cuper-tapa-spmv-u55c-20260522.xclbin \
    SPMV_REPEATS=3 DIFF_TOL=1e-1 \
    2>&1 | tee "$logdir/tapa_spmv_${d}.log"
  echo "${d}: ${PIPESTATUS[0]}" | tee -a "$logdir/tapa_spmv.status"
done
```

预期边界：

- `thermal2_n16` 到 `thermal2_n131072` 正常返回。
- `thermal2_n262144` 和完整 `thermal2` 在 180s 内不返回，记为预期 timeout。
- 成功点性能量级：`thermal2_n131072` 约 `0.220 ms`，约 `7.88 GFLOP/s`。

### 5.2 no-TAPA 4ch single SpMV

口径同 single SpMV，但 kernel 是 `cuper_packed_spmv_4ch_kernel`。

```bash
for d in thermal2_n16 thermal2_n1024 thermal2_n4096 thermal2_n16384 thermal2_n65536 thermal2_n131072 thermal2_n262144 thermal2; do
  timeout 180s make run-cuper-notapa-spmv-4ch-xrt TARGET=hw \
    DATASET="data/suitesparse/Schmid/csr/$d" \
    BITFILE=395bitstream/cuper-notapa-spmv-u55c-20260524.xclbin \
    SPMV_REPEATS=3 DIFF_TOL=1e-1 \
    2>&1 | tee "$logdir/notapa_spmv_4ch_${d}.log"
  echo "${d}: ${PIPESTATUS[0]}" | tee -a "$logdir/notapa_spmv_4ch.status"
done
```

预期边界：

- `thermal2_n16` 到完整 `thermal2` 全部返回。
- diff 阈值用 `DIFF_TOL=1e-1`。
- 大数据吞吐约 `0.08 GFLOP/s`；完整 `thermal2` kernel 平均约 `200.62 ms`。

### 5.3 no-TAPA full-PCG init proxy

这版没有纯 SpMV 分段计时，用 `TAU=1e100 MAX_ITERS=1` 让 kernel 做完
初始化 `A*x0`、`r/z/p` 后在 `iter=0` 收敛返回，作为 init SpMV 代理。

```bash
for d in thermal2_n16 thermal2_n1024 thermal2_n4096 thermal2_n16384 thermal2_n65536 thermal2_n131072 thermal2_n262144 thermal2; do
  timeout 180s make run-cuper-control-xrt TARGET=hw \
    DATASET="data/suitesparse/Schmid/csr/$d" \
    BITFILE=395bitstream/cuper-notapa-pcg-fpga-u55c-20260522.xclbin \
    TAU=1e100 MAX_ITERS=1 DIFF_TOL=1e-1 \
    2>&1 | tee "$logdir/notapa_fullpcg_init_proxy_${d}.log"
  echo "${d}: ${PIPESTATUS[0]}" | tee -a "$logdir/notapa_fullpcg_init_proxy.status"
done
```

预期边界：

- `thermal2_n16` 到完整 `thermal2` 全部返回。
- 所有成功点应为 `iter=0` / `converged` 或等价输出。
- 完整 `thermal2` kernel 时间量级约 `169.19 ms`。

### 5.4 TAPA full-PCG init-only proxy

同样用 `TAU=1e100 MAX_ITERS=1`，观察 TAPA `CuperPcg` 初始化路径。

```bash
for d in thermal2_n16 thermal2_n1024 thermal2_n4096 thermal2_n16384 thermal2_n65536 thermal2_n131072 thermal2_n262144 thermal2; do
  timeout 180s make run-cuper-pcg-tapa-fpga \
    DATASET="data/suitesparse/Schmid/csr/$d" \
    BITFILE=395bitstream/cuper-tapa-pcg-fpga-u55c-20260525.xclbin \
    TAU=1e100 MAX_ITERS=1 DIFF_TOL=1e-1 \
    2>&1 | tee "$logdir/tapa_fullpcg_init_proxy_${d}.log"
  echo "${d}: ${PIPESTATUS[0]}" | tee -a "$logdir/tapa_fullpcg_init_proxy.status"
done
```

预期边界：

- `thermal2_n16` 到 `thermal2_n262144` 正常返回。
- 成功点均应为 `iter=0` 收敛。
- 完整 `thermal2` 当前预期失败：direct register 启动后 `ctrl=0x0`，
  host 报 `direct register run did not complete` 或等价错误。

### 5.5 TAPA full-PCG 1iter proxy

用 `MAX_ITERS=1` 做一次 PCG 迭代代理。它不是纯 SpMV，包含第二次
`A*p`、`p^T A p`、`AP` 写回、`x/r/z/p` 更新、`rz/rr` 归约和 controller 开销。

```bash
for d in thermal2_n16 thermal2_n1024 thermal2_n4096 thermal2_n16384 thermal2_n65536 thermal2_n131072 thermal2_n262144 thermal2; do
  timeout 180s make run-cuper-pcg-tapa-fpga \
    DATASET="data/suitesparse/Schmid/csr/$d" \
    BITFILE=395bitstream/cuper-tapa-pcg-fpga-u55c-20260525.xclbin \
    MAX_ITERS=1 DIFF_TOL=1e-4 \
    2>&1 | tee "$logdir/tapa_fullpcg_1iter_${d}.log"
  echo "${d}: ${PIPESTATUS[0]}" | tee -a "$logdir/tapa_fullpcg_1iter.status"
done
```

预期边界：

- `thermal2_n16` 到 `thermal2_n262144` 正常返回。
- 成功点一般是 `iter=1 max_iter`，小矩阵也可能数值上提前满足收敛。
- 控制寄存器应从启动前 `0x4` 到完成后 `0xe`。
- 完整 `thermal2` 当前预期失败：direct register 启动后 `ctrl=0x0`。

## 6. 预期结果表

下面是 HTML 报告中记录的关键结果。服务器重测时允许小幅波动，但返回/超时边界
应优先匹配。

| Dataset | N | nnz | TAPA SpMV avg ms | no-TAPA 4ch avg ms | no-TAPA proxy ms | TAPA init proxy ms | TAPA 1iter ms | 预期状态 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `thermal2_n16` | 16 | 16 | 0.0670 | 0.2566 | 0.1150 | 14.9964 | 14.9905 | 全部 ok |
| `thermal2_n1024` | 1,024 | 6,362 | 0.0727 | 0.3676 | 0.2641 | 11.5716 | 11.7261 | 全部 ok |
| `thermal2_n4096` | 4,096 | 26,204 | 0.0780 | 0.8157 | 0.6863 | 11.3828 | 13.8168 | 全部 ok |
| `thermal2_n16384` | 16,384 | 107,908 | 0.0866 | 2.7311 | 2.3730 | 14.5852 | 22.2673 | 全部 ok |
| `thermal2_n65536` | 65,536 | 437,000 | 0.1379 | 10.8403 | 9.1265 | 25.3854 | 54.5996 | 全部 ok |
| `thermal2_n131072` | 131,072 | 866,060 | 0.2197 | 21.4812 | 18.6525 | 39.8743 | 99.8461 | 全部 ok |
| `thermal2_n262144` | 262,144 | 1,748,980 | timeout | 42.8973 | 36.2175 | 68.9655 | 188.8202 | TAPA SpMV timeout；其他 ok |
| `thermal2` | 1,228,045 | 8,580,313 | timeout | 200.6181 | 169.1891 | failed | failed | TAPA SpMV timeout；TAPA full-PCG failed；no-TAPA ok |

判定要点：

- TAPA single SpMV 最大可靠返回点：`thermal2_n131072`。
- no-TAPA 4ch single SpMV 最大可靠返回点：完整 `thermal2`。
- no-TAPA full-PCG init proxy 最大可靠返回点：完整 `thermal2`。
- TAPA full-PCG timed-debug 最大可靠返回点：`thermal2_n262144`。
- 完整 `thermal2` 上 TAPA full-PCG 当前预期失败，不要直接判成回归；要记录
  `ctrl`、host 错误和日志。

## 7. TAPA full-PCG 分段计时核验

timed-debug TAPA full-PCG 会输出 `[stage-ms]` 或等价分段信息。重点核验：

| Dataset | init kernel | 1iter kernel | init ctrl | 1iter ctrl | init SpMV | iter SpMV | ctrl delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `thermal2_n16` | 14.9964 | 14.9905 | 0.0035 | 0.0096 | 0.0025 | 0.0020 | 0.0061 |
| `thermal2_n1024` | 11.5716 | 11.7261 | 0.1571 | 0.4806 | 0.1392 | 0.1280 | 0.3235 |
| `thermal2_n4096` | 11.3828 | 13.8168 | 0.6228 | 1.9149 | 0.5546 | 0.5123 | 1.2921 |
| `thermal2_n16384` | 14.5852 | 22.2673 | 2.4874 | 7.6622 | 2.2159 | 2.0582 | 5.1749 |
| `thermal2_n65536` | 25.3854 | 54.5996 | 9.9404 | 30.6452 | 8.8584 | 8.2380 | 20.7047 |
| `thermal2_n131072` | 39.8743 | 99.8461 | 19.8779 | 61.2868 | 17.7141 | 16.4798 | 41.4088 |
| `thermal2_n262144` | 68.9655 | 188.8202 | 39.7527 | 122.5640 | 35.4265 | 32.9573 | 82.8113 |

解释：

- `init kernel` / `1iter kernel` 来自 host 输出的 `kernel_reported`。
- `init ctrl` / `1iter ctrl` 来自 FPGA controller 内部计时。
- `init SpMV` / `iter SpMV` 是 controller 内部 SpMV 分段。
- `1iter kernel - init kernel` 不是纯 SpMV，它包含第二轮 PCG 的向量更新和归约。
- 在 `thermal2_n262144`，init SpMV 和 iter SpMV 分别约 `35.43 ms` / `32.96 ms`；
  controller delta 约 `82.81 ms`，说明额外开销不只在 SpMV。

## 8. 日志收集与摘要

每轮测试至少保留：

```text
logs/codex_bitstream_test_<stamp>/
  *.log
  *.status
  summary.md
```

`summary.md` 建议包含：

```text
测试日期
机器 / BDF / XRT 版本
git commit
bitstream SHA256 / UUID
dataset 是否本地生成或从 Project-XS 上传
每条命令的退出码
成功/timeout/failed 边界
和本文件预期是否一致
```

可用下面命令快速抽取关键信息：

```bash
rg -n "timing-ms|stage-ms|status|iter=|converged|max_iter|max_abs_diff|max_rel_diff|ctrl|direct register|timeout|ERROR|failed" "$logdir"
```

## 9. 发现不一致时先查什么

1. 数据集目录是否对齐，是否误用了 Project-XS 的 generated 小数据。
2. `BITFILE=` 是否真的传给了 Makefile 目标。
3. no-TAPA SpMV 是否使用了 `run-cuper-notapa-spmv-4ch-xrt` 和
   `cuper_packed_spmv_4ch_kernel`。
4. TAPA full-PCG 完整 `thermal2` 失败时，先记录 `ctrl` 值和 direct-register
   错误，不要先改 kernel。
5. 如果 TAPA SpMV 在 `thermal2_n131072` 也不返回，优先查 TAPA runtime、
   host buffer / mmap 生命周期、XRT 版本和板卡状态。
6. 如果 no-TAPA 4ch 完整 `thermal2` 不返回，优先查 xclbin 是否不是当前
   `395bitstream/cuper-notapa-spmv-u55c-20260524.xclbin`，以及 HBM/板卡复位状态。
