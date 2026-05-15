#ifndef CUPER_H
#define CUPER_H

#include <ap_int.h>
#include <tapa.h>

#define VALUE_TYPE float
#define INDEX_TYPE int

// #define BINARY_READ

#define PINGPONG

#define FLEX_REUSE

//#define X_TABLE


constexpr INDEX_TYPE PE_NUM                 = 8;
constexpr INDEX_TYPE HBM_CHANNEL_NUM        = 16;
constexpr INDEX_TYPE ROW_HBM_NUM            = 4;
constexpr INDEX_TYPE Slice_SIZE             = HBM_CHANNEL_NUM * ROW_HBM_NUM;
constexpr INDEX_TYPE BATCH_SIZE             = 8192 / Slice_SIZE;
constexpr INDEX_TYPE WINDOWS                = 10;
constexpr INDEX_TYPE X_PARTITION_FACTOR     = 8;
constexpr INDEX_TYPE URAM_DEPTH             = (48 / HBM_CHANNEL_NUM) * 4096 / 2;
constexpr INDEX_TYPE FIFO_DEPTH             = 2;
constexpr INDEX_TYPE X_BRAM_DEPTH           = 4;
constexpr INDEX_TYPE X_TABLE_DEPTH          = 200;
constexpr INDEX_TYPE ITERATION_NUM          = 2;
constexpr INDEX_TYPE X_TABLE_ITERATION_NUM  = 1;
constexpr double     THRESHOLD              = 1e-10;

const     INDEX_TYPE HBM_CHANNEL_NUM_DIV_8    = HBM_CHANNEL_NUM >> 3;
const     INDEX_TYPE HBM_CHANNEL_NUM_MULT_16  = HBM_CHANNEL_NUM << 4;
const     INDEX_TYPE HBM_CHANNEL_NUM_MULT_2   = HBM_CHANNEL_NUM << 1;
const     INDEX_TYPE Slice_WIDTH            = Slice_SIZE * BATCH_SIZE;
const     INDEX_TYPE Slice_WIDTH_DIV_16     = Slice_WIDTH >> 4;

using int_v2    = tapa::vec_t<INDEX_TYPE, 2>;

using float_v2  = tapa::vec_t<VALUE_TYPE, 2>;
using float_v4  = tapa::vec_t<VALUE_TYPE, 4>;
using float_v8  = tapa::vec_t<VALUE_TYPE, 8>;
using float_v16 = tapa::vec_t<VALUE_TYPE, 16>;

//using row_v8    = tapa::vec_t<ap_uint<18>, 8>;

void Cuper(tapa::mmap<INDEX_TYPE> SpElement_list_ptr,
           tapa::mmaps<ap_uint<512>, HBM_CHANNEL_NUM> Matrix_data,
           tapa::mmap<float_v16> X,
           tapa::mmap<float_v16> Y_out,
          
           const INDEX_TYPE Batch_num,
           const INDEX_TYPE Matrix_len,
           const INDEX_TYPE Row_num,
           const INDEX_TYPE Column_num,
           const INDEX_TYPE Iteration_num
          );

#endif