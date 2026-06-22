#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$(cd "$ROOT_DIR/../.." && pwd)/cuper-jacobi-iteration-build}"

# shellcheck source=env_u55c.sh
source "$ROOT_DIR/scripts/env_u55c.sh"

mkdir -p "$BUILD_DIR"

TOP="${JACOBI_TOP:-CuperJacobiIteration}"
OUTPUT_XO="${1:-$BUILD_DIR/$TOP.xo}"
WORK_DIR="${WORK_DIR:-$BUILD_DIR/tapa_$TOP}"
if [[ "${JACOBI_TAPA_PACK_ONLY:-0}" != "0" && "${JACOBI_TAPA_PACK_ONLY:-}" != "" ]]; then
  echo "JACOBI_TAPA_PACK_ONLY=1: reuse existing TAPA WORK_DIR and run hotpatch pack only."
  "$ROOT_DIR/scripts/pack_hotpatch_xo_u55c.sh" "$OUTPUT_XO"
  exit 0
fi
if [[ "${CLOCK_PERIOD:-}" == "" ]]; then
  if [[ "$TOP" == "CuperJacobiIteration" ]] &&
     { [[ "${JACOBI_DEADLOCK_DEBUG:-0}" != "0" && "${JACOBI_DEADLOCK_DEBUG:-}" != "" ]] ||
       [[ "${JACOBI_TRACE_ISOTOPE:-0}" != "0" && "${JACOBI_TRACE_ISOTOPE:-}" != "" ]] ||
       [[ "${JACOBI_TRACE_LIGHT:-0}" != "0" && "${JACOBI_TRACE_LIGHT:-}" != "" ]]; }; then
    # debug full graph 先保守降频，避免 timing violation 污染大规模硬件判断。
    CLOCK_PERIOD="4.0"
  else
    CLOCK_PERIOD="2.0"
  fi
fi
JOBS="${JOBS:-$(nproc)}"

cflags=(
  "-I$ROOT_DIR/include"
  "-I$ROOT_DIR/src"
)

HBM_CHANNELS="${JACOBI_HBM_CHANNELS:-}"
if [[ "$HBM_CHANNELS" == "" && "${JACOBI_WIDE_HBM:-0}" != "0" && "${JACOBI_WIDE_HBM:-}" != "" ]]; then
  HBM_CHANNELS="24"
fi
HBM_CHANNELS="${HBM_CHANNELS:-16}"

if [[ "${JACOBI_DEADLOCK_DEBUG:-0}" != "0" && "${JACOBI_DEADLOCK_DEBUG:-}" != "" ]]; then
  cflags+=("-DJACOBI_DEADLOCK_DEBUG=1")
fi

if [[ "${JACOBI_TRACE_ISOTOPE:-0}" != "0" && "${JACOBI_TRACE_ISOTOPE:-}" != "" ]]; then
  cflags+=("-DJACOBI_TRACE_ISOTOPE=1")
fi

if [[ "${JACOBI_TRACE_LIGHT:-0}" != "0" && "${JACOBI_TRACE_LIGHT:-}" != "" ]]; then
  cflags+=("-DJACOBI_TRACE_LIGHT=1")
fi

if [[ "$HBM_CHANNELS" != "16" ]]; then
  if [[ "${JACOBI_TRACE_ISOTOPE:-0}" != "0" && "${JACOBI_TRACE_ISOTOPE:-}" != "" ]] ||
     [[ "${JACOBI_DEADLOCK_DEBUG:-0}" != "0" && "${JACOBI_DEADLOCK_DEBUG:-}" != "" ]]; then
    echo "JACOBI_HBM_CHANNELS=$HBM_CHANNELS currently supports no trace or JACOBI_TRACE_LIGHT only; full isotope/deadlock trace still enumerates 16 matrix/accumulator lanes." >&2
    exit 1
  fi
fi

if [[ "${JACOBI_WIDE_HBM:-0}" != "0" && "${JACOBI_WIDE_HBM:-}" != "" ]]; then
  cflags+=("-DJACOBI_WIDE_HBM=1")
fi

case "$HBM_CHANNELS" in
  16)
    ;;
  24)
    cflags+=("-DJACOBI_HBM_CHANNELS_24=1")
    ;;
  32)
    cflags+=("-DJACOBI_HBM_CHANNELS_32=1")
    ;;
  *)
    echo "Unsupported JACOBI_HBM_CHANNELS=$HBM_CHANNELS; use 16, 24, or 32." >&2
    exit 1
    ;;
esac

echo "Cuper Jacobi TAPA top: $TOP"
echo "Cuper Matrix_data HBM channels: $HBM_CHANNELS"

if [[ "${JACOBI_BLOCKING_ENTRY_PROBE:-0}" != "0" && "${JACOBI_BLOCKING_ENTRY_PROBE:-}" != "" ]]; then
  cflags+=("-DJACOBI_BLOCKING_ENTRY_PROBE=1")
fi

if [[ "${JACOBI_SPMV_STRIP_PADDING:-0}" != "0" && "${JACOBI_SPMV_STRIP_PADDING:-}" != "" ]]; then
  cflags+=("-DJACOBI_SPMV_STRIP_PADDING=1")
fi

if [[ "${JACOBI_SPMV_ACC_WINDOW:-}" != "" ]]; then
  if [[ ! "$JACOBI_SPMV_ACC_WINDOW" =~ ^[1-9][0-9]*$ ]]; then
    echo "JACOBI_SPMV_ACC_WINDOW must be a positive integer; got '$JACOBI_SPMV_ACC_WINDOW'." >&2
    exit 1
  fi
  cflags+=("-DJACOBI_SPMV_ACC_WINDOW=$JACOBI_SPMV_ACC_WINDOW")
fi

if [[ "${JACOBI_SPMV_COMPACT_PE:-0}" != "0" && "${JACOBI_SPMV_COMPACT_PE:-}" != "" ]]; then
  cflags+=("-DJACOBI_SPMV_COMPACT_PE=1")
fi

if [[ "${JACOBI_SPMV_OOO_ACCUMULATE_RTL:-0}" != "0" && "${JACOBI_SPMV_OOO_ACCUMULATE_RTL:-}" != "" ]]; then
  JACOBI_SPMV_LANE_STATIC_REAL=1
  JACOBI_SPMV_OOO_ACCUMULATE=1
fi

if [[ "${JACOBI_SPMV_SEGMENTED_ACCUMULATE:-0}" != "0" && "${JACOBI_SPMV_SEGMENTED_ACCUMULATE:-}" != "" ]]; then
  if [[ "${JACOBI_SPMV_OOO_ACCUMULATE_RTL:-0}" != "0" && "${JACOBI_SPMV_OOO_ACCUMULATE_RTL:-}" != "" ]]; then
    echo "JACOBI_SPMV_SEGMENTED_ACCUMULATE=1 is a non-RTL path; unset JACOBI_SPMV_OOO_ACCUMULATE_RTL." >&2
    exit 1
  fi
  JACOBI_SPMV_LANE_STATIC_REAL=1
  JACOBI_SPMV_OOO_ACCUMULATE=1
fi

if [[ "${JACOBI_SPMV_LANE_STATIC_REAL:-0}" != "0" && "${JACOBI_SPMV_LANE_STATIC_REAL:-}" != "" ]]; then
  cflags+=("-DJACOBI_SPMV_LANE_STATIC_REAL=1")
fi

if [[ "${JACOBI_SPMV_OOO_ACCUMULATE:-0}" != "0" && "${JACOBI_SPMV_OOO_ACCUMULATE:-}" != "" ]]; then
  cflags+=("-DJACOBI_SPMV_OOO_ACCUMULATE=1")
fi

if [[ "${JACOBI_SPMV_SEGMENTED_ACCUMULATE:-0}" != "0" && "${JACOBI_SPMV_SEGMENTED_ACCUMULATE:-}" != "" ]]; then
  cflags+=("-DJACOBI_SPMV_SEGMENTED_ACCUMULATE=1")
fi

if [[ "${JACOBI_SPMV_OOO_ACCUMULATE_RTL:-0}" != "0" && "${JACOBI_SPMV_OOO_ACCUMULATE_RTL:-}" != "" ]]; then
  CUSTOM_RTL_DIR="${CUSTOM_RTL_DIR:-$ROOT_DIR/../../verilog/tapa}"
  if [[ ! -d "$CUSTOM_RTL_DIR" ]]; then
    echo "JACOBI_SPMV_OOO_ACCUMULATE_RTL=1 but custom RTL dir does not exist: $CUSTOM_RTL_DIR" >&2
    exit 1
  fi
  cflags+=("-DJACOBI_SPMV_OOO_ACCUMULATE_RTL=1")
  echo "Cuper SpMV OOO accumulator RTL boundary: $CUSTOM_RTL_DIR"
fi

if [[ "${JACOBI_SPMV_OOO_ACCUMULATE_RTL:-0}" != "0" && "${JACOBI_SPMV_OOO_ACCUMULATE_RTL:-}" != "" ]]; then
  analyze_cmd=(
    tapa -w "$WORK_DIR" analyze
    -f "$ROOT_DIR/kernels/Cuper.cpp"
    -t "$TOP"
  )
  for cflag in "${cflags[@]}"; do
    analyze_cmd+=(-c "$cflag")
  done

  synth_cmd=(
    tapa -w "$WORK_DIR" synth
    -p "$DEVICE"
    --clock-period "$CLOCK_PERIOD"
    -j "$JOBS"
    --enable-synth-util
  )

  custom_rtl="$CUSTOM_RTL_DIR/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v"
  support_rtl="$CUSTOM_RTL_DIR/CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo.v"
  fadd_rtl="$CUSTOM_RTL_DIR/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1.v"
  fadd_ip_tcl="$CUSTOM_RTL_DIR/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1_ip.tcl"
  generated_rtl="$WORK_DIR/hdl/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v"
  generated_support_rtl="$WORK_DIR/hdl/CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo_support.vh"
  generated_fadd_rtl="$WORK_DIR/hdl/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1.v"
  generated_fadd_ip_tcl="$WORK_DIR/hdl/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1_ip.tcl"
  pack_cmd=(
    tapa -w "$WORK_DIR" pack
    -o "$OUTPUT_XO"
  )

  printf 'Running analyze:'
  printf ' %q' "${analyze_cmd[@]}"
  printf '\n'
  "${analyze_cmd[@]}"

  printf 'Running synth:'
  printf ' %q' "${synth_cmd[@]}"
  printf '\n'
  "${synth_cmd[@]}"

  if [[ ! -f "$custom_rtl" ]]; then
    echo "Missing custom RTL file: $custom_rtl" >&2
    exit 1
  fi
  if [[ ! -f "$support_rtl" ]]; then
    echo "Missing custom RTL support file: $support_rtl" >&2
    exit 1
  fi
  if [[ ! -f "$fadd_rtl" ]]; then
    echo "Missing custom RTL fadd wrapper: $fadd_rtl" >&2
    exit 1
  fi
  if [[ ! -f "$fadd_ip_tcl" ]]; then
    echo "Missing custom RTL fadd IP tcl: $fadd_ip_tcl" >&2
    exit 1
  fi
  if [[ ! -f "$generated_rtl" ]]; then
    echo "TAPA synth did not generate expected RTL wrapper: $generated_rtl" >&2
    exit 1
  fi
  cp "$custom_rtl" "$generated_rtl"
  cp "$support_rtl" "$generated_support_rtl"
  cp "$fadd_rtl" "$generated_fadd_rtl"
  cp "$fadd_ip_tcl" "$generated_fadd_ip_tcl"
  echo "Replaced generated RTL wrapper with custom RTL: $generated_rtl"
  echo "Copied owner-lane RTL support module: $generated_support_rtl"
  echo "Copied owner-bank fadd wrapper: $generated_fadd_rtl"
  echo "Copied owner-bank fadd IP tcl: $generated_fadd_ip_tcl"

  printf 'Running pack:'
  printf ' %q' "${pack_cmd[@]}"
  printf '\n'
  "${pack_cmd[@]}"
else
  cmd=(
    tapa -w "$WORK_DIR" compile
    -f "$ROOT_DIR/kernels/Cuper.cpp"
    -t "$TOP"
    -p "$DEVICE"
    --clock-period "$CLOCK_PERIOD"
    -j "$JOBS"
    --enable-synth-util
    -o "$OUTPUT_XO"
  )
  for cflag in "${cflags[@]}"; do
    cmd+=(-c "$cflag")
  done

  printf 'Running:'
  printf ' %q' "${cmd[@]}"
  printf '\n'
  "${cmd[@]}"
fi
