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
JACOBI_WIDE_GRAPH=0
SPMV_ONLY_GRAPH=0

HBM_CHANNELS="${JACOBI_HBM_CHANNELS:-}"
if [[ "$HBM_CHANNELS" == "" && "${JACOBI_WIDE_HBM:-0}" != "0" && "${JACOBI_WIDE_HBM:-}" != "" ]]; then
  HBM_CHANNELS="24"
fi
HBM_CHANNELS="${HBM_CHANNELS:-16}"

write_wide_connectivity_cfg() {
  local cfg_path="$1"
  {
    echo "[connectivity]"
    echo "nk=CuperJacobiIteration:1"
    echo
    echo "# Wide-HBM Jacobi demo:"
    echo "#   Matrix_data_0..23 use HBM[0..23]."
    echo "#   Non-A scalar/vector/control buffers share HBM[30]."
    echo "#   Metrics uses HBM[31] as the timing channel."
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
  } > "$cfg_path"
}

write_spmv_only_connectivity_cfg() {
  local cfg_path="$1"
  local channels="$2"
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
    else
      local ptr_bank=$channels
      local x_bank=$((channels + 1))
      local y_bank=$((channels + 2))
      echo "sp=CuperSpmvServiceOnly_1.SpElement_list_ptr:HBM[${ptr_bank}]"
      echo "sp=CuperSpmvServiceOnly_1.X:HBM[${x_bank}]"
      echo "sp=CuperSpmvServiceOnly_1.Y_out:HBM[${y_bank}]"
      echo "sp=CuperSpmvServiceOnly_1.Status:HBM[30]"
      echo "sp=CuperSpmvServiceOnly_1.Metrics:HBM[31]"
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
  16|24|32)
    ;;
  *)
    echo "Unsupported JACOBI_HBM_CHANNELS=$HBM_CHANNELS; use 16, 24, or 32." >&2
    exit 1
    ;;
esac

if [[ "$TOP" == "CuperSpmvServiceOnly" ]]; then
  SPMV_ONLY_GRAPH=1
  CONNECTIVITY_CFG="$BUILD_DIR/connectivity_spmv_only_${HBM_CHANNELS}hbm.cfg"
  write_spmv_only_connectivity_cfg "$CONNECTIVITY_CFG" "$HBM_CHANNELS"
elif [[ "$TOP" == "CuperJacobiIteration" && "$HBM_CHANNELS" == "24" ]]; then
  JACOBI_WIDE_GRAPH=1
  CONNECTIVITY_CFG="$BUILD_DIR/connectivity_wide_hbm.cfg"
  write_wide_connectivity_cfg "$CONNECTIVITY_CFG"
elif [[ "$TOP" == "CuperJacobiIteration" && "$HBM_CHANNELS" == "32" ]]; then
  echo "CuperJacobiIteration 32-lane connectivity is not enabled; use TOP=CuperSpmvServiceOnly for 32-lane SpMV-only exploration." >&2
  exit 1
fi

VPP_FREQ_ARGS=()
if [[ "${JACOBI_KERNEL_FREQUENCY:-}" != "" ]]; then
  # v++ 2022.2 这里用 MHz。性能/收 timing 实验可通过 JACOBI_KERNEL_FREQUENCY 覆盖。
  VPP_FREQ_ARGS+=(--kernel_frequency "$JACOBI_KERNEL_FREQUENCY")
  echo "Kernel link frequency: ${JACOBI_KERNEL_FREQUENCY} MHz"
fi

if [[ "$JACOBI_WIDE_GRAPH" == "1" ]]; then
  echo "Jacobi wide HBM connectivity: Matrix_data[0..23]=HBM[0..23], aux=HBM[30], metrics=HBM[31]"
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
