#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# shellcheck source=env_u55c.sh
source "$ROOT_DIR/scripts/env_u55c.sh"

OUTPUT_XO="${1:-$ROOT_DIR/Cuper_2022.xo}"
WORK_DIR="${WORK_DIR:-$ROOT_DIR/run/tapa_u55c}"
CLOCK_PERIOD="${CLOCK_PERIOD:-2.0}"
JOBS="${JOBS:-$(nproc)}"

cmd=(
  tapa -w "$WORK_DIR" compile
  -f "$ROOT_DIR/kernels/Cuper.cpp"
  -t Cuper
  -p "$DEVICE"
  --clock-period "$CLOCK_PERIOD"
  -j "$JOBS"
  --enable-synth-util
  -c "-I$ROOT_DIR/include"
  -c "-I$ROOT_DIR/src"
  -o "$OUTPUT_XO"
)

printf 'Running:'
printf ' %q' "${cmd[@]}"
printf '\n'

"${cmd[@]}"
