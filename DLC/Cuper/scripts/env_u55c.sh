#!/usr/bin/env bash

# Source this file before building or running Cuper on the local U55C machine.

export TAPA_ROOT="${TAPA_ROOT:-$HOME/.tapa/usr}"
export XILINX_VITIS="${XILINX_VITIS:-/tools/Xilinx2022/Vitis/2022.2}"
export XILINX_HLS="${XILINX_HLS:-/tools/Xilinx2022/Vitis_HLS/2022.2}"
export XILINX_XRT="${XILINX_XRT:-/opt/xilinx/xrt}"
export PLATFORM_REPO_PATHS="${PLATFORM_REPO_PATHS:-/opt/xilinx/platforms}"
export DEVICE="${DEVICE:-xilinx_u55c_gen3x16_xdma_3_202210_1}"
export XPLATFORM="${XPLATFORM:-$PLATFORM_REPO_PATHS/$DEVICE/$DEVICE.xpfm}"

export PATH="$TAPA_ROOT/bin:$XILINX_VITIS/bin:$XILINX_HLS/bin:$XILINX_XRT/bin:$PATH"
export LD_LIBRARY_PATH="$TAPA_ROOT/lib:$XILINX_XRT/lib:${LD_LIBRARY_PATH:-}"
export CMAKE_PREFIX_PATH="$TAPA_ROOT:${CMAKE_PREFIX_PATH:-}"
