#!/usr/bin/env python3

from __future__ import annotations

import argparse
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


LONG_BUILD_CONFIRM = "BUILD"


@dataclass(frozen=True)
class Dataset:
    name: str
    path: Path
    rows: int
    cols: int
    nnz: int


def shell_quote(value: Path | str) -> str:
    return shlex.quote(str(value))


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


def read_mtx_shape(path: Path) -> tuple[int, int, int]:
    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        for line in handle:
            if not line.startswith("%"):
                rows, cols, nnz = line.split()[:3]
                return int(rows), int(cols), int(nnz)
    raise RuntimeError(f"missing Matrix Market shape line: {path}")


def discover_datasets(root: Path) -> list[Dataset]:
    datasets = []
    for path in sorted((root / "data" / "matrices").glob("**/*.mtx")):
        rows, cols, nnz = read_mtx_shape(path)
        datasets.append(Dataset(path.stem, path, rows, cols, nnz))
    return datasets


def print_datasets(datasets: list[Dataset], root: Path) -> None:
    print()
    print("可用矩阵：")
    if not datasets:
        print("  未找到 data/matrices/**/*.mtx")
        return
    for index, dataset in enumerate(datasets, start=1):
        rel = dataset.path.relative_to(root)
        print(f"  {index:2d}. {dataset.name:12s} {dataset.rows}x{dataset.cols} nnz={dataset.nnz:<10d} {rel}")


def run_shell(command: str, cwd: Path) -> int:
    print()
    print("$ " + command, flush=True)
    print()
    return subprocess.call(["bash", "-lc", command], cwd=str(cwd))


def run_script(root: Path, script_name: str, *args: Path | str) -> int:
    command = " ".join([shell_quote(root / "scripts" / script_name), *[shell_quote(arg) for arg in args]])
    return run_shell(command, root)


def tmux_has_session(name: str) -> bool:
    return subprocess.call(
        ["tmux", "has-session", "-t", name],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    ) == 0


def start_hw_tmux(root: Path, session: str, force: bool) -> int:
    if tmux_has_session(session):
        if not force:
            print(f"tmux session 已存在: {session}")
            print(f"查看: tmux attach -t {session}")
            print("如需重启，使用 --force。")
            return 1
        subprocess.check_call(["tmux", "kill-session", "-t", session])

    log_dir = root / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / "build_hw_tmux.log"
    run_script_path = root / "build" / "run_hw_tmux.sh"
    run_script_path.parent.mkdir(parents=True, exist_ok=True)
    run_script_path.write_text(
        "\n".join(
            [
                "#!/usr/bin/env bash",
                "set -euo pipefail",
                f"cd {shell_quote(root)}",
                f"exec > >(tee {shell_quote(log_path)}) 2>&1",
                "date",
                f"{shell_quote(root / 'scripts' / 'build_host.sh')}",
                f"{shell_quote(root / 'scripts' / 'build_xo_u55c.sh')}",
                f"{shell_quote(root / 'scripts' / 'link_xclbin_u55c.sh')}",
                "date",
            ]
        )
        + "\n",
        encoding="utf-8",
    )
    run_script_path.chmod(0o755)
    subprocess.check_call(["tmux", "new-session", "-d", "-s", session, str(run_script_path)])
    print(f"已在 tmux 后台启动硬件 bitstream 生成: {session}")
    print(f"查看进度: tmux attach -t {session}")
    print(f"日志文件: {log_path}")
    print(f"执行脚本: {run_script_path}")
    return 0


def print_result(rc: int) -> None:
    print()
    if rc == 0:
        print("运行完成：成功")
    else:
        print(f"运行完成：失败，退出码 {rc}")
    pause()


def action_menu(dataset: Dataset, root: Path) -> int:
    while True:
        print()
        print(f"已选择: {dataset.name}  {dataset.rows}x{dataset.cols}  nnz={dataset.nnz}")
        print(f"路径: {dataset.path}")
        print()
        print("操作：")
        print("  1. build host")
        print("  2. run software simulation")
        print("  3. build XO")
        print("  4. link xclbin")
        print("  5. build HW bitstream in tmux")
        print("  6. run hardware xclbin")
        print("  q. 返回")
        answer = ask_text("选择操作: ")
        if answer is None or answer.lower() in {"q", "quit", "exit"}:
            return 0
        if answer == "1":
            rc = run_script(root, "build_host.sh")
            print_result(rc)
            return rc
        if answer == "2":
            command = (
                "source scripts/env_u55c.sh && "
                f"unset BITFILE && {shell_quote(root / 'build' / 'cuper_host')} {shell_quote(dataset.path)}"
            )
            rc = run_shell(command, root)
            print_result(rc)
            return rc
        if answer == "3":
            print("XO 生成会触发 TAPA/Vitis HLS，可能运行较久。")
            confirm = ask_text("确认请输入 BUILD: ")
            if confirm == LONG_BUILD_CONFIRM:
                rc = run_script(root, "build_xo_u55c.sh")
                print_result(rc)
                return rc
            print("已取消。")
            pause()
            continue
        if answer == "4":
            print("xclbin 链接会触发 Vitis link/implementation，可能运行很久。")
            confirm = ask_text("确认请输入 BUILD: ")
            if confirm == LONG_BUILD_CONFIRM:
                rc = run_script(root, "link_xclbin_u55c.sh")
                print_result(rc)
                return rc
            print("已取消。")
            pause()
            continue
        if answer == "5":
            rc = start_hw_tmux(root, "cuper_hw_build", force=False)
            print_result(rc)
            return rc
        if answer == "6":
            rc = run_script(root, "run_hw.sh", dataset.path)
            print_result(rc)
            return rc
        print("请输入 1-6，或 q 返回。")


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Cuper build/run launcher.")
    parser.add_argument("--root", type=Path, default=root)
    subparsers = parser.add_subparsers(dest="command")

    subparsers.add_parser("list", help="List matrix datasets.")
    subparsers.add_parser("build-host", help="Build build/cuper_host.")
    subparsers.add_parser("build-xo", help="Build Cuper_2022.xo.")
    subparsers.add_parser("link-xclbin", help="Link Cuper_2022.xclbin from Cuper_2022.xo.")

    hw_tmux = subparsers.add_parser("hw-tmux", help="Build host, XO, and xclbin in a tmux session.")
    hw_tmux.add_argument("--session", default="cuper_hw_build")
    hw_tmux.add_argument("--force", action="store_true")

    run_sw = subparsers.add_parser("run-sw", help="Run TAPA software simulation on a matrix.")
    run_sw.add_argument("matrix", type=Path)

    run_hw = subparsers.add_parser("run-hw", help="Run hardware xclbin on a matrix.")
    run_hw.add_argument("matrix", type=Path)

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.root.resolve()

    if args.command == "list":
        print_datasets(discover_datasets(root), root)
        return 0
    if args.command == "build-host":
        return run_script(root, "build_host.sh")
    if args.command == "build-xo":
        return run_script(root, "build_xo_u55c.sh")
    if args.command == "link-xclbin":
        return run_script(root, "link_xclbin_u55c.sh")
    if args.command == "hw-tmux":
        return start_hw_tmux(root, args.session, args.force)
    if args.command == "run-sw":
        matrix = args.matrix.resolve()
        command = (
            "source scripts/env_u55c.sh && "
            f"unset BITFILE && {shell_quote(root / 'build' / 'cuper_host')} {shell_quote(matrix)}"
        )
        return run_shell(command, root)
    if args.command == "run-hw":
        return run_script(root, "run_hw.sh", args.matrix.resolve())

    datasets = discover_datasets(root)
    while True:
        print()
        print("Cuper Launcher")
        print("  h. build HW bitstream in tmux")
        print("  q. 退出")
        print_datasets(datasets, root)
        answer = ask_text("选择矩阵编号，h 后台生成 bitstream，或 q 退出: ")
        if answer is None:
            return 0
        if answer.lower() in {"q", "quit", "exit"}:
            return 0
        if answer.lower() == "h":
            rc = start_hw_tmux(root, "cuper_hw_build", force=False)
            if rc != 0:
                return rc
            continue
        if answer.isdigit() and 1 <= int(answer) <= len(datasets):
            rc = action_menu(datasets[int(answer) - 1], root)
            if rc != 0:
                return rc
            datasets = discover_datasets(root)
            continue
        print(f"请输入 1-{len(datasets)}、h，或 q。")


if __name__ == "__main__":
    raise SystemExit(main())
