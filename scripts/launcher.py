#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


KMAX_N = 1024
THERMAL2_SIZE = 1_228_045
LONG_BUILD_CONFIRM = "BUILD"


@dataclass
class Dataset:
    name: str
    path: Path
    n: int
    nnz: int
    source: str
    runnable: bool


@dataclass(frozen=True)
class CodeVersion:
    key: str
    title: str
    description: str
    runner: str
    requires_kmax: bool
    spmv_arg: str | None = None


CODE_VERSIONS = [
    CodeVersion(
        key="local_csr",
        title="多 kernel 普通版",
        description="本地直接调用 spmv_csr/init/dot/update_*，用于对照旧的 CSR 多阶段流程。",
        runner="local",
        requires_kmax=True,
        spmv_arg="csr",
    ),
    CodeVersion(
        key="local_blocked",
        title="多 kernel 分块版",
        description="本地直接调用同一套多阶段流程，但 SpMV 入口切到 spmv_blocked_kernel。",
        runner="local",
        requires_kmax=True,
        spmv_arg="blocked",
    ),
    CodeVersion(
        key="xrt_control",
        title="当前单 control-kernel 版",
        description="XRT xclbin 只启动 pcg_control_kernel，PCG 控制、windowed block SpMV 和退出判断都在 FPGA kernel 内。",
        runner="xrt",
        requires_kmax=False,
    ),
    CodeVersion(
        key="cuper_pcg",
        title="Cuper-PCG 版",
        description="Project-XPlus 新 PCG 版本：host 控制 PCG，SpMV 阶段使用 Cuper slice/window 风格的 FP32 软件适配。",
        runner="cuper_pcg",
        requires_kmax=False,
    ),
    CodeVersion(
        key="cuper_pcg_tapa",
        title="Cuper-PCG TAPA 版",
        description="Project-XPlus 新 PCG 版本：host 控制 PCG，SpMV 阶段直接调用 DLC/Cuper 的 TAPA kernel。",
        runner="cuper_pcg_tapa",
        requires_kmax=False,
    ),
    CodeVersion(
        key="cuper_control",
        title="Cuper-PCG control-kernel 版",
        description="Project-XPlus 新 PCG control kernel：host launch 一次，PCG 控制和 Cuper column-batch/row-tile SpMV 都在 kernel 内。",
        runner="cuper_control",
        requires_kmax=False,
    ),
]


def count_words(path: Path) -> int:
    count = 0
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            count += len(line.split())
    return count


def read_meta(dataset_dir: Path) -> dict[str, str]:
    json_path = dataset_dir / "meta.json"
    if json_path.exists():
        with json_path.open("r", encoding="utf-8") as handle:
            raw = json.load(handle)
        return {str(key): str(value) for key, value in raw.items()}

    txt_path = dataset_dir / "meta.txt"
    if txt_path.exists():
        meta: dict[str, str] = {}
        for line in txt_path.read_text(encoding="utf-8").splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                meta[key.strip()] = value.strip()
        return meta

    return {}


def load_dataset(dataset_dir: Path, root: Path) -> Dataset:
    meta = read_meta(dataset_dir)
    rel_path = dataset_dir.relative_to(root)
    name = meta.get("name", dataset_dir.name)
    source = meta.get("source", str(rel_path))

    if "n" in meta and "nnz" in meta:
        n = int(meta["n"])
        nnz = int(meta["nnz"])
    elif "shape" in meta and "nnz_csr" in meta:
        shape_text = meta["shape"].strip("[]")
        n = int(shape_text.split(",", 1)[0])
        nnz = int(meta["nnz_csr"])
    else:
        row_ptr_count = count_words(dataset_dir / "row_ptr.txt")
        n = row_ptr_count - 1
        nnz = count_words(dataset_dir / "col_idx.txt")

    return Dataset(
        name=name,
        path=dataset_dir,
        n=n,
        nnz=nnz,
        source=source,
        runnable=n <= KMAX_N,
    )


def discover_datasets(root: Path) -> list[Dataset]:
    datasets = []
    for row_ptr in sorted((root / "data").glob("**/row_ptr.txt")):
        datasets.append(load_dataset(row_ptr.parent, root))
    return datasets


def ask_index(prompt: str, count: int) -> int | None:
    while True:
        try:
            answer = input(prompt).strip().lower()
        except EOFError:
            print()
            return None
        if answer in {"q", "quit", "exit"}:
            return None
        if answer.isdigit():
            index = int(answer)
            if 1 <= index <= count:
                return index - 1
        print(f"请输入 1-{count}，或 q 退出。")


def ask_text(prompt: str) -> str | None:
    try:
        return input(prompt).strip()
    except EOFError:
        print()
        return None


def pause(message: str = "按回车返回菜单...") -> None:
    try:
        input(message)
    except EOFError:
        print()


def shell_quote(path: Path | str) -> str:
    return shlex.quote(str(path))


def run_command(command: list[str], env: dict[str, str] | None = None, cwd: Path | None = None) -> int:
    print()
    print("$ " + " ".join(shell_quote(part) for part in command), flush=True)
    print()
    return subprocess.call(command, env=env, cwd=str(cwd) if cwd is not None else None)


def run_shell(shell_cmd: str, cwd: Path) -> int:
    print()
    print("$ " + shell_cmd, flush=True)
    print()
    return subprocess.call(["bash", "-lc", shell_cmd], cwd=str(cwd))


def run_xrt_command(executable: Path,
                    xclbin: Path,
                    dataset: Dataset,
                    vitis_settings: Path,
                    emulation_mode: str | None,
                    cwd: Path,
                    emconfig_path: Path | None = None,
                    json_out: Path | None = None,
                    txt_out: Path | None = None) -> int:
    if not xclbin.exists():
        print(f"缺少 xclbin: {xclbin}")
        return 1
    if not vitis_settings.exists():
        print(f"缺少 Vitis settings: {vitis_settings}")
        return 1

    cmd_parts = [
        "source",
        shell_quote(vitis_settings),
        ">/dev/null",
        "2>&1",
        "&&",
    ]
    if emulation_mode is not None:
        emconfig_path = emconfig_path if emconfig_path is not None else cwd
        cmd_parts.extend([
            f"EMCONFIG_PATH={shell_quote(emconfig_path)}",
            f"XCL_EMULATION_MODE={shlex.quote(emulation_mode)}",
        ])
    cmd_parts.extend([
        shell_quote(executable),
        shell_quote(xclbin),
        shell_quote(dataset.path),
    ])
    if json_out is not None and txt_out is not None:
        cmd_parts.extend([
            "--json-out",
            shell_quote(json_out),
            "--txt-out",
            shell_quote(txt_out),
        ])

    shell_cmd = " ".join(cmd_parts)
    print()
    print("$ " + shell_cmd, flush=True)
    print()
    return subprocess.call(["bash", "-lc", shell_cmd], cwd=str(cwd))


def render_report(root: Path, json_path: Path, html_path: Path, html_static_path: Path) -> None:
    if not json_path.exists():
        raise RuntimeError(f"报告 JSON 未生成: {json_path}")
    render = root / "scripts" / "render_report.py"
    subprocess.check_call([sys.executable, str(render), "interactive", str(json_path), str(html_path)], cwd=str(root))
    subprocess.check_call([sys.executable, str(render), "static", str(json_path), str(html_static_path)], cwd=str(root))


def print_datasets(datasets: list[Dataset], root: Path) -> None:
    print()
    print("可用数据集：")
    if not datasets:
        print("  当前没有找到 CSR 数据集。可以先选择 d 下载/生成 SuiteSparse 数据。")
        print()
        return
    for idx, dataset in enumerate(datasets, start=1):
        status = "build/run" if dataset.runnable else "staged"
        rel = dataset.path.relative_to(root)
        print(f"  {idx:2d}. [{status:9s}] {dataset.name:16s} n={dataset.n:<8d} nnz={dataset.nnz:<10d} {rel}")
    print()
    print("说明：build/run 表示本地多 kernel 版也可构建/运行；staged 表示数据已在本地，XRT control-kernel 版可尝试构建/运行。")


def print_code_versions() -> None:
    print()
    print("实现版本：")
    for index, version in enumerate(CODE_VERSIONS, start=1):
        limit = "kMaxN<=1024" if version.requires_kmax else "大矩阵路径"
        print(f"  {index}. {version.title} [{limit}]")
        print(f"     {version.description}")


def ask_code_version(dataset: Dataset) -> CodeVersion | None:
    while True:
        print_code_versions()
        answer = ask_text("选择实现版本，或 q 返回: ")
        if answer is None or answer.lower() in {"q", "quit", "exit"}:
            return None
        if answer.isdigit():
            index = int(answer)
            if 1 <= index <= len(CODE_VERSIONS):
                version = CODE_VERSIONS[index - 1]
                if version.requires_kmax and dataset.n > KMAX_N:
                    print(f"{version.title} 受 kMaxN={KMAX_N} 限制，不能运行 n={dataset.n} 的数据集。")
                    pause()
                    continue
                return version
        print(f"请输入 1-{len(CODE_VERSIONS)}，或 q 返回。")


def print_result(rc: int) -> None:
    print()
    if rc == 0:
        print("运行完成：成功")
    else:
        print(f"运行完成：失败，退出码 {rc}")
    pause()


def build_local_host(root: Path) -> int:
    return run_command(["make", "local-host"], cwd=root)


def build_xrt_host(root: Path) -> int:
    return run_command(["make", "xrt-host"], cwd=root)


def build_cuper_pcg_host(root: Path) -> int:
    return run_command(["make", "cuper-pcg-host"], cwd=root)


def build_cuper_tapa_pcg_host(root: Path) -> int:
    return run_command(["make", "cuper-tapa-pcg-host"], cwd=root)


def build_sw_xclbin(root: Path) -> int:
    print("sw_emu xclbin 会触发 Vitis 编译，通常比 host 编译慢。")
    confirm = ask_text("输入 BUILD 开始，直接回车取消: ")
    if confirm != LONG_BUILD_CONFIRM:
        print("已取消。")
        return 0
    return run_command(["make", "build-sw"], cwd=root)


def build_hw_xclbin(root: Path) -> int:
    print("硬件 bitstream 编译可能持续数小时，并会更新 build/hw/cgsolver_jacobi_pcg.xclbin。")
    confirm = ask_text("确认要重新综合/链接硬件 bitstream，请输入 BUILD: ")
    if confirm != LONG_BUILD_CONFIRM:
        print("已取消。")
        return 0
    return run_command(["make", "build-hw"], cwd=root)


def run_cuper_launcher(root: Path) -> int:
    return run_command(["make", "cuper-launch"], cwd=root)


def data_menu(root: Path) -> int:
    script = root / "scripts" / "download_suitesparse_data.py"
    while True:
        print()
        print("数据集下载/生成：")
        print("  1. 下载/生成默认数据 thermal2_n1024")
        print("  2. 按大小从完整 thermal2 生成 thermal2_n<N>")
        print("  3. 下载/生成全部登记数据")
        print("  4. 查看可下载数据集")
        print("  5. 手动输入 DATASETS 值")
        print("  q. 返回")
        answer = ask_text("选择操作: ")
        if answer is None or answer.lower() in {"q", "quit", "exit"}:
            return 0

        if answer == "1":
            rc = run_command([sys.executable, str(script), "--datasets", "thermal2_n1024"], cwd=root)
            print_result(rc)
            return rc

        if answer == "2":
            raw_size = ask_text(f"输入 N (1-{THERMAL2_SIZE})，例如 2048: ")
            if raw_size is None or raw_size.lower() in {"q", "quit", "exit"}:
                return 0
            if not raw_size.isdigit():
                print("N 必须是正整数。")
                pause()
                continue
            size = int(raw_size)
            if size <= 0 or size > THERMAL2_SIZE:
                print(f"N 必须在 1-{THERMAL2_SIZE} 之间。")
                pause()
                continue
            dataset_key = f"thermal2_n{size}"
            print(f"将生成完整 thermal2 的前 {size}x{size} 主子矩阵: {dataset_key}")
            print(f"注意：当前硬件 bitstream 的 kMaxN={KMAX_N}，超过 {KMAX_N} 的数据只能先下载/存档，不能直接跑现有硬件。")
            rc = run_command([sys.executable, str(script), "--datasets", dataset_key], cwd=root)
            print_result(rc)
            return rc

        if answer == "3":
            print("这会下载并转换全部登记数据，当前本地总量约数百 MB。")
            confirm = ask_text("确认继续请输入 DATA: ")
            if confirm != "DATA":
                print("已取消。")
                pause()
                return 0
            rc = run_command([sys.executable, str(script), "--datasets", "all"], cwd=root)
            print_result(rc)
            return rc

        if answer == "4":
            rc = run_command([sys.executable, str(script), "--list"], cwd=root)
            print_result(rc)
            return rc

        if answer == "5":
            raw_value = ask_text("输入 DATASETS 值，例如 nasa2910 thermal2_n4096 或 all: ")
            if raw_value is None or raw_value.lower() in {"q", "quit", "exit"}:
                return 0
            items = [part for part in raw_value.replace(",", " ").split() if part]
            if not items:
                print("没有输入数据集。")
                pause()
                continue
            rc = run_command([sys.executable, str(script), "--datasets", *items], cwd=root)
            print_result(rc)
            return rc

        print("请输入 1-5，或 q 返回。")


def report_menu(root: Path) -> int:
    while True:
        print()
        print("硬件报告/分析：")
        print("  1. export Vivado power report from existing hw build")
        print("  2. export all Vivado analysis reports from existing hw build")
        print("  3. XRT board electrical/power snapshot")
        print("  q. 返回")
        answer = ask_text("选择操作: ")
        if answer is None or answer.lower() in {"q", "quit", "exit"}:
            return 0

        if answer == "1":
            print("这会尝试从现有 build/hw 的 Vivado project/impl run 导出 report_power。")
            print("如果旧 build 没保留 implemented run，需要重新 make build-hw 后再导出。")
            rc = run_command(["make", "vivado-power-report"], cwd=root)
            print_result(rc)
            return rc
        if answer == "2":
            print("这会尝试导出 power/utilization/timing/methodology/drc/clock/qor 等 Vivado 分析报告。")
            print("单个可选报告失败时脚本会记录在 analysis_manifest.txt，继续导出其它报告。")
            rc = run_command(["make", "vivado-analysis"], cwd=root)
            print_result(rc)
            return rc
        if answer == "3":
            print("这会调用 xbutil 读取板卡 electrical/power 传感器快照；需要机器上能看到 XRT 设备。")
            rc = run_command(["make", "xrt-power-snapshot"], cwd=root)
            print_result(rc)
            return rc

        print("请输入 1-3，或 q 返回。")


def action_menu(dataset: Dataset, args: argparse.Namespace, root: Path) -> int:
    while True:
        print()
        print(f"已选择: {dataset.name}  n={dataset.n}  nnz={dataset.nnz}")
        print(f"路径: {dataset.path}")
        print(f"来源: {dataset.source}")
        if not dataset.runnable:
            print(f"注意：n>{KMAX_N}，本地多 kernel 版不能跑；Cuper-PCG 和 XRT control-kernel 版可以尝试运行。")

        version = ask_code_version(dataset)
        if version is None:
            return 0

        while True:
            print()
            print(f"已选择实现: {version.title}")
            print()
            print("构建/运行：")
            if version.runner == "local":
                print("  1. build local host")
                print("  2. build/run local host")
            elif version.runner == "cuper_pcg":
                print("  1. build Cuper-PCG host")
                print("  2. build/run Cuper-PCG host")
                print("  3. open DLC/Cuper launcher")
                print("  4. build DLC/Cuper HW bitstream in tmux")
            elif version.runner == "cuper_pcg_tapa":
                print("  1. build Cuper-PCG TAPA host")
                print("  2. build/run Cuper-PCG TAPA host")
                print("  3. build/run Cuper-PCG TAPA host smoke test")
                print("  4. build DLC/Cuper HW bitstream in tmux")
            elif version.runner == "cuper_control":
                print("  1. build Cuper-PCG control local host")
                print("  2. build/run Cuper-PCG control local host")
                print("  3. build Cuper-PCG control XRT host")
                print("  4. build sw_emu Cuper-PCG control xclbin")
                print("  5. build/run sw_emu Cuper-PCG control xclbin")
                print("  6. build hw Cuper-PCG control xclbin in tmux")
            else:
                print("  1. build XRT host")
                print("  2. build sw_emu xclbin")
                print("  3. build hw bitstream")
                print("  4. build/run hardware xclbin")
                print("  5. build/run hardware xclbin + report")
                print("  6. build/run sw_emu xclbin")
                print("  7. build/run sw_emu xclbin + report")
            print("  q. 返回实现版本选择")
            try:
                answer = input("选择操作: ").strip().lower()
            except EOFError:
                print()
                return 0
            if answer in {"q", "quit", "exit"}:
                break

            if version.runner == "cuper_pcg" and answer == "1":
                rc = build_cuper_pcg_host(root)
                print_result(rc)
                return rc

            if version.runner == "cuper_pcg" and answer == "2":
                rc = run_command([str(args.cuper_pcg_host), str(dataset.path)], cwd=root)
                print_result(rc)
                return rc

            if version.runner == "cuper_pcg" and answer == "3":
                rc = run_cuper_launcher(root)
                print_result(rc)
                return rc

            if version.runner == "cuper_pcg" and answer == "4":
                rc = run_command(["make", "cuper-hw-tmux"], cwd=root)
                print_result(rc)
                return rc

            if version.runner == "cuper_pcg_tapa" and answer == "1":
                rc = build_cuper_tapa_pcg_host(root)
                print_result(rc)
                return rc

            if version.runner == "cuper_pcg_tapa" and answer == "2":
                rc = run_command([str(args.cuper_tapa_pcg_host), str(dataset.path)], cwd=root)
                print_result(rc)
                return rc

            if version.runner == "cuper_pcg_tapa" and answer == "3":
                rc = run_command([
                    str(args.cuper_tapa_pcg_host),
                    str(dataset.path),
                    "--max-iters",
                    "1",
                    "--tau",
                    "1e6",
                    "--diff-tol",
                    "1e-1",
                ], cwd=root)
                print_result(rc)
                return rc

            if version.runner == "cuper_pcg_tapa" and answer == "4":
                rc = run_command(["make", "cuper-hw-tmux"], cwd=root)
                print_result(rc)
                return rc

            if version.runner == "cuper_control" and answer == "1":
                rc = run_command(["make", "cuper-control-local-host"], cwd=root)
                print_result(rc)
                return rc

            if version.runner == "cuper_control" and answer == "2":
                rc = run_command([str(args.cuper_control_local_host), str(dataset.path)], cwd=root)
                print_result(rc)
                return rc

            if version.runner == "cuper_control" and answer == "3":
                rc = run_command(["make", "cuper-control-xrt-host"], cwd=root)
                print_result(rc)
                return rc

            if version.runner == "cuper_control" and answer == "4":
                rc = run_command(["make", "build-cuper-control-sw"], cwd=root)
                print_result(rc)
                return rc

            if version.runner == "cuper_control" and answer == "5":
                rc = run_command([
                    "make",
                    "run-cuper-control-xrt",
                    "TARGET=sw_emu",
                    f"DATASET={dataset.path}",
                ], cwd=root)
                print_result(rc)
                return rc

            if version.runner == "cuper_control" and answer == "6":
                print("硬件 bitstream 编译可能持续数小时。")
                confirm = ask_text("确认要在 tmux 里构建 Cuper-PCG control HW，请输入 BUILD: ")
                if confirm != LONG_BUILD_CONFIRM:
                    print("已取消。")
                    pause()
                    continue
                rc = run_command(["make", "cuper-control-hw-tmux"], cwd=root)
                print_result(rc)
                return rc

            if version.runner == "local" and answer == "1":
                rc = build_local_host(root)
                print_result(rc)
                return rc

            if version.runner == "local" and answer == "2":
                command = [str(args.local_host), str(dataset.path)]
                if version.spmv_arg is not None:
                    command.extend(["--spmv", version.spmv_arg])
                rc = run_command(command, cwd=root)
                print_result(rc)
                return rc

            if version.runner == "xrt" and answer == "1":
                rc = build_xrt_host(root)
                print_result(rc)
                return rc

            if version.runner == "xrt" and answer == "2":
                rc = build_sw_xclbin(root)
                print_result(rc)
                return rc

            if version.runner == "xrt" and answer == "3":
                rc = build_hw_xclbin(root)
                print_result(rc)
                return rc

            if version.runner == "xrt" and answer == "4":
                rc = run_xrt_command(
                    args.xrt_host,
                    args.hw_xclbin,
                    dataset,
                    args.vitis_settings,
                    None,
                    root,
                )
                print_result(rc)
                return rc

            if version.runner == "xrt" and answer == "5":
                reports = root / "reports"
                reports.mkdir(parents=True, exist_ok=True)
                stem = f"HW_{dataset.name}"
                json_out = reports / f"{stem}.json"
                txt_out = reports / f"{stem}.txt"
                html_out = reports / f"{stem}.html"
                html_static_out = reports / f"{stem}_static.html"
                rc = run_xrt_command(
                    args.xrt_host,
                    args.hw_xclbin,
                    dataset,
                    args.vitis_settings,
                    None,
                    root,
                    json_out=json_out,
                    txt_out=txt_out,
                )
                if rc == 0:
                    render_report(root, json_out, html_out, html_static_out)
                    print(f"report json: {json_out}")
                    print(f"report txt : {txt_out}")
                    print(f"report html: {html_out}")
                    print(f"report html static: {html_static_out}")
                print_result(rc)
                return rc

            if version.runner == "xrt" and answer == "6":
                rc = run_xrt_command(
                    args.xrt_host,
                    args.sw_xclbin,
                    dataset,
                    args.vitis_settings,
                    "sw_emu",
                    root,
                    args.sw_emu_dir,
                )
                print_result(rc)
                return rc

            if version.runner == "xrt" and answer == "7":
                reports = root / "reports"
                reports.mkdir(parents=True, exist_ok=True)
                stem = f"SW_{dataset.name}"
                json_out = reports / f"{stem}.json"
                txt_out = reports / f"{stem}.txt"
                html_out = reports / f"{stem}.html"
                html_static_out = reports / f"{stem}_static.html"
                rc = run_xrt_command(
                    args.xrt_host,
                    args.sw_xclbin,
                    dataset,
                    args.vitis_settings,
                    "sw_emu",
                    root,
                    emconfig_path=args.sw_emu_dir,
                    json_out=json_out,
                    txt_out=txt_out,
                )
                if rc == 0:
                    render_report(root, json_out, html_out, html_static_out)
                    print(f"report json: {json_out}")
                    print(f"report txt : {txt_out}")
                    print(f"report html: {html_out}")
                    print(f"report html static: {html_static_out}")
                print_result(rc)
                return rc

            if version.runner == "local":
                print("请输入 1-2，或 q 返回。")
            elif version.runner == "cuper_pcg":
                print("请输入 1-4，或 q 返回。")
            elif version.runner == "cuper_pcg_tapa":
                print("请输入 1-4，或 q 返回。")
            elif version.runner == "cuper_control":
                print("请输入 1-6，或 q 返回。")
            else:
                print("请输入 1-7，或 q 返回。")


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Interactive Project-XPlus build/run launcher.")
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--local-host", type=Path, default=root / "build" / "xplus_host")
    parser.add_argument("--cuper-pcg-host", type=Path, default=root / "build" / "xplus_cuper_pcg_host")
    parser.add_argument("--cuper-tapa-pcg-host", type=Path, default=root / "build" / "xplus_cuper_tapa_pcg_host")
    parser.add_argument("--cuper-control-local-host", type=Path, default=root / "build" / "xplus_cuper_control_local_host")
    parser.add_argument("--xrt-host", type=Path, default=root / "build" / "xplus_xrt_host")
    parser.add_argument("--hw-xclbin", type=Path, default=root / "build" / "hw" / "cgsolver_jacobi_pcg.xclbin")
    parser.add_argument("--sw-xclbin", type=Path, default=root / "build" / "sw_emu" / "cgsolver_jacobi_pcg.xclbin")
    parser.add_argument("--sw-emu-dir", type=Path, default=root / "build" / "sw_emu")
    parser.add_argument("--vitis-settings", type=Path, default=Path(os.environ.get("VITIS_SETTINGS", "")))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    datasets = discover_datasets(root)

    while True:
        print()
        print("Project-XPlus Launcher")
        print("  r. 硬件报告/分析")
        print("  d. 数据集下载/生成")
        print("  c. DLC/Cuper 独立 SpMV 工具")
        print("  q. 退出")
        print_datasets(datasets, root)
        raw = ask_text("选择数据集编号，c Cuper，r 硬件报告/分析，d 数据集下载/生成，或 q 退出: ")
        if raw is None:
            return 0
        lowered = raw.lower()
        if lowered in {"q", "quit", "exit"}:
            return 0
        if lowered == "r":
            rc = report_menu(root)
            if rc != 0:
                return rc
            datasets = discover_datasets(root)
            continue
        if lowered == "d":
            rc = data_menu(root)
            if rc != 0:
                return rc
            datasets = discover_datasets(root)
            continue
        if lowered == "c":
            rc = run_cuper_launcher(root)
            if rc != 0:
                return rc
            datasets = discover_datasets(root)
            continue
        if not raw.isdigit() or not (1 <= int(raw) <= len(datasets)):
            if datasets:
                print(f"请输入 1-{len(datasets)}、c、r、d，或 q。")
            else:
                print("当前没有可选数据集，请先输入 d 下载/生成数据，或输入 q 退出。")
            continue
        selected = int(raw) - 1
        if selected is None:
            return 0
        rc = action_menu(datasets[selected], args, root)
        if rc != 0:
            return rc


if __name__ == "__main__":
    raise SystemExit(main())
