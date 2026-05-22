SHELL := /bin/bash
empty :=
space := $(empty) $(empty)

CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2
PYTHON ?= python3

N ?= 4096
TARGET ?= sw_emu
DEVICE ?= xilinx_u55c_gen3x16_xdma_3_202210_1
XPLATFORM ?= /opt/xilinx/platforms/$(DEVICE)/$(DEVICE).xpfm
XILINX_XRT ?= /opt/xilinx/xrt
VITIS_ROOT ?= $(strip $(shell \
	if [ -n "$$XILINX_VITIS" ] && [ -x "$$XILINX_VITIS/bin/v++" ]; then \
		printf '%s\n' "$$XILINX_VITIS"; \
	elif [ -x /tools/Xilinx2022/Vitis/2022.2/bin/v++ ]; then \
		printf '%s\n' /tools/Xilinx2022/Vitis/2022.2; \
	elif [ -x /tools/Xilinx/Vitis/2022.2/bin/v++ ]; then \
		printf '%s\n' /tools/Xilinx/Vitis/2022.2; \
	elif [ -x /tools/Xilinx/Vitis/2021.2/bin/v++ ]; then \
		printf '%s\n' /tools/Xilinx/Vitis/2021.2; \
	elif [ -d /tools/Xilinx/Vitis ]; then \
		find /tools/Xilinx/Vitis -maxdepth 1 -mindepth 1 -type d | sort -V | tail -n 1; \
	elif command -v v++ >/dev/null 2>&1; then \
		dirname "$$(dirname "$$(command -v v++)")"; \
	fi))
VITIS_VERSION := $(notdir $(VITIS_ROOT))
XILINX_ROOT := $(patsubst %/Vitis/$(VITIS_VERSION),%,$(VITIS_ROOT))
VIVADO_ROOT ?= $(if $(wildcard $(XILINX_ROOT)/Vivado/$(VITIS_VERSION)/bin/vivado),$(XILINX_ROOT)/Vivado/$(VITIS_VERSION))
VPP ?= $(if $(VITIS_ROOT),$(VITIS_ROOT)/bin/v++,v++)
EMCONFIGUTIL ?= $(if $(VITIS_ROOT),$(VITIS_ROOT)/bin/emconfigutil,emconfigutil)
VITIS_SETTINGS ?= $(if $(VITIS_ROOT),$(VITIS_ROOT)/settings64.sh)

ROOT_DIR := $(abspath .)
BUILD_DIR := $(ROOT_DIR)/build
TARGET_BUILD_DIR := $(BUILD_DIR)/$(TARGET)
HOST_DIR := $(ROOT_DIR)/host
INCLUDE_DIR := $(ROOT_DIR)/include
SRC_DIR := $(ROOT_DIR)/src
KERNEL_DIR := $(ROOT_DIR)/kernels
CFG_DIR := $(ROOT_DIR)/cfg
SCRIPT_DIR := $(ROOT_DIR)/scripts
REPORT_DIR := $(ROOT_DIR)/reports
LOG_DIR := $(ROOT_DIR)/logs
VIVADO_LINK_DIR := $(TARGET_BUILD_DIR)/_x_temp/link/vivado/vpl
VIVADO_REPORT_DIR := $(TARGET_BUILD_DIR)/_x_temp/reports/link
VIVADO_PACKAGE_DIR := $(BUILD_DIR)/packages
VIVADO_PACKAGE_NAME ?= project-xplus-vivado-view
VIVADO_PACKAGE_FULL := $(VIVADO_PACKAGE_DIR)/$(VIVADO_PACKAGE_NAME)-full.tar.gz
REPORT_BASENAME ?= n$(N)
REPORT_BASENAME_HW ?= n$(N)
REPORT_BASENAME_SW_FULL := SW_$(REPORT_BASENAME)
REPORT_BASENAME_HW_FULL := HW_$(REPORT_BASENAME_HW)
REPORT_JSON := $(REPORT_DIR)/$(REPORT_BASENAME_SW_FULL).json
REPORT_TXT := $(REPORT_DIR)/$(REPORT_BASENAME_SW_FULL).txt
REPORT_HTML := $(REPORT_DIR)/$(REPORT_BASENAME_SW_FULL).html
REPORT_HTML_STATIC := $(REPORT_DIR)/$(REPORT_BASENAME_SW_FULL)_static.html
REPORT_LOG := $(REPORT_DIR)/$(REPORT_BASENAME_SW_FULL).log
REPORT_JSON_HW := $(REPORT_DIR)/$(REPORT_BASENAME_HW_FULL).json
REPORT_TXT_HW := $(REPORT_DIR)/$(REPORT_BASENAME_HW_FULL).txt
REPORT_HTML_HW := $(REPORT_DIR)/$(REPORT_BASENAME_HW_FULL).html
REPORT_HTML_STATIC_HW := $(REPORT_DIR)/$(REPORT_BASENAME_HW_FULL)_static.html
REPORT_LOG_HW := $(REPORT_DIR)/$(REPORT_BASENAME_HW_FULL).log
SW_XCLBIN := $(BUILD_DIR)/sw_emu/cgsolver_jacobi_pcg.xclbin
HW_XCLBIN := $(BUILD_DIR)/hw/cgsolver_jacobi_pcg.xclbin

LOCAL_HOST := $(BUILD_DIR)/xplus_host
XRT_HOST := $(BUILD_DIR)/xplus_xrt_host

DATASET_DIR ?= $(ROOT_DIR)/data/generated/cgsolver/n$(N)
SIZE ?= $(N)
ASPECT_RATIO ?= 1.6
TAU ?= 1e-10
MAX_ITERS ?= 0
DEVICE_INDEX ?= 0
HOST_ARGS ?=

KERNEL_NAMES := spmv_csr_kernel init_pcg_kernel dot_kernel update_xrz_kernel update_p_kernel
# 手动切换 SpMV 硬件实现时，改这里的源文件即可。
# 默认使用 CSR 版本：
#   SPMV_KERNEL_SOURCE ?= $(KERNEL_DIR)/spmv_csr_kernel.cpp
# 如需切到 blocked 占位实现，可以手动改成：
#   SPMV_KERNEL_SOURCE ?= $(KERNEL_DIR)/spmv_blocked_kernel.cpp
# 也可以不改文件，直接在命令行覆盖：
#   make build-sw SPMV_KERNEL_SOURCE=$(KERNEL_DIR)/spmv_blocked_kernel.cpp
#   make build-hw SPMV_KERNEL_SOURCE=$(KERNEL_DIR)/spmv_blocked_kernel.cpp
SPMV_KERNEL_SOURCE ?= $(KERNEL_DIR)/spmv_blocked_kernel.cpp
XO_SP_MV := $(BUILD_DIR)/spmv_csr_kernel.xo
XO_INIT := $(BUILD_DIR)/init_pcg_kernel.xo
XO_DOT := $(BUILD_DIR)/dot_kernel.xo
XO_UPDATE_XRZ := $(BUILD_DIR)/update_xrz_kernel.xo
XO_UPDATE_P := $(BUILD_DIR)/update_p_kernel.xo
XO_SP_MV := $(TARGET_BUILD_DIR)/spmv_csr_kernel.xo
XO_INIT := $(TARGET_BUILD_DIR)/init_pcg_kernel.xo
XO_DOT := $(TARGET_BUILD_DIR)/dot_kernel.xo
XO_UPDATE_XRZ := $(TARGET_BUILD_DIR)/update_xrz_kernel.xo
XO_UPDATE_P := $(TARGET_BUILD_DIR)/update_p_kernel.xo
XOS := $(XO_SP_MV) $(XO_INIT) $(XO_DOT) $(XO_UPDATE_XRZ) $(XO_UPDATE_P)
XCLBIN := $(TARGET_BUILD_DIR)/cgsolver_jacobi_pcg.xclbin
EMCONFIG := $(TARGET_BUILD_DIR)/emconfig.json

XRT_CXXFLAGS := $(CXXFLAGS) -Wall -Wextra -I$(XILINX_XRT)/include
XRT_LDFLAGS := -L$(XILINX_XRT)/lib -lxrt_coreutil -luuid -pthread -lrt
XRT_LDFLAGS += -Wl,-rpath,$(XILINX_XRT)/lib

VPP_FLAGS += -t $(TARGET) --platform $(XPLATFORM) --save-temps
VPP_FLAGS += --temp_dir $(TARGET_BUILD_DIR)/_x_temp
VPP_FLAGS += -I$(INCLUDE_DIR) -I$(KERNEL_DIR)
VPP_LDFLAGS += --config $(CFG_DIR)/connectivity_u55c.cfg

HOST_RUN_ARGS := $(HOST_ARGS)
VITIS_ENV_CMD := source "$(VITIS_SETTINGS)" >/dev/null 2>&1 &&
SUMMARY_GREP := ^(\\[xplus-xrt\\]|\\[init\\]|\\[done\\]|\\[check\\]|\\[host-timing-ms\\]|\\[kernel-timing-ms\\])

ifneq ($(strip $(VITIS_ROOT)),)
export XILINX_VITIS := $(VITIS_ROOT)
endif
export XILINX_XRT := $(XILINX_XRT)

.PHONY: all help env generate local-host xrt-host run run-local run-xrt run-sw-report run-sw-report-existing run-hw-report run-hw-report-existing _run-hw-report render-report render-hw-report vivado-package-full build build-sw build-hw clean

all: run-local

help:
	@echo "Project-XPlus Jacobi-PCG multi-kernel project"
	@echo ""
	@echo "Local reference path:"
	@echo "  make run-local"
	@echo ""
	@echo "Build XRT host only:"
	@echo "  make xrt-host"
	@echo ""
	@echo "Generate dataset:"
	@echo "  make generate"
	@echo ""
	@echo "Build sw_emu xclbin:"
	@echo "  make build-sw"
	@echo "  make build-sw SPMV_KERNEL_SOURCE=$(KERNEL_DIR)/spmv_blocked_kernel.cpp"
	@echo ""
	@echo "Run sw_emu:"
	@echo "  make run-xrt TARGET=sw_emu"
	@echo "  make run-sw-report"
	@echo "  make run-sw-report-existing"
	@echo ""
	@echo "Build hardware xclbin:"
	@echo "  make build-hw"
	@echo "  make build-hw SPMV_KERNEL_SOURCE=$(KERNEL_DIR)/spmv_blocked_kernel.cpp"
	@echo "  make vivado-package-full"
	@echo ""
	@echo "Run hardware:"
	@echo "  make run-xrt TARGET=hw"
	@echo "  make run-hw-report"
	@echo "  make run-hw-report-existing"

env:
	@test -f "$(XPLATFORM)" || (echo "ERROR: platform not found: $(XPLATFORM)" && exit 1)
	@test -d "$(XILINX_XRT)" || (echo "ERROR: XILINX_XRT not found: $(XILINX_XRT)" && exit 1)
	@test -x "$(VPP)" || command -v "$(VPP)" >/dev/null || (echo "ERROR: v++ not found" && exit 1)
	@test -x "$(EMCONFIGUTIL)" || command -v "$(EMCONFIGUTIL)" >/dev/null || (echo "ERROR: emconfigutil not found" && exit 1)
	@test -f "$(VITIS_SETTINGS)" || (echo "ERROR: Vitis settings script not found: $(VITIS_SETTINGS)" && exit 1)

generate:
	$(PYTHON) "$(SCRIPT_DIR)/generate_cg_dataset.py" --size $(SIZE) --aspect-ratio $(ASPECT_RATIO) --output-dir "$(DATASET_DIR)"

local-host: $(LOCAL_HOST)

xrt-host: $(XRT_HOST)

$(LOCAL_HOST): $(HOST_DIR)/main.cpp $(HOST_DIR)/cpu_reference.hpp $(HOST_DIR)/dataset_bridge.hpp $(HOST_DIR)/multi_kernel_solver.hpp $(INCLUDE_DIR)/cg_common.hpp $(INCLUDE_DIR)/cg_kernels.hpp $(KERNEL_DIR)/cg_kernels.cpp $(SRC_DIR)/CgSolverGolden.hpp $(SRC_DIR)/CsrDataset.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -I$(HOST_DIR) -I$(SRC_DIR) $(HOST_DIR)/main.cpp $(KERNEL_DIR)/cg_kernels.cpp -o $(LOCAL_HOST)

$(XRT_HOST): $(HOST_DIR)/xrt_host.cpp $(HOST_DIR)/dataset_bridge.hpp $(HOST_DIR)/cpu_reference.hpp $(INCLUDE_DIR)/cg_common.hpp $(SRC_DIR)/CgSolverGolden.hpp $(SRC_DIR)/CsrDataset.hpp | $(BUILD_DIR)
	$(CXX) $(XRT_CXXFLAGS) -I$(INCLUDE_DIR) -I$(HOST_DIR) -I$(SRC_DIR) $(HOST_DIR)/xrt_host.cpp -o $(XRT_HOST) $(XRT_LDFLAGS)

$(XO_SP_MV): $(SPMV_KERNEL_SOURCE) $(INCLUDE_DIR)/cg_common.hpp | $(TARGET_BUILD_DIR) env
	# 这里统一按 -k spmv_csr_kernel 编译，对外保持原 kernel 名字不变。
	# 因此切换到 blocked 版本时，只需要替换源文件，不需要改 xrt_host 或 connectivity。
	$(VITIS_ENV_CMD) cd $(TARGET_BUILD_DIR) && $(VPP) -c $(VPP_FLAGS) -k spmv_csr_kernel -o $@ $<

$(XO_INIT): $(KERNEL_DIR)/init_pcg_kernel.cpp $(INCLUDE_DIR)/cg_common.hpp | $(TARGET_BUILD_DIR) env
	$(VITIS_ENV_CMD) cd $(TARGET_BUILD_DIR) && $(VPP) -c $(VPP_FLAGS) -k init_pcg_kernel -o $@ $<

$(XO_DOT): $(KERNEL_DIR)/dot_kernel.cpp $(INCLUDE_DIR)/cg_common.hpp | $(TARGET_BUILD_DIR) env
	$(VITIS_ENV_CMD) cd $(TARGET_BUILD_DIR) && $(VPP) -c $(VPP_FLAGS) -k dot_kernel -o $@ $<

$(XO_UPDATE_XRZ): $(KERNEL_DIR)/update_xrz_kernel.cpp $(INCLUDE_DIR)/cg_common.hpp | $(TARGET_BUILD_DIR) env
	$(VITIS_ENV_CMD) cd $(TARGET_BUILD_DIR) && $(VPP) -c $(VPP_FLAGS) -k update_xrz_kernel -o $@ $<

$(XO_UPDATE_P): $(KERNEL_DIR)/update_p_kernel.cpp $(INCLUDE_DIR)/cg_common.hpp | $(TARGET_BUILD_DIR) env
	$(VITIS_ENV_CMD) cd $(TARGET_BUILD_DIR) && $(VPP) -c $(VPP_FLAGS) -k update_p_kernel -o $@ $<

$(XCLBIN): $(XOS) $(CFG_DIR)/connectivity_u55c.cfg | $(TARGET_BUILD_DIR) env
	$(VITIS_ENV_CMD) cd $(TARGET_BUILD_DIR) && $(VPP) -l $(VPP_FLAGS) $(VPP_LDFLAGS) -o $@ $(XOS)

$(EMCONFIG): | $(TARGET_BUILD_DIR) env
	$(VITIS_ENV_CMD) cd $(TARGET_BUILD_DIR) && $(EMCONFIGUTIL) --platform $(XPLATFORM) --od .

build: $(XCLBIN)

build-sw:
	$(MAKE) build TARGET=sw_emu

build-hw:
	$(MAKE) build TARGET=hw

run: run-local

run-local: $(LOCAL_HOST) generate
	$(LOCAL_HOST) $(DATASET_DIR) --tau $(TAU) --max-iters $(MAX_ITERS)

run-xrt: $(XRT_HOST) $(XCLBIN) generate
ifeq ($(TARGET),hw)
	$(VITIS_ENV_CMD) $(XRT_HOST) $(XCLBIN) $(DATASET_DIR) --tau $(TAU) --max-iters $(MAX_ITERS) --device-index $(DEVICE_INDEX) $(HOST_RUN_ARGS)
else
	$(MAKE) $(EMCONFIG) TARGET=$(TARGET)
	$(VITIS_ENV_CMD) cd $(TARGET_BUILD_DIR) && EMCONFIG_PATH=$$PWD XCL_EMULATION_MODE=$(TARGET) "$(XRT_HOST)" cgsolver_jacobi_pcg.xclbin "$(DATASET_DIR)" --tau $(TAU) --max-iters $(MAX_ITERS) --device-index $(DEVICE_INDEX) $(HOST_RUN_ARGS)
endif

run-sw-report:
	@mkdir -p "$(REPORT_DIR)"
	@: > "$(REPORT_LOG)"
	@$(MAKE) xrt-host >>"$(REPORT_LOG)" 2>&1
	@$(MAKE) build-sw >>"$(REPORT_LOG)" 2>&1
	@$(MAKE) generate DATASET_DIR="$(DATASET_DIR)" SIZE="$(SIZE)" ASPECT_RATIO="$(ASPECT_RATIO)" >>"$(REPORT_LOG)" 2>&1
	@$(MAKE) $(BUILD_DIR)/sw_emu/emconfig.json TARGET=sw_emu >>"$(REPORT_LOG)" 2>&1
	@{ $(VITIS_ENV_CMD) cd "$(BUILD_DIR)/sw_emu" && EMCONFIG_PATH=$$PWD XCL_EMULATION_MODE=sw_emu "$(XRT_HOST)" cgsolver_jacobi_pcg.xclbin "$(DATASET_DIR)" --tau $(TAU) --max-iters $(MAX_ITERS) --device-index $(DEVICE_INDEX) --timing --json-out "$(REPORT_JSON)" --txt-out "$(REPORT_TXT)" $(HOST_RUN_ARGS); } >>"$(REPORT_LOG)" 2>&1
	@$(PYTHON) "$(SCRIPT_DIR)/render_report.py" interactive "$(REPORT_JSON)" "$(REPORT_HTML)" >>"$(REPORT_LOG)" 2>&1
	@$(PYTHON) "$(SCRIPT_DIR)/render_report.py" static "$(REPORT_JSON)" "$(REPORT_HTML_STATIC)" >>"$(REPORT_LOG)" 2>&1
	@grep -E "$(SUMMARY_GREP)" "$(REPORT_LOG)" || true
	@echo "report json: $(REPORT_JSON)"
	@echo "report txt : $(REPORT_TXT)"
	@echo "report html: $(REPORT_HTML)"
	@echo "report html static: $(REPORT_HTML_STATIC)"
	@echo "report log : $(REPORT_LOG)"

run-sw-report-existing:
	@mkdir -p "$(REPORT_DIR)"
	@: > "$(REPORT_LOG)"
	@$(MAKE) xrt-host >>"$(REPORT_LOG)" 2>&1
	@$(MAKE) generate DATASET_DIR="$(DATASET_DIR)" SIZE="$(SIZE)" ASPECT_RATIO="$(ASPECT_RATIO)" >>"$(REPORT_LOG)" 2>&1
	@test -f "$(SW_XCLBIN)" || ($(MAKE) build-sw >>"$(REPORT_LOG)" 2>&1)
	@test -f "$(BUILD_DIR)/sw_emu/emconfig.json" || ($(MAKE) $(BUILD_DIR)/sw_emu/emconfig.json TARGET=sw_emu >>"$(REPORT_LOG)" 2>&1)
	@{ $(VITIS_ENV_CMD) cd "$(BUILD_DIR)/sw_emu" && EMCONFIG_PATH=$$PWD XCL_EMULATION_MODE=sw_emu "$(XRT_HOST)" cgsolver_jacobi_pcg.xclbin "$(DATASET_DIR)" --tau $(TAU) --max-iters $(MAX_ITERS) --device-index $(DEVICE_INDEX) --timing --json-out "$(REPORT_JSON)" --txt-out "$(REPORT_TXT)" $(HOST_RUN_ARGS); } >>"$(REPORT_LOG)" 2>&1
	@$(PYTHON) "$(SCRIPT_DIR)/render_report.py" interactive "$(REPORT_JSON)" "$(REPORT_HTML)" >>"$(REPORT_LOG)" 2>&1
	@$(PYTHON) "$(SCRIPT_DIR)/render_report.py" static "$(REPORT_JSON)" "$(REPORT_HTML_STATIC)" >>"$(REPORT_LOG)" 2>&1
	@grep -E "$(SUMMARY_GREP)" "$(REPORT_LOG)" || true
	@echo "report json: $(REPORT_JSON)"
	@echo "report txt : $(REPORT_TXT)"
	@echo "report html: $(REPORT_HTML)"
	@echo "report html static: $(REPORT_HTML_STATIC)"
	@echo "report log : $(REPORT_LOG)"

run-hw-report:
	$(MAKE) _run-hw-report TARGET=hw REPORT_BASENAME_HW="$(REPORT_BASENAME_HW)" DATASET_DIR="$(DATASET_DIR)" TAU="$(TAU)" MAX_ITERS="$(MAX_ITERS)" DEVICE_INDEX="$(DEVICE_INDEX)" HOST_ARGS='$(HOST_ARGS)'

_run-hw-report:
	@mkdir -p "$(REPORT_DIR)"
	@: > "$(REPORT_LOG_HW)"
	@$(MAKE) xrt-host >>"$(REPORT_LOG_HW)" 2>&1
	@$(MAKE) build-hw >>"$(REPORT_LOG_HW)" 2>&1
	@$(MAKE) generate DATASET_DIR="$(DATASET_DIR)" SIZE="$(SIZE)" ASPECT_RATIO="$(ASPECT_RATIO)" >>"$(REPORT_LOG_HW)" 2>&1
	@{ $(VITIS_ENV_CMD) "$(XRT_HOST)" "$(XCLBIN)" "$(DATASET_DIR)" --tau $(TAU) --max-iters $(MAX_ITERS) --device-index $(DEVICE_INDEX) --timing --json-out "$(REPORT_JSON_HW)" --txt-out "$(REPORT_TXT_HW)" $(HOST_RUN_ARGS); } >>"$(REPORT_LOG_HW)" 2>&1
	@$(PYTHON) "$(SCRIPT_DIR)/render_report.py" interactive "$(REPORT_JSON_HW)" "$(REPORT_HTML_HW)" >>"$(REPORT_LOG_HW)" 2>&1
	@$(PYTHON) "$(SCRIPT_DIR)/render_report.py" static "$(REPORT_JSON_HW)" "$(REPORT_HTML_STATIC_HW)" >>"$(REPORT_LOG_HW)" 2>&1
	@grep -E "$(SUMMARY_GREP)" "$(REPORT_LOG_HW)" || true
	@echo "report json: $(REPORT_JSON_HW)"
	@echo "report txt : $(REPORT_TXT_HW)"
	@echo "report html: $(REPORT_HTML_HW)"
	@echo "report html static: $(REPORT_HTML_STATIC_HW)"
	@echo "report log : $(REPORT_LOG_HW)"

run-hw-report-existing:
	@mkdir -p "$(REPORT_DIR)"
	@: > "$(REPORT_LOG_HW)"
	@$(MAKE) xrt-host >>"$(REPORT_LOG_HW)" 2>&1
	@$(MAKE) generate DATASET_DIR="$(DATASET_DIR)" SIZE="$(SIZE)" ASPECT_RATIO="$(ASPECT_RATIO)" >>"$(REPORT_LOG_HW)" 2>&1
	@test -f "$(HW_XCLBIN)" || ($(MAKE) build-hw >>"$(REPORT_LOG_HW)" 2>&1)
	@{ $(VITIS_ENV_CMD) "$(XRT_HOST)" "$(HW_XCLBIN)" "$(DATASET_DIR)" --tau $(TAU) --max-iters $(MAX_ITERS) --device-index $(DEVICE_INDEX) --timing --json-out "$(REPORT_JSON_HW)" --txt-out "$(REPORT_TXT_HW)" $(HOST_RUN_ARGS); } >>"$(REPORT_LOG_HW)" 2>&1
	@$(PYTHON) "$(SCRIPT_DIR)/render_report.py" interactive "$(REPORT_JSON_HW)" "$(REPORT_HTML_HW)" >>"$(REPORT_LOG_HW)" 2>&1
	@$(PYTHON) "$(SCRIPT_DIR)/render_report.py" static "$(REPORT_JSON_HW)" "$(REPORT_HTML_STATIC_HW)" >>"$(REPORT_LOG_HW)" 2>&1
	@grep -E "$(SUMMARY_GREP)" "$(REPORT_LOG_HW)" || true
	@echo "report json: $(REPORT_JSON_HW)"
	@echo "report txt : $(REPORT_TXT_HW)"
	@echo "report html: $(REPORT_HTML_HW)"
	@echo "report html static: $(REPORT_HTML_STATIC_HW)"
	@echo "report log : $(REPORT_LOG_HW)"

render-report:
	$(PYTHON) "$(SCRIPT_DIR)/render_report.py" interactive "$(REPORT_JSON)" "$(REPORT_HTML)"
	$(PYTHON) "$(SCRIPT_DIR)/render_report.py" static "$(REPORT_JSON)" "$(REPORT_HTML_STATIC)"

render-hw-report:
	$(PYTHON) "$(SCRIPT_DIR)/render_report.py" interactive "$(REPORT_JSON_HW)" "$(REPORT_HTML_HW)"
	$(PYTHON) "$(SCRIPT_DIR)/render_report.py" static "$(REPORT_JSON_HW)" "$(REPORT_HTML_STATIC_HW)"

vivado-package-full:
	@test -d "$(BUILD_DIR)/hw/_x_temp/link/vivado/vpl" || (echo "ERROR: Vivado link directory not found: $(BUILD_DIR)/hw/_x_temp/link/vivado/vpl. Build hardware first with: make build-hw" && exit 1)
	@test -d "$(BUILD_DIR)/hw/_x_temp/reports/link" || (echo "ERROR: hardware report directory not found: $(BUILD_DIR)/hw/_x_temp/reports/link. Build hardware first with: make build-hw" && exit 1)
	@mkdir -p "$(VIVADO_PACKAGE_DIR)"
	cd "$(ROOT_DIR)" && tar -czf "$(VIVADO_PACKAGE_FULL)" \
		"build/hw/_x_temp/link/vivado/vpl" \
		"build/hw/_x_temp/reports/link" \
		"README.md" \
		"docs/design/hls.md" \
		"docs/design/hls_source_walkthrough_zh.md"
	@echo "Created: $(VIVADO_PACKAGE_FULL)"
	@ls -lh "$(VIVADO_PACKAGE_FULL)"

clean:
	rm -rf $(BUILD_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET_BUILD_DIR):
	mkdir -p $(TARGET_BUILD_DIR)
