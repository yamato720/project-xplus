#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# shellcheck source=env_u55c.sh
source "$ROOT_DIR/scripts/env_u55c.sh"

cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build" \
  -DTAPA_ROOT="$TAPA_ROOT" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT_DIR/build" -j"$(nproc)"
