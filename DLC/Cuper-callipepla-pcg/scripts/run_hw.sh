#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$(cd "$ROOT_DIR/../.." && pwd)/cuper-tapa-pcg-callipepla-build}"

# shellcheck source=env_u55c.sh
source "$ROOT_DIR/scripts/env_u55c.sh"

BITFILE="${BITFILE:-$BUILD_DIR/CuperPcgCallipepla.xclbin}"
DATASET="${1:-$(cd "$ROOT_DIR/../.." && pwd)/data/suitesparse/Schmid/csr/thermal2_n16}"

if [[ ! -x "$BUILD_DIR/cuper_callipepla_pcg_host" ]]; then
  echo "Missing host executable: $BUILD_DIR/cuper_callipepla_pcg_host" >&2
  echo "Run: $ROOT_DIR/scripts/build_host.sh" >&2
  exit 1
fi
if [[ ! -f "$BITFILE" ]]; then
  echo "Missing bitstream: $BITFILE" >&2
  exit 1
fi

exec "$BUILD_DIR/cuper_callipepla_pcg_host" "$DATASET" \
  --bitstream "$BITFILE" \
  --tau "${TAU:-1e-10}" \
  --max-iters "${MAX_ITERS:-0}" \
  --diff-tol "${DIFF_TOL:-1e-3}" \
  --kernel-timeout-sec "${KERNEL_TIMEOUT_SEC:-60}" \
  --live-status-poll-sec "${LIVE_STATUS_POLL_SEC:-0}"
