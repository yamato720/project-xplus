#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/cuper-spmv-chisel8-build/hw}"
RTL_FILE="${RTL_FILE:-$ROOT_DIR/verilog/chisel/CuperSpmvChisel8.sv}"
XO_PATH="${XO_PATH:-$BUILD_DIR/CuperSpmvChisel8.xo}"
KERNEL_XML="${KERNEL_XML:-$ROOT_DIR/chisel/cuper-spmv8/packaging/CuperSpmvChisel8.kernel.xml}"
VIVADO_BIN="${VIVADO:-vivado}"
VIVADO_PART="${VIVADO_PART:-xcu55c-fsvh2892-2L-e}"
TCL_SCRIPT="$ROOT_DIR/chisel/cuper-spmv8/scripts/package_kernel_xo.tcl"

if [[ ! -f "$RTL_FILE" ]]; then
  echo "missing RTL file: $RTL_FILE" >&2
  echo "run: $ROOT_DIR/chisel/cuper-spmv8/scripts/generate_kernel.sh" >&2
  exit 1
fi
if [[ ! -f "$KERNEL_XML" ]]; then
  echo "missing kernel XML: $KERNEL_XML" >&2
  exit 1
fi

mkdir -p "$BUILD_DIR"
"$VIVADO_BIN" -mode batch -source "$TCL_SCRIPT" -tclargs "$BUILD_DIR" "$RTL_FILE" "$XO_PATH" "$KERNEL_XML" "$VIVADO_PART" "$ROOT_DIR"

python3 - "$XO_PATH" "$KERNEL_XML" <<'PY'
import sys
import zipfile
from pathlib import Path

xo_path = Path(sys.argv[1])
xml_path = Path(sys.argv[2])
entry = "CuperSpmvChisel8/kernel.xml"
tmp = xo_path.with_suffix(xo_path.suffix + ".tmp")
with zipfile.ZipFile(xo_path, "r") as src, zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as dst:
    for item in src.infolist():
        if item.filename == entry:
            continue
        dst.writestr(item, src.read(item.filename))
    dst.writestr(entry, xml_path.read_bytes())
tmp.replace(xo_path)
PY

echo "xo: $XO_PATH"
