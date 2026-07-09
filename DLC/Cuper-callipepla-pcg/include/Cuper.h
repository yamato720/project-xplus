#ifndef CUPER_CALLIPEPLA_PCG_CUPER_H
#define CUPER_CALLIPEPLA_PCG_CUPER_H

#include <ap_int.h>
#include <tapa.h>

#define VALUE_TYPE float
#define INDEX_TYPE int

#define PINGPONG
#define FLEX_REUSE

#ifndef CUPER_CALLIPEPLA_PROBE_MODE_ID
#define CUPER_CALLIPEPLA_PROBE_MODE_ID 0
#endif

#if CUPER_CALLIPEPLA_PROBE_MODE_ID > 0
#define CUPER_CALLIPEPLA_PROBE_ENABLED 1
#endif

#if defined(CUPER_CALLIPEPLA_TRACE_LIGHT) && defined(CUPER_CALLIPEPLA_PROBE_ENABLED)
#error "CUPER_CALLIPEPLA_TRACE_LIGHT and CUPER_CALLIPEPLA_PROBE_MODE are mutually exclusive."
#endif

#if defined(CUPER_CALLIPEPLA_TRACE_LIGHT)
#define CUPER_CALLIPEPLA_TRACE_ENABLED 1
#endif

#ifndef CUPER_CALLIPEPLA_PROBE_LOADER_LEVEL
#define CUPER_CALLIPEPLA_PROBE_LOADER_LEVEL 1
#endif

// The Callipepla-style PCG experiment keeps the proven Cuper strip16 SpMV
// datapath.  The host/build scripts map CUPER_CALLIPEPLA_* env vars to the
// existing JACOBI_* compile flags used by the copied strip16 service code.
constexpr INDEX_TYPE PE_NUM = 8;

#if defined(JACOBI_HBM_CHANNELS_32)
#define JACOBI_HBM_CHANNELS_GE_16 1
#define JACOBI_HBM_CHANNELS_GE_24 1
#define JACOBI_HBM_CHANNELS_GE_32 1
constexpr INDEX_TYPE HBM_CHANNEL_NUM = 32;
#elif defined(JACOBI_HBM_CHANNELS_24)
#define JACOBI_HBM_CHANNELS_GE_16 1
#define JACOBI_HBM_CHANNELS_GE_24 1
constexpr INDEX_TYPE HBM_CHANNEL_NUM = 24;
#elif defined(JACOBI_HBM_CHANNELS_8)
constexpr INDEX_TYPE HBM_CHANNEL_NUM = 8;
#else
#define JACOBI_HBM_CHANNELS_GE_16 1
constexpr INDEX_TYPE HBM_CHANNEL_NUM = 16;
#endif

static_assert(HBM_CHANNEL_NUM == 8 ||
              HBM_CHANNEL_NUM == 16 ||
              HBM_CHANNEL_NUM == 24 ||
              HBM_CHANNEL_NUM == 32,
              "Cuper Callipepla PCG supports 8/16/24/32 HBM service builds.");
static_assert((HBM_CHANNEL_NUM % 8) == 0,
              "HBM channel count must be divisible by 8 for checker grouping.");

constexpr INDEX_TYPE ROW_HBM_NUM = 4;
constexpr INDEX_TYPE Slice_SIZE = HBM_CHANNEL_NUM * ROW_HBM_NUM;
constexpr INDEX_TYPE BATCH_SIZE = 8192 / Slice_SIZE;
#ifndef JACOBI_SPMV_ACC_WINDOW
constexpr INDEX_TYPE WINDOWS = 10;
#else
constexpr INDEX_TYPE WINDOWS = JACOBI_SPMV_ACC_WINDOW;
#endif
static_assert(WINDOWS > 0, "CUPER_CALLIPEPLA_SPMV_ACC_WINDOW must be positive.");
constexpr INDEX_TYPE X_PARTITION_FACTOR = 8;
constexpr INDEX_TYPE URAM_DEPTH = (48 / HBM_CHANNEL_NUM) * 4096 / 2;
constexpr INDEX_TYPE FIFO_DEPTH = 2;
constexpr INDEX_TYPE X_BRAM_DEPTH = 4;
constexpr INDEX_TYPE X_TABLE_DEPTH = 200;
constexpr INDEX_TYPE ITERATION_NUM = 2;
constexpr INDEX_TYPE X_TABLE_ITERATION_NUM = 1;
constexpr double THRESHOLD = 1e-10;

const INDEX_TYPE HBM_CHANNEL_NUM_DIV_8 = HBM_CHANNEL_NUM >> 3;
const INDEX_TYPE HBM_CHANNEL_NUM_MULT_16 = HBM_CHANNEL_NUM << 4;
const INDEX_TYPE HBM_CHANNEL_NUM_MULT_2 = HBM_CHANNEL_NUM << 1;
const INDEX_TYPE Slice_WIDTH = Slice_SIZE * BATCH_SIZE;
const INDEX_TYPE Slice_WIDTH_DIV_16 = Slice_WIDTH >> 4;
constexpr INDEX_TYPE JACOBI_UPDATE_PAIR_NUM = 8;
constexpr INDEX_TYPE JACOBI_ACC_GROUP_SIZE = HBM_CHANNEL_NUM / JACOBI_UPDATE_PAIR_NUM;

inline INDEX_TYPE Cuper_NumFloatV16Packets(const INDEX_TYPE element_count) {
#pragma HLS inline
    return (element_count + 15) >> 4;
}

inline INDEX_TYPE Cuper_NumDoubleV8Packets(const INDEX_TYPE element_count) {
#pragma HLS inline
    return (element_count + 7) >> 3;
}

inline INDEX_TYPE Cuper_NumAccumulatorInitGroups(const INDEX_TYPE row_num) {
#pragma HLS inline
    return (row_num + HBM_CHANNEL_NUM_MULT_16 - 1) / HBM_CHANNEL_NUM_MULT_16;
}

inline INDEX_TYPE Cuper_NumAccumulatorOutputs(const INDEX_TYPE row_num) {
#pragma HLS inline
    return (row_num + HBM_CHANNEL_NUM_MULT_2 - 1) / HBM_CHANNEL_NUM_MULT_2;
}

inline INDEX_TYPE Cuper_NumCheckerPeOutputs(const INDEX_TYPE row_num) {
#pragma HLS inline
    return Cuper_NumAccumulatorOutputs(row_num) * HBM_CHANNEL_NUM_DIV_8;
}

using int_v2 = tapa::vec_t<INDEX_TYPE, 2>;
using float_v2 = tapa::vec_t<VALUE_TYPE, 2>;
using float_v4 = tapa::vec_t<VALUE_TYPE, 4>;
using float_v8 = tapa::vec_t<VALUE_TYPE, 8>;
using float_v16 = tapa::vec_t<VALUE_TYPE, 16>;
using double_v8 = tapa::vec_t<double, 8>;

void CuperPcgCallipepla(tapa::mmap<INDEX_TYPE> SpElement_list_ptr,
                        tapa::mmaps<ap_uint<512>, HBM_CHANNEL_NUM> Matrix_data,
                        tapa::mmap<double_v8> X_0,
                        tapa::mmap<double_v8> X_1,
                        tapa::mmap<double_v8> P_0,
                        tapa::mmap<double_v8> P_1,
                        tapa::mmap<float_v16> AP,
                        tapa::mmap<double_v8> R_0,
                        tapa::mmap<double_v8> R_1,
                        tapa::mmap<double_v8> M_inv,
                        tapa::mmap<double> Residuals,
                        tapa::mmap<INDEX_TYPE> Status,
                        tapa::mmap<double> Metrics,
                        const INDEX_TYPE Batch_num,
                        const INDEX_TYPE Matrix_len,
                        const INDEX_TYPE Row_num,
                        const INDEX_TYPE Column_num,
                        const INDEX_TYPE Max_iters,
                        const double Tau);

#endif
