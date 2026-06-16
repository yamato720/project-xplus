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

if [[ ! -f "$INPUT_XO" ]]; then
  echo "Missing XO: $INPUT_XO" >&2
  echo "Run: $ROOT_DIR/scripts/build_xo_u55c.sh" >&2
  exit 1
fi

mkdir -p "$BUILD_DIR/logs" "$BUILD_DIR/reports" "$BUILD_DIR/vpp_tmp"

if [[ "$TOP" == "CuperJacobiIteration" ]] &&
   [[ "${JACOBI_WIDE_HBM:-0}" != "0" && "${JACOBI_WIDE_HBM:-}" != "" ]]; then
  JACOBI_WIDE_GRAPH=1
  CONNECTIVITY_CFG="$BUILD_DIR/connectivity_wide_hbm.cfg"
  write_wide_connectivity_cfg "$CONNECTIVITY_CFG"
fi

VPP_FREQ_ARGS=()
if [[ "$TOP" == "CuperJacobiIteration" && "${JACOBI_KERNEL_FREQUENCY:-}" != "" ]]; then
  # v++ 2022.2 这里用 MHz。性能/收 timing 实验可通过 JACOBI_KERNEL_FREQUENCY 覆盖。
  VPP_FREQ_ARGS+=(--kernel_frequency "$JACOBI_KERNEL_FREQUENCY")
  echo "Jacobi link frequency: ${JACOBI_KERNEL_FREQUENCY} MHz"
fi

if [[ "$JACOBI_WIDE_GRAPH" == "1" ]]; then
  echo "Jacobi wide HBM connectivity: Matrix_data[0..23]=HBM[0..23], aux=HBM[30], metrics=HBM[31]"
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
