SHELL := /bin/bash

CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2
PYTHON ?= python3

TARGET ?= sw_emu
DEVICE ?= xilinx_u55c_gen3x16_xdma_3_202210_1
XPLATFORM ?= /opt/xilinx/platforms/$(DEVICE)/$(DEVICE).xpfm
XILINX_XRT ?= /opt/xilinx/xrt

ROOT_DIR := $(abspath .)
BUILD_DIR := $(ROOT_DIR)/build
REPORT_DIR := $(ROOT_DIR)/reports
LOG_DIR := $(ROOT_DIR)/logs
HOST_DIR := $(ROOT_DIR)/host
INCLUDE_DIR := $(ROOT_DIR)/include
SRC_DIR := $(ROOT_DIR)/src
KERNEL_DIR := $(ROOT_DIR)/kernels
CFG_DIR := $(ROOT_DIR)/cfg
SCRIPT_DIR := $(ROOT_DIR)/scripts
DATA_DIR := $(ROOT_DIR)/data

.PHONY: all help tree clean

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

tree:
	@find "$(ROOT_DIR)" -maxdepth 2 | sort

clean:
	rm -rf "$(BUILD_DIR)"
