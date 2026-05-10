#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

SESSION_NAME="${SESSION_NAME:-project-xplus-hw-build}"
STAMP="$(date +%Y%m%d_%H%M%S)"
mkdir -p logs
LOG_PATH="logs/build_hw_${STAMP}.log"
EXTRA_ARGS=""

if [ "$#" -gt 0 ]; then
  EXTRA_ARGS="$(printf ' %q' "$@")"
fi

if tmux has-session -t "${SESSION_NAME}" 2>/dev/null; then
  echo "tmux session already exists: ${SESSION_NAME}" >&2
  exit 1
fi

tmux new-session -d -s "${SESSION_NAME}" \
  "cd '$PWD' && source \"${VITIS_SETTINGS:-/tools/Xilinx2022/Vitis/2022.2/settings64.sh}\" >/dev/null 2>&1 && make build-hw${EXTRA_ARGS} 2>&1 | tee '${LOG_PATH}'"

echo "session: ${SESSION_NAME}"
echo "log: ${PWD}/${LOG_PATH}"
echo "attach: tmux attach -t ${SESSION_NAME}"
echo "tail:   tail -f ${PWD}/${LOG_PATH}"
