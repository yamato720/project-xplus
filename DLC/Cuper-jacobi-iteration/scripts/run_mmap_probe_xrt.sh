#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$(cd "$ROOT_DIR/../.." && pwd)/cuper-jacobi-iteration-build}"

# shellcheck source=env_u55c.sh
source "$ROOT_DIR/scripts/env_u55c.sh"

BITFILE="${BITFILE:-$BUILD_DIR/CuperJacobiMmapProbeOnly.xclbin}"

if [[ ! -x "$BUILD_DIR/cuper_jacobi_mmap_probe_xrt" ]]; then
  echo "Missing native XRT debug runner: $BUILD_DIR/cuper_jacobi_mmap_probe_xrt" >&2
  echo "Run: $ROOT_DIR/scripts/build_host.sh" >&2
  exit 1
fi

if [[ ! -f "$BITFILE" ]]; then
  echo "Missing xclbin: $BITFILE" >&2
  echo "Run with JACOBI_TOP=CuperJacobiMmapProbeOnly: $ROOT_DIR/scripts/build_xo_u55c.sh && $ROOT_DIR/scripts/link_xclbin_u55c.sh" >&2
  exit 1
fi

exec "$BUILD_DIR/cuper_jacobi_mmap_probe_xrt" "$BITFILE" "$@"
