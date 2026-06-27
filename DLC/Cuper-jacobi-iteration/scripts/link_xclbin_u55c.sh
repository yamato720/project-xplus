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
JACOBI_DEBUG_GRAPH=0
JACOBI_WIDE_GRAPH=0
SPMV_ONLY_GRAPH=0

HBM_CHANNELS="${JACOBI_HBM_CHANNELS:-}"
if [[ "$HBM_CHANNELS" == "" && "${JACOBI_WIDE_HBM:-0}" != "0" && "${JACOBI_WIDE_HBM:-}" != "" ]]; then
  HBM_CHANNELS="24"
fi
HBM_CHANNELS="${HBM_CHANNELS:-16}"

write_wide_connectivity_cfg() {
  local cfg_path="$1"
  local debug_graph="$2"
  {
    echo "[connectivity]"
    echo "nk=CuperJacobiIteration:1"
    echo
    echo "# Wide-HBM Jacobi demo:"
    echo "#   Matrix_data_0..23 use HBM[0..23]."
    echo "#   Non-A scalar/vector/control buffers share HBM[30]."
    echo "#   Metrics and optional Debug share HBM[31] as the debug/time channel."
    echo "sp=CuperJacobiIteration_1.SpElement_list_ptr:HBM[30]"
    echo
    for channel in $(seq 0 23); do
      echo "sp=CuperJacobiIteration_1.Matrix_data_${channel}:HBM[${channel}]"
    done
    echo
    echo "sp=CuperJacobiIteration_1.B:HBM[30]"
    echo "sp=CuperJacobiIteration_1.Diag_inv:HBM[30]"
    echo "sp=CuperJacobiIteration_1.X:HBM[30]"
    echo "sp=CuperJacobiIteration_1.Status:HBM[30]"
    echo "sp=CuperJacobiIteration_1.Metrics:HBM[31]"
    if [[ "$debug_graph" == "1" ]]; then
      echo "sp=CuperJacobiIteration_1.Debug:HBM[31]"
    fi
  } > "$cfg_path"
}

write_jacobi_8hbm_connectivity_cfg() {
  local cfg_path="$1"
  local debug_graph="$2"
  {
    echo "[connectivity]"
    echo "nk=CuperJacobiIteration:1"
    echo
    echo "# 8-HBM Jacobi experiment:"
    echo "#   Matrix_data_0..7 use HBM[0..7]."
    echo "#   Aux buffers stay on the same banks as the proven 16-HBM debug ABI."
    echo "sp=CuperJacobiIteration_1.SpElement_list_ptr:HBM[0]"
    echo
    for channel in $(seq 0 7); do
      echo "sp=CuperJacobiIteration_1.Matrix_data_${channel}:HBM[${channel}]"
    done
    echo
    echo "sp=CuperJacobiIteration_1.B:HBM[20]"
    echo "sp=CuperJacobiIteration_1.Diag_inv:HBM[21]"
    echo "sp=CuperJacobiIteration_1.X:HBM[22]"
    echo "sp=CuperJacobiIteration_1.Status:HBM[24]"
    echo "sp=CuperJacobiIteration_1.Metrics:HBM[25]"
    if [[ "$debug_graph" == "1" ]]; then
      echo "sp=CuperJacobiIteration_1.Debug:HBM[26]"
    fi
  } > "$cfg_path"
}

write_spmv_only_connectivity_cfg() {
  local cfg_path="$1"
  local channels="$2"
  local scoreboard_debug="$3"
  {
    echo "[connectivity]"
    echo "nk=CuperSpmvServiceOnly:1"
    echo
    echo "# Cuper SpMV service-only wide-HBM experiment."
    echo "#   Matrix_data uses ${channels} HBM channels."
    if [[ "$channels" == "32" ]]; then
      echo "#   32-lane mode consumes all HBM banks for Matrix_data, so aux buffers share HBM[28..31]."
    else
      echo "#   Aux buffers use free HBM banks after Matrix_data to avoid the old one-bank aux pile-up."
    fi
    echo
    for channel in $(seq 0 $((channels - 1))); do
      echo "sp=CuperSpmvServiceOnly_1.Matrix_data_${channel}:HBM[${channel}]"
    done
    echo
    if [[ "$channels" == "32" ]]; then
      echo "sp=CuperSpmvServiceOnly_1.SpElement_list_ptr:HBM[28]"
      echo "sp=CuperSpmvServiceOnly_1.X:HBM[29]"
      echo "sp=CuperSpmvServiceOnly_1.Y_out:HBM[30]"
      echo "sp=CuperSpmvServiceOnly_1.Status:HBM[31]"
      echo "sp=CuperSpmvServiceOnly_1.Metrics:HBM[31]"
      if [[ "$scoreboard_debug" == "1" ]]; then
        echo "sp=CuperSpmvServiceOnly_1.Debug:HBM[31]"
      fi
    else
      local ptr_bank=$channels
      local x_bank=$((channels + 1))
      local y_bank=$((channels + 2))
      echo "sp=CuperSpmvServiceOnly_1.SpElement_list_ptr:HBM[${ptr_bank}]"
      echo "sp=CuperSpmvServiceOnly_1.X:HBM[${x_bank}]"
      echo "sp=CuperSpmvServiceOnly_1.Y_out:HBM[${y_bank}]"
      echo "sp=CuperSpmvServiceOnly_1.Status:HBM[30]"
      echo "sp=CuperSpmvServiceOnly_1.Metrics:HBM[31]"
      if [[ "$scoreboard_debug" == "1" ]]; then
        echo "sp=CuperSpmvServiceOnly_1.Debug:HBM[31]"
      fi
    fi
  } > "$cfg_path"
}

if [[ ! -f "$INPUT_XO" ]]; then
  echo "Missing XO: $INPUT_XO" >&2
  echo "Run: $ROOT_DIR/scripts/build_xo_u55c.sh" >&2
  exit 1
fi

mkdir -p "$BUILD_DIR/logs" "$BUILD_DIR/reports" "$BUILD_DIR/vpp_tmp"

case "$HBM_CHANNELS" in
  8|16|24|32)
    ;;
  *)
    echo "Unsupported JACOBI_HBM_CHANNELS=$HBM_CHANNELS; use 8, 16, 24, or 32." >&2
    exit 1
    ;;
esac

if [[ "$TOP" == "CuperSpmvServiceOnly" ]]; then
  SPMV_ONLY_GRAPH=1
  CONNECTIVITY_CFG="$BUILD_DIR/connectivity_spmv_only_${HBM_CHANNELS}hbm.cfg"
  SPMV_SCOREBOARD_DEBUG_GRAPH=0
  if [[ "${JACOBI_SPMV_SCOREBOARD_DEBUG:-0}" != "0" && "${JACOBI_SPMV_SCOREBOARD_DEBUG:-}" != "" ]]; then
    SPMV_SCOREBOARD_DEBUG_GRAPH=1
  fi
  write_spmv_only_connectivity_cfg "$CONNECTIVITY_CFG" "$HBM_CHANNELS" "$SPMV_SCOREBOARD_DEBUG_GRAPH"
elif [[ "$TOP" == "CuperJacobiMmapProbeOnly" ]]; then
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
  JACOBI_DEBUG_GRAPH=1
fi

if [[ "$TOP" == "CuperJacobiIteration" && "$HBM_CHANNELS" == "8" ]]; then
  CONNECTIVITY_CFG="$BUILD_DIR/connectivity_jacobi_8hbm.cfg"
  write_jacobi_8hbm_connectivity_cfg "$CONNECTIVITY_CFG" "$JACOBI_DEBUG_GRAPH"
elif [[ "$TOP" == "CuperJacobiIteration" && "$HBM_CHANNELS" == "24" ]]; then
  if [[ "${JACOBI_TRACE_ISOTOPE:-0}" != "0" && "${JACOBI_TRACE_ISOTOPE:-}" != "" ]] ||
     [[ "${JACOBI_DEADLOCK_DEBUG:-0}" != "0" && "${JACOBI_DEADLOCK_DEBUG:-}" != "" ]]; then
    echo "JACOBI_WIDE_HBM currently supports no trace or JACOBI_TRACE_LIGHT only; full isotope/deadlock trace still enumerates 16 matrix/accumulator lanes." >&2
    exit 1
  fi
  JACOBI_WIDE_GRAPH=1
  CONNECTIVITY_CFG="$BUILD_DIR/connectivity_wide_hbm.cfg"
  write_wide_connectivity_cfg "$CONNECTIVITY_CFG" "$JACOBI_DEBUG_GRAPH"
elif [[ "$TOP" == "CuperJacobiIteration" && "$HBM_CHANNELS" == "32" ]]; then
  echo "CuperJacobiIteration 32-lane connectivity is not enabled; use JACOBI_TOP=CuperSpmvServiceOnly for 32-lane SpMV-only exploration." >&2
  exit 1
elif [[ "$TOP" == "CuperJacobiIteration" && "$JACOBI_DEBUG_GRAPH" == "1" ]]; then
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

VPP_FREQ_ARGS=()
if [[ "$TOP" == "CuperJacobiIteration" && "$JACOBI_DEBUG_GRAPH" == "1" ]]; then
  JACOBI_KERNEL_FREQUENCY="${JACOBI_KERNEL_FREQUENCY:-150}"
  # v++ 2022.2 这里用 MHz。先用全 kernel 频率降压，目标是拿到 timing 更可信的
  # full graph debug xclbin；性能版可通过 JACOBI_KERNEL_FREQUENCY 覆盖。
  VPP_FREQ_ARGS+=(--kernel_frequency "$JACOBI_KERNEL_FREQUENCY")
  echo "Jacobi debug link frequency: ${JACOBI_KERNEL_FREQUENCY} MHz"
elif [[ "${JACOBI_KERNEL_FREQUENCY:-}" != "" ]]; then
  # v++ 2022.2 这里用 MHz。性能/收 timing 实验可通过 JACOBI_KERNEL_FREQUENCY 覆盖。
  VPP_FREQ_ARGS+=(--kernel_frequency "$JACOBI_KERNEL_FREQUENCY")
  echo "Kernel link frequency: ${JACOBI_KERNEL_FREQUENCY} MHz"
fi

if [[ "$JACOBI_WIDE_GRAPH" == "1" ]]; then
  echo "Jacobi wide HBM connectivity: Matrix_data[0..23]=HBM[0..23], aux=HBM[30], debug/time=HBM[31]"
fi
if [[ "$TOP" == "CuperJacobiIteration" && "$HBM_CHANNELS" == "8" ]]; then
  echo "Jacobi 8-HBM connectivity: Matrix_data[0..7]=HBM[0..7], cfg=$CONNECTIVITY_CFG"
fi
if [[ "$SPMV_ONLY_GRAPH" == "1" ]]; then
  echo "SpMV-only connectivity: Matrix_data[0..$((HBM_CHANNELS - 1))]=HBM[0..$((HBM_CHANNELS - 1))], cfg=$CONNECTIVITY_CFG"
fi

cmd=(
  v++ --link
  --target hw
  --platform "$XPLATFORM"
  "${VPP_FREQ_ARGS[@]}"
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
