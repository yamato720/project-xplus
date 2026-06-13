#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$(cd "$ROOT_DIR/../.." && pwd)/cuper-jacobi-iteration-build}"

# shellcheck source=env_u55c.sh
source "$ROOT_DIR/scripts/env_u55c.sh"

mkdir -p "$BUILD_DIR"

TOP="${JACOBI_TOP:-CuperJacobiIteration}"
OUTPUT_XO="${1:-$BUILD_DIR/$TOP.xo}"
WORK_DIR="${WORK_DIR:-$BUILD_DIR/tapa_$TOP}"
CLOCK_PERIOD="${CLOCK_PERIOD:-2.0}"
JOBS="${JOBS:-$(nproc)}"

cmd=(
  tapa -w "$WORK_DIR" compile
  -f "$ROOT_DIR/kernels/Cuper.cpp"
  -t "$TOP"
  -p "$DEVICE"
  --clock-period "$CLOCK_PERIOD"
  -j "$JOBS"
  --enable-synth-util
  -c "-I$ROOT_DIR/include"
  -c "-I$ROOT_DIR/src"
  -o "$OUTPUT_XO"
)

if [[ "${JACOBI_DEADLOCK_DEBUG:-0}" != "0" && "${JACOBI_DEADLOCK_DEBUG:-}" != "" ]]; then
  cmd+=(-c "-DJACOBI_DEADLOCK_DEBUG=1")
fi

printf 'Running:'
printf ' %q' "${cmd[@]}"
printf '\n'

"${cmd[@]}"
