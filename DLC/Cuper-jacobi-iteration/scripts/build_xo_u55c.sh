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
if [[ "${CLOCK_PERIOD:-}" == "" ]]; then
  if [[ "$TOP" == "CuperJacobiIteration" ]] &&
     { [[ "${JACOBI_DEADLOCK_DEBUG:-0}" != "0" && "${JACOBI_DEADLOCK_DEBUG:-}" != "" ]] ||
       [[ "${JACOBI_TRACE_ISOTOPE:-0}" != "0" && "${JACOBI_TRACE_ISOTOPE:-}" != "" ]] ||
       [[ "${JACOBI_TRACE_LIGHT:-0}" != "0" && "${JACOBI_TRACE_LIGHT:-}" != "" ]]; }; then
    # debug full graph 先保守降频，避免 timing violation 污染大规模硬件判断。
    CLOCK_PERIOD="4.0"
  else
    CLOCK_PERIOD="2.0"
  fi
fi
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

HBM_CHANNELS="${JACOBI_HBM_CHANNELS:-}"
if [[ "$HBM_CHANNELS" == "" && "${JACOBI_WIDE_HBM:-0}" != "0" && "${JACOBI_WIDE_HBM:-}" != "" ]]; then
  HBM_CHANNELS="24"
fi
HBM_CHANNELS="${HBM_CHANNELS:-16}"

if [[ "${JACOBI_DEADLOCK_DEBUG:-0}" != "0" && "${JACOBI_DEADLOCK_DEBUG:-}" != "" ]]; then
  cmd+=(-c "-DJACOBI_DEADLOCK_DEBUG=1")
fi

if [[ "${JACOBI_TRACE_ISOTOPE:-0}" != "0" && "${JACOBI_TRACE_ISOTOPE:-}" != "" ]]; then
  cmd+=(-c "-DJACOBI_TRACE_ISOTOPE=1")
fi

if [[ "${JACOBI_TRACE_LIGHT:-0}" != "0" && "${JACOBI_TRACE_LIGHT:-}" != "" ]]; then
  cmd+=(-c "-DJACOBI_TRACE_LIGHT=1")
fi

if [[ "$HBM_CHANNELS" != "16" ]]; then
  if [[ "${JACOBI_TRACE_ISOTOPE:-0}" != "0" && "${JACOBI_TRACE_ISOTOPE:-}" != "" ]] ||
     [[ "${JACOBI_DEADLOCK_DEBUG:-0}" != "0" && "${JACOBI_DEADLOCK_DEBUG:-}" != "" ]]; then
    echo "JACOBI_HBM_CHANNELS=$HBM_CHANNELS currently supports no trace or JACOBI_TRACE_LIGHT only; full isotope/deadlock trace still enumerates 16 matrix/accumulator lanes." >&2
    exit 1
  fi
fi

if [[ "${JACOBI_WIDE_HBM:-0}" != "0" && "${JACOBI_WIDE_HBM:-}" != "" ]]; then
  cmd+=(-c "-DJACOBI_WIDE_HBM=1")
fi

case "$HBM_CHANNELS" in
  16)
    ;;
  24)
    cmd+=(-c "-DJACOBI_HBM_CHANNELS_24=1")
    ;;
  32)
    cmd+=(-c "-DJACOBI_HBM_CHANNELS_32=1")
    ;;
  *)
    echo "Unsupported JACOBI_HBM_CHANNELS=$HBM_CHANNELS; use 16, 24, or 32." >&2
    exit 1
    ;;
esac

echo "Cuper Jacobi TAPA top: $TOP"
echo "Cuper Matrix_data HBM channels: $HBM_CHANNELS"

if [[ "${JACOBI_BLOCKING_ENTRY_PROBE:-0}" != "0" && "${JACOBI_BLOCKING_ENTRY_PROBE:-}" != "" ]]; then
  cmd+=(-c "-DJACOBI_BLOCKING_ENTRY_PROBE=1")
fi

printf 'Running:'
printf ' %q' "${cmd[@]}"
printf '\n'

"${cmd[@]}"
