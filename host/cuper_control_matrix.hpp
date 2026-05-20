#ifndef PROJECT_XPLUS_CUPER_CONTROL_MATRIX_HPP
#define PROJECT_XPLUS_CUPER_CONTROL_MATRIX_HPP

#include "../include/cg_common.hpp"
#include "dataset_bridge.hpp"

#include <cstddef>
#include <vector>

namespace project_xplus::cgsolver {

struct CuperControlMatrix {
    static constexpr int kSliceWidth = 8192;
    static constexpr int kRowTileHeight = kSpmvRowTileBlockRows * kSpmvBlockSize;

    std::vector<index_t> batch_ptr;
    std::vector<index_t> batch_tile_ptr;
    std::vector<index_t> element_rows;
    std::vector<index_t> element_cols;
    std::vector<float> element_values;
    int batch_count = 0;
    int row_tile_count = 0;
};

inline CuperControlMatrix build_cuper_control_matrix(const Dataset& dataset) {
    CuperControlMatrix matrix;
    matrix.batch_count = (dataset.n() + CuperControlMatrix::kSliceWidth - 1) /
                         CuperControlMatrix::kSliceWidth;
    matrix.row_tile_count = (dataset.n() + CuperControlMatrix::kRowTileHeight - 1) /
                            CuperControlMatrix::kRowTileHeight;
    matrix.batch_ptr.assign(static_cast<std::size_t>(matrix.batch_count + 1), 0);
    std::vector<index_t> tile_counts(
        static_cast<std::size_t>(matrix.batch_count * matrix.row_tile_count), 0);

    for (int row = 0; row < dataset.n(); ++row) {
        const int row_tile = row / CuperControlMatrix::kRowTileHeight;
        for (int offset = dataset.row_ptr()[static_cast<std::size_t>(row)];
             offset < dataset.row_ptr()[static_cast<std::size_t>(row + 1)];
             ++offset) {
            const int col = dataset.col_idx()[static_cast<std::size_t>(offset)];
            const int batch = col / CuperControlMatrix::kSliceWidth;
            ++matrix.batch_ptr[static_cast<std::size_t>(batch + 1)];
            ++tile_counts[static_cast<std::size_t>(batch * matrix.row_tile_count + row_tile)];
        }
    }

    for (int batch = 0; batch < matrix.batch_count; ++batch) {
        matrix.batch_ptr[static_cast<std::size_t>(batch + 1)] +=
            matrix.batch_ptr[static_cast<std::size_t>(batch)];
    }

    matrix.batch_tile_ptr.assign(
        static_cast<std::size_t>(matrix.batch_count * (matrix.row_tile_count + 1)), 0);
    for (int batch = 0; batch < matrix.batch_count; ++batch) {
        const std::size_t base =
            static_cast<std::size_t>(batch * (matrix.row_tile_count + 1));
        index_t offset = matrix.batch_ptr[static_cast<std::size_t>(batch)];
        matrix.batch_tile_ptr[base] = offset;
        for (int row_tile = 0; row_tile < matrix.row_tile_count; ++row_tile) {
            offset += tile_counts[static_cast<std::size_t>(
                batch * matrix.row_tile_count + row_tile)];
            matrix.batch_tile_ptr[base + static_cast<std::size_t>(row_tile + 1)] = offset;
        }
    }

    matrix.element_rows.assign(static_cast<std::size_t>(dataset.nnz()), 0);
    matrix.element_cols.assign(static_cast<std::size_t>(dataset.nnz()), 0);
    matrix.element_values.assign(static_cast<std::size_t>(dataset.nnz()), 0.0f);

    std::vector<index_t> cursor(
        static_cast<std::size_t>(matrix.batch_count * matrix.row_tile_count), 0);
    for (int batch = 0; batch < matrix.batch_count; ++batch) {
        const std::size_t base =
            static_cast<std::size_t>(batch * (matrix.row_tile_count + 1));
        for (int row_tile = 0; row_tile < matrix.row_tile_count; ++row_tile) {
            cursor[static_cast<std::size_t>(batch * matrix.row_tile_count + row_tile)] =
                matrix.batch_tile_ptr[base + static_cast<std::size_t>(row_tile)];
        }
    }

    for (int row = 0; row < dataset.n(); ++row) {
        const int row_tile = row / CuperControlMatrix::kRowTileHeight;
        for (int offset = dataset.row_ptr()[static_cast<std::size_t>(row)];
             offset < dataset.row_ptr()[static_cast<std::size_t>(row + 1)];
             ++offset) {
            const int col = dataset.col_idx()[static_cast<std::size_t>(offset)];
            const int batch = col / CuperControlMatrix::kSliceWidth;
            const int out =
                cursor[static_cast<std::size_t>(batch * matrix.row_tile_count + row_tile)]++;
            matrix.element_rows[static_cast<std::size_t>(out)] = row;
            matrix.element_cols[static_cast<std::size_t>(out)] = col;
            matrix.element_values[static_cast<std::size_t>(out)] =
                static_cast<float>(dataset.values()[static_cast<std::size_t>(offset)]);
        }
    }

    return matrix;
}

}  // namespace project_xplus::cgsolver

#endif
