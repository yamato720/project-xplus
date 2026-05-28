#!/usr/bin/env python3
"""Patch TAPA-generated control FSMs inside a TAPA XO.

The generated TAPA wrapper uses small control FSMs without register initial
values. On hardware, a missing or too-short kernel reset can leave the top
ap_ctrl_hs FSM outside its idle state, making the AXI-lite control register
read as 0x0 forever. This patch makes the XO robust by initializing those
FSM state registers and recovering the top FSM from its unused state.
"""

from __future__ import annotations

import argparse
import re
import shutil
import tempfile
import zipfile
from pathlib import Path


def find_fsm_in_xo(src: zipfile.ZipFile, xo_path: Path) -> str:
    candidates = [
        name
        for name in src.namelist()
        if name.startswith("ip_repo/tapa_xrtl_")
        and "/src/" in name
        and name.endswith("_fsm.v")
    ]
    if not candidates:
        raise RuntimeError(f"{xo_path} does not contain a TAPA *_fsm.v file")

    stem = xo_path.stem
    preferred = [
        name
        for name in candidates
        if name.endswith(f"/{stem}_fsm.v") or f"tapa_xrtl_{stem}_1_0" in name
    ]
    if len(preferred) == 1:
        return preferred[0]
    if len(candidates) == 1:
        return candidates[0]

    raise RuntimeError(
        f"{xo_path} contains multiple TAPA FSM files; candidates: {', '.join(candidates)}"
    )


def find_fsm_in_workdir(work_dir: Path, xo_path: Path) -> Path | None:
    preferred = work_dir / "hdl" / f"{xo_path.stem}_fsm.v"
    if preferred.exists():
        return preferred

    hdl_dir = work_dir / "hdl"
    if not hdl_dir.exists():
        return None
    candidates = sorted(hdl_dir.glob("*_fsm.v"))
    if len(candidates) == 1:
        return candidates[0]
    return None


def patch_fsm_text(text: str) -> tuple[str, int, int]:
    patched, init_count = re.subn(
        r"\breg \[1:0\] ((?:[A-Za-z0-9_]+__state)|tapa_state);",
        r"reg [1:0] \1 = 2'b00;",
        text,
    )

    old_top_case_tail = """      2'b10: begin
        tapa_state <= 2'b00;
      end
    endcase"""
    new_top_case_tail = """      2'b10: begin
        tapa_state <= 2'b00;
      end
      default: begin
        tapa_state <= 2'b00;
      end
    endcase"""
    patched = patched.replace(old_top_case_tail, new_top_case_tail)
    default_count = patched.count(new_top_case_tail) - text.count(new_top_case_tail)

    return patched, init_count, default_count


def patch_xo(xo_path: Path) -> tuple[int, int]:
    with zipfile.ZipFile(xo_path, "r") as src:
        infos = src.infolist()
        fsm_in_xo = find_fsm_in_xo(src, xo_path)

        original = src.read(fsm_in_xo).decode("utf-8")
        patched, init_count, default_count = patch_fsm_text(original)
        already_initialized = "reg [1:0] tapa_state = 2'b00;" in patched
        already_has_default = "default: begin\n        tapa_state <= 2'b00;" in patched
        if init_count == 0 and not already_initialized:
            raise RuntimeError(f"{fsm_in_xo}: no uninitialized state registers found")
        if default_count == 0 and not already_has_default:
            raise RuntimeError(f"{fsm_in_xo}: failed to add top FSM default recovery")

        with tempfile.NamedTemporaryFile(
            dir=str(xo_path.parent), prefix=xo_path.name + ".", suffix=".tmp", delete=False
        ) as tmp:
            tmp_path = Path(tmp.name)

        try:
            with zipfile.ZipFile(tmp_path, "w") as dst:
                for info in infos:
                    data = patched.encode("utf-8") if info.filename == fsm_in_xo else src.read(info.filename)
                    out_info = zipfile.ZipInfo(info.filename, date_time=info.date_time)
                    out_info.compress_type = info.compress_type
                    out_info.comment = info.comment
                    out_info.extra = info.extra
                    out_info.internal_attr = info.internal_attr
                    out_info.external_attr = info.external_attr
                    out_info.create_system = info.create_system
                    dst.writestr(out_info, data)
            shutil.move(str(tmp_path), xo_path)
        finally:
            tmp_path.unlink(missing_ok=True)

    return init_count, default_count


def patch_workdir(work_dir: Path, xo_path: Path) -> bool:
    fsm_path = find_fsm_in_workdir(work_dir, xo_path)
    if fsm_path is None:
        return False
    original = fsm_path.read_text(encoding="utf-8")
    patched, _, _ = patch_fsm_text(original)
    if patched != original:
        fsm_path.write_text(patched, encoding="utf-8")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("xo", type=Path)
    parser.add_argument("--work-dir", type=Path, default=None)
    args = parser.parse_args()

    init_count, default_count = patch_xo(args.xo)
    workdir_patched = patch_workdir(args.work_dir, args.xo) if args.work_dir is not None else False
    print(
        f"patched {args.xo}: initialized {init_count} FSM state regs, "
        f"top defaults added {default_count}, workdir_patched={workdir_patched}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
