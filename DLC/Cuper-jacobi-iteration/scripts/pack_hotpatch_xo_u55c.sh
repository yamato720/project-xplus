#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$(cd "$ROOT_DIR/../.." && pwd)/cuper-jacobi-iteration-build}"

# shellcheck source=env_u55c.sh
source "$ROOT_DIR/scripts/env_u55c.sh"

TOP="${JACOBI_TOP:-CuperJacobiIteration}"
OUTPUT_XO="${1:-$BUILD_DIR/$TOP.xo}"
WORK_DIR="${WORK_DIR:-$BUILD_DIR/tapa_$TOP}"
HOTPATCH_RTL_DIR="${JACOBI_TAPA_HOTPATCH_RTL_DIR:-${HOTPATCH_RTL_DIR:-}}"

copy_with_backup() {
  local src="$1"
  local dst="$2"
  local backup_dir="$3"

  if [[ ! -f "$src" ]]; then
    echo "Missing hotpatch RTL source: $src" >&2
    exit 1
  fi
  if [[ -f "$dst" ]]; then
    mkdir -p "$backup_dir"
    cp "$dst" "$backup_dir/$(basename "$dst")"
  fi
  cp "$src" "$dst"
  echo "Hotpatched RTL: $dst <- $src"
}

if [[ ! -d "$WORK_DIR" ]]; then
  echo "Missing TAPA work dir: $WORK_DIR" >&2
  echo "Run a full build first, or set WORK_DIR to an existing tapa_<top> directory." >&2
  exit 1
fi
if [[ ! -d "$WORK_DIR/hdl" ]]; then
  echo "Missing TAPA generated hdl dir: $WORK_DIR/hdl" >&2
  exit 1
fi

mkdir -p "$(dirname "$OUTPUT_XO")"
backup_dir="$WORK_DIR/hotpatch_backups/$(date +%Y%m%d_%H%M%S)"

if [[ "$HOTPATCH_RTL_DIR" != "" ]]; then
  if [[ ! -d "$HOTPATCH_RTL_DIR" ]]; then
    echo "JACOBI_TAPA_HOTPATCH_RTL_DIR does not exist: $HOTPATCH_RTL_DIR" >&2
    exit 1
  fi
  while IFS= read -r -d '' rtl_file; do
    rel_path="${rtl_file#"$HOTPATCH_RTL_DIR"/}"
    dst="$WORK_DIR/hdl/$rel_path"
    mkdir -p "$(dirname "$dst")"
    copy_with_backup "$rtl_file" "$dst" "$backup_dir"
  done < <(find "$HOTPATCH_RTL_DIR" -type f \( -name '*.v' -o -name '*.sv' -o -name '*.vh' \) -print0)
fi

if [[ "${JACOBI_SPMV_OOO_ACCUMULATE_RTL:-0}" != "0" && "${JACOBI_SPMV_OOO_ACCUMULATE_RTL:-}" != "" ]]; then
  CUSTOM_RTL_DIR="${CUSTOM_RTL_DIR:-$ROOT_DIR/../../verilog/tapa}"
  custom_bank_rtl="$CUSTOM_RTL_DIR/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v"
  custom_lane_rtl="$CUSTOM_RTL_DIR/CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo.v"
  custom_fadd_rtl="$CUSTOM_RTL_DIR/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1.v"
  custom_fadd_ip_tcl="$CUSTOM_RTL_DIR/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1_ip.tcl"
  generated_bank_rtl="$WORK_DIR/hdl/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v"
  generated_lane_support="$WORK_DIR/hdl/CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo_support.vh"
  generated_fadd_rtl="$WORK_DIR/hdl/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1.v"
  generated_fadd_ip_tcl="$WORK_DIR/hdl/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1_ip.tcl"

  if [[ ! -d "$CUSTOM_RTL_DIR" ]]; then
    echo "CUSTOM_RTL_DIR does not exist: $CUSTOM_RTL_DIR" >&2
    exit 1
  fi
  copy_with_backup "$custom_bank_rtl" "$generated_bank_rtl" "$backup_dir"
  copy_with_backup "$custom_lane_rtl" "$generated_lane_support" "$backup_dir"
  copy_with_backup "$custom_fadd_rtl" "$generated_fadd_rtl" "$backup_dir"
  copy_with_backup "$custom_fadd_ip_tcl" "$generated_fadd_ip_tcl" "$backup_dir"
fi

if [[ "${JACOBI_SPMV_OOO_SCOREBOARD_RTL:-0}" != "0" && "${JACOBI_SPMV_OOO_SCOREBOARD_RTL:-}" != "" ]]; then
  CUSTOM_RTL_DIR="${CUSTOM_RTL_DIR:-$ROOT_DIR/../../verilog/tapa}"
  custom_scoreboard_rtl="$CUSTOM_RTL_DIR/CuperSpmvOnly_RtlOwnerScoreboardOoo.v"
  custom_issue_rtl="$CUSTOM_RTL_DIR/CuperSpmvOnly_RtlIssueScoreboard8.v"
  generated_scoreboard_rtl="$WORK_DIR/hdl/CuperSpmvOnly_RtlOwnerScoreboardOoo.v"
  generated_issue_rtl="$WORK_DIR/hdl/CuperSpmvOnly_RtlIssueScoreboard8.v"

  if [[ ! -d "$CUSTOM_RTL_DIR" ]]; then
    echo "CUSTOM_RTL_DIR does not exist: $CUSTOM_RTL_DIR" >&2
    exit 1
  fi
  copy_with_backup "$custom_scoreboard_rtl" "$generated_scoreboard_rtl" "$backup_dir"
  copy_with_backup "$custom_issue_rtl" "$generated_issue_rtl" "$backup_dir"
  if [[ "${JACOBI_SPMV_SCOREBOARD_DEPTH:-}" != "" ]]; then
    if [[ ! "$JACOBI_SPMV_SCOREBOARD_DEPTH" =~ ^[1-9][0-9]*$ ]]; then
      echo "JACOBI_SPMV_SCOREBOARD_DEPTH must be a positive integer; got '$JACOBI_SPMV_SCOREBOARD_DEPTH'." >&2
      exit 1
    fi
    sed -i "s/parameter integer SCOREBOARD_DEPTH = 12;/parameter integer SCOREBOARD_DEPTH = $JACOBI_SPMV_SCOREBOARD_DEPTH;/" \
      "$generated_scoreboard_rtl"
  fi
fi

pack_cmd=(
  tapa -w "$WORK_DIR" pack
  -o "$OUTPUT_XO"
)

echo "Cuper Jacobi hotpatch pack:"
echo "  TOP       = $TOP"
echo "  WORK_DIR  = $WORK_DIR"
echo "  OUTPUT_XO = $OUTPUT_XO"
if [[ -d "$backup_dir" ]]; then
  echo "  Backup    = $backup_dir"
fi

printf 'Running pack:'
printf ' %q' "${pack_cmd[@]}"
printf '\n'
"${pack_cmd[@]}"
