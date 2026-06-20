BUILD_DIR ?= build
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

CC ?= gcc
CXX ?= g++
VERILATOR ?= verilator

GEN := $(BUILD_DIR)/gen_ooo_accum_vectors
CSR_GEN := $(BUILD_DIR)/gen_csr_accum_vectors
INPUT := $(BUILD_DIR)/input.mem
EXPECTED := $(BUILD_DIR)/expected.mem
CONFIG := $(BUILD_DIR)/ooo_accum_config.vh
OBJ_DIR := $(BUILD_DIR)/obj
CSR_INPUT := $(BUILD_DIR)/csr_input.tsv
CSR_EXPECTED := $(BUILD_DIR)/csr_expected.tsv
CSR_META := $(BUILD_DIR)/csr_meta.env
CSR_OBJ_DIR := $(BUILD_DIR)/csr_obj

.PHONY: all vectors sim csr-vectors csr-sim tapa-lint tapa-sim tapa-bank-lint tapa-bank-sim clean

all: sim

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(GEN): csrc/gen_ooo_accum_vectors.c | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra $< -o $@

$(CSR_GEN): csrc/gen_csr_accum_vectors.c | $(BUILD_DIR)
	$(CC) -std=c11 -O2 -Wall -Wextra $< -lm -o $@

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

clean:
	rm -rf $(BUILD_DIR)
