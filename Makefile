SHELL := /bin/bash
empty :=
space := $(empty) $(empty)

CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2
PYTHON ?= python3

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
VIVADO ?= $(if $(VIVADO_ROOT),$(VIVADO_ROOT)/bin/vivado,vivado)
EMCONFIGUTIL ?= $(if $(VITIS_ROOT),$(VITIS_ROOT)/bin/emconfigutil,emconfigutil)
VITIS_SETTINGS ?= $(if $(VITIS_ROOT),$(VITIS_ROOT)/settings64.sh)
VITIS_HLS_ROOT ?= $(strip $(shell \
	if [ -n "$$XILINX_HLS" ] && [ -d "$$XILINX_HLS/include" ]; then \
		printf '%s\n' "$$XILINX_HLS"; \
	elif [ -d /tools/Xilinx2022/Vitis_HLS/2022.2/include ]; then \
		printf '%s\n' /tools/Xilinx2022/Vitis_HLS/2022.2; \
	elif [ -d /tools/Xilinx/Vitis_HLS/2022.2/include ]; then \
		printf '%s\n' /tools/Xilinx/Vitis_HLS/2022.2; \
	elif [ -d /tools/Xilinx/Vitis_HLS/2021.2/include ]; then \
		printf '%s\n' /tools/Xilinx/Vitis_HLS/2021.2; \
	fi))
TAPA_ROOT ?= $(strip $(shell \
	if [ -n "$$TAPA_ROOT" ] && [ -f "$$TAPA_ROOT/include/tapa.h" ]; then \
		printf '%s\n' "$$TAPA_ROOT"; \
	elif [ -f "$$HOME/.tapa/usr/include/tapa.h" ]; then \
		printf '%s\n' "$$HOME/.tapa/usr"; \
	elif [ -f "$$HOME/.rapidstream-tapa/usr/include/tapa.h" ]; then \
		printf '%s\n' "$$HOME/.rapidstream-tapa/usr"; \
	fi))

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
CUPER_DIR := $(ROOT_DIR)/DLC/Cuper
CUPER_CONTROL_CFG := $(CFG_DIR)/connectivity_cuper_control_u55c.cfg
VIVADO_LINK_DIR := $(TARGET_BUILD_DIR)/_x_temp/link/vivado/vpl
VIVADO_REPORT_DIR := $(TARGET_BUILD_DIR)/_x_temp/reports/link
ANALYSIS_TARGET ?= hw
ANALYSIS_BUILD_DIR := $(BUILD_DIR)/$(ANALYSIS_TARGET)
ANALYSIS_VIVADO_LINK_DIR := $(ANALYSIS_BUILD_DIR)/_x_temp/link/vivado/vpl
ANALYSIS_REPORT_DIR ?= $(ANALYSIS_BUILD_DIR)/_x_temp/reports/analysis
VIVADO_ANALYSIS_INPUT ?= $(ANALYSIS_VIVADO_LINK_DIR)
VIVADO_ANALYSIS_RUN ?= impl_1
VIVADO_ANALYSIS_REPORTS ?= all
VIVADO_ANALYSIS_CFG := $(CFG_DIR)/vivado_analysis_reports.cfg
ENABLE_VIVADO_ANALYSIS ?= 1
VIVADO_PACKAGE_DIR := $(BUILD_DIR)/packages
VIVADO_PACKAGE_NAME ?= project-xplus-vivado-view
VIVADO_PACKAGE_FULL := $(VIVADO_PACKAGE_DIR)/$(VIVADO_PACKAGE_NAME)-full.tar.gz
REPORT_BASENAME ?= thermal2_n1024
REPORT_BASENAME_HW ?= thermal2_n1024
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
CUPER_PCG_HOST := $(BUILD_DIR)/xplus_cuper_pcg_host
CUPER_TAPA_PCG_HOST := $(BUILD_DIR)/xplus_cuper_tapa_pcg_host
CUPER_CONTROL_LOCAL_HOST := $(BUILD_DIR)/xplus_cuper_control_local_host
CUPER_CONTROL_XRT_HOST := $(BUILD_DIR)/xplus_cuper_control_xrt_host
XRT_HOST := $(BUILD_DIR)/xplus_xrt_host

SIZE ?= 512
ASPECT_RATIO ?= 1.6
DATASETS ?= thermal2_n1024

KERNEL_NAMES := pcg_control_kernel
# 默认 XRT xclbin 只链接 pcg_control_kernel。当前 pcg_control_kernel
# 内部 SpMV 已经切到 4x4 block/bitmap 格式，host 会在运行时把 CSR
# 转换成 b_row_ptr / b_col_idx / blocks 三个 BO。
#
# 注意：SPMV_KERNEL_SOURCE 只影响下面保留的旧 spmv_csr_kernel.xo
# 编译规则；当前 XOS := $(XO_PCG_CONTROL)，所以默认 build-sw/build-hw
# 不会把独立的 spmv_csr_kernel.xo 或 spmv_blocked_kernel.xo 链接进 xclbin。
#
# spmv_blocked_kernel.cpp 仍保留为旧拆分 kernel 的参考实现；默认 PCG
# 路径真正使用的是 kernels/pcg_control_kernel.cpp 里的
# spmv_blocked_local(...)。
SPMV_KERNEL_SOURCE ?= $(KERNEL_DIR)/spmv_csr_kernel.cpp
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
XO_PCG_CONTROL := $(TARGET_BUILD_DIR)/pcg_control_kernel.xo
XOS := $(XO_PCG_CONTROL)
XCLBIN := $(TARGET_BUILD_DIR)/cgsolver_jacobi_pcg.xclbin
CUPER_CONTROL_XO := $(TARGET_BUILD_DIR)/cuper_pcg_control_kernel.xo
CUPER_CONTROL_XCLBIN := $(TARGET_BUILD_DIR)/cuper_pcg_control_kernel.xclbin
EMCONFIG := $(TARGET_BUILD_DIR)/emconfig.json

XRT_CXXFLAGS := $(CXXFLAGS) -Wall -Wextra -I$(XILINX_XRT)/include
XRT_LDFLAGS := -L$(XILINX_XRT)/lib -lxrt_coreutil -luuid -pthread -lrt
XRT_LDFLAGS += -Wl,-rpath,$(XILINX_XRT)/lib

TAPA_CXXFLAGS := $(CXXFLAGS) -Wall -Wextra
TAPA_CXXFLAGS += -I$(INCLUDE_DIR) -I$(HOST_DIR) -I$(SRC_DIR)
TAPA_CXXFLAGS += -I$(CUPER_DIR)/include -I$(TAPA_ROOT)/include
TAPA_CXXFLAGS += -I$(VITIS_HLS_ROOT)/include -I$(VITIS_HLS_ROOT)/common/technology/autopilot
TAPA_RUNTIME_LIBS := \
	$(TAPA_ROOT)/lib/libtapa.so \
	$(TAPA_ROOT)/lib/libcontext.so \
	$(TAPA_ROOT)/lib/libfrt.so \
	$(TAPA_ROOT)/lib/libOpenCL.so \
	$(TAPA_ROOT)/lib/libglog.so \
	$(TAPA_ROOT)/lib/libgflags.so \
	$(TAPA_ROOT)/lib/libthread.so \
	$(TAPA_ROOT)/lib/libtinyxml2.so \
	$(TAPA_ROOT)/lib/libyaml-cpp.so
TAPA_LDFLAGS := $(TAPA_RUNTIME_LIBS) -pthread
TAPA_LDFLAGS += -Wl,-rpath,$(TAPA_ROOT)/lib
TAPA_LDFLAGS += -Wl,-rpath,$(XILINX_XRT)/lib

VPP_FLAGS += -t $(TARGET) --platform $(XPLATFORM) --save-temps
VPP_FLAGS += --temp_dir $(TARGET_BUILD_DIR)/_x_temp
VPP_FLAGS += -I$(INCLUDE_DIR) -I$(KERNEL_DIR)
VPP_LDFLAGS += --config $(CFG_DIR)/connectivity_u55c.cfg
ifeq ($(TARGET),hw)
ifeq ($(ENABLE_VIVADO_ANALYSIS),1)
VPP_LDFLAGS += --config $(VIVADO_ANALYSIS_CFG)
endif
endif

VITIS_ENV_CMD := source "$(VITIS_SETTINGS)" >/dev/null 2>&1 &&
SUMMARY_GREP := ^(\\[xplus-xrt\\]|\\[init\\]|\\[done\\]|\\[check\\]|\\[host-timing-ms\\]|\\[kernel-timing-ms\\])

ifneq ($(strip $(VITIS_ROOT)),)
export XILINX_VITIS := $(VITIS_ROOT)
endif
export XILINX_XRT := $(XILINX_XRT)

.PHONY: all help env tapa-env vivado-env generate download-suitesparse-data list-suitesparse-data local-host cuper-pcg-host cuper-tapa-pcg-host cuper-control-local-host cuper-control-xrt-host xrt-host launch launcher menu run run-local run-cuper-pcg run-cuper-pcg-tapa run-cuper-control-local run-cuper-control-xrt run-xrt run-sw-report run-sw-report-existing run-hw-report run-hw-report-existing _run-hw-report render-report render-hw-report vivado-power-report vivado-analysis xrt-power-snapshot vivado-package-full build build-sw build-hw build-cuper-control build-cuper-control-sw build-cuper-control-hw cuper-control-hw-tmux cuper-launch cuper-launcher cuper-build-host cuper-run-sw cuper-build-xo cuper-link-xclbin cuper-hw-tmux cuper-run-hw clean clean-reports

all: run-local

help:
	@echo "Project-XPlus Jacobi-PCG single-control-kernel project"
	@echo "Default run parameters live in host/run_defaults.hpp."
	@echo ""
	@echo "Local reference path:"
	@echo "  make run-local"
	@echo ""
	@echo "Project-XPlus Cuper-PCG path:"
	@echo "  make run-cuper-pcg DATASET=data/suitesparse/Schmid/csr/thermal2_n1024"
	@echo "  make run-cuper-pcg-tapa DATASET=data/suitesparse/Schmid/csr/thermal2_n16 MAX_ITERS=1 TAU=1e6"
	@echo "  make run-cuper-control-local DATASET=data/suitesparse/Schmid/csr/thermal2_n1024"
	@echo "  make build-cuper-control-sw"
	@echo "  make run-cuper-control-xrt TARGET=sw_emu DATASET=data/suitesparse/Schmid/csr/thermal2_n16"
	@echo "  make cuper-control-hw-tmux"
	@echo ""
	@echo "Build XRT host only:"
	@echo "  make xrt-host"
	@echo ""
	@echo "Interactive run launcher:"
	@echo "  make launcher"
	@echo "  make cuper-launcher"
	@echo ""
	@echo "Generate dataset:"
	@echo "  make generate"
	@echo "  make download-suitesparse-data"
	@echo "  make download-suitesparse-data DATASETS=all"
	@echo "  make list-suitesparse-data"
	@echo ""
	@echo "Build sw_emu xclbin:"
	@echo "  make build-sw"
	@echo ""
	@echo "Run sw_emu:"
	@echo "  make run-xrt TARGET=sw_emu"
	@echo "  make run-sw-report"
	@echo "  make run-sw-report-existing"
	@echo ""
	@echo "Build hardware xclbin:"
	@echo "  make build-hw"
	@echo "  make build-hw ENABLE_VIVADO_ANALYSIS=0"
	@echo "  make vivado-package-full"
	@echo "  make vivado-power-report"
	@echo "  make vivado-analysis"
	@echo "  make xrt-power-snapshot"
	@echo ""
	@echo "Run hardware:"
	@echo "  make run-xrt TARGET=hw"
	@echo "  make run-hw-report"
	@echo "  make run-hw-report-existing"
	@echo ""
	@echo "DLC/Cuper independent SpMV tool:"
	@echo "  make cuper-launch"
	@echo "  make cuper-build-host"
	@echo "  make cuper-run-sw MATRIX=data/matrices/sit100/sit100.mtx"
	@echo "  make cuper-build-xo"
	@echo "  make cuper-link-xclbin"
	@echo "  make cuper-hw-tmux"
	@echo "  make cuper-run-hw MATRIX=data/matrices/sit100/sit100.mtx"

env:
	@test -f "$(XPLATFORM)" || (echo "ERROR: platform not found: $(XPLATFORM)" && exit 1)
	@test -d "$(XILINX_XRT)" || (echo "ERROR: XILINX_XRT not found: $(XILINX_XRT)" && exit 1)
	@test -x "$(VPP)" || command -v "$(VPP)" >/dev/null || (echo "ERROR: v++ not found" && exit 1)
	@test -x "$(EMCONFIGUTIL)" || command -v "$(EMCONFIGUTIL)" >/dev/null || (echo "ERROR: emconfigutil not found" && exit 1)
	@test -f "$(VITIS_SETTINGS)" || (echo "ERROR: Vitis settings script not found: $(VITIS_SETTINGS)" && exit 1)

tapa-env:
	@test -f "$(TAPA_ROOT)/include/tapa.h" || (echo "ERROR: TAPA_ROOT not found or invalid: $(TAPA_ROOT)" && exit 1)
	@test -f "$(VITIS_HLS_ROOT)/include/ap_int.h" || (echo "ERROR: VITIS_HLS_ROOT not found or invalid: $(VITIS_HLS_ROOT)" && exit 1)
	@test -d "$(XILINX_XRT)" || (echo "ERROR: XILINX_XRT not found: $(XILINX_XRT)" && exit 1)
	@test -f "$(TAPA_ROOT)/lib/libtapa.so" || (echo "ERROR: libtapa.so not found under $(TAPA_ROOT)/lib" && exit 1)

vivado-env: env
	@test -x "$(VIVADO)" || command -v "$(VIVADO)" >/dev/null || (echo "ERROR: vivado not found" && exit 1)

generate:
	$(PYTHON) "$(SCRIPT_DIR)/generate_cg_dataset.py" --size $(SIZE) --aspect-ratio $(ASPECT_RATIO)

download-suitesparse-data:
	$(PYTHON) "$(SCRIPT_DIR)/download_suitesparse_data.py" --datasets $(DATASETS)

list-suitesparse-data:
	$(PYTHON) "$(SCRIPT_DIR)/download_suitesparse_data.py" --list

local-host: $(LOCAL_HOST)

cuper-pcg-host: $(CUPER_PCG_HOST)

cuper-tapa-pcg-host: $(CUPER_TAPA_PCG_HOST)

cuper-control-local-host: $(CUPER_CONTROL_LOCAL_HOST)

cuper-control-xrt-host: $(CUPER_CONTROL_XRT_HOST)

xrt-host: $(XRT_HOST)

launch: launcher

launcher menu: $(LOCAL_HOST) $(CUPER_PCG_HOST) $(CUPER_CONTROL_LOCAL_HOST) $(CUPER_CONTROL_XRT_HOST) $(XRT_HOST)
	@$(PYTHON) "$(SCRIPT_DIR)/launcher.py" \
		--vitis-settings "$(VITIS_SETTINGS)" \
		--local-host "$(LOCAL_HOST)" \
		--cuper-pcg-host "$(CUPER_PCG_HOST)" \
		--cuper-tapa-pcg-host "$(CUPER_TAPA_PCG_HOST)" \
		--cuper-control-local-host "$(CUPER_CONTROL_LOCAL_HOST)" \
		--xrt-host "$(XRT_HOST)" \
		--hw-xclbin "$(HW_XCLBIN)" \
		--sw-xclbin "$(SW_XCLBIN)" \
		--sw-emu-dir "$(BUILD_DIR)/sw_emu"

cuper-launch cuper-launcher:
	@$(MAKE) -C "$(CUPER_DIR)" launch

cuper-build-host:
	@$(MAKE) -C "$(CUPER_DIR)" build-host

cuper-run-sw:
	@$(MAKE) -C "$(CUPER_DIR)" run-sw MATRIX="$(or $(MATRIX),data/matrices/sit100/sit100.mtx)"

cuper-build-xo:
	@$(MAKE) -C "$(CUPER_DIR)" build-xo

cuper-link-xclbin:
	@$(MAKE) -C "$(CUPER_DIR)" link-xclbin

cuper-hw-tmux:
	@$(MAKE) -C "$(CUPER_DIR)" hw-tmux $(if $(FORCE),FORCE=$(FORCE))

cuper-run-hw:
	@$(MAKE) -C "$(CUPER_DIR)" run-hw MATRIX="$(or $(MATRIX),data/matrices/sit100/sit100.mtx)"

$(LOCAL_HOST): $(HOST_DIR)/main.cpp $(HOST_DIR)/run_defaults.hpp $(HOST_DIR)/cpu_reference.hpp $(HOST_DIR)/dataset_bridge.hpp $(HOST_DIR)/multi_kernel_solver.hpp $(INCLUDE_DIR)/cg_common.hpp $(INCLUDE_DIR)/cg_kernels.hpp $(KERNEL_DIR)/cg_kernels.cpp $(SRC_DIR)/CgSolverGolden.hpp $(SRC_DIR)/CsrDataset.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -I$(HOST_DIR) -I$(SRC_DIR) $(HOST_DIR)/main.cpp $(KERNEL_DIR)/cg_kernels.cpp -o $(LOCAL_HOST)

$(CUPER_PCG_HOST): $(HOST_DIR)/cuper_pcg_main.cpp $(HOST_DIR)/cuper_pcg_solver.hpp $(HOST_DIR)/run_defaults.hpp $(HOST_DIR)/cpu_reference.hpp $(HOST_DIR)/dataset_bridge.hpp $(HOST_DIR)/multi_kernel_solver.hpp $(INCLUDE_DIR)/cg_common.hpp $(SRC_DIR)/CgSolverGolden.hpp $(SRC_DIR)/CsrDataset.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -I$(HOST_DIR) -I$(SRC_DIR) $(HOST_DIR)/cuper_pcg_main.cpp $(KERNEL_DIR)/cg_kernels.cpp -o $(CUPER_PCG_HOST)

$(CUPER_TAPA_PCG_HOST): $(HOST_DIR)/cuper_tapa_pcg_main.cpp $(HOST_DIR)/cuper_pcg_solver.hpp $(HOST_DIR)/run_defaults.hpp $(HOST_DIR)/cpu_reference.hpp $(HOST_DIR)/dataset_bridge.hpp $(HOST_DIR)/multi_kernel_solver.hpp $(INCLUDE_DIR)/cg_common.hpp $(SRC_DIR)/CgSolverGolden.hpp $(SRC_DIR)/CsrDataset.hpp $(CUPER_DIR)/include/Cuper.h $(CUPER_DIR)/include/Cuper_common.h $(CUPER_DIR)/kernels/Cuper.cpp | $(BUILD_DIR) tapa-env
	$(CXX) $(TAPA_CXXFLAGS) $(HOST_DIR)/cuper_tapa_pcg_main.cpp $(CUPER_DIR)/kernels/Cuper.cpp -o $(CUPER_TAPA_PCG_HOST) $(TAPA_LDFLAGS)

$(CUPER_CONTROL_LOCAL_HOST): $(HOST_DIR)/cuper_control_local_main.cpp $(HOST_DIR)/cuper_control_matrix.hpp $(HOST_DIR)/run_defaults.hpp $(HOST_DIR)/cpu_reference.hpp $(HOST_DIR)/dataset_bridge.hpp $(HOST_DIR)/multi_kernel_solver.hpp $(INCLUDE_DIR)/cg_common.hpp $(KERNEL_DIR)/cuper_pcg_control_kernel.cpp $(SRC_DIR)/CgSolverGolden.hpp $(SRC_DIR)/CsrDataset.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -I$(HOST_DIR) -I$(SRC_DIR) $(HOST_DIR)/cuper_control_local_main.cpp $(KERNEL_DIR)/cuper_pcg_control_kernel.cpp -o $(CUPER_CONTROL_LOCAL_HOST)

$(CUPER_CONTROL_XRT_HOST): $(HOST_DIR)/cuper_control_xrt_host.cpp $(HOST_DIR)/cuper_control_matrix.hpp $(HOST_DIR)/run_defaults.hpp $(HOST_DIR)/cpu_reference.hpp $(HOST_DIR)/dataset_bridge.hpp $(HOST_DIR)/multi_kernel_solver.hpp $(INCLUDE_DIR)/cg_common.hpp $(SRC_DIR)/CgSolverGolden.hpp $(SRC_DIR)/CsrDataset.hpp | $(BUILD_DIR)
	$(CXX) $(XRT_CXXFLAGS) -I$(INCLUDE_DIR) -I$(HOST_DIR) -I$(SRC_DIR) $(HOST_DIR)/cuper_control_xrt_host.cpp -o $(CUPER_CONTROL_XRT_HOST) $(XRT_LDFLAGS)

$(XRT_HOST): $(HOST_DIR)/xrt_host.cpp $(HOST_DIR)/run_defaults.hpp $(HOST_DIR)/dataset_bridge.hpp $(HOST_DIR)/cpu_reference.hpp $(INCLUDE_DIR)/cg_common.hpp $(SRC_DIR)/CgSolverGolden.hpp $(SRC_DIR)/CsrDataset.hpp | $(BUILD_DIR)
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

$(XO_PCG_CONTROL): $(KERNEL_DIR)/pcg_control_kernel.cpp $(INCLUDE_DIR)/cg_common.hpp | $(TARGET_BUILD_DIR) env
	$(VITIS_ENV_CMD) cd $(TARGET_BUILD_DIR) && $(VPP) -c $(VPP_FLAGS) -k pcg_control_kernel -o $@ $<

$(CUPER_CONTROL_XO): $(KERNEL_DIR)/cuper_pcg_control_kernel.cpp $(INCLUDE_DIR)/cg_common.hpp | $(TARGET_BUILD_DIR) env
	$(VITIS_ENV_CMD) cd $(TARGET_BUILD_DIR) && $(VPP) -c $(VPP_FLAGS) -k cuper_pcg_control_kernel -o $@ $<

$(XCLBIN): $(XOS) $(CFG_DIR)/connectivity_u55c.cfg | $(TARGET_BUILD_DIR) env
	$(VITIS_ENV_CMD) cd $(TARGET_BUILD_DIR) && $(VPP) -l $(VPP_FLAGS) $(VPP_LDFLAGS) -o $@ $(XOS)

$(CUPER_CONTROL_XCLBIN): $(CUPER_CONTROL_XO) $(CUPER_CONTROL_CFG) | $(TARGET_BUILD_DIR) env
	$(VITIS_ENV_CMD) cd $(TARGET_BUILD_DIR) && $(VPP) -l $(VPP_FLAGS) --config $(CUPER_CONTROL_CFG) -o $@ $(CUPER_CONTROL_XO)

$(EMCONFIG): | $(TARGET_BUILD_DIR) env
	$(VITIS_ENV_CMD) cd $(TARGET_BUILD_DIR) && $(EMCONFIGUTIL) --platform $(XPLATFORM) --od .

build: $(XCLBIN)

build-sw:
	$(MAKE) build TARGET=sw_emu

build-hw:
	$(MAKE) build TARGET=hw

build-cuper-control: $(CUPER_CONTROL_XCLBIN)

build-cuper-control-sw:
	$(MAKE) build-cuper-control TARGET=sw_emu

build-cuper-control-hw:
	$(MAKE) build-cuper-control TARGET=hw

cuper-control-hw-tmux:
	@mkdir -p "$(LOG_DIR)"
	@if tmux has-session -t "project-xplus-cuper-control-hw" 2>/dev/null; then \
		echo "tmux session already exists: project-xplus-cuper-control-hw"; \
		echo "attach: tmux attach -t project-xplus-cuper-control-hw"; \
		exit 1; \
	fi
	@stamp=$$(date +%Y%m%d_%H%M%S); \
	log="$(LOG_DIR)/cuper_control_hw_$${stamp}.log"; \
	tmux new-session -d -s "project-xplus-cuper-control-hw" \
		"cd '$(ROOT_DIR)' && $(MAKE) build-cuper-control-hw 2>&1 | tee '$$log'"; \
	echo "session: project-xplus-cuper-control-hw"; \
	echo "log: $$log"; \
	echo "attach: tmux attach -t project-xplus-cuper-control-hw"; \
	echo "tail: tail -f $$log"

run: run-local

run-local: $(LOCAL_HOST)
	$(LOCAL_HOST)

run-cuper-pcg: $(CUPER_PCG_HOST)
	$(CUPER_PCG_HOST) "$(or $(DATASET),$(ROOT_DIR)/data/suitesparse/Schmid/csr/thermal2_n1024)" $(if $(TAU),--tau $(TAU)) $(if $(MAX_ITERS),--max-iters $(MAX_ITERS)) $(if $(DIFF_TOL),--diff-tol $(DIFF_TOL))

run-cuper-pcg-tapa: $(CUPER_TAPA_PCG_HOST)
	LD_LIBRARY_PATH="$(TAPA_ROOT)/lib:$(XILINX_XRT)/lib:$${LD_LIBRARY_PATH:-}" $(CUPER_TAPA_PCG_HOST) "$(or $(DATASET),$(ROOT_DIR)/data/suitesparse/Schmid/csr/thermal2_n16)" $(if $(BITFILE),--bitstream "$(BITFILE)") $(if $(TAU),--tau $(TAU)) $(if $(MAX_ITERS),--max-iters $(MAX_ITERS)) $(if $(DIFF_TOL),--diff-tol $(DIFF_TOL))

run-cuper-control-local: $(CUPER_CONTROL_LOCAL_HOST)
	$(CUPER_CONTROL_LOCAL_HOST) "$(or $(DATASET),$(ROOT_DIR)/data/suitesparse/Schmid/csr/thermal2_n1024)" $(if $(TAU),--tau $(TAU)) $(if $(MAX_ITERS),--max-iters $(MAX_ITERS)) $(if $(DIFF_TOL),--diff-tol $(DIFF_TOL))

run-cuper-control-xrt: $(CUPER_CONTROL_XRT_HOST)
ifeq ($(TARGET),hw)
	@test -f "$(CUPER_CONTROL_XCLBIN)" || (echo "ERROR: xclbin not found: $(CUPER_CONTROL_XCLBIN). Build it first with: make build-cuper-control-hw" && exit 1)
	$(VITIS_ENV_CMD) $(CUPER_CONTROL_XRT_HOST) "$(CUPER_CONTROL_XCLBIN)" "$(abspath $(or $(DATASET),$(ROOT_DIR)/data/suitesparse/Schmid/csr/thermal2_n16))" $(if $(TAU),--tau $(TAU)) $(if $(MAX_ITERS),--max-iters $(MAX_ITERS)) $(if $(DIFF_TOL),--diff-tol $(DIFF_TOL))
else
	@test -f "$(CUPER_CONTROL_XCLBIN)" || (echo "ERROR: xclbin not found: $(CUPER_CONTROL_XCLBIN). Build it first with: make build-cuper-control-sw" && exit 1)
	$(MAKE) $(EMCONFIG) TARGET=$(TARGET)
	$(VITIS_ENV_CMD) cd $(TARGET_BUILD_DIR) && EMCONFIG_PATH=$$PWD XCL_EMULATION_MODE=$(TARGET) "$(CUPER_CONTROL_XRT_HOST)" "$(CUPER_CONTROL_XCLBIN)" "$(abspath $(or $(DATASET),$(ROOT_DIR)/data/suitesparse/Schmid/csr/thermal2_n16))" $(if $(TAU),--tau $(TAU)) $(if $(MAX_ITERS),--max-iters $(MAX_ITERS)) $(if $(DIFF_TOL),--diff-tol $(DIFF_TOL))
endif

run-xrt: $(XRT_HOST)
ifeq ($(TARGET),hw)
	@test -f "$(XCLBIN)" || (echo "ERROR: xclbin not found: $(XCLBIN). Build it first with: make build-hw" && exit 1)
	$(VITIS_ENV_CMD) $(XRT_HOST)
else
	@test -f "$(XCLBIN)" || (echo "ERROR: xclbin not found: $(XCLBIN). Build it first with: make build-sw" && exit 1)
	$(MAKE) $(EMCONFIG) TARGET=$(TARGET)
	$(VITIS_ENV_CMD) cd $(TARGET_BUILD_DIR) && EMCONFIG_PATH=$$PWD XCL_EMULATION_MODE=$(TARGET) "$(XRT_HOST)"
endif

run-sw-report:
	@mkdir -p "$(REPORT_DIR)"
	@: > "$(REPORT_LOG)"
	@$(MAKE) xrt-host >>"$(REPORT_LOG)" 2>&1
	@$(MAKE) build-sw >>"$(REPORT_LOG)" 2>&1
	@$(MAKE) $(BUILD_DIR)/sw_emu/emconfig.json TARGET=sw_emu >>"$(REPORT_LOG)" 2>&1
	@{ $(VITIS_ENV_CMD) cd "$(BUILD_DIR)/sw_emu" && EMCONFIG_PATH=$$PWD XCL_EMULATION_MODE=sw_emu "$(XRT_HOST)"; } >>"$(REPORT_LOG)" 2>&1
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
	@test -f "$(SW_XCLBIN)" || ($(MAKE) build-sw >>"$(REPORT_LOG)" 2>&1)
	@test -f "$(BUILD_DIR)/sw_emu/emconfig.json" || ($(MAKE) $(BUILD_DIR)/sw_emu/emconfig.json TARGET=sw_emu >>"$(REPORT_LOG)" 2>&1)
	@{ $(VITIS_ENV_CMD) cd "$(BUILD_DIR)/sw_emu" && EMCONFIG_PATH=$$PWD XCL_EMULATION_MODE=sw_emu "$(XRT_HOST)"; } >>"$(REPORT_LOG)" 2>&1
	@$(PYTHON) "$(SCRIPT_DIR)/render_report.py" interactive "$(REPORT_JSON)" "$(REPORT_HTML)" >>"$(REPORT_LOG)" 2>&1
	@$(PYTHON) "$(SCRIPT_DIR)/render_report.py" static "$(REPORT_JSON)" "$(REPORT_HTML_STATIC)" >>"$(REPORT_LOG)" 2>&1
	@grep -E "$(SUMMARY_GREP)" "$(REPORT_LOG)" || true
	@echo "report json: $(REPORT_JSON)"
	@echo "report txt : $(REPORT_TXT)"
	@echo "report html: $(REPORT_HTML)"
	@echo "report html static: $(REPORT_HTML_STATIC)"
	@echo "report log : $(REPORT_LOG)"

run-hw-report:
	$(MAKE) _run-hw-report TARGET=hw REPORT_BASENAME_HW="$(REPORT_BASENAME_HW)"

_run-hw-report:
	@mkdir -p "$(REPORT_DIR)"
	@: > "$(REPORT_LOG_HW)"
	@$(MAKE) xrt-host >>"$(REPORT_LOG_HW)" 2>&1
	@$(MAKE) build-hw >>"$(REPORT_LOG_HW)" 2>&1
	@{ $(VITIS_ENV_CMD) "$(XRT_HOST)"; } >>"$(REPORT_LOG_HW)" 2>&1
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
	@test -f "$(HW_XCLBIN)" || ($(MAKE) build-hw >>"$(REPORT_LOG_HW)" 2>&1)
	@{ $(VITIS_ENV_CMD) "$(XRT_HOST)"; } >>"$(REPORT_LOG_HW)" 2>&1
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

vivado-power-report:
	$(MAKE) vivado-analysis VIVADO_ANALYSIS_REPORTS=power

vivado-analysis: vivado-env
	@mkdir -p "$(ANALYSIS_REPORT_DIR)"
	$(VITIS_ENV_CMD) "$(VIVADO)" -mode batch -source "$(SCRIPT_DIR)/export_vivado_analysis.tcl" \
		-tclargs "$(VIVADO_ANALYSIS_INPUT)" "$(ANALYSIS_REPORT_DIR)" "$(VIVADO_ANALYSIS_RUN)" "$(VIVADO_ANALYSIS_REPORTS)"
	@echo "analysis reports: $(ANALYSIS_REPORT_DIR)"

xrt-power-snapshot:
	@mkdir -p "$(REPORT_DIR)/xrt"
	@{ \
		if ! command -v xbutil >/dev/null 2>&1; then \
			if [ -f "$(VITIS_SETTINGS)" ]; then source "$(VITIS_SETTINGS)" >/dev/null 2>&1; fi; \
		fi; \
		if ! command -v xbutil >/dev/null 2>&1; then \
			echo "ERROR: xbutil not found. Source XRT/Vitis settings first."; \
			exit 1; \
		fi; \
		out="$(REPORT_DIR)/xrt/electrical_$$(date +%Y%m%d_%H%M%S).json"; \
		if xbutil examine --report electrical --format JSON --output "$$out"; then \
			echo "xrt electrical snapshot: $$out"; \
		else \
			echo "JSON electrical snapshot failed; printing text report instead."; \
			xbutil examine --report electrical; \
		fi; \
	}

vivado-package-full:
	@test -d "$(BUILD_DIR)/hw/_x_temp/link/vivado/vpl" || (echo "ERROR: Vivado link directory not found: $(BUILD_DIR)/hw/_x_temp/link/vivado/vpl. Build hardware first with: make build-hw" && exit 1)
	@mkdir -p "$(VIVADO_PACKAGE_DIR)"
	@cd "$(ROOT_DIR)" && { \
		paths="build/hw/_x_temp/link/vivado/vpl README.md docs/design/hls.md docs/design/hls_source_walkthrough_zh.md"; \
		if [ -d "build/hw/_x_temp/reports/link" ]; then paths="$$paths build/hw/_x_temp/reports/link"; fi; \
		if [ -d "build/hw/_x_temp/reports/analysis" ]; then paths="$$paths build/hw/_x_temp/reports/analysis"; fi; \
		tar -czf "$(VIVADO_PACKAGE_FULL)" $$paths; \
	}
	@echo "Created: $(VIVADO_PACKAGE_FULL)"
	@ls -lh "$(VIVADO_PACKAGE_FULL)"

clean:
	rm -rf $(BUILD_DIR)

clean-reports:
	rm -rf $(REPORT_DIR)
	mkdir -p $(REPORT_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET_BUILD_DIR):
	mkdir -p $(TARGET_BUILD_DIR)
