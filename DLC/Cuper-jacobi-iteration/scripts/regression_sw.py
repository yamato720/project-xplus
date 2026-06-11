#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import re
import signal
import shlex
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path


@dataclass(frozen=True)
class RegressionCase:
    name: str
    matrix: Path
    max_iters: int


@dataclass
class RegressionResult:
    name: str
    state: str
    rc: int | None
    log_path: Path | None
    message: str = ""
    status: str = ""
    final_buffer: str = ""
    iterations: str = ""
    final_diff: str = ""
    error_num: str = ""
    float_v16_packets: str = ""
    spmv_update_packets: str = ""
    spmv_update_cycles: str = ""
    controller_total_cycles: str = ""
    timer_total_cycles: str = ""
    spmv_update_avg_cycles: str = ""


def shell_quote(value: Path | str) -> str:
    return shlex.quote(str(value))


def default_build_dir(root: Path) -> Path:
    return Path(os.environ.get("BUILD_DIR", root.parent.parent / "cuper-jacobi-iteration-build")).resolve()


def default_cases(root: Path) -> dict[str, RegressionCase]:
    repo_root = root.parent.parent
    return {
        "cant": RegressionCase(
            "cant",
            root / "data" / "matrices" / "cant.mtx",
            2,
        ),
        "thermal2_n65536": RegressionCase(
            "thermal2_n65536",
            repo_root / "data" / "suitesparse" / "Schmid" / "csr" / "thermal2_n65536",
            1,
        ),
        "thermal2_n262144": RegressionCase(
            "thermal2_n262144",
            repo_root / "data" / "suitesparse" / "Schmid" / "csr" / "thermal2_n262144",
            1,
        ),
    }


def selected_cases(args: argparse.Namespace, root: Path) -> list[RegressionCase]:
    cases = default_cases(root)
    selected = list(args.case or [])
    selected.extend(os.environ.get("CASES", "").split())
    if selected:
        unknown = [name for name in selected if name not in cases]
        if unknown:
            available = ", ".join(sorted(cases))
            raise SystemExit(f"unknown case(s): {', '.join(unknown)}; available: {available}")
        return [cases[name] for name in selected]
    if args.mode == "quick":
        return [cases["cant"], cases["thermal2_n65536"]]
    return [cases["cant"], cases["thermal2_n65536"], cases["thermal2_n262144"]]


def extract_number(pattern: str, text: str) -> str:
    match = re.search(pattern, text, flags=re.MULTILINE)
    return match.group(1) if match else ""


def extract_key_values(prefix: str, text: str) -> dict[str, str]:
    for line in text.splitlines():
        if line.startswith(prefix):
            values: dict[str, str] = {}
            for item in line.split("]", 1)[1].strip().split():
                if "=" not in item:
                    continue
                key, value = item.split("=", 1)
                values[key] = value
            return values
    return {}


def parse_log(case: RegressionCase, log_path: Path, rc: int | None, timed_out: bool) -> RegressionResult:
    text = log_path.read_text(encoding="utf-8", errors="ignore")
    work = extract_key_values("[jacobi-timing-work]", text)
    cycles = extract_key_values("[jacobi-stage-cycles]", text)
    result = RegressionResult(
        name=case.name,
        state="FAIL",
        rc=rc,
        log_path=log_path,
        status=extract_number(r"Jacobi On FPGA\]\s+Status:\s+([0-9eE+\-.]+)", text),
        final_buffer=extract_number(r"Jacobi On FPGA\]\s+Final buffer:\s+([0-9eE+\-.]+)", text),
        iterations=extract_number(r"Jacobi On FPGA\]\s+Iterations:\s+([0-9eE+\-.]+)", text),
        final_diff=extract_number(r"Jacobi On FPGA\]\s+Final diff:\s+([0-9eE+\-.]+)", text),
        error_num=extract_number(r"Error Num:\s*([0-9]+)", text),
        float_v16_packets=work.get("float_v16_packets", ""),
        spmv_update_packets=work.get("spmv_update_packets", ""),
        spmv_update_cycles=cycles.get("spmv_update", ""),
        controller_total_cycles=cycles.get("controller_total", ""),
        timer_total_cycles=cycles.get("timer_total", ""),
        spmv_update_avg_cycles=cycles.get("spmv_update_avg", ""),
    )
    if timed_out:
        result.message = "timeout"
    elif rc != 0:
        result.message = f"exit code {rc}"
    elif result.error_num == "0":
        result.state = "PASS"
    elif result.error_num:
        result.message = f"Error Num={result.error_num}"
    else:
        result.message = "missing Error Num"
    return result


def run_build(root: Path, build_dir: Path, log_dir: Path) -> int:
    log_path = log_dir / "build_host.log"
    env = os.environ.copy()
    env["BUILD_DIR"] = str(build_dir)
    print(f"[build] start log={log_path}", flush=True)
    with log_path.open("w", encoding="utf-8") as log:
        proc = subprocess.run(
            [str(root / "scripts" / "build_host.sh")],
            cwd=str(root),
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=False,
        )
    state = "PASS" if proc.returncode == 0 else "FAIL"
    print(f"[build] {state} rc={proc.returncode} log={log_path}", flush=True)
    return proc.returncode


def run_case(
    root: Path,
    build_dir: Path,
    log_dir: Path,
    case: RegressionCase,
    tau: str,
    timeout_sec: int,
) -> RegressionResult:
    if not case.matrix.exists():
        return RegressionResult(
            name=case.name,
            state="SKIP",
            rc=None,
            log_path=None,
            message=f"missing matrix: {case.matrix}",
        )

    host = build_dir / "cuper_jacobi_host"
    if not host.exists():
        return RegressionResult(
            name=case.name,
            state="FAIL",
            rc=None,
            log_path=None,
            message=f"missing host: {host}",
        )

    log_path = log_dir / f"{case.name}.log"
    command = " && ".join(
        [
            f"source {shell_quote(root / 'scripts' / 'env_u55c.sh')}",
            "unset BITFILE",
            f"export MAX_ITERS={case.max_iters}",
            f"export TAU={shell_quote(tau)}",
            f"{shell_quote(host)} {shell_quote(case.matrix)}",
        ]
    )
    timed_out = False
    rc: int | None
    with log_path.open("w", encoding="utf-8") as log:
        log.write(f"$ {command}\n\n")
        log.flush()
        proc = subprocess.Popen(
            ["bash", "-lc", command],
            cwd=str(root),
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            rc = proc.wait(timeout=None if timeout_sec <= 0 else timeout_sec)
        except subprocess.TimeoutExpired:
            timed_out = True
            rc = None
            os.killpg(proc.pid, signal.SIGTERM)
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
            log.write(f"\n[regression] timeout after {timeout_sec} seconds\n")

    return parse_log(case, log_path, rc, timed_out)


def write_summary(log_dir: Path, args: argparse.Namespace, results: list[RegressionResult]) -> None:
    md_path = log_dir / "summary.md"
    tsv_path = log_dir / "summary.tsv"
    timestamp = datetime.now().isoformat(timespec="seconds")
    with md_path.open("w", encoding="utf-8") as md:
        md.write("# Cuper Jacobi Software Regression\n\n")
        md.write(f"- time: `{timestamp}`\n")
        md.write(f"- mode: `{args.mode}`\n")
        md.write(f"- tau: `{args.tau}`\n")
        md.write(f"- timeout_sec: `{args.timeout_sec}`\n")
        md.write(f"- no_build: `{args.no_build}`\n\n")
        md.write("| case | state | rc | status | final_buffer | iterations | final_diff | error_num | spmv_update_cycles | log |\n")
        md.write("| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |\n")
        for result in results:
            log_name = result.log_path.name if result.log_path else ""
            md.write(
                f"| `{result.name}` | {result.state} | {result.rc if result.rc is not None else ''} "
                f"| {result.status} | {result.final_buffer} | {result.iterations} | {result.final_diff} "
                f"| {result.error_num} | {result.spmv_update_cycles} | `{log_name}` |\n"
            )
        failures = [result for result in results if result.state != "PASS"]
        if failures:
            md.write("\n## Failures\n\n")
            for result in failures:
                md.write(f"- `{result.name}`: {result.message}\n")

    with tsv_path.open("w", encoding="utf-8") as tsv:
        tsv.write(
            "case\tstate\trc\tstatus\tfinal_buffer\titerations\tfinal_diff\terror_num\t"
            "float_v16_packets\tspmv_update_packets\tspmv_update_cycles\tcontroller_total_cycles\t"
            "timer_total_cycles\tspmv_update_avg_cycles\tlog\n"
        )
        for result in results:
            tsv.write(
                "\t".join(
                    [
                        result.name,
                        result.state,
                        "" if result.rc is None else str(result.rc),
                        result.status,
                        result.final_buffer,
                        result.iterations,
                        result.final_diff,
                        result.error_num,
                        result.float_v16_packets,
                        result.spmv_update_packets,
                        result.spmv_update_cycles,
                        result.controller_total_cycles,
                        result.timer_total_cycles,
                        result.spmv_update_avg_cycles,
                        "" if result.log_path is None else str(result.log_path),
                    ]
                )
                + "\n"
            )


def print_case_summary(result: RegressionResult) -> None:
    if result.state == "PASS":
        print(
            f"[case {result.name}] PASS status={result.status} final_buffer={result.final_buffer} "
            f"iters={result.iterations} final_diff={result.final_diff} error_num={result.error_num} "
            f"spmv_update={result.spmv_update_cycles} log={result.log_path}",
            flush=True,
        )
        return
    print(f"[case {result.name}] {result.state} {result.message} log={result.log_path or ''}", flush=True)


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent.parent
    cases = sorted(default_cases(root))
    parser = argparse.ArgumentParser(description="Run Cuper Jacobi software regression with compact output.")
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--mode", choices=["quick", "full"], default=os.environ.get("MODE", "quick"))
    parser.add_argument("--case", choices=cases, action="append", help="Run selected case; can be repeated.")
    parser.add_argument("--no-build", action="store_true", default=os.environ.get("NO_BUILD", "") != "")
    parser.add_argument("--allow-missing", action="store_true", default=os.environ.get("ALLOW_MISSING", "") != "")
    parser.add_argument("--timeout-sec", type=int, default=int(os.environ.get("TIMEOUT_SEC", "1800")))
    parser.add_argument("--tau", default=os.environ.get("TAU", "1e-5"))
    parser.add_argument("--log-dir", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    build_dir = default_build_dir(root)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_dir = (args.log_dir or (build_dir / "regression" / f"{stamp}_{args.mode}")).resolve()
    log_dir.mkdir(parents=True, exist_ok=True)

    print(f"[regression] root={root}")
    print(f"[regression] build_dir={build_dir}")
    print(f"[regression] log_dir={log_dir}")
    print(f"[regression] mode={args.mode} timeout_sec={args.timeout_sec}")

    if not args.no_build:
        build_rc = run_build(root, build_dir, log_dir)
        if build_rc != 0:
            print("[summary] build failed; skip cases", flush=True)
            return build_rc

    results: list[RegressionResult] = []
    for case in selected_cases(args, root):
        result = run_case(root, build_dir, log_dir, case, args.tau, args.timeout_sec)
        if result.state == "SKIP" and not args.allow_missing:
            result.state = "FAIL"
        results.append(result)
        print_case_summary(result)

    write_summary(log_dir, args, results)
    passed = sum(1 for result in results if result.state == "PASS")
    failed = sum(1 for result in results if result.state == "FAIL")
    skipped = sum(1 for result in results if result.state == "SKIP")
    print(f"[summary] pass={passed} fail={failed} skip={skipped}")
    print(f"[summary] markdown={log_dir / 'summary.md'}")
    print(f"[summary] tsv={log_dir / 'summary.tsv'}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
