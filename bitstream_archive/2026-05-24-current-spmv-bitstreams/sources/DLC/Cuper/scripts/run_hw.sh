#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# shellcheck source=env_u55c.sh
source "$ROOT_DIR/scripts/env_u55c.sh"

BITFILE="${BITFILE:-$ROOT_DIR/Cuper_2022.xclbin}"
MATRIX_FILE="${1:-$ROOT_DIR/data/matrices/sit100/sit100.mtx}"

if [[ ! -x "$ROOT_DIR/build/cuper_host" ]]; then
  echo "Missing host executable: $ROOT_DIR/build/cuper_host" >&2
  echo "Run: $ROOT_DIR/scripts/build_host.sh" >&2
  exit 1
fi

if [[ ! -f "$BITFILE" ]]; then
  echo "Missing bitstream: $BITFILE" >&2
  echo "Set BITFILE=/path/to/Cuper_2022.xclbin or copy it into this directory." >&2
  exit 1
fi

export BITFILE
exec "$ROOT_DIR/build/cuper_host" "$MATRIX_FILE"
