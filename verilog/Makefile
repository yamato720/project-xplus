BUILD_DIR ?= build
TAPA_RTL_DIR ?= ../cuper-tapa-spmv-ooobank16-fix-hw-150m-20260620-build/tapa_CuperSpmvServiceOnly/hdl
TAPA_RTL_DIR_8 ?= ../cuper-tapa-spmv-ownerbank8-hw-150m-build/tapa_CuperSpmvServiceOnly/hdl
ROWS ?= 128
EVENTS ?= 4096
ENTRIES ?= 8
SEED ?= 1
TIMEOUT ?= 200000
CSR_MATRIX ?= ../data/suitesparse/Schmid/csr/thermal2_n1024
CSR_SCALE ?= 1024
CSR_DROP_DIAGONAL ?= 0
FREQ_MHZ ?= 150
HBM_CHANNELS ?= 16
DATASET_TIMEOUT ?= 1000000

CC ?= gcc
CXX ?= g++
VERILATOR ?= verilator
VERILATOR_BUILD_JOBS ?= 32
VIVADO ?= /home/pyx/vivado/Vivado/2022.2/bin/vivado
VIVADO_PART ?= xcu55c-fsvh2892-2L-e
VERILATOR_FP32_DPI := csrc/verilator_fp32_dpi.cpp
VERILATOR_FP32_DEFINE := -DCUPER_VERILATOR_DPI_FP=1

GEN := $(BUILD_DIR)/gen_ooo_accum_vectors
CSR_GEN := $(BUILD_DIR)/gen_csr_accum_vectors
BACKEND_XSIM_GEN := $(BUILD_DIR)/gen_tapa_backend_xsim_vectors
TOP_XSIM_GEN := $(BUILD_DIR)/gen_tapa_top_xsim_vectors
INPUT := $(BUILD_DIR)/input.mem
EXPECTED := $(BUILD_DIR)/expected.mem
CONFIG := $(BUILD_DIR)/ooo_accum_config.vh
OBJ_DIR := $(BUILD_DIR)/obj
CSR_INPUT := $(BUILD_DIR)/csr_input.tsv
CSR_EXPECTED := $(BUILD_DIR)/csr_expected.tsv
CSR_META := $(BUILD_DIR)/csr_meta.env
CSR_OBJ_DIR := $(BUILD_DIR)/csr_obj
SCOREBOARD_DATASET_OBJ_DIR := $(BUILD_DIR)/scoreboard_dataset_obj
BACKEND_XSIM_DIR := $(BUILD_DIR)/backend_xsim_vectors
TOP_XSIM_DIR := $(BUILD_DIR)/top_xsim_vectors
OWNER ?= 0
TRACE ?= 32
SCOREBOARD_DEPTH ?= 12
FIFO_DEPTH ?=
CSV ?=

TAPA_SCATTER8_BASE := tapa_mangled_Z36CuperSpmvOnly_TaggedScatterWriterOooILi8EEviiiiiRN4tapa8istreamsI27CuperSpmvOnly_TaggedFloatV2XT_EEERNS0_10async_mmapIfEERNS0_7ostreamI26CuperSpmvOnlyProgressEventEE
TAPA_SCATTER8_TOP := $(TAPA_SCATTER8_BASE)_CuperSpmvOnly_TaggedScatterWriterOoo_8_Pipeline_scatter
TAPA_SCATTER8_PREFIX := VCuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter

.PHONY: all vectors sim csr-vectors csr-sim tapa-lint tapa-sim tapa-bank-lint tapa-bank-sim tapa-vector-scoreboard-sim tapa-owner-scoreboard-sim tapa-scoreboard-dataset-cpp-sim tapa-bank-scatter-cpp-sim tapa-splitter-bank-scatter-cpp-sim tapa-splitter-bank-generated-scatter-cpp-sim tapa-core-splitter-bank-scatter-cpp-sim tapa-splitter16-bank16-dataset-cpp-sim tapa-ownerbank8-dataset-cpp-sim tapa-scatter-only-cpp-sim tapa-scatter-only-sim tapa-scatter-only-rtl-sim tapa-scatter-sim tapa-scatter-rtl-sim tapa-corestrip-xsim backend-xsim-vectors tapa-backend-dataset-xsim top-xsim-vectors cuper-spmv-service-only-top-xsim clean

all: sim

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(GEN): csrc/gen_ooo_accum_vectors.c | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra $< -o $@

$(CSR_GEN): csrc/gen_csr_accum_vectors.c | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra $< -lm -o $@

$(BACKEND_XSIM_GEN): csrc/gen_tapa_backend_xsim_vectors.c | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra $< -lm -o $@

$(TOP_XSIM_GEN): csrc/gen_tapa_top_xsim_vectors.cpp | $(BUILD_DIR)
	$(CXX) -std=c++17 -O2 -Wall -Wextra $< -o $@

vectors: $(GEN)
	$(GEN) \
	  --rows $(ROWS) \
	  --events $(EVENTS) \
	  --entries $(ENTRIES) \
	  --seed $(SEED) \
	  --timeout $(TIMEOUT) \
	  --input $(INPUT) \
	  --expected $(EXPECTED) \
	  --config $(CONFIG)

sim: vectors
	$(VERILATOR) --binary --timing -Wno-fatal -Wno-WIDTH -Wno-DECLFILENAME \
	  --top-module tb_ooo_accumulator \
	  -I$(BUILD_DIR) -Ivsrc \
	  -Mdir $(OBJ_DIR) \
	  vsrc/ooo_accumulator.sv vsrc/tb_ooo_accumulator.sv
	$(OBJ_DIR)/Vtb_ooo_accumulator +INPUT=$(INPUT) +EXPECTED=$(EXPECTED)

csr-vectors: $(CSR_GEN)
	$(CSR_GEN) \
	  --matrix $(CSR_MATRIX) \
	  --entries $(ENTRIES) \
	  --scale $(CSR_SCALE) \
	  $(if $(filter 1,$(CSR_DROP_DIAGONAL)),--drop-diagonal,) \
	  --input $(CSR_INPUT) \
	  --expected $(CSR_EXPECTED) \
	  --meta $(CSR_META)

csr-sim: csr-vectors
	. $(CSR_META); \
	$(VERILATOR) -cc --exe --build --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME \
	  --top-module ooo_accumulator \
	  -GROW_COUNT=$$CSR_ROWS \
	  -GROW_WIDTH=$$CSR_ROW_WIDTH \
	  -GENTRY_NUM=$(ENTRIES) \
	  -Mdir $(CSR_OBJ_DIR) \
	  vsrc/ooo_accumulator.sv csrc/sim_ooo_accumulator.cpp
	. $(CSR_META); \
	$(CSR_OBJ_DIR)/Vooo_accumulator \
	  --input $(CSR_INPUT) \
	  --expected $(CSR_EXPECTED) \
	  --timeout-cycles $(TIMEOUT) \
	  --row-count $$CSR_ROWS \
	  --hbm-channels $(HBM_CHANNELS) \
	  --entries $(ENTRIES) \
	  --freq-mhz $(FREQ_MHZ)

backend-xsim-vectors: $(BACKEND_XSIM_GEN)
	mkdir -p $(BACKEND_XSIM_DIR)
	$(BACKEND_XSIM_GEN) \
	  --matrix $(CSR_MATRIX) \
	  --out-dir $(BACKEND_XSIM_DIR) \
	  --iteration-num 1 \
	  $(if $(filter 1,$(BACKEND_XSIM_SOURCE_ORDER)),--source-order,)

top-xsim-vectors: $(TOP_XSIM_GEN)
	mkdir -p $(TOP_XSIM_DIR)
	$(TOP_XSIM_GEN) \
	  --matrix $(CSR_MATRIX) \
	  --out-dir $(TOP_XSIM_DIR) \
	  --iteration-num 1

tapa-lint:
	$(VERILATOR) --lint-only -Wall -Wno-fatal -Wno-UNUSED -Wno-WIDTH \
	  tapa/CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo.v

tapa-sim: | $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/tapa_obj
	$(VERILATOR) --binary --timing -Wno-fatal -Wno-WIDTH -Wno-DECLFILENAME \
	  --top-module tb_tapa_owner_lane_accumulator \
	  -Mdir $(BUILD_DIR)/tapa_obj \
	  tapa/CuperSpmvOnly_RtlOwnerLaneAccumulatorOoo.v \
	  vsrc/tb_tapa_owner_lane_accumulator.sv
	$(BUILD_DIR)/tapa_obj/Vtb_tapa_owner_lane_accumulator

tapa-bank-lint:
	$(VERILATOR) --lint-only -Wall -Wno-fatal -Wno-UNUSED -Wno-WIDTH \
	  -Itapa \
	  tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v

tapa-bank-sim: | $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/tapa_bank_obj
	$(VERILATOR) --binary --timing -Wno-fatal -Wno-WIDTH -Wno-DECLFILENAME \
	  -Itapa \
	  --top-module tb_tapa_owner_bank_accumulator \
	  -Mdir $(BUILD_DIR)/tapa_bank_obj \
	  tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v \
	  vsrc/tb_tapa_owner_bank_accumulator.sv
	$(BUILD_DIR)/tapa_bank_obj/Vtb_tapa_owner_bank_accumulator

tapa-vector-scoreboard-sim: | $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/vector_scoreboard8_obj
	$(VERILATOR) --binary --timing -Wno-fatal -Wno-WIDTH -Wno-DECLFILENAME \
	  -Itapa \
	  --top-module tb_tapa_vector_issue_scoreboard8 \
	  -Mdir $(BUILD_DIR)/vector_scoreboard8_obj \
	  tapa/CuperSpmvOnly_RtlIssueScoreboard8.v \
	  vsrc/tb_tapa_vector_issue_scoreboard8.sv
	$(BUILD_DIR)/vector_scoreboard8_obj/Vtb_tapa_vector_issue_scoreboard8

tapa-owner-scoreboard-sim: | $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/owner_scoreboard_obj
	$(VERILATOR) --binary --timing -Wno-fatal -Wno-WIDTH -Wno-DECLFILENAME \
	  -Itapa \
	  --top-module tb_tapa_owner_scoreboard_ooo \
	  -Mdir $(BUILD_DIR)/owner_scoreboard_obj \
	  tapa/CuperSpmvOnly_RtlIssueScoreboard8.v \
	  tapa/CuperSpmvOnly_RtlOwnerScoreboardOoo.v \
	  vsrc/tb_tapa_owner_scoreboard_ooo.sv
	$(BUILD_DIR)/owner_scoreboard_obj/Vtb_tapa_owner_scoreboard_ooo

tapa-scoreboard-dataset-cpp-sim: | $(BUILD_DIR)
	mkdir -p $(SCOREBOARD_DATASET_OBJ_DIR)/scoreboard
	$(VERILATOR) -cc -Wno-fatal -Wno-WIDTH -Wno-DECLFILENAME \
	  -Itapa \
	  --top-module CuperSpmvOnly_RtlIssueScoreboard8 \
	  -GADDR_WIDTH=13 \
	  -GSCOREBOARD_DEPTH=$(SCOREBOARD_DEPTH) \
	  -Mdir $(SCOREBOARD_DATASET_OBJ_DIR)/scoreboard \
	  tapa/CuperSpmvOnly_RtlIssueScoreboard8.v
	$(MAKE) -C $(SCOREBOARD_DATASET_OBJ_DIR)/scoreboard \
	  -f VCuperSpmvOnly_RtlIssueScoreboard8.mk \
	  VCuperSpmvOnly_RtlIssueScoreboard8__ALL.a verilated.o verilated_threads.o
	$(CXX) -std=c++17 -O2 -Wall -Wextra \
	  -I$(SCOREBOARD_DATASET_OBJ_DIR)/scoreboard \
	  -I/usr/local/share/verilator/include \
	  -I/usr/local/share/verilator/include/vltstd \
	  -DVERILATOR=1 \
	  csrc/sim_tapa_scoreboard_dataset.cpp \
	  $(SCOREBOARD_DATASET_OBJ_DIR)/scoreboard/VCuperSpmvOnly_RtlIssueScoreboard8__ALL.a \
	  $(SCOREBOARD_DATASET_OBJ_DIR)/scoreboard/verilated.o \
	  $(SCOREBOARD_DATASET_OBJ_DIR)/scoreboard/verilated_threads.o \
	  -pthread -lpthread -latomic \
	  -o $(SCOREBOARD_DATASET_OBJ_DIR)/sim_tapa_scoreboard_dataset
	$(SCOREBOARD_DATASET_OBJ_DIR)/sim_tapa_scoreboard_dataset \
	  --matrix $(CSR_MATRIX) \
	  --owner $(OWNER) \
	  --trace $(TRACE) \
	  $(if $(FIFO_DEPTH),--fifo-depth $(FIFO_DEPTH),) \
	  $(if $(CSV),--csv $(CSV),) \
	  --timeout-cycles $(TIMEOUT)

tapa-bank-scatter-cpp-sim: | $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/tapa_bank_scatter_cpp_obj
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  $(VERILATOR_FP32_DEFINE) \
	  -Itapa \
	  --top-module CuperSpmvOnly_RtlOwnerBankAccumulatorOoo \
	  -Mdir $(BUILD_DIR)/tapa_bank_scatter_cpp_obj/bank \
	  tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v
	$(MAKE) -C $(BUILD_DIR)/tapa_bank_scatter_cpp_obj/bank \
	  -f VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo.mk \
	  VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo__ALL.a verilated.o verilated_threads.o
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  -Itapa \
	  --top-module CuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel \
	  -Mdir $(BUILD_DIR)/tapa_bank_scatter_cpp_obj/scatter \
	  tapa/CuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel.v
	$(MAKE) -C $(BUILD_DIR)/tapa_bank_scatter_cpp_obj/scatter \
	  -f VCuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel.mk \
	  VCuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel__ALL.a
	$(CXX) -std=c++17 -O2 -Wall -Wextra \
	  -I$(BUILD_DIR)/tapa_bank_scatter_cpp_obj/bank \
	  -I$(BUILD_DIR)/tapa_bank_scatter_cpp_obj/scatter \
	  -I/usr/local/share/verilator/include \
	  -I/usr/local/share/verilator/include/vltstd \
	  -DVERILATOR=1 \
	  csrc/sim_tapa_bank_to_scatter.cpp \
	  $(VERILATOR_FP32_DPI) \
	  $(BUILD_DIR)/tapa_bank_scatter_cpp_obj/bank/VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo__ALL.a \
	  $(BUILD_DIR)/tapa_bank_scatter_cpp_obj/scatter/VCuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel__ALL.a \
	  $(BUILD_DIR)/tapa_bank_scatter_cpp_obj/bank/verilated.o \
	  $(BUILD_DIR)/tapa_bank_scatter_cpp_obj/bank/verilated_threads.o \
	  -pthread -lpthread -latomic \
	  -o $(BUILD_DIR)/tapa_bank_scatter_cpp_obj/sim_tapa_bank_to_scatter
	$(BUILD_DIR)/tapa_bank_scatter_cpp_obj/sim_tapa_bank_to_scatter

tapa-splitter-bank-scatter-cpp-sim: | $(BUILD_DIR)
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo_CuperSpmvOnly_SourceLaneSplitterOoo_Pipeline_split_packets.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo_flow_control_loop_pipe_sequential_init.v"
	mkdir -p $(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  -I$(TAPA_RTL_DIR) \
	  --top-module CuperSpmvOnly_SourceLaneSplitterOoo \
	  -Mdir $(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj/splitter \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo_flow_control_loop_pipe_sequential_init.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo_CuperSpmvOnly_SourceLaneSplitterOoo_Pipeline_split_packets.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo.v
	$(MAKE) -C $(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj/splitter \
	  -f VCuperSpmvOnly_SourceLaneSplitterOoo.mk \
	  VCuperSpmvOnly_SourceLaneSplitterOoo__ALL.a verilated.o verilated_threads.o verilated_timing.o
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  $(VERILATOR_FP32_DEFINE) \
	  -Itapa \
	  --top-module CuperSpmvOnly_RtlOwnerBankAccumulatorOoo \
	  -Mdir $(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj/bank \
	  tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v
	$(MAKE) -C $(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj/bank \
	  -f VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo.mk \
	  VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo__ALL.a
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  -Itapa \
	  --top-module CuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel \
	  -Mdir $(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj/scatter \
	  tapa/CuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel.v
	$(MAKE) -C $(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj/scatter \
	  -f VCuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel.mk \
	  VCuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel__ALL.a
	$(CXX) -std=c++17 -O2 -Wall -Wextra \
	  -I$(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj/splitter \
	  -I$(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj/bank \
	  -I$(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj/scatter \
	  -I/usr/local/share/verilator/include \
	  -I/usr/local/share/verilator/include/vltstd \
	  -DVERILATOR=1 \
	  csrc/sim_tapa_splitter_bank_scatter.cpp \
	  $(VERILATOR_FP32_DPI) \
	  $(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj/splitter/VCuperSpmvOnly_SourceLaneSplitterOoo__ALL.a \
	  $(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj/bank/VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo__ALL.a \
	  $(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj/scatter/VCuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel__ALL.a \
	  $(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj/splitter/verilated.o \
	  $(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj/splitter/verilated_threads.o \
	  $(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj/splitter/verilated_timing.o \
	  -pthread -lpthread -latomic \
	  -o $(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj/sim_tapa_splitter_bank_scatter
	$(BUILD_DIR)/tapa_splitter_bank_scatter_cpp_obj/sim_tapa_splitter_bank_scatter

tapa-splitter-bank-generated-scatter-cpp-sim: | $(BUILD_DIR)
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo_CuperSpmvOnly_SourceLaneSplitterOoo_Pipeline_split_packets.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo_flow_control_loop_pipe_sequential_init.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_flow_control_loop_pipe_sequential_init.v"
	mkdir -p $(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  -I$(TAPA_RTL_DIR) \
	  --top-module CuperSpmvOnly_SourceLaneSplitterOoo \
	  -Mdir $(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj/splitter \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo_flow_control_loop_pipe_sequential_init.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo_CuperSpmvOnly_SourceLaneSplitterOoo_Pipeline_split_packets.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo.v
	$(MAKE) -C $(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj/splitter \
	  -f VCuperSpmvOnly_SourceLaneSplitterOoo.mk \
	  VCuperSpmvOnly_SourceLaneSplitterOoo__ALL.a verilated.o verilated_threads.o verilated_timing.o
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  $(VERILATOR_FP32_DEFINE) \
	  -Itapa \
	  --top-module CuperSpmvOnly_RtlOwnerBankAccumulatorOoo \
	  -Mdir $(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj/bank \
	  tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v
	$(MAKE) -C $(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj/bank \
	  -f VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo.mk \
	  VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo__ALL.a
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  -I$(TAPA_RTL_DIR) \
	  --top-module CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter \
	  -Mdir $(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj/scatter \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_flow_control_loop_pipe_sequential_init.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.v
	$(MAKE) -C $(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj/scatter \
	  -f VCuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.mk \
	  VCuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter__ALL.a
	$(CXX) -std=c++17 -O2 -Wall -Wextra \
	  -I$(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj/splitter \
	  -I$(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj/bank \
	  -I$(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj/scatter \
	  -I/usr/local/share/verilator/include \
	  -I/usr/local/share/verilator/include/vltstd \
	  -DVERILATOR=1 -DUSE_TAPA_GENERATED_SCATTER=1 \
	  csrc/sim_tapa_splitter_bank_scatter.cpp \
	  $(VERILATOR_FP32_DPI) \
	  $(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj/splitter/VCuperSpmvOnly_SourceLaneSplitterOoo__ALL.a \
	  $(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj/bank/VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo__ALL.a \
	  $(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj/scatter/VCuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter__ALL.a \
	  $(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj/splitter/verilated.o \
	  $(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj/splitter/verilated_threads.o \
	  $(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj/splitter/verilated_timing.o \
	  -pthread -lpthread -latomic \
	  -o $(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj/sim_tapa_splitter_bank_generated_scatter
	$(BUILD_DIR)/tapa_splitter_bank_generated_scatter_cpp_obj/sim_tapa_splitter_bank_generated_scatter

tapa-core-splitter-bank-scatter-cpp-sim: | $(BUILD_DIR)
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip_flow_control_loop_pipe_sequential_init.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip_CuperSpmvOnly_CoreStrip_Pipeline_read_boundary_group.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip_CuperSpmvOnly_CoreStrip_Pipeline_read_boundary_group1.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip_CuperSpmvOnly_CoreStrip_Pipeline_load_vector.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip_CuperSpmvOnly_CoreStrip_Pipeline_decode_matrix.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip_local_X_RAM_AUTO_1R1W.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip_mux_83_32_1_1.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo_CuperSpmvOnly_SourceLaneSplitterOoo_Pipeline_split_packets.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo_flow_control_loop_pipe_sequential_init.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_flow_control_loop_pipe_sequential_init.v"
	mkdir -p $(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  -Itapa -I$(TAPA_RTL_DIR) \
	  --top-module CuperSpmvOnly_CoreStrip \
	  -Mdir $(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/core \
	  tapa/CuperSpmvOnly_CoreStrip_fmul_32ns_32ns_32_8_max_dsp_1_ip_sim.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip_flow_control_loop_pipe_sequential_init.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip_mux_83_32_1_1.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip_local_X_RAM_AUTO_1R1W.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip_fmul_32ns_32ns_32_8_max_dsp_1.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip_CuperSpmvOnly_CoreStrip_Pipeline_read_boundary_group.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip_CuperSpmvOnly_CoreStrip_Pipeline_read_boundary_group1.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip_CuperSpmvOnly_CoreStrip_Pipeline_load_vector.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip_CuperSpmvOnly_CoreStrip_Pipeline_decode_matrix.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip.v
	$(MAKE) -C $(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/core \
	  -f VCuperSpmvOnly_CoreStrip.mk \
	  VCuperSpmvOnly_CoreStrip__ALL.a verilated.o verilated_threads.o verilated_timing.o
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  -I$(TAPA_RTL_DIR) \
	  --top-module CuperSpmvOnly_SourceLaneSplitterOoo \
	  -Mdir $(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/splitter \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo_flow_control_loop_pipe_sequential_init.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo_CuperSpmvOnly_SourceLaneSplitterOoo_Pipeline_split_packets.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo.v
	$(MAKE) -C $(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/splitter \
	  -f VCuperSpmvOnly_SourceLaneSplitterOoo.mk \
	  VCuperSpmvOnly_SourceLaneSplitterOoo__ALL.a
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  $(VERILATOR_FP32_DEFINE) \
	  -Itapa \
	  --top-module CuperSpmvOnly_RtlOwnerBankAccumulatorOoo \
	  -Mdir $(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/bank \
	  tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v
	$(MAKE) -C $(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/bank \
	  -f VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo.mk \
	  VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo__ALL.a
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  -I$(TAPA_RTL_DIR) \
	  --top-module CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter \
	  -Mdir $(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/scatter \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_flow_control_loop_pipe_sequential_init.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.v
	$(MAKE) -C $(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/scatter \
	  -f VCuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.mk \
	  VCuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter__ALL.a
	$(CXX) -std=c++17 -O2 -Wall -Wextra \
	  -I$(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/core \
	  -I$(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/splitter \
	  -I$(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/bank \
	  -I$(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/scatter \
	  -I/usr/local/share/verilator/include \
	  -I/usr/local/share/verilator/include/vltstd \
	  -DVERILATOR=1 \
	  csrc/sim_tapa_core_splitter_bank_scatter.cpp \
	  $(VERILATOR_FP32_DPI) \
	  $(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/core/VCuperSpmvOnly_CoreStrip__ALL.a \
	  $(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/splitter/VCuperSpmvOnly_SourceLaneSplitterOoo__ALL.a \
	  $(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/bank/VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo__ALL.a \
	  $(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/scatter/VCuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter__ALL.a \
	  $(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/core/verilated.o \
	  $(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/core/verilated_threads.o \
	  $(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/core/verilated_timing.o \
	  -pthread -lpthread -latomic \
	  -o $(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/sim_tapa_core_splitter_bank_scatter
	$(BUILD_DIR)/tapa_core_splitter_bank_scatter_cpp_obj/sim_tapa_core_splitter_bank_scatter

tapa-splitter16-bank16-dataset-cpp-sim: | $(BUILD_DIR)
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo_CuperSpmvOnly_SourceLaneSplitterOoo_Pipeline_split_packets.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo_flow_control_loop_pipe_sequential_init.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_flow_control_loop_pipe_sequential_init.v"
	mkdir -p $(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  -I$(TAPA_RTL_DIR) \
	  --top-module CuperSpmvOnly_SourceLaneSplitterOoo \
	  -Mdir $(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj/splitter \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo_flow_control_loop_pipe_sequential_init.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo_CuperSpmvOnly_SourceLaneSplitterOoo_Pipeline_split_packets.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_SourceLaneSplitterOoo.v
	$(MAKE) -C $(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj/splitter \
	  -f VCuperSpmvOnly_SourceLaneSplitterOoo.mk \
	  VCuperSpmvOnly_SourceLaneSplitterOoo__ALL.a verilated.o verilated_threads.o verilated_timing.o
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  $(VERILATOR_FP32_DEFINE) \
	  -Itapa \
	  --top-module CuperSpmvOnly_RtlOwnerBankAccumulatorOoo \
	  -Mdir $(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj/bank \
	  tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v
	$(MAKE) -C $(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj/bank \
	  -f VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo.mk \
	  VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo__ALL.a
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  -I$(TAPA_RTL_DIR) \
	  --top-module CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter \
	  -Mdir $(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj/scatter \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_flow_control_loop_pipe_sequential_init.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.v
	$(MAKE) -C $(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj/scatter \
	  -f VCuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.mk \
	  VCuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter__ALL.a
	$(CXX) -std=c++17 -O2 -Wall -Wextra \
	  -I$(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj/splitter \
	  -I$(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj/bank \
	  -I$(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj/scatter \
	  -I/usr/local/share/verilator/include \
	  -I/usr/local/share/verilator/include/vltstd \
	  -DVERILATOR=1 \
	  csrc/sim_tapa_splitter16_bank16_dataset.cpp \
	  $(VERILATOR_FP32_DPI) \
	  $(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj/splitter/VCuperSpmvOnly_SourceLaneSplitterOoo__ALL.a \
	  $(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj/bank/VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo__ALL.a \
	  $(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj/scatter/VCuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter__ALL.a \
	  $(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj/splitter/verilated.o \
	  $(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj/splitter/verilated_threads.o \
	  $(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj/splitter/verilated_timing.o \
	  -pthread -lpthread -latomic \
	  -o $(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj/sim_tapa_splitter16_bank16_dataset
	$(BUILD_DIR)/tapa_splitter16_bank16_dataset_cpp_obj/sim_tapa_splitter16_bank16_dataset \
	  --matrix $(CSR_MATRIX) \
	  --timeout-cycles $(TIMEOUT)

tapa-ownerbank8-dataset-cpp-sim: | $(BUILD_DIR)
	test -f "$(TAPA_RTL_DIR_8)/CuperSpmvOnly_SourceLaneSplitterOoo.v"
	test -f "$(TAPA_RTL_DIR_8)/CuperSpmvOnly_SourceLaneSplitterOoo_CuperSpmvOnly_SourceLaneSplitterOoo_Pipeline_split_packets.v"
	test -f "$(TAPA_RTL_DIR_8)/CuperSpmvOnly_SourceLaneSplitterOoo_flow_control_loop_pipe_sequential_init.v"
	test -f "$(TAPA_RTL_DIR_8)/$(TAPA_SCATTER8_TOP).v"
	test -f "$(TAPA_RTL_DIR_8)/$(TAPA_SCATTER8_BASE)_flow_control_loop_pipe_sequential_init.v"
	mkdir -p $(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  -I$(TAPA_RTL_DIR_8) \
	  --top-module CuperSpmvOnly_SourceLaneSplitterOoo \
	  -Mdir $(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/splitter \
	  $(TAPA_RTL_DIR_8)/CuperSpmvOnly_SourceLaneSplitterOoo_flow_control_loop_pipe_sequential_init.v \
	  $(TAPA_RTL_DIR_8)/CuperSpmvOnly_SourceLaneSplitterOoo_CuperSpmvOnly_SourceLaneSplitterOoo_Pipeline_split_packets.v \
	  $(TAPA_RTL_DIR_8)/CuperSpmvOnly_SourceLaneSplitterOoo.v
	$(MAKE) -C $(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/splitter \
	  -f VCuperSpmvOnly_SourceLaneSplitterOoo.mk \
	  VCuperSpmvOnly_SourceLaneSplitterOoo__ALL.a verilated.o verilated_threads.o verilated_timing.o
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  $(VERILATOR_FP32_DEFINE) \
	  -Itapa \
	  --top-module CuperSpmvOnly_RtlOwnerBankAccumulatorOoo \
	  -GHBM_CHANNEL_NUM=8 \
	  -Mdir $(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/bank \
	  tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v
	$(MAKE) -C $(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/bank \
	  -f VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo.mk \
	  VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo__ALL.a
	$(VERILATOR) -cc --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  -I$(TAPA_RTL_DIR_8) \
	  --top-module $(TAPA_SCATTER8_TOP) \
	  --prefix $(TAPA_SCATTER8_PREFIX) \
	  -Mdir $(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/scatter \
	  $(TAPA_RTL_DIR_8)/$(TAPA_SCATTER8_BASE)_flow_control_loop_pipe_sequential_init.v \
	  $(TAPA_RTL_DIR_8)/$(TAPA_SCATTER8_TOP).v
	$(MAKE) -C $(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/scatter \
	  -f $(TAPA_SCATTER8_PREFIX).mk \
	  $(TAPA_SCATTER8_PREFIX)__ALL.a
	$(CXX) -std=c++17 -O2 -Wall -Wextra \
	  -I$(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/splitter \
	  -I$(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/bank \
	  -I$(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/scatter \
	  -I/usr/local/share/verilator/include \
	  -I/usr/local/share/verilator/include/vltstd \
	  -DVERILATOR=1 -DCUPER_HBM_CHANNELS=8 -DCUPER_SCATTER_HAS_PROGRESS=1 \
	  csrc/sim_tapa_splitter16_bank16_dataset.cpp \
	  $(VERILATOR_FP32_DPI) \
	  $(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/splitter/VCuperSpmvOnly_SourceLaneSplitterOoo__ALL.a \
	  $(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/bank/VCuperSpmvOnly_RtlOwnerBankAccumulatorOoo__ALL.a \
	  $(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/scatter/$(TAPA_SCATTER8_PREFIX)__ALL.a \
	  $(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/splitter/verilated.o \
	  $(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/splitter/verilated_threads.o \
	  $(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/splitter/verilated_timing.o \
	  -pthread -lpthread -latomic \
	  -o $(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/sim_tapa_ownerbank8_dataset
	$(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/sim_tapa_ownerbank8_dataset \
	  --matrix ../data/suitesparse/Schmid/csr/thermal2_n16 \
	  --timeout-cycles $(DATASET_TIMEOUT)
	$(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/sim_tapa_ownerbank8_dataset \
	  --matrix ../data/suitesparse/Schmid/csr/thermal2_n16 \
	  --timeout-cycles $(DATASET_TIMEOUT) \
	  --fifo-depth 4 --write-response-delay 7 \
	  --output-stall-period 19 --output-stall-cycles 5
	$(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/sim_tapa_ownerbank8_dataset \
	  --matrix ../data/suitesparse/Schmid/csr/thermal2_n1024 \
	  --timeout-cycles $(DATASET_TIMEOUT)
	$(BUILD_DIR)/tapa_ownerbank8_dataset_cpp_obj/sim_tapa_ownerbank8_dataset \
	  --matrix ../data/suitesparse/Schmid/csr/thermal2_n1024 \
	  --timeout-cycles $(DATASET_TIMEOUT) \
	  --fifo-depth 16 --write-response-delay 11 \
	  --output-stall-period 31 --output-stall-cycles 3

tapa-scatter-only-cpp-sim: | $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/tapa_scatter_cpp_obj
	$(VERILATOR) -cc --exe --build --timing -Wno-fatal -Wno-WIDTH \
	  -Wno-DECLFILENAME -Wno-SHORTREAL \
	  -Itapa \
	  --top-module CuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel \
	  -Mdir $(BUILD_DIR)/tapa_scatter_cpp_obj \
	  tapa/CuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel.v \
	  csrc/sim_tapa_scatter_pipeline.cpp
	$(BUILD_DIR)/tapa_scatter_cpp_obj/VCuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel

tapa-scatter-only-sim: | $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/tapa_scatter_only_obj
	$(VERILATOR) --binary --timing -Wno-fatal -Wno-WIDTH -Wno-DECLFILENAME \
	  -Wno-SHORTREAL \
	  -Itapa \
	  --top-module tb_tapa_scatter_pipeline \
	  -Mdir $(BUILD_DIR)/tapa_scatter_only_obj \
	  tapa/CuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel.v \
	  vsrc/tb_tapa_scatter_pipeline.sv
	$(BUILD_DIR)/tapa_scatter_only_obj/Vtb_tapa_scatter_pipeline

tapa-scatter-only-rtl-sim: | $(BUILD_DIR)
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_flow_control_loop_pipe_sequential_init.v"
	mkdir -p $(BUILD_DIR)/tapa_scatter_only_obj
	$(VERILATOR) --binary --timing -Wno-fatal -Wno-WIDTH -Wno-DECLFILENAME \
	  -Wno-SHORTREAL \
	  -DTAPA_SCATTER_MODULE=CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter \
	  -Itapa -I$(TAPA_RTL_DIR) \
	  --top-module tb_tapa_scatter_pipeline \
	  -Mdir $(BUILD_DIR)/tapa_scatter_only_obj \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_flow_control_loop_pipe_sequential_init.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.v \
	  vsrc/tb_tapa_scatter_pipeline.sv
	$(BUILD_DIR)/tapa_scatter_only_obj/Vtb_tapa_scatter_pipeline

tapa-scatter-sim: | $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/tapa_scatter_obj
	$(VERILATOR) --binary --timing -Wno-fatal -Wno-WIDTH -Wno-DECLFILENAME \
	  -Wno-SHORTREAL \
	  -Itapa \
	  --top-module tb_tapa_owner_bank_to_scatter \
	  -Mdir $(BUILD_DIR)/tapa_scatter_obj \
	  tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v \
	  tapa/CuperSpmvOnly_TaggedScatterWriterOoo_PipelineScatterModel.v \
	  vsrc/tb_tapa_owner_bank_to_scatter.sv
	$(BUILD_DIR)/tapa_scatter_obj/Vtb_tapa_owner_bank_to_scatter

tapa-scatter-rtl-sim: | $(BUILD_DIR)
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_flow_control_loop_pipe_sequential_init.v"
	mkdir -p $(BUILD_DIR)/tapa_scatter_obj
	$(VERILATOR) --binary --timing -Wno-fatal -Wno-WIDTH -Wno-DECLFILENAME \
	  -Wno-SHORTREAL \
	  -DTAPA_SCATTER_MODULE=CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter \
	  -Itapa -I$(TAPA_RTL_DIR) \
	  --top-module tb_tapa_owner_bank_to_scatter \
	  -Mdir $(BUILD_DIR)/tapa_scatter_obj \
	  tapa/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_flow_control_loop_pipe_sequential_init.v \
	  $(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.v \
	  vsrc/tb_tapa_owner_bank_to_scatter.sv
	$(BUILD_DIR)/tapa_scatter_obj/Vtb_tapa_owner_bank_to_scatter

tapa-corestrip-xsim: | $(BUILD_DIR)
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_CoreStrip_fmul_32ns_32ns_32_8_max_dsp_1_ip.tcl"
	mkdir -p $(BUILD_DIR)/xsim_corestrip
	TAPA_RTL_DIR="$(abspath $(TAPA_RTL_DIR))" \
	XSIM_BUILD_DIR="$(abspath $(BUILD_DIR)/xsim_corestrip)" \
	VIVADO_PART="$(VIVADO_PART)" \
	$(VIVADO) -mode batch -source scripts/run_tapa_corestrip_xsim.tcl

tapa-backend-dataset-xsim: backend-xsim-vectors
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1_ip.tcl"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_RtlOwnerBankAccumulatorOoo_fadd_32ns_32ns_32_13_full_dsp_1.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvOnly_TaggedScatterWriterOoo_CuperSpmvOnly_TaggedScatterWriterOoo_Pipeline_scatter.v"
	mkdir -p $(BUILD_DIR)/xsim_backend_dataset
	. $(BACKEND_XSIM_DIR)/meta.env; \
	TAPA_RTL_DIR="$(abspath $(TAPA_RTL_DIR))" \
	XSIM_BUILD_DIR="$(abspath $(BUILD_DIR)/xsim_backend_dataset)" \
	XSIM_DATA_DIR="$(abspath $(BACKEND_XSIM_DIR))" \
	XSIM_ROWS="$$ROWS" \
	XSIM_ITERATION_NUM="$$ITERATION_NUM" \
	XSIM_TAGGED_PAIRS_TOTAL="$$TAGGED_PAIRS_TOTAL" \
	XSIM_SCALAR_WRITES_TOTAL="$$SCALAR_WRITES_TOTAL" \
	XSIM_TIMEOUT_CYCLES="$(or $(BACKEND_XSIM_TIMEOUT_CYCLES),300000)" \
	VIVADO_PART="$(VIVADO_PART)" \
	$(VIVADO) -mode batch -source scripts/run_tapa_backend_dataset_xsim.tcl

cuper-spmv-service-only-top-xsim: top-xsim-vectors
	test -f "$(TAPA_RTL_DIR)/CuperSpmvServiceOnly.v"
	test -f "$(TAPA_RTL_DIR)/CuperSpmvServiceOnly_fsm.v"
	mkdir -p $(BUILD_DIR)/xsim_top
	python3 csrc/gen_tapa_top_xsim_tb.py \
	  "$(TAPA_RTL_DIR)/CuperSpmvServiceOnly.v" \
	  "$(BUILD_DIR)/xsim_top/tb_cuper_spmv_service_only_top_xsim.sv"
	. $(TOP_XSIM_DIR)/meta.env; \
	TAPA_RTL_DIR="$(abspath $(TAPA_RTL_DIR))" \
	XSIM_BUILD_DIR="$(abspath $(BUILD_DIR)/xsim_top)" \
	XSIM_DATA_DIR="$(abspath $(TOP_XSIM_DIR))" \
	XSIM_ROWS="$$ROWS" \
	XSIM_COLS="$$COLS" \
	XSIM_BATCH_NUM="$$BATCH_NUM" \
	XSIM_MATRIX_LEN="$$MATRIX_LEN" \
	XSIM_ITERATION_NUM="$$ITERATION_NUM" \
	XSIM_TIMEOUT_CYCLES="$(or $(TOP_XSIM_TIMEOUT_CYCLES),300000)" \
	XSIM_AXI_ARREADY_DELAY="$(XSIM_AXI_ARREADY_DELAY)" \
	XSIM_AXI_AWREADY_DELAY="$(XSIM_AXI_AWREADY_DELAY)" \
	XSIM_AXI_RVALID_DELAY="$(XSIM_AXI_RVALID_DELAY)" \
	XSIM_AXI_R_STALL_PERIOD="$(XSIM_AXI_R_STALL_PERIOD)" \
	XSIM_AXI_R_STALL_CYCLES="$(XSIM_AXI_R_STALL_CYCLES)" \
	XSIM_AXI_W_STALL_PERIOD="$(XSIM_AXI_W_STALL_PERIOD)" \
	XSIM_AXI_W_STALL_CYCLES="$(XSIM_AXI_W_STALL_CYCLES)" \
	XSIM_AXI_BVALID_DELAY="$(XSIM_AXI_BVALID_DELAY)" \
	VIVADO_PART="$(VIVADO_PART)" \
	$(VIVADO) -mode batch -source scripts/run_cuper_spmv_service_only_top_xsim.tcl

clean:
	rm -rf $(BUILD_DIR)
