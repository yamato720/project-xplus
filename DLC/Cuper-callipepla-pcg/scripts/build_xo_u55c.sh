#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$(cd "$ROOT_DIR/../.." && pwd)/cuper-tapa-pcg-callipepla-build}"

# shellcheck source=env_u55c.sh
source "$ROOT_DIR/scripts/env_u55c.sh"

mkdir -p "$BUILD_DIR"

TOP="CuperPcgCallipepla"
OUTPUT_XO="${1:-$BUILD_DIR/$TOP.xo}"
WORK_DIR="${WORK_DIR:-$BUILD_DIR/tapa_$TOP}"
CLOCK_PERIOD="${CLOCK_PERIOD:-3.3}"
JOBS="${JOBS:-$(nproc)}"
ENABLE_SYNTH_UTIL="${CUPER_CALLIPEPLA_TAPA_ENABLE_SYNTH_UTIL:-1}"

HBM_CHANNELS="${CUPER_CALLIPEPLA_HBM_CHANNELS:-16}"
if [[ "$HBM_CHANNELS" != "16" ]]; then
  echo "CuperPcgCallipepla ABI currently fixes 16 Matrix_data ports; got CUPER_CALLIPEPLA_HBM_CHANNELS=$HBM_CHANNELS." >&2
  exit 1
fi

cflags=(
  "-I$ROOT_DIR/include"
)

if [[ "${CUPER_CALLIPEPLA_SPMV_STRIP_PADDING:-0}" != "0" && "${CUPER_CALLIPEPLA_SPMV_STRIP_PADDING:-}" != "" ]]; then
  cflags+=("-DJACOBI_SPMV_STRIP_PADDING=1")
fi

if [[ "${CUPER_CALLIPEPLA_SPMV_ACC_WINDOW:-10}" != "" ]]; then
  if [[ ! "${CUPER_CALLIPEPLA_SPMV_ACC_WINDOW:-10}" =~ ^[1-9][0-9]*$ ]]; then
    echo "CUPER_CALLIPEPLA_SPMV_ACC_WINDOW must be a positive integer." >&2
    exit 1
  fi
  cflags+=("-DJACOBI_SPMV_ACC_WINDOW=${CUPER_CALLIPEPLA_SPMV_ACC_WINDOW:-10}")
fi

cmd=(
  tapa -w "$WORK_DIR" compile
  -f "$ROOT_DIR/kernels/Cuper.cpp"
  -t "$TOP"
  -p "$DEVICE"
  --clock-period "$CLOCK_PERIOD"
  -j "$JOBS"
  -o "$OUTPUT_XO"
)
if [[ "$ENABLE_SYNTH_UTIL" != "0" && "$ENABLE_SYNTH_UTIL" != "" ]]; then
  cmd+=(--enable-synth-util)
fi
for cflag in "${cflags[@]}"; do
  cmd+=(-c "$cflag")
done

printf 'Running:'
printf ' %q' "${cmd[@]}"
printf '\n'
"${cmd[@]}"
