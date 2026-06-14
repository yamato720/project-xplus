#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$(cd "$ROOT_DIR/../.." && pwd)/cuper-jacobi-iteration-build}"

# shellcheck source=env_u55c.sh
source "$ROOT_DIR/scripts/env_u55c.sh"

TOP="${JACOBI_TOP:-CuperJacobiIteration}"
INPUT_XO="${1:-$BUILD_DIR/$TOP.xo}"
OUTPUT_XCLBIN="${2:-$BUILD_DIR/$TOP.xclbin}"
CONNECTIVITY_CFG="$ROOT_DIR/cfg/connectivity.cfg"

if [[ ! -f "$INPUT_XO" ]]; then
  echo "Missing XO: $INPUT_XO" >&2
  echo "Run: $ROOT_DIR/scripts/build_xo_u55c.sh" >&2
  exit 1
fi

mkdir -p "$BUILD_DIR/logs" "$BUILD_DIR/reports" "$BUILD_DIR/vpp_tmp"

if [[ "$TOP" == "CuperJacobiMmapProbeOnly" ]]; then
  CONNECTIVITY_CFG="$BUILD_DIR/connectivity_mmap_probe.cfg"
  {
    echo "[connectivity]"
    echo "nk=CuperJacobiMmapProbeOnly:1"
    echo
    echo "sp=CuperJacobiMmapProbeOnly_1.Status:HBM[24]"
    if [[ "${JACOBI_MMAP_PROBE_SPLIT:-0}" != "0" && "${JACOBI_MMAP_PROBE_SPLIT:-}" != "" ]]; then
      echo "sp=CuperJacobiMmapProbeOnly_1.Metrics:HBM[25]"
      echo "sp=CuperJacobiMmapProbeOnly_1.Debug:HBM[26]"
    else
      echo "sp=CuperJacobiMmapProbeOnly_1.Metrics:HBM[24]"
      echo "sp=CuperJacobiMmapProbeOnly_1.Debug:HBM[24]"
    fi
  } > "$CONNECTIVITY_CFG"
elif { [[ "${JACOBI_DEADLOCK_DEBUG:-0}" != "0" && "${JACOBI_DEADLOCK_DEBUG:-}" != "" ]] ||
       [[ "${JACOBI_TRACE_ISOTOPE:-0}" != "0" && "${JACOBI_TRACE_ISOTOPE:-}" != "" ]] ||
       [[ "${JACOBI_TRACE_LIGHT:-0}" != "0" && "${JACOBI_TRACE_LIGHT:-}" != "" ]]; }; then
  CONNECTIVITY_CFG="$BUILD_DIR/connectivity_trace_debug.cfg"
  awk '
    /^sp=CuperJacobiIteration_1.Metrics:/ {
      print "sp=CuperJacobiIteration_1.Metrics:HBM[25]"
      next
    }
    { print }
  ' "$ROOT_DIR/cfg/connectivity.cfg" > "$CONNECTIVITY_CFG"
  {
    echo
    echo "# Optional trace/debug output buffer. Keep Status/Metrics/Debug split"
    echo "# so pre-Finish snapshots use the mmap boundary already proven by"
    echo "# CuperJacobiMmapProbeOnly split-bank smoke."
    echo "sp=CuperJacobiIteration_1.Debug:HBM[26]"
  } >> "$CONNECTIVITY_CFG"
fi

cmd=(
  v++ --link
  --target hw
  --platform "$XPLATFORM"
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

# v++ 2022.2 的打包步骤需要使用 Vitis 自带的 xclbinutil。这里如果继承
# 本机 XRT 2.18 的 XILINX_XRT，最后一步会去加载 Boost 1.83 版本库并失败；
# host 编译/上板运行仍由 env_u55c.sh 提供 XRT 路径，不受这个子进程环境影响。
env -u XILINX_XRT "${cmd[@]}"
