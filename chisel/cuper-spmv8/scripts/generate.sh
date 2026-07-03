#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
CHISEL_DIR="$ROOT_DIR/chisel/cuper-spmv8"
if [[ "${1:-}" == "" ]]; then
  OUT_DIR="$ROOT_DIR/verilog/tapa"
elif [[ "$1" = /* ]]; then
  OUT_DIR="$1"
else
  OUT_DIR="$(pwd)/$1"
fi

mkdir -p "$OUT_DIR"

(
  cd "$CHISEL_DIR"
  sbt "runMain cuper.spmv.GenerateCuperSpmvOnlyChiselDataPath8 $OUT_DIR"
)
