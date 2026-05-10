#!/usr/bin/env python3

from __future__ import annotations

import argparse
import math
from pathlib import Path


def choose_grid_shape(size: int, aspect_ratio: float) -> tuple[int, int]:
    cols = max(1, math.ceil(math.sqrt(size * aspect_ratio)))
    rows = max(1, math.ceil(size / cols))
    return rows, cols


def node_position(index: int, cols: int) -> tuple[int, int]:
    return index // cols, index % cols


def add_edge(row_maps: list[dict[int, float]], diag: list[float], lhs: int, rhs: int, weight: float) -> None:
    if lhs == rhs or weight <= 0.0:
        return
    row_maps[lhs][rhs] = row_maps[lhs].get(rhs, 0.0) - weight
    row_maps[rhs][lhs] = row_maps[rhs].get(lhs, 0.0) - weight
    diag[lhs] += weight
    diag[rhs] += weight


def horizontal_weight(row: int, col: int) -> float:
    base = 1.4 + 0.18 * math.sin(0.17 * row) + 0.11 * math.cos(0.09 * col)
    channel = 0.22 if (row // 9) % 2 == 0 else -0.08
    return max(0.35, base + channel)


def vertical_weight(row: int, col: int) -> float:
    base = 0.95 + 0.15 * math.cos(0.13 * row) + 0.14 * math.sin(0.11 * col)
    rib = 0.20 if (col // 11) % 3 == 1 else 0.02
    return max(0.25, base + rib)


def contact_weight(row: int, col: int) -> float:
    return 0.08 + 0.03 * (1.0 + math.sin(0.23 * row + 0.19 * col))


def mass_term(index: int, row: int, col: int) -> float:
    return 0.18 + 0.04 * math.sin(0.07 * index) + 0.03 * math.cos(0.15 * row - 0.08 * col)


def build_sparse_spd_matrix(size: int, aspect_ratio: float) -> tuple[int, int, list[int], list[int], list[float], list[float]]:
    rows, cols = choose_grid_shape(size, aspect_ratio)
    row_maps = [dict() for _ in range(size)]
    diag = [0.0] * size

    for index in range(size):
        row, col = node_position(index, cols)

        right = index + 1
        if col + 1 < cols and right < size:
            add_edge(row_maps, diag, index, right, horizontal_weight(row, col))

        down = index + cols
        if down < size:
            add_edge(row_maps, diag, index, down, vertical_weight(row, col))

        if row % 13 == 5 and col % 7 == 2:
            target = index + 2 * cols + 1
            if col + 1 < cols and target < size:
                add_edge(row_maps, diag, index, target, contact_weight(row, col))

        if row % 10 == 3 and col % 9 == 4:
            target = index + cols - 1
            if col > 0 and target < size:
                add_edge(row_maps, diag, index, target, 0.85 * contact_weight(row, col))

    for index in range(size):
        row, col = node_position(index, cols)
        diag[index] += mass_term(index, row, col)

    row_ptr = [0]
    col_idx: list[int] = []
    values: list[float] = []

    for index in range(size):
        entries = row_maps[index]
        entries[index] = diag[index]
        for col in sorted(entries):
            col_idx.append(col)
            values.append(entries[col])
        row_ptr.append(len(col_idx))

    return rows, cols, row_ptr, col_idx, values, diag


def build_reference_solution(size: int, cols: int) -> list[float]:
    reference = []
    hotspot_center = max(1, size - 1) * 0.63
    hotspot_width = max(8.0, size * 0.07)
    for index in range(size):
        row, col = node_position(index, cols)
        smooth = 0.75 + 0.18 * math.sin(0.06 * row) + 0.12 * math.cos(0.08 * col)
        gradient = 0.0009 * index
        hotspot = 0.28 * math.exp(-((index - hotspot_center) / hotspot_width) ** 2)
        reference.append(smooth + gradient + hotspot)
    return reference


def build_initial_guess(size: int, cols: int) -> list[float]:
    initial = []
    for index in range(size):
        row, col = node_position(index, cols)
        value = 0.09 * math.cos(0.04 * index) - 0.06 * math.sin(0.09 * row) + 0.03 * math.cos(0.21 * col)
        initial.append(value)
    return initial


def csr_spmv(size: int, row_ptr: list[int], col_idx: list[int], values: list[float], vector: list[float]) -> list[float]:
    result = [0.0] * size
    for row in range(size):
        accum = 0.0
        for offset in range(row_ptr[row], row_ptr[row + 1]):
            accum += values[offset] * vector[col_idx[offset]]
        result[row] = accum
    return result


def write_float_array(path: Path, values: list[float]) -> None:
    path.write_text(" ".join(f"{value:.17g}" for value in values) + "\n", encoding="utf-8")


def write_int_array(path: Path, values: list[int]) -> None:
    path.write_text(" ".join(str(value) for value in values) + "\n", encoding="utf-8")


def cleanup_output_dir(output_dir: Path) -> None:
    for file_name in [
        "A.mtx",
        "meta.json",
        "meta.txt",
        "diag.txt",
        "jacobi_diag.txt",
        "rhs.txt",
        "x_ref.txt",
        "x_expected.txt",
        "row_ptr.txt",
        "col_idx.txt",
        "values.txt",
        "b.txt",
        "x0.txt",
    ]:
        path = output_dir / file_name
        if path.exists():
            path.unlink()


def generate_dataset(size: int, output_dir: Path, aspect_ratio: float) -> None:
    mesh_rows, mesh_cols, row_ptr, col_idx, values, _diag = build_sparse_spd_matrix(size, aspect_ratio)
    x_ref = build_reference_solution(size, mesh_cols)
    x0 = build_initial_guess(size, mesh_cols)
    b = csr_spmv(size, row_ptr, col_idx, values, x_ref)
    nnz = len(col_idx)

    output_dir.mkdir(parents=True, exist_ok=True)
    cleanup_output_dir(output_dir)

    write_int_array(output_dir / "row_ptr.txt", row_ptr)
    write_int_array(output_dir / "col_idx.txt", col_idx)
    write_float_array(output_dir / "values.txt", values)
    write_float_array(output_dir / "b.txt", b)
    write_float_array(output_dir / "x0.txt", x0)

    print(
        "generated cg dataset "
        f"n={size} nnz={nnz} mesh={mesh_rows}x{mesh_cols} -> {output_dir}"
    )
    print("files: row_ptr.txt col_idx.txt values.txt b.txt x0.txt")


def parse_args() -> argparse.Namespace:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(
        description="Generate a deterministic sparse SPD dataset for CG solver validation."
    )
    parser.add_argument("--size", type=int, default=512, help="number of unknowns, default: 512")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=script_dir.parent / "data" / "generated" / "cgsolver" / "n512",
        help="output directory for generated files",
    )
    parser.add_argument(
        "--aspect-ratio",
        type=float,
        default=1.6,
        help="approximate mesh width/height ratio, default: 1.6",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    if args.size <= 0:
        raise SystemExit("--size must be positive")
    if args.aspect_ratio <= 0.0:
        raise SystemExit("--aspect-ratio must be positive")

    default_output_dir = Path(__file__).resolve().parent.parent / "data" / "generated" / "cgsolver" / f"n{args.size}"
    output_dir = args.output_dir
    if output_dir.name == "n512" and args.size != 512:
        output_dir = default_output_dir

    generate_dataset(
        size=args.size,
        output_dir=output_dir,
        aspect_ratio=args.aspect_ratio,
    )


if __name__ == "__main__":
    main()
