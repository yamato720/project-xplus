#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
make run-xrt TARGET=sw_emu "$@"
