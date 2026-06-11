#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$(cd "$ROOT_DIR/../.." && pwd)/cuper-jacobi-iteration-build}"

# shellcheck source=env_u55c.sh
source "$ROOT_DIR/scripts/env_u55c.sh"

BITFILE="${BITFILE:-$BUILD_DIR/CuperJacobiIteration.xclbin}"
MATRIX_FILE="${1:-$ROOT_DIR/data/matrices/cant.mtx}"

if [[ ! -x "$BUILD_DIR/cuper_jacobi_host" ]]; then
  echo "Missing host executable: $BUILD_DIR/cuper_jacobi_host" >&2
  echo "Run: $ROOT_DIR/scripts/build_host.sh" >&2
  exit 1
fi

if [[ ! -f "$BITFILE" ]]; then
  echo "Missing bitstream: $BITFILE" >&2
  echo "Set BITFILE=/path/to/CuperJacobiIteration.xclbin or copy it into this directory." >&2
  exit 1
fi

export BITFILE
exec "$BUILD_DIR/cuper_jacobi_host" "$MATRIX_FILE"
