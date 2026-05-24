#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# shellcheck source=env_u55c.sh
source "$ROOT_DIR/scripts/env_u55c.sh"

INPUT_XO="${1:-$ROOT_DIR/Cuper_2022.xo}"
OUTPUT_XCLBIN="${2:-$ROOT_DIR/Cuper_2022.xclbin}"

if [[ ! -f "$INPUT_XO" ]]; then
  echo "Missing XO: $INPUT_XO" >&2
  echo "Run: $ROOT_DIR/scripts/build_xo_u55c.sh" >&2
  exit 1
fi

mkdir -p "$ROOT_DIR/logs" "$ROOT_DIR/reports" "$ROOT_DIR/build/vpp_tmp"

cmd=(
  v++ --link
  --target hw
  --platform "$XPLATFORM"
  --config "$ROOT_DIR/cfg/connectivity.cfg"
  --temp_dir "$ROOT_DIR/build/vpp_tmp"
  --log_dir "$ROOT_DIR/logs"
  --report_dir "$ROOT_DIR/reports"
  -o "$OUTPUT_XCLBIN"
  "$INPUT_XO"
)

printf 'Running:'
printf ' %q' "${cmd[@]}"
printf '\n'

"${cmd[@]}"
