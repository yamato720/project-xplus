#include "../include/cg_common.hpp"

namespace {

using data_t = project_xplus::cgsolver::data_t;
using index_t = project_xplus::cgsolver::index_t;

constexpr data_t kBreakdownEps = project_xplus::cgsolver::kBreakdownEps;
constexpr int kCuperSliceWidth = 8192;
constexpr int kCuperRowTileHeight =
    project_xplus::cgsolver::kSpmvRowTileBlockRows * project_xplus::cgsolver::kSpmvBlockSize;

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

void load_x_slice(const data_t* x,
                  data_t x_slice[kCuperSliceWidth],
                  const int slice_begin,
                  const int n) {
#pragma HLS INLINE off
load_x_slice_loop:
    for (int offset = 0; offset < kCuperSliceWidth; ++offset) {
#pragma HLS PIPELINE II = 1
        const int global_col = slice_begin + offset;
        x_slice[offset] = (global_col < n) ? x[global_col] : 0.0;
    }
}

void zero_y_tile(data_t y_tile[kCuperRowTileHeight], const int tile_entries) {
#pragma HLS INLINE off
zero_y_tile_loop:
    for (int offset = 0; offset < tile_entries; ++offset) {
#pragma HLS PIPELINE II = 1
        y_tile[offset] = 0.0;
    }
}

void flush_row_sum(data_t y_tile[kCuperRowTileHeight],
                   const int local_row,
                   const data_t row_sum) {
#pragma HLS INLINE
    if (local_row >= 0 && local_row < kCuperRowTileHeight) {
        y_tile[local_row] += row_sum;
    }
}

void accumulate_batch_tile(const index_t* batch_tile_ptr,
                           const index_t* rows,
                           const index_t* cols,
                           const float* values,
                           const data_t x_slice[kCuperSliceWidth],
                           data_t y_tile[kCuperRowTileHeight],
                           const int batch,
                           const int row_tile,
                           const int row_tile_count,
                           const int row_tile_begin,
                           const int slice_begin) {
#pragma HLS INLINE off
    const int ptr_base = batch * (row_tile_count + 1) + row_tile;
    const int begin = batch_tile_ptr[ptr_base];
    const int end = batch_tile_ptr[ptr_base + 1];

    int current_local_row = -1;
    data_t row_sum = 0.0;

spmv_batch_tile_elements:
    for (int offset = begin; offset < end; ++offset) {
#pragma HLS PIPELINE II = 1
        const int row = rows[offset];
        const int local_row = row - row_tile_begin;
        const int local_col = cols[offset] - slice_begin;
        const data_t x_value =
            (local_col >= 0 && local_col < kCuperSliceWidth) ? x_slice[local_col] : 0.0;
        const data_t product = static_cast<data_t>(values[offset]) * x_value;

        if (local_row != current_local_row) {
            flush_row_sum(y_tile, current_local_row, row_sum);
            current_local_row = local_row;
            row_sum = product;
        } else {
            row_sum += product;
        }
    }

    flush_row_sum(y_tile, current_local_row, row_sum);
}

data_t write_y_tile(data_t* y,
                    const data_t* x,
                    const data_t y_tile[kCuperRowTileHeight],
                    const int n,
                    const int row_tile_begin,
                    const int tile_entries,
                    const bool accumulate_x_dot_y) {
#pragma HLS INLINE off
    data_t dot = 0.0;
write_y_tile_loop:
    for (int offset = 0; offset < tile_entries; ++offset) {
#pragma HLS PIPELINE II = 1
        const int row = row_tile_begin + offset;
        if (row < n) {
            const data_t y_value = y_tile[offset];
            y[row] = y_value;
            if (accumulate_x_dot_y) {
                dot += x[row] * y_value;
            }
        }
    }
    return dot;
}

data_t cuper_tiled_spmv_and_dot(const index_t* batch_tile_ptr,
                                const index_t* rows,
                                const index_t* cols,
                                const float* values,
                                const data_t* x,
                                data_t* y,
                                const int batch_count,
                                const int n,
                                const bool accumulate_x_dot_y) {
#pragma HLS INLINE off
    const int row_tile_count = (n + kCuperRowTileHeight - 1) / kCuperRowTileHeight;
    data_t dot = 0.0;

    data_t x_slice[kCuperSliceWidth];
    data_t y_tile[kCuperRowTileHeight];
#pragma HLS BIND_STORAGE variable = x_slice type = ram_2p impl = bram
#pragma HLS BIND_STORAGE variable = y_tile type = ram_2p impl = bram

row_tiles:
    for (int row_tile = 0; row_tile < row_tile_count; ++row_tile) {
        const int row_tile_begin = row_tile * kCuperRowTileHeight;
        const int remaining_rows = n - row_tile_begin;
        const int tile_entries =
            (remaining_rows < kCuperRowTileHeight) ? remaining_rows : kCuperRowTileHeight;

        zero_y_tile(y_tile, tile_entries);

    column_batches:
        for (int batch = 0; batch < batch_count; ++batch) {
            const int slice_begin = batch * kCuperSliceWidth;
            load_x_slice(x, x_slice, slice_begin, n);
            accumulate_batch_tile(batch_tile_ptr,
                                  rows,
                                  cols,
                                  values,
                                  x_slice,
                                  y_tile,
                                  batch,
                                  row_tile,
                                  row_tile_count,
                                  row_tile_begin,
                                  slice_begin);
        }

        dot += write_y_tile(y,
                            x,
                            y_tile,
                            n,
                            row_tile_begin,
                            tile_entries,
                            accumulate_x_dot_y);
    }

    return dot;
}

}  // namespace

extern "C" {

void cuper_pcg_control_kernel(const project_xplus::cgsolver::index_t* batch_ptr,
                              const project_xplus::cgsolver::index_t* batch_tile_ptr,
                              const project_xplus::cgsolver::index_t* element_rows,
                              const project_xplus::cgsolver::index_t* element_cols,
                              const float* element_values,
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
// 和 pcg_control_kernel.cpp 的目标一致：
//   host 只 launch 一次 kernel
//   kernel 内部完成 init + PCG loop + 收敛/breakdown 判断
//
// 区别是 SpMV 输入格式按 Cuper 的列窗口 batch 组织：
//   batch_ptr / batch_tile_ptr / element_rows / element_cols / element_values
// 每个 batch 对应 8192 宽列窗口；每个 batch 内再按 8192 行 row tile
// 建立 offset。SpMV 计算时把 x 的当前列 slice 和 y 的当前 row tile
// 放在片上 BRAM 中累加，最后一次性写回 ap/y。
// 后续可以继续把 accumulate_batch_tile 替换为 DLC/Cuper 的多 HBM/PE 管线。
#pragma HLS INTERFACE s_axilite port = batch_ptr bundle = control
#pragma HLS INTERFACE s_axilite port = batch_tile_ptr bundle = control
#pragma HLS INTERFACE s_axilite port = element_rows bundle = control
#pragma HLS INTERFACE s_axilite port = element_cols bundle = control
#pragma HLS INTERFACE s_axilite port = element_values bundle = control
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

#pragma HLS INTERFACE m_axi port = batch_ptr offset = slave bundle = gmem_meta
#pragma HLS INTERFACE m_axi port = batch_tile_ptr offset = slave bundle = gmem_meta
#pragma HLS INTERFACE m_axi port = element_rows offset = slave bundle = gmem_row
#pragma HLS INTERFACE m_axi port = element_cols offset = slave bundle = gmem_col
#pragma HLS INTERFACE m_axi port = element_values offset = slave bundle = gmem_val
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
    (void)batch_ptr;

    if (n <= 0 || batch_count <= 0 || max_iters < 0 || tau <= 0.0 || invalid_scalar(tau)) {
        status[0] = kStatusBreakdown;
        status[1] = 0;
        metrics[0] = 0.0;
        metrics[1] = 0.0;
        metrics[2] = 0.0;
        metrics[3] = 0.0;
        return;
    }

    (void)cuper_tiled_spmv_and_dot(batch_tile_ptr,
                                   element_rows,
                                   element_cols,
                                   element_values,
                                   x,
                                   ap,
                                   batch_count,
                                   n,
                                   false);

init_vectors:
    for (int index = 0; index < n; ++index) {
#pragma HLS PIPELINE II = 1
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

        p_ap = cuper_tiled_spmv_and_dot(batch_tile_ptr,
                                        element_rows,
                                        element_cols,
                                        element_values,
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
            p[index] = z[index] + beta * p[index];
        }
    }

    if (status_code == kStatusMaxIter && rr <= tau) {
        status_code = kStatusConverged;
    }

    metrics[0] = rz;
    metrics[1] = rr;
    metrics[2] = p_ap;
    metrics[3] = alpha;
    status[0] = status_code;
    status[1] = iterations;
}

}
