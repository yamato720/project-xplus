SHELL := /bin/bash

CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2
PYTHON ?= python3

TARGET ?= sw_emu
DEVICE ?= xilinx_u55c_gen3x16_xdma_3_202210_1
XPLATFORM ?= /opt/xilinx/platforms/$(DEVICE)/$(DEVICE).xpfm
XILINX_XRT ?= /opt/xilinx/xrt

ROOT_DIR := $(abspath .)
BUILD_DIR ?= $(abspath $(ROOT_DIR)/../..)/cuper-tapa-spmv-build
REPORT_DIR := $(ROOT_DIR)/reports
LOG_DIR := $(ROOT_DIR)/logs
HOST_DIR := $(ROOT_DIR)/host
INCLUDE_DIR := $(ROOT_DIR)/include
SRC_DIR := $(ROOT_DIR)/src
KERNEL_DIR := $(ROOT_DIR)/kernels
CFG_DIR := $(ROOT_DIR)/cfg
SCRIPT_DIR := $(ROOT_DIR)/scripts
DATA_DIR := $(ROOT_DIR)/data

.PHONY: all help tree clean launch launcher build-host run-sw build-xo link-xclbin hw-tmux run-hw

all: help

help:
	@echo "Cuper standalone HLS subproject skeleton"
	@echo ""
	@echo "This subproject is intentionally not wired into Project-XPlus root Makefile yet."
	@echo "Current phase: fill host/kernel/cfg sources first, then decide build integration."
	@echo ""
	@echo "Useful paths:"
	@echo "  ROOT_DIR    = $(ROOT_DIR)"
	@echo "  HOST_DIR    = $(HOST_DIR)"
	@echo "  KERNEL_DIR  = $(KERNEL_DIR)"
	@echo "  INCLUDE_DIR = $(INCLUDE_DIR)"
	@echo "  CFG_DIR     = $(CFG_DIR)"
	@echo "  SRC_DIR     = $(SRC_DIR)"
	@echo "  DATA_DIR    = $(DATA_DIR)"
	@echo "  BUILD_DIR   = $(BUILD_DIR)"
	@echo "  REPORT_DIR  = $(REPORT_DIR)"
	@echo "  LOG_DIR     = $(LOG_DIR)"
	@echo ""
	@echo "Targets:"
	@echo "  make launch       Open the Cuper launcher"
	@echo "  make launcher     Alias of make launch"
	@echo "  make build-host   Build $(BUILD_DIR)/cuper_host"
	@echo "  make run-sw       Run software simulation, MATRIX=<path>"
	@echo "  make build-xo     Build Cuper_2022.xo"
	@echo "  make link-xclbin  Link Cuper_2022.xclbin"
	@echo "  make hw-tmux      Build host/XO/xclbin in tmux"
	@echo "  make run-hw       Run hardware xclbin, MATRIX=<path>"

tree:
	@find "$(ROOT_DIR)" -maxdepth 2 | sort

clean:
	rm -rf "$(BUILD_DIR)"

launch launcher:
	BUILD_DIR="$(BUILD_DIR)" "$(PYTHON)" "$(SCRIPT_DIR)/launcher.py"

build-host:
	BUILD_DIR="$(BUILD_DIR)" "$(SCRIPT_DIR)/build_host.sh"

run-sw:
	BUILD_DIR="$(BUILD_DIR)" "$(PYTHON)" "$(SCRIPT_DIR)/launcher.py" run-sw "$(or $(MATRIX),$(DATA_DIR)/matrices/sit100/sit100.mtx)"

build-xo:
	BUILD_DIR="$(BUILD_DIR)" "$(SCRIPT_DIR)/build_xo_u55c.sh"

link-xclbin:
	BUILD_DIR="$(BUILD_DIR)" "$(SCRIPT_DIR)/link_xclbin_u55c.sh"

hw-tmux:
	BUILD_DIR="$(BUILD_DIR)" "$(PYTHON)" "$(SCRIPT_DIR)/launcher.py" hw-tmux $(if $(FORCE),--force)

run-hw:
	BUILD_DIR="$(BUILD_DIR)" "$(PYTHON)" "$(SCRIPT_DIR)/launcher.py" run-hw "$(or $(MATRIX),$(DATA_DIR)/matrices/sit100/sit100.mtx)"
