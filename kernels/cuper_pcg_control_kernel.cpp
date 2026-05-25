#include "../include/cg_common.hpp"

#include <cstdint>

// no-TAPA Cuper 的主线 kernel 都在本文件中：
//
//   1. cuper_packed_spmv_kernel
//      只执行一次 y = A * x，用于 no-TAPA Cuper / single SpMV。
//
//   2. cuper_packed_spmv_4ch_kernel
//      实验入口：仍使用同一套 16-HBM 打包数据，但每次只并行 4 个
//      matrix channel，4 组顺序跑，用于验证降并发后是否能修复 Fmax。
//
//   3. cuper_pcg_control_kernel
//      在 FPGA 内执行完整 Jacobi-PCG，用于 no-TAPA Cuper / FPGA-PCG。
//
// 这些入口共用同一套 Cuper packed 16-HBM SpMV 数据格式，区别在于
// PCG 控制和向量更新是在 host 侧还是 kernel 侧。
#if __has_include(<ap_int.h>)
#include <ap_int.h>
#define PROJECT_XPLUS_HAS_AP_INT 1
#else
#define PROJECT_XPLUS_HAS_AP_INT 0
#endif

#if __has_include(<hls_stream.h>)
#include <hls_stream.h>
#else
#include <queue>

namespace hls {
template <typename T>
class stream {
public:
    void write(const T& value) {
        queue_.push(value);
    }

    T read() {
        const T value = queue_.front();
        queue_.pop();
        return value;
    }

private:
    std::queue<T> queue_;
};
}  // namespace hls
#endif

namespace {

using data_t = project_xplus::cgsolver::data_t;
using index_t = project_xplus::cgsolver::index_t;
using spmv_data_t = float;

// 这些常量必须和 host/cuper_control_matrix.hpp 中的打包规则一致。
// Cuper 矩阵按 8192 列切成 batch，再映射到 16 个 HBM channel；
// 每个 channel 内有 8 条 lane，对应原始 Cuper 设计中的 128 个物理 PE。
constexpr data_t kBreakdownEps = project_xplus::cgsolver::kBreakdownEps;
constexpr int kCuperSliceWidth = 8192;
constexpr int kCuperHbmChannelNum = 16;
constexpr int kCuperPePerHbm = 8;
constexpr int kCuperPeNum = kCuperHbmChannelNum * kCuperPePerHbm;
constexpr int kCuperLanesPerWord = 8;
constexpr int kCuperXBankNum = 4;
constexpr int kCuperUramDepth = 6144;
constexpr int kCuperMaxBatchCount = 4096;
constexpr int kDotPartialCount = 16;

#if PROJECT_XPLUS_HAS_AP_INT
using cuper_word_t = ap_uint<512>;
using cuper_matrix_ptr_t = const cuper_word_t*;

inline unsigned long read_cuper_matrix_lane(const cuper_word_t word, const int lane) {
#pragma HLS INLINE
    return static_cast<unsigned long>(word(63 + 64 * lane, 64 * lane));
}
#else
using cuper_word_t = unsigned long;
using cuper_matrix_ptr_t = const unsigned long*;

inline unsigned long read_cuper_matrix_lane(cuper_matrix_ptr_t matrix_data,
                                            const int index,
                                            const int lane) {
#pragma HLS INLINE
    return matrix_data[index * kCuperLanesPerWord + lane];
}
#endif

enum StatusCode {
    kStatusConverged = 0,
    kStatusMaxIter = 1,
    kStatusBreakdown = 2,
};

struct CuperOutputItem {
    int even_row;
    int odd_row;
    spmv_data_t even_value;
    spmv_data_t odd_value;
};

struct CuperBatchParam {
    int begin;
    int end;
    int slice_begin;
};

struct CuperXPacket {
    spmv_data_t values[16];
};

struct PcgInitResult {
    data_t rz[kDotPartialCount];
    data_t rr[kDotPartialCount];
};

inline data_t abs_value(const data_t value) {
    return value < 0.0 ? -value : value;
}

inline bool invalid_scalar(const data_t value) {
    return value != value;
}

// host 端为了复用 Cuper 的压缩格式，把矩阵值以 fp32 bit pattern
// 塞进 64-bit word 的低 32 bit。kernel 侧只在 SpMV 乘法处恢复成 float。
inline float bits_to_float(const std::uint32_t value) {
    union {
        unsigned int u;
        float f;
    } converter{};
    converter.u = static_cast<unsigned int>(value);
    return converter.f;
}

// packed_row 是 channel/lane 内的局部 row group 编号。
// 这里反解 host 端 cuper_physical_pe_for_row() 的映射，恢复成全局行号。
inline int restore_global_row(const int channel, const int lane, const int packed_row) {
    const int checker_id = channel / 2;
    const int acc_offset = channel & 1;
    const int pe_in_acc = lane;
    const int packet_id =
        ((packed_row >> 1) * kCuperPeNum) + checker_id + acc_offset * 8 + pe_in_acc * 16;
    return packet_id * 2 + (packed_row & 1);
}

void zero_vector(data_t* output, const int n) {
#pragma HLS INLINE off
zero_vector_loop:
    for (int index = 0; index < n; ++index) {
#pragma HLS PIPELINE II = 1
        output[index] = 0.0;
    }
}

void load_x_slice(const data_t* x,
                  data_t x_slice[kCuperSliceWidth],
                  const int slice_begin,
                  const int n) {
#pragma HLS INLINE off
    // 每个 batch 只需要当前 8192 列窗口内的 x。越界部分补 0，
    // 这样最后一个不满窗口的 batch 可以复用同一套流水。
load_x_slice_loop:
    for (int offset = 0; offset < kCuperSliceWidth; ++offset) {
#pragma HLS PIPELINE II = 1
        const int global_col = slice_begin + offset;
        x_slice[offset] = (global_col < n) ? x[global_col] : 0.0;
    }
}

void load_x_slice_stream(hls::stream<CuperXPacket>& x_stream,
                         spmv_data_t x_slice[kCuperXBankNum][kCuperSliceWidth]) {
#pragma HLS INLINE off
    // 原 TAPA Cuper 用 float_v16 流传 x。这里也按 16-lane packet
    // 传输 x slice，减少 stream token 数量和 loader 控制开销。
load_x_slice_packets:
    for (int packet_index = 0; packet_index < kCuperSliceWidth / 16; ++packet_index) {
#pragma HLS PIPELINE II = 1
        const CuperXPacket packet = x_stream.read();
    load_x_slice_lanes:
        for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
            const int offset = (packet_index << 4) + lane;
            const spmv_data_t value = packet.values[lane];
        load_x_slice_banks:
            for (int bank = 0; bank < kCuperXBankNum; ++bank) {
#pragma HLS UNROLL
                x_slice[bank][offset] = value;
            }
        }
    }
}

void broadcast_cuper_inputs(const index_t* sp_element_list_ptr,
                            const data_t* x,
                            const int batch_count,
                            const int n,
                            hls::stream<CuperBatchParam> batch_streams[kCuperHbmChannelNum],
                            hls::stream<CuperXPacket> x_streams[kCuperHbmChannelNum]) {
#pragma HLS INLINE off
    int begin = sp_element_list_ptr[0];

broadcast_batch_loop:
    for (int batch = 0; batch < batch_count; ++batch) {
#pragma HLS LOOP_TRIPCOUNT min = 1 max = 128
        const int end = sp_element_list_ptr[batch + 1];
        const int slice_begin = batch * kCuperSliceWidth;
        const CuperBatchParam param{begin, end, slice_begin};

    broadcast_param:
        for (int channel = 0; channel < kCuperHbmChannelNum; ++channel) {
#pragma HLS UNROLL
            batch_streams[channel].write(param);
        }

    broadcast_x_packets:
        for (int packet_index = 0; packet_index < kCuperSliceWidth / 16; ++packet_index) {
#pragma HLS PIPELINE II = 1
            CuperXPacket packet;
        fill_x_packet:
            for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
                const int global_col = slice_begin + (packet_index << 4) + lane;
                packet.values[lane] =
                    (global_col < n) ? static_cast<spmv_data_t>(x[global_col]) : 0.0f;
            }
        broadcast_x_channels:
            for (int channel = 0; channel < kCuperHbmChannelNum; ++channel) {
#pragma HLS UNROLL
                x_streams[channel].write(packet);
            }
        }

        begin = end;
    }
}

spmv_data_t decode_product(const unsigned long packed_word,
                           const int slice_begin,
                           const spmv_data_t x_slice[kCuperXBankNum][kCuperSliceWidth],
                           const int lane,
                           int& packed_row,
                           const int n) {
#pragma HLS INLINE
    // packed word layout:
    //   [63:50] local_col, [49:32] packed_row, [31:0] fp32 value bits.
    // row bit17 全 1 的 dummy row 用来填充 PE/channel 对齐空洞。
    const std::uint64_t word = static_cast<std::uint64_t>(packed_word);
    packed_row = static_cast<int>((word >> 32) & 0x3FFFFULL);
    if ((packed_row & (1 << 17)) != 0) {
        return 0.0f;
    }

    const int local_col = static_cast<int>((word >> 50) & 0x3FFFULL);
    const int global_col = slice_begin + local_col;
    if (global_col < 0 || global_col >= n) {
        return 0.0f;
    }

    const int x_bank = lane / (kCuperPePerHbm / kCuperXBankNum);
    return bits_to_float(static_cast<std::uint32_t>(word & 0xFFFFFFFFULL)) *
           x_slice[x_bank][local_col];
}

void process_cuper_channel_stream(cuper_matrix_ptr_t matrix_data,
                                  const int channel,
                                  const int batch_count,
                                  const int n,
                                  hls::stream<CuperBatchParam>& batch_stream,
                                  hls::stream<CuperXPacket>& x_stream,
                                  hls::stream<CuperOutputItem>& output_stream) {
#pragma HLS INLINE off
    // 每个 lane 只负责一部分固定行。active_depth 是该 lane 需要保存的
    // row group 数量，偶/奇行分两个 URAM 阵列避免同周期读写冲突。
    const int active_depth = (n + (2 * kCuperPeNum - 1)) / (2 * kCuperPeNum);

    spmv_data_t local_y_even[kCuperPePerHbm][kCuperUramDepth];
    spmv_data_t local_y_odd[kCuperPePerHbm][kCuperUramDepth];
#pragma HLS BIND_STORAGE variable = local_y_even type = ram_2p impl = uram
#pragma HLS BIND_STORAGE variable = local_y_odd type = ram_2p impl = uram
#pragma HLS ARRAY_PARTITION variable = local_y_even complete dim = 1
#pragma HLS ARRAY_PARTITION variable = local_y_odd complete dim = 1

    spmv_data_t x_slice[kCuperXBankNum][kCuperSliceWidth];
#pragma HLS BIND_STORAGE variable = x_slice type = ram_2p impl = bram
#pragma HLS ARRAY_PARTITION variable = x_slice complete dim = 1

zero_local_y:
    for (int index = 0; index < active_depth; ++index) {
#pragma HLS PIPELINE II = 1
    zero_local_lanes:
        for (int lane = 0; lane < kCuperPePerHbm; ++lane) {
#pragma HLS UNROLL
            local_y_even[lane][index] = 0.0f;
            local_y_odd[lane][index] = 0.0f;
        }
    }

batch_loop:
    for (int batch = 0; batch < batch_count; ++batch) {
#pragma HLS LOOP_TRIPCOUNT min = 1 max = 128
        const CuperBatchParam param = batch_stream.read();
        load_x_slice_stream(x_stream, x_slice);

process_cuper_channel_loop:
        for (int index = param.begin; index < param.end; ++index) {
#pragma HLS PIPELINE II = 1
#pragma HLS DEPENDENCE variable = local_y_even inter true distance = 10
#pragma HLS DEPENDENCE variable = local_y_odd inter true distance = 10
            // 一个 matrix_data channel 的同一 index 连续存 8 个 lane word。
            // lane 完全展开后，每拍并行处理 8 个候选非零元。
#if PROJECT_XPLUS_HAS_AP_INT
            // 硬件路径必须显式只读一次 512-bit word，再拆 8 个 64-bit lane。
            // 如果在展开的 lane 循环里各自读 matrix_data[index]，HLS 可能会
            // 生成 8 个同地址读请求或把循环 II 拉高，吞吐会和 TAPA Cuper 差很多。
            const cuper_word_t matrix_word = matrix_data[index];
#endif
    process_lanes:
            for (int lane = 0; lane < kCuperPePerHbm; ++lane) {
#pragma HLS UNROLL
                int packed_row = 0;
                const spmv_data_t product =
#if PROJECT_XPLUS_HAS_AP_INT
                    decode_product(read_cuper_matrix_lane(matrix_word, lane),
                                   param.slice_begin,
                                   x_slice,
                                   lane,
                                   packed_row,
                                   n);
#else
                    decode_product(read_cuper_matrix_lane(matrix_data, index, lane),
                                   param.slice_begin,
                                   x_slice,
                                   lane,
                                   packed_row,
                                   n);
#endif
                if ((packed_row & (1 << 17)) == 0) {
                    const int row_group = packed_row >> 1;
                    if (row_group >= 0 && row_group < active_depth) {
                        if ((packed_row & 1) == 0) {
                            local_y_even[lane][row_group] += product;
                        } else {
                            local_y_odd[lane][row_group] += product;
                        }
                    }
                }
            }
        }
    }

flush_local_y:
    for (int row_group = 0; row_group < active_depth; ++row_group) {
    flush_lanes:
        for (int lane = 0; lane < kCuperPePerHbm; ++lane) {
#pragma HLS PIPELINE II = 1
            CuperOutputItem item{};
            item.even_row = restore_global_row(channel, lane, row_group << 1);
            item.odd_row = item.even_row + 1;
            item.even_value = local_y_even[lane][row_group];
            item.odd_value = local_y_odd[lane][row_group];
            output_stream.write(item);
        }
    }
}

template <bool InitializeVectors>
void write_cuper_output_item(data_t* y,
                             const data_t* b,
                             const data_t* m_inv,
                             data_t* r,
                             data_t* z,
                             data_t* p,
                             const CuperOutputItem& item,
                             const int n,
                             const int partial_bank,
                             PcgInitResult& init_result) {
#pragma HLS INLINE
    if (item.even_row < n) {
        const data_t y_value = static_cast<data_t>(item.even_value);
        y[item.even_row] = y_value;
        if (InitializeVectors) {
            const data_t r_value = b[item.even_row] - y_value;
            const data_t z_value = m_inv[item.even_row] * r_value;
            r[item.even_row] = r_value;
            z[item.even_row] = z_value;
            p[item.even_row] = z_value;
            init_result.rz[partial_bank] += r_value * z_value;
            init_result.rr[partial_bank] += r_value * r_value;
        }
    }
    if (item.odd_row < n) {
        const data_t y_value = static_cast<data_t>(item.odd_value);
        y[item.odd_row] = y_value;
        if (InitializeVectors) {
            const data_t r_value = b[item.odd_row] - y_value;
            const data_t z_value = m_inv[item.odd_row] * r_value;
            r[item.odd_row] = r_value;
            z[item.odd_row] = z_value;
            p[item.odd_row] = z_value;
            init_result.rz[partial_bank] += r_value * z_value;
            init_result.rr[partial_bank] += r_value * r_value;
        }
    }
}

CuperOutputItem read_cuper_output_item(hls::stream<CuperOutputItem>& output_stream_0,
                                       hls::stream<CuperOutputItem>& output_stream_1,
                                       hls::stream<CuperOutputItem>& output_stream_2,
                                       hls::stream<CuperOutputItem>& output_stream_3,
                                       hls::stream<CuperOutputItem>& output_stream_4,
                                       hls::stream<CuperOutputItem>& output_stream_5,
                                       hls::stream<CuperOutputItem>& output_stream_6,
                                       hls::stream<CuperOutputItem>& output_stream_7,
                                       hls::stream<CuperOutputItem>& output_stream_8,
                                       hls::stream<CuperOutputItem>& output_stream_9,
                                       hls::stream<CuperOutputItem>& output_stream_10,
                                       hls::stream<CuperOutputItem>& output_stream_11,
                                       hls::stream<CuperOutputItem>& output_stream_12,
                                       hls::stream<CuperOutputItem>& output_stream_13,
                                       hls::stream<CuperOutputItem>& output_stream_14,
                                       hls::stream<CuperOutputItem>& output_stream_15,
                                       const int channel) {
#pragma HLS INLINE
    switch (channel) {
        case 0:
            return output_stream_0.read();
        case 1:
            return output_stream_1.read();
        case 2:
            return output_stream_2.read();
        case 3:
            return output_stream_3.read();
        case 4:
            return output_stream_4.read();
        case 5:
            return output_stream_5.read();
        case 6:
            return output_stream_6.read();
        case 7:
            return output_stream_7.read();
        case 8:
            return output_stream_8.read();
        case 9:
            return output_stream_9.read();
        case 10:
            return output_stream_10.read();
        case 11:
            return output_stream_11.read();
        case 12:
            return output_stream_12.read();
        case 13:
            return output_stream_13.read();
        case 14:
            return output_stream_14.read();
        default:
            return output_stream_15.read();
    }
}

template <bool InitializeVectors>
void write_cuper_outputs(hls::stream<CuperOutputItem>& output_stream_0,
                         hls::stream<CuperOutputItem>& output_stream_1,
                         hls::stream<CuperOutputItem>& output_stream_2,
                         hls::stream<CuperOutputItem>& output_stream_3,
                         hls::stream<CuperOutputItem>& output_stream_4,
                         hls::stream<CuperOutputItem>& output_stream_5,
                         hls::stream<CuperOutputItem>& output_stream_6,
                         hls::stream<CuperOutputItem>& output_stream_7,
                         hls::stream<CuperOutputItem>& output_stream_8,
                         hls::stream<CuperOutputItem>& output_stream_9,
                         hls::stream<CuperOutputItem>& output_stream_10,
                         hls::stream<CuperOutputItem>& output_stream_11,
                         hls::stream<CuperOutputItem>& output_stream_12,
                         hls::stream<CuperOutputItem>& output_stream_13,
                         hls::stream<CuperOutputItem>& output_stream_14,
                         hls::stream<CuperOutputItem>& output_stream_15,
                         const data_t* b,
                         const data_t* m_inv,
                         data_t* y,
                         data_t* r,
                         data_t* z,
                         data_t* p,
                         data_t* init_rz_out,
                         data_t* init_rr_out,
                         const int n) {
#pragma HLS INLINE off
    const int active_depth = (n + (2 * kCuperPeNum - 1)) / (2 * kCuperPeNum);
    PcgInitResult init_result{};
#pragma HLS ARRAY_PARTITION variable = init_result.rz complete dim = 1
#pragma HLS ARRAY_PARTITION variable = init_result.rr complete dim = 1
init_pcg_partials:
    for (int index = 0; index < kDotPartialCount; ++index) {
#pragma HLS UNROLL
        init_result.rz[index] = 0.0;
        init_result.rr[index] = 0.0;
    }

    // 16 路 channel 的输出行互不重叠。按 Cuper 的 packet_id 顺序交错读取，
    // 等价于 TAPA Vector_Checker/Mult_Sort_Tree 把各路 float_v2 重新排回
    // 全局行序。这样 writer 不会先清空单一路 stream，也能减少 ap/y 端口
    // 每拍过多散写导致的 II 压力。
write_row_groups:
    for (int row_group = 0; row_group < active_depth; ++row_group) {
    write_packets:
        for (int packet_offset = 0; packet_offset < kCuperPeNum; ++packet_offset) {
#pragma HLS PIPELINE II = 1
            const int checker_id = packet_offset & 7;
            const int acc_offset = (packet_offset >> 3) & 1;
            const int channel = checker_id * 2 + acc_offset;
            const CuperOutputItem item =
                read_cuper_output_item(output_stream_0,
                                       output_stream_1,
                                       output_stream_2,
                                       output_stream_3,
                                       output_stream_4,
                                       output_stream_5,
                                       output_stream_6,
                                       output_stream_7,
                                       output_stream_8,
                                       output_stream_9,
                                       output_stream_10,
                                       output_stream_11,
                                       output_stream_12,
                                       output_stream_13,
                                       output_stream_14,
                                       output_stream_15,
                                       channel);
            const int dot_bank = packet_offset & (kDotPartialCount - 1);
            write_cuper_output_item<InitializeVectors>(y,
                                                       b,
                                                       m_inv,
                                                       r,
                                                       z,
                                                       p,
                                                       item,
                                                       n,
                                                       dot_bank,
                                                       init_result);
        }
    }

    data_t init_rz = 0.0;
    data_t init_rr = 0.0;
reduce_init_partials:
    for (int index = 0; index < kDotPartialCount; ++index) {
#pragma HLS UNROLL
        init_rz += init_result.rz[index];
        init_rr += init_result.rr[index];
    }
    *init_rz_out = init_rz;
    *init_rr_out = init_rr;
}

constexpr int kCuperSpmvGroupChannels = 4;

void broadcast_cuper_inputs_4ch(const index_t* sp_element_list_ptr,
                                const data_t* x,
                                const int batch_count,
                                const int n,
                                hls::stream<CuperBatchParam> batch_streams[kCuperSpmvGroupChannels],
                                hls::stream<CuperXPacket> x_streams[kCuperSpmvGroupChannels]) {
#pragma HLS INLINE off
    int begin = sp_element_list_ptr[0];

broadcast_4ch_batch_loop:
    for (int batch = 0; batch < batch_count; ++batch) {
#pragma HLS LOOP_TRIPCOUNT min = 1 max = 128
        const int end = sp_element_list_ptr[batch + 1];
        const int slice_begin = batch * kCuperSliceWidth;
        const CuperBatchParam param{begin, end, slice_begin};

    broadcast_4ch_param:
        for (int channel = 0; channel < kCuperSpmvGroupChannels; ++channel) {
#pragma HLS UNROLL
            batch_streams[channel].write(param);
        }

    broadcast_4ch_x_packets:
        for (int packet_index = 0; packet_index < kCuperSliceWidth / 16; ++packet_index) {
#pragma HLS PIPELINE II = 1
            CuperXPacket packet;
        fill_4ch_x_packet:
            for (int lane = 0; lane < 16; ++lane) {
#pragma HLS UNROLL
                const int global_col = slice_begin + (packet_index << 4) + lane;
                packet.values[lane] =
                    (global_col < n) ? static_cast<spmv_data_t>(x[global_col]) : 0.0f;
            }
        broadcast_4ch_x_channels:
            for (int channel = 0; channel < kCuperSpmvGroupChannels; ++channel) {
#pragma HLS UNROLL
                x_streams[channel].write(packet);
            }
        }

        begin = end;
    }
}

CuperOutputItem read_cuper_output_item_4ch(hls::stream<CuperOutputItem>& output_stream_0,
                                           hls::stream<CuperOutputItem>& output_stream_1,
                                           hls::stream<CuperOutputItem>& output_stream_2,
                                           hls::stream<CuperOutputItem>& output_stream_3,
                                           const int stream_index) {
#pragma HLS INLINE
    switch (stream_index) {
        case 0:
            return output_stream_0.read();
        case 1:
            return output_stream_1.read();
        case 2:
            return output_stream_2.read();
        default:
            return output_stream_3.read();
    }
}

void write_cuper_outputs_4ch(hls::stream<CuperOutputItem>& output_stream_0,
                             hls::stream<CuperOutputItem>& output_stream_1,
                             hls::stream<CuperOutputItem>& output_stream_2,
                             hls::stream<CuperOutputItem>& output_stream_3,
                             data_t* y,
                             const int n) {
#pragma HLS INLINE off
    const int active_depth = (n + (2 * kCuperPeNum - 1)) / (2 * kCuperPeNum);
    constexpr int kItemsPerRowGroup = kCuperSpmvGroupChannels * kCuperPePerHbm;

write_4ch_row_groups:
    for (int row_group = 0; row_group < active_depth; ++row_group) {
    write_4ch_items:
        for (int item_index = 0; item_index < kItemsPerRowGroup; ++item_index) {
#pragma HLS PIPELINE II = 1
            const int stream_index = item_index & (kCuperSpmvGroupChannels - 1);
            const CuperOutputItem item = read_cuper_output_item_4ch(output_stream_0,
                                                                   output_stream_1,
                                                                   output_stream_2,
                                                                   output_stream_3,
                                                                   stream_index);
            if (item.even_row < n) {
                y[item.even_row] = static_cast<data_t>(item.even_value);
            }
            if (item.odd_row < n) {
                y[item.odd_row] = static_cast<data_t>(item.odd_value);
            }
        }
    }
}

void cuper_packed_spmv_4ch_dataflow(const index_t* sp_element_list_ptr,
                                    cuper_matrix_ptr_t matrix_data_0,
                                    cuper_matrix_ptr_t matrix_data_1,
                                    cuper_matrix_ptr_t matrix_data_2,
                                    cuper_matrix_ptr_t matrix_data_3,
                                    const int channel_base,
                                    const data_t* x,
                                    data_t* y,
                                    const int batch_count,
                                    const int n) {
#pragma HLS INLINE off
    hls::stream<CuperOutputItem> output_stream_0;
    hls::stream<CuperOutputItem> output_stream_1;
    hls::stream<CuperOutputItem> output_stream_2;
    hls::stream<CuperOutputItem> output_stream_3;
    hls::stream<CuperBatchParam> batch_streams[kCuperSpmvGroupChannels];
    hls::stream<CuperXPacket> x_streams[kCuperSpmvGroupChannels];
#pragma HLS STREAM variable = output_stream_0 depth = 256
#pragma HLS STREAM variable = output_stream_1 depth = 256
#pragma HLS STREAM variable = output_stream_2 depth = 256
#pragma HLS STREAM variable = output_stream_3 depth = 256
#pragma HLS STREAM variable = batch_streams depth = 4
#pragma HLS STREAM variable = x_streams depth = 64
#pragma HLS ARRAY_PARTITION variable = batch_streams complete dim = 1
#pragma HLS ARRAY_PARTITION variable = x_streams complete dim = 1

#pragma HLS DATAFLOW
    broadcast_cuper_inputs_4ch(sp_element_list_ptr, x, batch_count, n, batch_streams, x_streams);
    process_cuper_channel_stream(matrix_data_0,
                                 channel_base + 0,
                                 batch_count,
                                 n,
                                 batch_streams[0],
                                 x_streams[0],
                                 output_stream_0);
    process_cuper_channel_stream(matrix_data_1,
                                 channel_base + 1,
                                 batch_count,
                                 n,
                                 batch_streams[1],
                                 x_streams[1],
                                 output_stream_1);
    process_cuper_channel_stream(matrix_data_2,
                                 channel_base + 2,
                                 batch_count,
                                 n,
                                 batch_streams[2],
                                 x_streams[2],
                                 output_stream_2);
    process_cuper_channel_stream(matrix_data_3,
                                 channel_base + 3,
                                 batch_count,
                                 n,
                                 batch_streams[3],
                                 x_streams[3],
                                 output_stream_3);
    write_cuper_outputs_4ch(output_stream_0, output_stream_1, output_stream_2, output_stream_3, y, n);
}

void cuper_packed_spmv_4ch(const index_t* sp_element_list_ptr,
                           cuper_matrix_ptr_t matrix_data_0,
                           cuper_matrix_ptr_t matrix_data_1,
                           cuper_matrix_ptr_t matrix_data_2,
                           cuper_matrix_ptr_t matrix_data_3,
                           cuper_matrix_ptr_t matrix_data_4,
                           cuper_matrix_ptr_t matrix_data_5,
                           cuper_matrix_ptr_t matrix_data_6,
                           cuper_matrix_ptr_t matrix_data_7,
                           cuper_matrix_ptr_t matrix_data_8,
                           cuper_matrix_ptr_t matrix_data_9,
                           cuper_matrix_ptr_t matrix_data_10,
                           cuper_matrix_ptr_t matrix_data_11,
                           cuper_matrix_ptr_t matrix_data_12,
                           cuper_matrix_ptr_t matrix_data_13,
                           cuper_matrix_ptr_t matrix_data_14,
                           cuper_matrix_ptr_t matrix_data_15,
                           const data_t* x,
                           data_t* y,
                           const int batch_count,
                           const int n) {
#pragma HLS INLINE off
#pragma HLS ALLOCATION instances = cuper_packed_spmv_4ch_dataflow limit = 1 function
    // 一次只放 4 个 matrix channel 进 DATAFLOW 区域，4 组顺序执行。
    // 这样保留原 16-HBM host 打包格式，同时显著降低单个调度区域的
    // ap_enable/ap_block 扇出和跨 SLR URAM/DSP 布线压力。
    cuper_packed_spmv_4ch_dataflow(sp_element_list_ptr,
                                   matrix_data_0,
                                   matrix_data_1,
                                   matrix_data_2,
                                   matrix_data_3,
                                   0,
                                   x,
                                   y,
                                   batch_count,
                                   n);
    cuper_packed_spmv_4ch_dataflow(sp_element_list_ptr,
                                   matrix_data_4,
                                   matrix_data_5,
                                   matrix_data_6,
                                   matrix_data_7,
                                   4,
                                   x,
                                   y,
                                   batch_count,
                                   n);
    cuper_packed_spmv_4ch_dataflow(sp_element_list_ptr,
                                   matrix_data_8,
                                   matrix_data_9,
                                   matrix_data_10,
                                   matrix_data_11,
                                   8,
                                   x,
                                   y,
                                   batch_count,
                                   n);
    cuper_packed_spmv_4ch_dataflow(sp_element_list_ptr,
                                   matrix_data_12,
                                   matrix_data_13,
                                   matrix_data_14,
                                   matrix_data_15,
                                   12,
                                   x,
                                   y,
                                   batch_count,
                                   n);
}

template <bool InitializeVectors>
void cuper_packed_spmv_dataflow(const index_t* sp_element_list_ptr,
                                cuper_matrix_ptr_t matrix_data_0,
                                cuper_matrix_ptr_t matrix_data_1,
                                cuper_matrix_ptr_t matrix_data_2,
                                cuper_matrix_ptr_t matrix_data_3,
                                cuper_matrix_ptr_t matrix_data_4,
                                cuper_matrix_ptr_t matrix_data_5,
                                cuper_matrix_ptr_t matrix_data_6,
                                cuper_matrix_ptr_t matrix_data_7,
                                cuper_matrix_ptr_t matrix_data_8,
                                cuper_matrix_ptr_t matrix_data_9,
                                cuper_matrix_ptr_t matrix_data_10,
                                cuper_matrix_ptr_t matrix_data_11,
                                cuper_matrix_ptr_t matrix_data_12,
                                cuper_matrix_ptr_t matrix_data_13,
                                cuper_matrix_ptr_t matrix_data_14,
                                cuper_matrix_ptr_t matrix_data_15,
                                const data_t* x,
                                const data_t* b,
                                const data_t* m_inv,
                                data_t* y,
                                data_t* r,
                                data_t* z,
                                data_t* p,
                                data_t* init_rz_out,
                                data_t* init_rr_out,
                                const int batch_count,
                                const int n) {
#pragma HLS INLINE off
    hls::stream<CuperOutputItem> output_stream_0;
    hls::stream<CuperOutputItem> output_stream_1;
    hls::stream<CuperOutputItem> output_stream_2;
    hls::stream<CuperOutputItem> output_stream_3;
    hls::stream<CuperOutputItem> output_stream_4;
    hls::stream<CuperOutputItem> output_stream_5;
    hls::stream<CuperOutputItem> output_stream_6;
    hls::stream<CuperOutputItem> output_stream_7;
    hls::stream<CuperOutputItem> output_stream_8;
    hls::stream<CuperOutputItem> output_stream_9;
    hls::stream<CuperOutputItem> output_stream_10;
    hls::stream<CuperOutputItem> output_stream_11;
    hls::stream<CuperOutputItem> output_stream_12;
    hls::stream<CuperOutputItem> output_stream_13;
    hls::stream<CuperOutputItem> output_stream_14;
    hls::stream<CuperOutputItem> output_stream_15;
    hls::stream<CuperBatchParam> batch_streams[kCuperHbmChannelNum];
    hls::stream<CuperXPacket> x_streams[kCuperHbmChannelNum];
#pragma HLS STREAM variable = output_stream_0 depth = 512
#pragma HLS STREAM variable = output_stream_1 depth = 512
#pragma HLS STREAM variable = output_stream_2 depth = 512
#pragma HLS STREAM variable = output_stream_3 depth = 512
#pragma HLS STREAM variable = output_stream_4 depth = 512
#pragma HLS STREAM variable = output_stream_5 depth = 512
#pragma HLS STREAM variable = output_stream_6 depth = 512
#pragma HLS STREAM variable = output_stream_7 depth = 512
#pragma HLS STREAM variable = output_stream_8 depth = 512
#pragma HLS STREAM variable = output_stream_9 depth = 512
#pragma HLS STREAM variable = output_stream_10 depth = 512
#pragma HLS STREAM variable = output_stream_11 depth = 512
#pragma HLS STREAM variable = output_stream_12 depth = 512
#pragma HLS STREAM variable = output_stream_13 depth = 512
#pragma HLS STREAM variable = output_stream_14 depth = 512
#pragma HLS STREAM variable = output_stream_15 depth = 512
#pragma HLS STREAM variable = batch_streams depth = 4
#pragma HLS STREAM variable = x_streams depth = 64
#pragma HLS ARRAY_PARTITION variable = batch_streams complete dim = 1
#pragma HLS ARRAY_PARTITION variable = x_streams complete dim = 1

#pragma HLS DATAFLOW
    broadcast_cuper_inputs(sp_element_list_ptr, x, batch_count, n, batch_streams, x_streams);

    // 16 个 HBM channel 现在在 DATAFLOW 区域内并发执行，结构上更接近
    // 原始 TAPA Cuper 的 16 路 Matrix_Loader/Core/Accumulator。
    process_cuper_channel_stream(matrix_data_0, 0, batch_count, n, batch_streams[0], x_streams[0], output_stream_0);
    process_cuper_channel_stream(matrix_data_1, 1, batch_count, n, batch_streams[1], x_streams[1], output_stream_1);
    process_cuper_channel_stream(matrix_data_2, 2, batch_count, n, batch_streams[2], x_streams[2], output_stream_2);
    process_cuper_channel_stream(matrix_data_3, 3, batch_count, n, batch_streams[3], x_streams[3], output_stream_3);
    process_cuper_channel_stream(matrix_data_4, 4, batch_count, n, batch_streams[4], x_streams[4], output_stream_4);
    process_cuper_channel_stream(matrix_data_5, 5, batch_count, n, batch_streams[5], x_streams[5], output_stream_5);
    process_cuper_channel_stream(matrix_data_6, 6, batch_count, n, batch_streams[6], x_streams[6], output_stream_6);
    process_cuper_channel_stream(matrix_data_7, 7, batch_count, n, batch_streams[7], x_streams[7], output_stream_7);
    process_cuper_channel_stream(matrix_data_8, 8, batch_count, n, batch_streams[8], x_streams[8], output_stream_8);
    process_cuper_channel_stream(matrix_data_9, 9, batch_count, n, batch_streams[9], x_streams[9], output_stream_9);
    process_cuper_channel_stream(matrix_data_10, 10, batch_count, n, batch_streams[10], x_streams[10], output_stream_10);
    process_cuper_channel_stream(matrix_data_11, 11, batch_count, n, batch_streams[11], x_streams[11], output_stream_11);
    process_cuper_channel_stream(matrix_data_12, 12, batch_count, n, batch_streams[12], x_streams[12], output_stream_12);
    process_cuper_channel_stream(matrix_data_13, 13, batch_count, n, batch_streams[13], x_streams[13], output_stream_13);
    process_cuper_channel_stream(matrix_data_14, 14, batch_count, n, batch_streams[14], x_streams[14], output_stream_14);
    process_cuper_channel_stream(matrix_data_15, 15, batch_count, n, batch_streams[15], x_streams[15], output_stream_15);

    write_cuper_outputs<InitializeVectors>(output_stream_0,
                                           output_stream_1,
                                           output_stream_2,
                                           output_stream_3,
                                           output_stream_4,
                                           output_stream_5,
                                           output_stream_6,
                                           output_stream_7,
                                           output_stream_8,
                                           output_stream_9,
                                           output_stream_10,
                                           output_stream_11,
                                           output_stream_12,
                                           output_stream_13,
                                           output_stream_14,
                                           output_stream_15,
                                           b,
                                           m_inv,
                                           y,
                                           r,
                                           z,
                                           p,
                                           init_rz_out,
                                           init_rr_out,
                                           n);
}

template <bool InitializeVectors>
void cuper_packed_spmv(const index_t* sp_element_list_ptr,
                       cuper_matrix_ptr_t matrix_data_0,
                       cuper_matrix_ptr_t matrix_data_1,
                       cuper_matrix_ptr_t matrix_data_2,
                       cuper_matrix_ptr_t matrix_data_3,
                       cuper_matrix_ptr_t matrix_data_4,
                       cuper_matrix_ptr_t matrix_data_5,
                       cuper_matrix_ptr_t matrix_data_6,
                       cuper_matrix_ptr_t matrix_data_7,
                       cuper_matrix_ptr_t matrix_data_8,
                       cuper_matrix_ptr_t matrix_data_9,
                       cuper_matrix_ptr_t matrix_data_10,
                       cuper_matrix_ptr_t matrix_data_11,
                       cuper_matrix_ptr_t matrix_data_12,
                       cuper_matrix_ptr_t matrix_data_13,
                       cuper_matrix_ptr_t matrix_data_14,
                       cuper_matrix_ptr_t matrix_data_15,
                       const data_t* x,
                       const data_t* b,
                       const data_t* m_inv,
                       data_t* y,
                       data_t* r,
                       data_t* z,
                       data_t* p,
                       data_t* init_rz_out,
                       data_t* init_rr_out,
                       const int batch_count,
                       const int n) {
#pragma HLS INLINE off
    cuper_packed_spmv_dataflow<InitializeVectors>(sp_element_list_ptr,
                                                  matrix_data_0,
                                                  matrix_data_1,
                                                  matrix_data_2,
                                                  matrix_data_3,
                                                  matrix_data_4,
                                                  matrix_data_5,
                                                  matrix_data_6,
                                                  matrix_data_7,
                                                  matrix_data_8,
                                                  matrix_data_9,
                                                  matrix_data_10,
                                                  matrix_data_11,
                                                  matrix_data_12,
                                                  matrix_data_13,
                                                  matrix_data_14,
                                                  matrix_data_15,
                                                  x,
                                                  b,
                                                  m_inv,
                                                  y,
                                                  r,
                                                  z,
                                                  p,
                                                  init_rz_out,
                                                  init_rr_out,
                                                  batch_count,
                                                  n);
}

data_t dot_vectors(const data_t* lhs, const data_t* rhs, const int n) {
#pragma HLS INLINE off
    data_t dot = 0.0;
dot_vectors_loop:
    for (int index = 0; index < n; ++index) {
#pragma HLS PIPELINE II = 1
        dot += lhs[index] * rhs[index];
    }
    return dot;
}

}  // namespace

extern "C" {

void cuper_pcg_control_kernel(const project_xplus::cgsolver::index_t* sp_element_list_ptr,
                              cuper_matrix_ptr_t matrix_data_0,
                              cuper_matrix_ptr_t matrix_data_1,
                              cuper_matrix_ptr_t matrix_data_2,
                              cuper_matrix_ptr_t matrix_data_3,
                              cuper_matrix_ptr_t matrix_data_4,
                              cuper_matrix_ptr_t matrix_data_5,
                              cuper_matrix_ptr_t matrix_data_6,
                              cuper_matrix_ptr_t matrix_data_7,
                              cuper_matrix_ptr_t matrix_data_8,
                              cuper_matrix_ptr_t matrix_data_9,
                              cuper_matrix_ptr_t matrix_data_10,
                              cuper_matrix_ptr_t matrix_data_11,
                              cuper_matrix_ptr_t matrix_data_12,
                              cuper_matrix_ptr_t matrix_data_13,
                              cuper_matrix_ptr_t matrix_data_14,
                              cuper_matrix_ptr_t matrix_data_15,
                              const project_xplus::cgsolver::data_t* b,
                              const project_xplus::cgsolver::data_t* m_inv,
                              project_xplus::cgsolver::data_t* x,
                              project_xplus::cgsolver::data_t* r,
                              project_xplus::cgsolver::data_t* z,
                              project_xplus::cgsolver::data_t* p,
                              project_xplus::cgsolver::data_t* ap,
                              project_xplus::cgsolver::data_t* metrics,
                              int* status,
                              project_xplus::cgsolver::data_t tau,
                              int max_iters,
                              int n,
                              int batch_count) {
// 单顶层 Cuper-PCG control kernel。
//
// 与 TAPA Cuper 版不同，本 kernel 不只是 SpMV：
//   1. host 只 launch 一次 cuper_pcg_control_kernel
//   2. kernel 内部完成 A*x0、PCG init、迭代更新和收敛/breakdown 判断
//   3. 矩阵输入采用 16 HBM channel 的 Cuper packed 格式
//
// 参数顺序是 ABI：host/cuper_control_xrt_host.cpp 的 BO arg_index 必须逐项对应。
#pragma HLS INTERFACE s_axilite port = sp_element_list_ptr bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_0 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_1 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_2 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_3 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_4 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_5 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_6 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_7 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_8 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_9 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_10 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_11 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_12 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_13 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_14 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_15 bundle = control
#pragma HLS INTERFACE s_axilite port = b bundle = control
#pragma HLS INTERFACE s_axilite port = m_inv bundle = control
#pragma HLS INTERFACE s_axilite port = x bundle = control
#pragma HLS INTERFACE s_axilite port = r bundle = control
#pragma HLS INTERFACE s_axilite port = z bundle = control
#pragma HLS INTERFACE s_axilite port = p bundle = control
#pragma HLS INTERFACE s_axilite port = ap bundle = control
#pragma HLS INTERFACE s_axilite port = metrics bundle = control
#pragma HLS INTERFACE s_axilite port = status bundle = control
#pragma HLS INTERFACE s_axilite port = tau bundle = control
#pragma HLS INTERFACE s_axilite port = max_iters bundle = control
#pragma HLS INTERFACE s_axilite port = n bundle = control
#pragma HLS INTERFACE s_axilite port = batch_count bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS INTERFACE m_axi port = sp_element_list_ptr offset = slave bundle = gmem_meta num_read_outstanding = 16 max_read_burst_length = 64
#pragma HLS INTERFACE m_axi port = matrix_data_0 offset = slave bundle = gmem_matrix0 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_1 offset = slave bundle = gmem_matrix1 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_2 offset = slave bundle = gmem_matrix2 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_3 offset = slave bundle = gmem_matrix3 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_4 offset = slave bundle = gmem_matrix4 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_5 offset = slave bundle = gmem_matrix5 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_6 offset = slave bundle = gmem_matrix6 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_7 offset = slave bundle = gmem_matrix7 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_8 offset = slave bundle = gmem_matrix8 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_9 offset = slave bundle = gmem_matrix9 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_10 offset = slave bundle = gmem_matrix10 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_11 offset = slave bundle = gmem_matrix11 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_12 offset = slave bundle = gmem_matrix12 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_13 offset = slave bundle = gmem_matrix13 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_14 offset = slave bundle = gmem_matrix14 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_15 offset = slave bundle = gmem_matrix15 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = b offset = slave bundle = gmem_b num_read_outstanding = 16 max_read_burst_length = 64
#pragma HLS INTERFACE m_axi port = m_inv offset = slave bundle = gmem_minv num_read_outstanding = 16 max_read_burst_length = 64
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x num_read_outstanding = 16 num_write_outstanding = 16 max_read_burst_length = 64 max_write_burst_length = 64
#pragma HLS INTERFACE m_axi port = r offset = slave bundle = gmem_r num_read_outstanding = 16 num_write_outstanding = 16 max_read_burst_length = 64 max_write_burst_length = 64
#pragma HLS INTERFACE m_axi port = z offset = slave bundle = gmem_z num_read_outstanding = 16 num_write_outstanding = 16 max_read_burst_length = 64 max_write_burst_length = 64
#pragma HLS INTERFACE m_axi port = p offset = slave bundle = gmem_p num_read_outstanding = 16 num_write_outstanding = 16 max_read_burst_length = 64 max_write_burst_length = 64
#pragma HLS INTERFACE m_axi port = ap offset = slave bundle = gmem_ap num_read_outstanding = 16 num_write_outstanding = 16 max_read_burst_length = 64 max_write_burst_length = 64
#pragma HLS INTERFACE m_axi port = metrics offset = slave bundle = gmem_metrics num_write_outstanding = 4 max_write_burst_length = 16
#pragma HLS INTERFACE m_axi port = status offset = slave bundle = gmem_status num_write_outstanding = 4 max_write_burst_length = 16

    data_t rz = 0.0;
    data_t rr = 0.0;
    data_t p_ap = 0.0;
    data_t alpha = 0.0;
    data_t beta = 0.0;
    int iterations = 0;
    int status_code = kStatusMaxIter;

    // 参数非法时直接返回 breakdown，避免 HLS kernel 内部访问空范围或生成 NaN。
    if (n <= 0 || batch_count <= 0 || batch_count > kCuperMaxBatchCount ||
        max_iters < 0 || tau <= 0.0 || invalid_scalar(tau)) {
        status[0] = kStatusBreakdown;
        status[1] = 0;
        metrics[0] = 0.0;
        metrics[1] = 0.0;
        metrics[2] = 0.0;
        metrics[3] = 0.0;
        return;
    }

    // 初始 SpMV：ap = A * x0，并在写回 AP 时直接初始化 r/z/p。
    // 这样省掉原先 init_vectors 对 b/m_inv/ap 的独立 HBM 扫描。
    data_t unused_init_metric = 0.0;
    cuper_packed_spmv<true>(sp_element_list_ptr,
                            matrix_data_0,
                            matrix_data_1,
                            matrix_data_2,
                            matrix_data_3,
                            matrix_data_4,
                            matrix_data_5,
                            matrix_data_6,
                            matrix_data_7,
                            matrix_data_8,
                            matrix_data_9,
                            matrix_data_10,
                            matrix_data_11,
                            matrix_data_12,
                            matrix_data_13,
                            matrix_data_14,
                            matrix_data_15,
                            x,
                            b,
                            m_inv,
                            ap,
                            r,
                            z,
                            p,
                            &rz,
                            &rr,
                            batch_count,
                            n);

pcg_loop:
    for (int iteration = 0; iteration < max_iters; ++iteration) {
        if (rr <= tau) {
            status_code = kStatusConverged;
            break;
        }
        if (invalid_scalar(rz) || invalid_scalar(rr) || abs_value(rz) <= kBreakdownEps) {
            status_code = kStatusBreakdown;
            break;
        }

        // ap = A*p。p^T * AP 不能放进 SpMV 的 DATAFLOW writer 里，
        // 否则 HLS 会看到 gmem_p 被 broadcast 和 writer 两个 process 同时读。
        // 先保持单独 dot 扫描，后续再用更细粒度 task 拆分消掉这次 HBM 往返。
        cuper_packed_spmv<false>(sp_element_list_ptr,
                                 matrix_data_0,
                                 matrix_data_1,
                                 matrix_data_2,
                                 matrix_data_3,
                                 matrix_data_4,
                                 matrix_data_5,
                                 matrix_data_6,
                                 matrix_data_7,
                                 matrix_data_8,
                                 matrix_data_9,
                                 matrix_data_10,
                                 matrix_data_11,
                                 matrix_data_12,
                                 matrix_data_13,
                                 matrix_data_14,
                                 matrix_data_15,
                                 p,
                                 b,
                                 m_inv,
                                 ap,
                                 r,
                                 z,
                                 p,
                                 &unused_init_metric,
                                 &unused_init_metric,
                                 batch_count,
                                 n);
        p_ap = dot_vectors(p, ap, n);

        if (invalid_scalar(p_ap) || abs_value(p_ap) <= kBreakdownEps) {
            status_code = kStatusBreakdown;
            break;
        }

        const data_t rz_old = rz;
        alpha = rz / p_ap;
        if (invalid_scalar(alpha)) {
            status_code = kStatusBreakdown;
            break;
        }

        data_t rz_new = 0.0;
        data_t rr_new = 0.0;
update_xrz_loop:
        for (int index = 0; index < n; ++index) {
#pragma HLS PIPELINE II = 1
            // x、r、z 三个向量在同一趟流水里更新，减少 HBM 往返次数。
            const data_t x_value = x[index] + alpha * p[index];
            const data_t r_value = r[index] - alpha * ap[index];
            const data_t z_value = m_inv[index] * r_value;
            x[index] = x_value;
            r[index] = r_value;
            z[index] = z_value;
            rz_new += r_value * z_value;
            rr_new += r_value * r_value;
        }

        rz = rz_new;
        rr = rr_new;
        iterations = iteration + 1;

        if (invalid_scalar(rz) || invalid_scalar(rr)) {
            status_code = kStatusBreakdown;
            break;
        }
        if (rr <= tau) {
            status_code = kStatusConverged;
            break;
        }
        if (abs_value(rz_old) <= kBreakdownEps) {
            status_code = kStatusBreakdown;
            break;
        }

        beta = rz / rz_old;
        if (invalid_scalar(beta)) {
            status_code = kStatusBreakdown;
            break;
        }

update_p_loop:
        for (int index = 0; index < n; ++index) {
#pragma HLS PIPELINE II = 1
            // 下一轮搜索方向：p_{k+1} = z_{k+1} + beta * p_k。
            p[index] = z[index] + beta * p[index];
        }
    }

    if (status_code == kStatusMaxIter && rr <= tau) {
        status_code = kStatusConverged;
    }

    // metrics 供 host 侧调试/报告使用，不参与 kernel 后续计算。
    metrics[0] = rz;
    metrics[1] = rr;
    metrics[2] = p_ap;
    metrics[3] = alpha;
    status[0] = status_code;
    status[1] = iterations;
}

void cuper_packed_spmv_kernel(const project_xplus::cgsolver::index_t* sp_element_list_ptr,
                              cuper_matrix_ptr_t matrix_data_0,
                              cuper_matrix_ptr_t matrix_data_1,
                              cuper_matrix_ptr_t matrix_data_2,
                              cuper_matrix_ptr_t matrix_data_3,
                              cuper_matrix_ptr_t matrix_data_4,
                              cuper_matrix_ptr_t matrix_data_5,
                              cuper_matrix_ptr_t matrix_data_6,
                              cuper_matrix_ptr_t matrix_data_7,
                              cuper_matrix_ptr_t matrix_data_8,
                              cuper_matrix_ptr_t matrix_data_9,
                              cuper_matrix_ptr_t matrix_data_10,
                              cuper_matrix_ptr_t matrix_data_11,
                              cuper_matrix_ptr_t matrix_data_12,
                              cuper_matrix_ptr_t matrix_data_13,
                              cuper_matrix_ptr_t matrix_data_14,
                              cuper_matrix_ptr_t matrix_data_15,
                              const project_xplus::cgsolver::data_t* x,
                              project_xplus::cgsolver::data_t* y,
                              int batch_count,
                              int n) {
// 单 SpMV 顶层 kernel。
//
// 这个入口只复用 Cuper packed SpMV 数据通路，不触碰 PCG 状态：
//   sp_element_list_ptr + matrix_data_0..15 + x -> y
// host/cuper_notapa_pcg_xrt_main.cpp 用它做 TAPA/no-TAPA SpMV 吞吐对比。
#pragma HLS INTERFACE s_axilite port = sp_element_list_ptr bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_0 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_1 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_2 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_3 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_4 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_5 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_6 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_7 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_8 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_9 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_10 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_11 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_12 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_13 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_14 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_15 bundle = control
#pragma HLS INTERFACE s_axilite port = x bundle = control
#pragma HLS INTERFACE s_axilite port = y bundle = control
#pragma HLS INTERFACE s_axilite port = batch_count bundle = control
#pragma HLS INTERFACE s_axilite port = n bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS INTERFACE m_axi port = sp_element_list_ptr offset = slave bundle = gmem_meta num_read_outstanding = 16 max_read_burst_length = 64
#pragma HLS INTERFACE m_axi port = matrix_data_0 offset = slave bundle = gmem_matrix0 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_1 offset = slave bundle = gmem_matrix1 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_2 offset = slave bundle = gmem_matrix2 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_3 offset = slave bundle = gmem_matrix3 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_4 offset = slave bundle = gmem_matrix4 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_5 offset = slave bundle = gmem_matrix5 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_6 offset = slave bundle = gmem_matrix6 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_7 offset = slave bundle = gmem_matrix7 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_8 offset = slave bundle = gmem_matrix8 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_9 offset = slave bundle = gmem_matrix9 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_10 offset = slave bundle = gmem_matrix10 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_11 offset = slave bundle = gmem_matrix11 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_12 offset = slave bundle = gmem_matrix12 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_13 offset = slave bundle = gmem_matrix13 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_14 offset = slave bundle = gmem_matrix14 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_15 offset = slave bundle = gmem_matrix15 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x num_read_outstanding = 16 max_read_burst_length = 64
#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y num_write_outstanding = 16 max_write_burst_length = 64

    if (n <= 0 || batch_count <= 0 || batch_count > kCuperMaxBatchCount) {
        return;
    }

    data_t unused_init_metric = 0.0;
    cuper_packed_spmv<false>(sp_element_list_ptr,
                             matrix_data_0,
                             matrix_data_1,
                             matrix_data_2,
                             matrix_data_3,
                             matrix_data_4,
                             matrix_data_5,
                             matrix_data_6,
                             matrix_data_7,
                             matrix_data_8,
                             matrix_data_9,
                             matrix_data_10,
                             matrix_data_11,
                             matrix_data_12,
                             matrix_data_13,
                             matrix_data_14,
                             matrix_data_15,
                             x,
                             x,
                             x,
                             y,
                             y,
                             y,
                             y,
                             &unused_init_metric,
                             &unused_init_metric,
                             batch_count,
                             n);
}

void cuper_packed_spmv_4ch_kernel(const project_xplus::cgsolver::index_t* sp_element_list_ptr,
                                  cuper_matrix_ptr_t matrix_data_0,
                                  cuper_matrix_ptr_t matrix_data_1,
                                  cuper_matrix_ptr_t matrix_data_2,
                                  cuper_matrix_ptr_t matrix_data_3,
                                  cuper_matrix_ptr_t matrix_data_4,
                                  cuper_matrix_ptr_t matrix_data_5,
                                  cuper_matrix_ptr_t matrix_data_6,
                                  cuper_matrix_ptr_t matrix_data_7,
                                  cuper_matrix_ptr_t matrix_data_8,
                                  cuper_matrix_ptr_t matrix_data_9,
                                  cuper_matrix_ptr_t matrix_data_10,
                                  cuper_matrix_ptr_t matrix_data_11,
                                  cuper_matrix_ptr_t matrix_data_12,
                                  cuper_matrix_ptr_t matrix_data_13,
                                  cuper_matrix_ptr_t matrix_data_14,
                                  cuper_matrix_ptr_t matrix_data_15,
                                  const project_xplus::cgsolver::data_t* x,
                                  project_xplus::cgsolver::data_t* y,
                                  int batch_count,
                                  int n) {
// 4-channel 实验 SpMV 顶层。
//
// ABI 和 cuper_packed_spmv_kernel 完全一致，host 只需要换 kernel name
// 和 xclbin。这样可以直接比较：
//   - 16ch：单个 DATAFLOW 区域内并行 16 个 matrix channel
//   - 4ch ：单个 DATAFLOW 区域内并行 4 个 matrix channel，跑 4 组
//
// 预期是牺牲一部分吞吐，换取更低的 URAM/DSP/控制扇出和更好的布线频率。
#pragma HLS INTERFACE s_axilite port = sp_element_list_ptr bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_0 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_1 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_2 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_3 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_4 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_5 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_6 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_7 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_8 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_9 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_10 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_11 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_12 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_13 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_14 bundle = control
#pragma HLS INTERFACE s_axilite port = matrix_data_15 bundle = control
#pragma HLS INTERFACE s_axilite port = x bundle = control
#pragma HLS INTERFACE s_axilite port = y bundle = control
#pragma HLS INTERFACE s_axilite port = batch_count bundle = control
#pragma HLS INTERFACE s_axilite port = n bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS INTERFACE m_axi port = sp_element_list_ptr offset = slave bundle = gmem_meta num_read_outstanding = 16 max_read_burst_length = 64
#pragma HLS INTERFACE m_axi port = matrix_data_0 offset = slave bundle = gmem_matrix0 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_1 offset = slave bundle = gmem_matrix1 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_2 offset = slave bundle = gmem_matrix2 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_3 offset = slave bundle = gmem_matrix3 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_4 offset = slave bundle = gmem_matrix4 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_5 offset = slave bundle = gmem_matrix5 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_6 offset = slave bundle = gmem_matrix6 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_7 offset = slave bundle = gmem_matrix7 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_8 offset = slave bundle = gmem_matrix8 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_9 offset = slave bundle = gmem_matrix9 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_10 offset = slave bundle = gmem_matrix10 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_11 offset = slave bundle = gmem_matrix11 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_12 offset = slave bundle = gmem_matrix12 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_13 offset = slave bundle = gmem_matrix13 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_14 offset = slave bundle = gmem_matrix14 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = matrix_data_15 offset = slave bundle = gmem_matrix15 num_read_outstanding = 32 max_read_burst_length = 128
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x num_read_outstanding = 16 max_read_burst_length = 64
#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y num_write_outstanding = 16 max_write_burst_length = 64

    if (n <= 0 || batch_count <= 0 || batch_count > kCuperMaxBatchCount) {
        return;
    }

    cuper_packed_spmv_4ch(sp_element_list_ptr,
                          matrix_data_0,
                          matrix_data_1,
                          matrix_data_2,
                          matrix_data_3,
                          matrix_data_4,
                          matrix_data_5,
                          matrix_data_6,
                          matrix_data_7,
                          matrix_data_8,
                          matrix_data_9,
                          matrix_data_10,
                          matrix_data_11,
                          matrix_data_12,
                          matrix_data_13,
                          matrix_data_14,
                          matrix_data_15,
                          x,
                          y,
                          batch_count,
                          n);
}

}
