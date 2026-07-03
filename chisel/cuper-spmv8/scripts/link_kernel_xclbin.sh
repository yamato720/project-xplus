#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TARGET="${TARGET:-hw}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/cuper-spmv-chisel8-build/$TARGET}"
XO_PATH="${XO_PATH:-$BUILD_DIR/CuperSpmvChisel8.xo}"
XCLBIN_PATH="${XCLBIN_PATH:-$BUILD_DIR/CuperSpmvChisel8.xclbin}"
CONNECTIVITY="${CONNECTIVITY:-$ROOT_DIR/cfg/connectivity_cuper_spmv_chisel8_u55c.cfg}"
VPP_BIN="${VPP:-v++}"
XPLATFORM="${XPLATFORM:-${PLATFORM:-}}"
KERNEL_FREQUENCY="${CUPER_SPMV_CHISEL8_KERNEL_FREQUENCY:-150}"

if [[ -z "$XPLATFORM" ]]; then
  DEVICE="${DEVICE:-xilinx_u55c_gen3x16_xdma_3_202210_1}"
  LOCAL_XILINX_DIR="${LOCAL_XILINX_DIR:-$ROOT_DIR/../xilinx-local}"
  if [[ -f "$LOCAL_XILINX_DIR/opt/xilinx/platforms/$DEVICE/$DEVICE.xpfm" ]]; then
    XPLATFORM="$LOCAL_XILINX_DIR/opt/xilinx/platforms/$DEVICE/$DEVICE.xpfm"
  else
    XPLATFORM="/opt/xilinx/platforms/$DEVICE/$DEVICE.xpfm"
  fi
fi

test -f "$XO_PATH" || { echo "missing XO: $XO_PATH" >&2; exit 1; }
test -f "$CONNECTIVITY" || { echo "missing connectivity cfg: $CONNECTIVITY" >&2; exit 1; }
test -f "$XPLATFORM" || { echo "missing platform: $XPLATFORM" >&2; exit 1; }

mkdir -p "$BUILD_DIR/logs" "$BUILD_DIR/reports"
"$VPP_BIN" --link \
  --target "$TARGET" \
  --platform "$XPLATFORM" \
  --config "$CONNECTIVITY" \
  --kernel_frequency "$KERNEL_FREQUENCY" \
  --temp_dir "$BUILD_DIR/vpp_tmp" \
  --log_dir "$BUILD_DIR/logs" \
  --report_dir "$BUILD_DIR/reports" \
  --report_level 0 \
  --output "$XCLBIN_PATH" \
  "$XO_PATH"

echo "xclbin: $XCLBIN_PATH"
