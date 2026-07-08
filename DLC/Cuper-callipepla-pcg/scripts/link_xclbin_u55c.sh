#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$(cd "$ROOT_DIR/../.." && pwd)/cuper-tapa-pcg-callipepla-build}"

# shellcheck source=env_u55c.sh
source "$ROOT_DIR/scripts/env_u55c.sh"

TOP="CuperPcgCallipepla"
INPUT_XO="${1:-$BUILD_DIR/$TOP.xo}"
OUTPUT_XCLBIN="${2:-$BUILD_DIR/$TOP.xclbin}"
CONNECTIVITY_CFG="$ROOT_DIR/cfg/connectivity.cfg"

if [[ ! -f "$INPUT_XO" ]]; then
  echo "Missing XO: $INPUT_XO" >&2
  echo "Run: $ROOT_DIR/scripts/build_xo_u55c.sh" >&2
  exit 1
fi

mkdir -p "$BUILD_DIR/logs" "$BUILD_DIR/reports" "$BUILD_DIR/vpp_tmp"

VPP_FREQ_ARGS=()
if [[ "${CUPER_CALLIPEPLA_KERNEL_FREQUENCY:-}" != "" ]]; then
  VPP_FREQ_ARGS+=(--kernel_frequency "$CUPER_CALLIPEPLA_KERNEL_FREQUENCY")
fi

cmd=(
  v++ --link
  --target hw
  --platform "$XPLATFORM"
  "${VPP_FREQ_ARGS[@]}"
  --config "$CONNECTIVITY_CFG"
  --temp_dir "$BUILD_DIR/vpp_tmp"
  --log_dir "$BUILD_DIR/logs"
  --report_dir "$BUILD_DIR/reports"
  -o "$OUTPUT_XCLBIN"
  "$INPUT_XO"
)

printf 'Running:'
printf ' %q' "${cmd[@]}"
printf '\n'
env -u XILINX_XRT "${cmd[@]}"
