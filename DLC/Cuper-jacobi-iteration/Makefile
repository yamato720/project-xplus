SHELL := /bin/bash

CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2
PYTHON ?= python3

TARGET ?= sw_emu
DEVICE ?= xilinx_u55c_gen3x16_xdma_3_202210_1
XPLATFORM ?= /opt/xilinx/platforms/$(DEVICE)/$(DEVICE).xpfm
XILINX_XRT ?= /opt/xilinx/xrt

ROOT_DIR := $(abspath .)
BUILD_DIR ?= $(abspath $(ROOT_DIR)/../..)/cuper-jacobi-iteration-build
REPORT_DIR := $(ROOT_DIR)/reports
LOG_DIR := $(ROOT_DIR)/logs
HOST_DIR := $(ROOT_DIR)/host
INCLUDE_DIR := $(ROOT_DIR)/include
KERNEL_DIR := $(ROOT_DIR)/kernels
CFG_DIR := $(ROOT_DIR)/cfg
SCRIPT_DIR := $(ROOT_DIR)/scripts
DATA_DIR := $(ROOT_DIR)/data
JACOBI_DEBUG_ENV := $(if $(JACOBI_DEADLOCK_DEBUG),JACOBI_DEADLOCK_DEBUG="$(JACOBI_DEADLOCK_DEBUG)") $(if $(JACOBI_TRACE_ISOTOPE),JACOBI_TRACE_ISOTOPE="$(JACOBI_TRACE_ISOTOPE)") $(if $(JACOBI_TRACE_LIGHT),JACOBI_TRACE_LIGHT="$(JACOBI_TRACE_LIGHT)") $(if $(JACOBI_BLOCKING_ENTRY_PROBE),JACOBI_BLOCKING_ENTRY_PROBE="$(JACOBI_BLOCKING_ENTRY_PROBE)")

.PHONY: all help tree clean launch launcher build-host build-mmap-probe-xrt-host run-sw regression-sw build-xo link-xclbin build-mmap-probe-xo link-mmap-probe-xclbin link-mmap-probe-xclbin-split hw-tmux run-hw run-mmap-probe-xrt

all: help

help:
	@echo "Cuper Jacobi iteration TAPA service-SpMV subproject"
	@echo ""
	@echo "本子项目也可从 Project-XPlus 根 Makefile 的 cuper-jacobi-* target 调用。"
	@echo "当前阶段：保留可重复触发的 Cuper single-SpMV service，并在 Cuper 输出端融合 Jacobi 更新。"
	@echo ""
	@echo "Useful paths:"
	@echo "  ROOT_DIR    = $(ROOT_DIR)"
	@echo "  HOST_DIR    = $(HOST_DIR)"
	@echo "  KERNEL_DIR  = $(KERNEL_DIR)"
	@echo "  INCLUDE_DIR = $(INCLUDE_DIR)"
	@echo "  CFG_DIR     = $(CFG_DIR)"
	@echo "  DATA_DIR    = $(DATA_DIR)"
	@echo "  BUILD_DIR   = $(BUILD_DIR)"
	@echo "  REPORT_DIR  = $(REPORT_DIR)"
	@echo "  LOG_DIR     = $(LOG_DIR)"
	@echo ""
	@echo "Targets:"
	@echo "  make launch       Open the Cuper launcher"
	@echo "  make launcher     Alias of make launch"
	@echo "  make build-host   Build $(BUILD_DIR)/cuper_jacobi_host"
	@echo "  make build-mmap-probe-xrt-host Build native XRT mmap probe runner"
	@echo "  make run-sw       Run software simulation, MATRIX=<path>"
	@echo "  make regression-sw Run compact software regression, MODE=quick|full"
	@echo "  make build-xo     Build CuperJacobiIteration.xo"
	@echo "  make link-xclbin  Link CuperJacobiIteration.xclbin"
	@echo "  make build-mmap-probe-xo Build CuperJacobiMmapProbeOnly.xo"
	@echo "  make link-mmap-probe-xclbin Link mmap probe xclbin, same HBM[24]"
	@echo "  make link-mmap-probe-xclbin-split Link mmap probe xclbin, HBM[24/25/26]"
	@echo "  make hw-tmux      Build host/XO/xclbin in tmux"
	@echo "  make run-hw       Run hardware xclbin, MATRIX=<path>"
	@echo "  make run-mmap-probe-xrt Run native XRT mmap probe, BITFILE=<path>"

tree:
	@find "$(ROOT_DIR)" -maxdepth 2 | sort

clean:
	rm -rf "$(BUILD_DIR)"

launch launcher:
	BUILD_DIR="$(BUILD_DIR)" $(JACOBI_DEBUG_ENV) "$(PYTHON)" "$(SCRIPT_DIR)/launcher.py"

build-host:
	BUILD_DIR="$(BUILD_DIR)" $(JACOBI_DEBUG_ENV) "$(SCRIPT_DIR)/build_host.sh"

build-mmap-probe-xrt-host: build-host

run-sw:
	BUILD_DIR="$(BUILD_DIR)" $(JACOBI_DEBUG_ENV) "$(PYTHON)" "$(SCRIPT_DIR)/launcher.py" run-sw "$(or $(MATRIX),$(DATA_DIR)/matrices/cant.mtx)"

regression-sw:
	@BUILD_DIR="$(BUILD_DIR)" $(JACOBI_DEBUG_ENV) CASES="$(strip $(CASE) $(CASES))" "$(PYTHON)" "$(SCRIPT_DIR)/regression_sw.py" --mode "$(or $(MODE),quick)" $(if $(NO_BUILD),--no-build) $(if $(ALLOW_MISSING),--allow-missing) $(if $(TIMEOUT_SEC),--timeout-sec "$(TIMEOUT_SEC)") $(if $(TAU),--tau "$(TAU)")

build-xo:
	BUILD_DIR="$(BUILD_DIR)" $(JACOBI_DEBUG_ENV) "$(SCRIPT_DIR)/build_xo_u55c.sh"

link-xclbin:
	BUILD_DIR="$(BUILD_DIR)" $(JACOBI_DEBUG_ENV) "$(SCRIPT_DIR)/link_xclbin_u55c.sh"

build-mmap-probe-xo:
	BUILD_DIR="$(BUILD_DIR)" JACOBI_TOP=CuperJacobiMmapProbeOnly "$(SCRIPT_DIR)/build_xo_u55c.sh"

link-mmap-probe-xclbin:
	BUILD_DIR="$(BUILD_DIR)" JACOBI_TOP=CuperJacobiMmapProbeOnly "$(SCRIPT_DIR)/link_xclbin_u55c.sh"

link-mmap-probe-xclbin-split:
	BUILD_DIR="$(BUILD_DIR)" JACOBI_TOP=CuperJacobiMmapProbeOnly JACOBI_MMAP_PROBE_SPLIT=1 "$(SCRIPT_DIR)/link_xclbin_u55c.sh"

hw-tmux:
	BUILD_DIR="$(BUILD_DIR)" $(JACOBI_DEBUG_ENV) "$(PYTHON)" "$(SCRIPT_DIR)/launcher.py" hw-tmux $(if $(FORCE),--force)

run-hw:
	BUILD_DIR="$(BUILD_DIR)" $(JACOBI_DEBUG_ENV) "$(PYTHON)" "$(SCRIPT_DIR)/launcher.py" run-hw "$(or $(MATRIX),$(DATA_DIR)/matrices/cant.mtx)"

run-mmap-probe-xrt:
	BUILD_DIR="$(BUILD_DIR)" $(if $(BITFILE),BITFILE="$(BITFILE)") "$(SCRIPT_DIR)/run_mmap_probe_xrt.sh" $(if $(ROW_NUM),--row-num "$(ROW_NUM)") $(if $(MAX_ITERS),--max-iters "$(MAX_ITERS)") $(if $(COLUMN_NUM),--column-num "$(COLUMN_NUM)") $(if $(WAIT_TIMEOUT_MS),--wait-timeout-ms "$(WAIT_TIMEOUT_MS)") $(if $(SAMPLE_DELAY_MS),--sample-delay-ms "$(SAMPLE_DELAY_MS)")
