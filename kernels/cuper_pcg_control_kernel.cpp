#include "../include/cg_common.hpp"

#include <cstdint>

namespace {

using data_t = project_xplus::cgsolver::data_t;
using index_t = project_xplus::cgsolver::index_t;

// 这些常量必须和 host/cuper_control_matrix.hpp 中的打包规则一致。
// Cuper 矩阵按 8192 列切成 batch，再映射到 16 个 HBM channel；
// 每个 channel 内有 8 条 lane，对应原始 Cuper 设计中的 128 个物理 PE。
constexpr data_t kBreakdownEps = project_xplus::cgsolver::kBreakdownEps;
constexpr int kCuperSliceWidth = 8192;
constexpr int kCuperHbmChannelNum = 16;
constexpr int kCuperPePerHbm = 8;
constexpr int kCuperPeNum = kCuperHbmChannelNum * kCuperPePerHbm;
constexpr int kCuperLanesPerWord = 8;
constexpr int kCuperUramDepth = 6144;

enum StatusCode {
    kStatusConverged = 0,
    kStatusMaxIter = 1,
    kStatusBreakdown = 2,
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

data_t decode_product(const unsigned long packed_word,
                      const int slice_begin,
                      const data_t x_slice[kCuperSliceWidth],
                      int& packed_row,
                      const int n) {
#pragma HLS INLINE
    // packed word layout:
    //   [63:50] local_col, [49:32] packed_row, [31:0] fp32 value bits.
    // row bit17 全 1 的 dummy row 用来填充 PE/channel 对齐空洞。
    const std::uint64_t word = static_cast<std::uint64_t>(packed_word);
    packed_row = static_cast<int>((word >> 32) & 0x3FFFFULL);
    if ((packed_row & (1 << 17)) != 0) {
        return 0.0;
    }

    const int local_col = static_cast<int>((word >> 50) & 0x3FFFULL);
    const int global_col = slice_begin + local_col;
    if (global_col < 0 || global_col >= n) {
        return 0.0;
    }

    return static_cast<data_t>(bits_to_float(static_cast<std::uint32_t>(word & 0xFFFFFFFFULL))) *
           x_slice[local_col];
}

data_t process_cuper_channel(const unsigned long* matrix_data,
                             const int begin,
                             const int end,
                             const int channel,
                             const int slice_begin,
                             const data_t x_slice[kCuperSliceWidth],
                             data_t* y,
                             const data_t* x,
                             const int n,
                             const bool accumulate_x_dot_y) {
#pragma HLS INLINE off
    data_t dot = 0.0;
    // 每个 lane 只负责一部分固定行。active_depth 是该 lane 需要保存的
    // row group 数量，偶/奇行分两个 URAM 阵列避免同周期读写冲突。
    const int active_depth = (n + (2 * kCuperPeNum - 1)) / (2 * kCuperPeNum);

    data_t local_y_even[kCuperPePerHbm][kCuperUramDepth];
    data_t local_y_odd[kCuperPePerHbm][kCuperUramDepth];
#pragma HLS BIND_STORAGE variable = local_y_even type = ram_2p impl = uram
#pragma HLS BIND_STORAGE variable = local_y_odd type = ram_2p impl = uram
#pragma HLS ARRAY_PARTITION variable = local_y_even complete dim = 1
#pragma HLS ARRAY_PARTITION variable = local_y_odd complete dim = 1

zero_local_y:
    for (int index = 0; index < active_depth; ++index) {
#pragma HLS PIPELINE II = 1
    zero_local_lanes:
        for (int lane = 0; lane < kCuperPePerHbm; ++lane) {
#pragma HLS UNROLL
            local_y_even[lane][index] = 0.0;
            local_y_odd[lane][index] = 0.0;
        }
    }

process_cuper_channel_loop:
    for (int index = begin; index < end; ++index) {
#pragma HLS PIPELINE II = 1
        const int word_base = index * kCuperLanesPerWord;
        // 一个 matrix_data channel 的同一 index 连续存 8 个 lane word。
        // lane 完全展开后，每拍并行处理 8 个候选非零元。
    process_lanes:
        for (int lane = 0; lane < kCuperPePerHbm; ++lane) {
#pragma HLS UNROLL
            int packed_row = 0;
            const data_t product = decode_product(matrix_data[word_base + lane],
                                                  slice_begin,
                                                  x_slice,
                                                  packed_row,
                                                  n);
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

flush_local_y:
    for (int row_group = 0; row_group < active_depth; ++row_group) {
#pragma HLS PIPELINE II = 1
        // local_y 写回全局 y/ap 时顺手累计 x dot y。
        // PCG 中第二次 SpMV 调用用它得到 p^T A p，初始化 A*x0 时则关闭。
    flush_lanes:
        for (int lane = 0; lane < kCuperPePerHbm; ++lane) {
#pragma HLS UNROLL
            const int even_row = restore_global_row(channel, lane, row_group << 1);
            const int odd_row = even_row + 1;
            const data_t even_value = local_y_even[lane][row_group];
            const data_t odd_value = local_y_odd[lane][row_group];
            if (even_row < n) {
                y[even_row] = even_value;
                if (accumulate_x_dot_y) {
                    dot += x[even_row] * even_value;
                }
            }
            if (odd_row < n) {
                y[odd_row] = odd_value;
                if (accumulate_x_dot_y) {
                    dot += x[odd_row] * odd_value;
                }
            }
        }
    }
    return dot;
}

data_t cuper_packed_spmv_and_dot(const index_t* sp_element_list_ptr,
                                 const unsigned long* matrix_data_0,
                                 const unsigned long* matrix_data_1,
                                 const unsigned long* matrix_data_2,
                                 const unsigned long* matrix_data_3,
                                 const unsigned long* matrix_data_4,
                                 const unsigned long* matrix_data_5,
                                 const unsigned long* matrix_data_6,
                                 const unsigned long* matrix_data_7,
                                 const unsigned long* matrix_data_8,
                                 const unsigned long* matrix_data_9,
                                 const unsigned long* matrix_data_10,
                                 const unsigned long* matrix_data_11,
                                 const unsigned long* matrix_data_12,
                                 const unsigned long* matrix_data_13,
                                 const unsigned long* matrix_data_14,
                                 const unsigned long* matrix_data_15,
                                 const data_t* x,
                                 data_t* y,
                                 const int batch_count,
                                 const int n,
                                 const bool accumulate_x_dot_y) {
#pragma HLS INLINE off
    data_t dot = 0.0;
    data_t x_slice[kCuperSliceWidth];
#pragma HLS BIND_STORAGE variable = x_slice type = ram_2p impl = bram

    // 所有 batch/channel 都会写完整 y 的不同行分片；先清零可保证
    // dummy word、越界列和空 channel 不会留下上一次 SpMV 的结果。
    zero_vector(y, n);

batch_loop:
    for (int batch = 0; batch < batch_count; ++batch) {
#pragma HLS LOOP_TRIPCOUNT min = 1 max = 64
        const int begin = sp_element_list_ptr[batch];
        const int end = sp_element_list_ptr[batch + 1];
        const int slice_begin = batch * kCuperSliceWidth;
        load_x_slice(x, x_slice, slice_begin, n);

        // 16 个 HBM channel 分别处理自己的矩阵流。这里保持显式调用，
        // 让 HLS 生成独立 m_axi 端口，避免被数组端口合并。
        dot += process_cuper_channel(matrix_data_0, begin, end, 0, slice_begin, x_slice, y, x, n, accumulate_x_dot_y);
        dot += process_cuper_channel(matrix_data_1, begin, end, 1, slice_begin, x_slice, y, x, n, accumulate_x_dot_y);
        dot += process_cuper_channel(matrix_data_2, begin, end, 2, slice_begin, x_slice, y, x, n, accumulate_x_dot_y);
        dot += process_cuper_channel(matrix_data_3, begin, end, 3, slice_begin, x_slice, y, x, n, accumulate_x_dot_y);
        dot += process_cuper_channel(matrix_data_4, begin, end, 4, slice_begin, x_slice, y, x, n, accumulate_x_dot_y);
        dot += process_cuper_channel(matrix_data_5, begin, end, 5, slice_begin, x_slice, y, x, n, accumulate_x_dot_y);
        dot += process_cuper_channel(matrix_data_6, begin, end, 6, slice_begin, x_slice, y, x, n, accumulate_x_dot_y);
        dot += process_cuper_channel(matrix_data_7, begin, end, 7, slice_begin, x_slice, y, x, n, accumulate_x_dot_y);
        dot += process_cuper_channel(matrix_data_8, begin, end, 8, slice_begin, x_slice, y, x, n, accumulate_x_dot_y);
        dot += process_cuper_channel(matrix_data_9, begin, end, 9, slice_begin, x_slice, y, x, n, accumulate_x_dot_y);
        dot += process_cuper_channel(matrix_data_10, begin, end, 10, slice_begin, x_slice, y, x, n, accumulate_x_dot_y);
        dot += process_cuper_channel(matrix_data_11, begin, end, 11, slice_begin, x_slice, y, x, n, accumulate_x_dot_y);
        dot += process_cuper_channel(matrix_data_12, begin, end, 12, slice_begin, x_slice, y, x, n, accumulate_x_dot_y);
        dot += process_cuper_channel(matrix_data_13, begin, end, 13, slice_begin, x_slice, y, x, n, accumulate_x_dot_y);
        dot += process_cuper_channel(matrix_data_14, begin, end, 14, slice_begin, x_slice, y, x, n, accumulate_x_dot_y);
        dot += process_cuper_channel(matrix_data_15, begin, end, 15, slice_begin, x_slice, y, x, n, accumulate_x_dot_y);
    }

    return dot;
}

}  // namespace

extern "C" {

void cuper_pcg_control_kernel(const project_xplus::cgsolver::index_t* sp_element_list_ptr,
                              const unsigned long* matrix_data_0,
                              const unsigned long* matrix_data_1,
                              const unsigned long* matrix_data_2,
                              const unsigned long* matrix_data_3,
                              const unsigned long* matrix_data_4,
                              const unsigned long* matrix_data_5,
                              const unsigned long* matrix_data_6,
                              const unsigned long* matrix_data_7,
                              const unsigned long* matrix_data_8,
                              const unsigned long* matrix_data_9,
                              const unsigned long* matrix_data_10,
                              const unsigned long* matrix_data_11,
                              const unsigned long* matrix_data_12,
                              const unsigned long* matrix_data_13,
                              const unsigned long* matrix_data_14,
                              const unsigned long* matrix_data_15,
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

#pragma HLS INTERFACE m_axi port = sp_element_list_ptr offset = slave bundle = gmem_meta
#pragma HLS INTERFACE m_axi port = matrix_data_0 offset = slave bundle = gmem_matrix0
#pragma HLS INTERFACE m_axi port = matrix_data_1 offset = slave bundle = gmem_matrix1
#pragma HLS INTERFACE m_axi port = matrix_data_2 offset = slave bundle = gmem_matrix2
#pragma HLS INTERFACE m_axi port = matrix_data_3 offset = slave bundle = gmem_matrix3
#pragma HLS INTERFACE m_axi port = matrix_data_4 offset = slave bundle = gmem_matrix4
#pragma HLS INTERFACE m_axi port = matrix_data_5 offset = slave bundle = gmem_matrix5
#pragma HLS INTERFACE m_axi port = matrix_data_6 offset = slave bundle = gmem_matrix6
#pragma HLS INTERFACE m_axi port = matrix_data_7 offset = slave bundle = gmem_matrix7
#pragma HLS INTERFACE m_axi port = matrix_data_8 offset = slave bundle = gmem_matrix8
#pragma HLS INTERFACE m_axi port = matrix_data_9 offset = slave bundle = gmem_matrix9
#pragma HLS INTERFACE m_axi port = matrix_data_10 offset = slave bundle = gmem_matrix10
#pragma HLS INTERFACE m_axi port = matrix_data_11 offset = slave bundle = gmem_matrix11
#pragma HLS INTERFACE m_axi port = matrix_data_12 offset = slave bundle = gmem_matrix12
#pragma HLS INTERFACE m_axi port = matrix_data_13 offset = slave bundle = gmem_matrix13
#pragma HLS INTERFACE m_axi port = matrix_data_14 offset = slave bundle = gmem_matrix14
#pragma HLS INTERFACE m_axi port = matrix_data_15 offset = slave bundle = gmem_matrix15
#pragma HLS INTERFACE m_axi port = b offset = slave bundle = gmem_b
#pragma HLS INTERFACE m_axi port = m_inv offset = slave bundle = gmem_minv
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x
#pragma HLS INTERFACE m_axi port = r offset = slave bundle = gmem_r
#pragma HLS INTERFACE m_axi port = z offset = slave bundle = gmem_z
#pragma HLS INTERFACE m_axi port = p offset = slave bundle = gmem_p
#pragma HLS INTERFACE m_axi port = ap offset = slave bundle = gmem_ap
#pragma HLS INTERFACE m_axi port = metrics offset = slave bundle = gmem_metrics
#pragma HLS INTERFACE m_axi port = status offset = slave bundle = gmem_status

    data_t rz = 0.0;
    data_t rr = 0.0;
    data_t p_ap = 0.0;
    data_t alpha = 0.0;
    data_t beta = 0.0;
    int iterations = 0;
    int status_code = kStatusMaxIter;

    // 参数非法时直接返回 breakdown，避免 HLS kernel 内部访问空范围或生成 NaN。
    if (n <= 0 || batch_count <= 0 || max_iters < 0 || tau <= 0.0 || invalid_scalar(tau)) {
        status[0] = kStatusBreakdown;
        status[1] = 0;
        metrics[0] = 0.0;
        metrics[1] = 0.0;
        metrics[2] = 0.0;
        metrics[3] = 0.0;
        return;
    }

    // 初始 SpMV：ap = A * x0。此时只需要 ap，不需要 dot product。
    (void)cuper_packed_spmv_and_dot(sp_element_list_ptr,
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
                                    ap,
                                    batch_count,
                                    n,
                                    false);

init_vectors:
    for (int index = 0; index < n; ++index) {
#pragma HLS PIPELINE II = 1
        // 标准 Jacobi 预条件 PCG 初始化：
        // r0 = b - A*x0, z0 = M^-1*r0, p0 = z0。
        const data_t r_value = b[index] - ap[index];
        const data_t z_value = m_inv[index] * r_value;
        r[index] = r_value;
        z[index] = z_value;
        p[index] = z_value;
        rz += r_value * z_value;
        rr += r_value * r_value;
    }

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

        // ap = A*p，同时在 SpMV 写回阶段累计 p_ap = p^T * ap。
        p_ap = cuper_packed_spmv_and_dot(sp_element_list_ptr,
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
                                         ap,
                                         batch_count,
                                         n,
                                         true);

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

}
