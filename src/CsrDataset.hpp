#ifndef PROJECT_XPLUS_CSR_DATASET_HPP
#define PROJECT_XPLUS_CSR_DATASET_HPP

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace project_xplus::cgsolver {

namespace fs = std::filesystem;

class CsrDataset {
  public:
    static CsrDataset load(const fs::path& dataset_dir) {
        CsrDataset dataset;
        dataset.row_ptr_ = read_array_file<int>(dataset_dir / "row_ptr.txt");
        dataset.col_idx_ = read_array_file<int>(dataset_dir / "col_idx.txt");
        dataset.values_ = read_array_file<double>(dataset_dir / "values.txt");

        if (dataset.row_ptr_.size() < 2) {
            throw std::runtime_error("row_ptr.txt must contain at least 2 entries");
        }

        dataset.n_ = static_cast<int>(dataset.row_ptr_.size()) - 1;
        dataset.nnz_ = static_cast<int>(dataset.col_idx_.size());

        if (dataset.values_.size() != dataset.col_idx_.size()) {
            throw std::runtime_error("col_idx.txt and values.txt size mismatch");
        }
        if (dataset.row_ptr_.front() != 0) {
            throw std::runtime_error("row_ptr[0] must be 0");
        }
        if (dataset.row_ptr_.back() != dataset.nnz_) {
            throw std::runtime_error("row_ptr[n] must equal nnz");
        }

        for (int row = 0; row < dataset.n_; ++row) {
            const int row_begin = dataset.row_ptr_[static_cast<std::size_t>(row)];
            const int row_end = dataset.row_ptr_[static_cast<std::size_t>(row + 1)];
            if (row_begin > row_end) {
                throw std::runtime_error("row_ptr must be nondecreasing");
            }
            for (int offset = row_begin; offset < row_end; ++offset) {
                const int col = dataset.col_idx_[static_cast<std::size_t>(offset)];
                if (col < 0 || col >= dataset.n_) {
                    throw std::runtime_error("col_idx out of range");
                }
            }
        }

        dataset.b_ = read_array_file<double>(dataset_dir / "b.txt");
        dataset.x0_ = read_array_file<double>(dataset_dir / "x0.txt");

        if (static_cast<int>(dataset.b_.size()) != dataset.n_) {
            throw std::runtime_error("b.txt length must equal n");
        }
        if (static_cast<int>(dataset.x0_.size()) != dataset.n_) {
            throw std::runtime_error("x0.txt length must equal n");
        }

        return dataset;
    }

    int n() const { return n_; }
    int nnz() const { return nnz_; }

    const std::vector<int>& row_ptr() const { return row_ptr_; }
    const std::vector<int>& col_idx() const { return col_idx_; }
    const std::vector<double>& values() const { return values_; }
    const std::vector<double>& b() const { return b_; }
    const std::vector<double>& x0() const { return x0_; }

    void spmv(const std::vector<double>& x, std::vector<double>& y) const {
        y.assign(static_cast<std::size_t>(n_), 0.0);
        for (int row = 0; row < n_; ++row) {
            double acc = 0.0;
            for (int offset = row_ptr_[static_cast<std::size_t>(row)];
                 offset < row_ptr_[static_cast<std::size_t>(row + 1)];
                 ++offset) {
                acc += values_[static_cast<std::size_t>(offset)] *
                       x[static_cast<std::size_t>(col_idx_[static_cast<std::size_t>(offset)])];
            }
            y[static_cast<std::size_t>(row)] = acc;
        }
    }

    std::vector<double> extract_jacobi_diag() const {
        std::vector<double> diag(static_cast<std::size_t>(n_), 0.0);

        for (int row = 0; row < n_; ++row) {
            bool found = false;
            for (int offset = row_ptr_[static_cast<std::size_t>(row)];
                 offset < row_ptr_[static_cast<std::size_t>(row + 1)];
                 ++offset) {
                if (col_idx_[static_cast<std::size_t>(offset)] == row) {
                    diag[static_cast<std::size_t>(row)] = values_[static_cast<std::size_t>(offset)];
                    found = true;
                    break;
                }
            }

            if (!found) {
                throw std::runtime_error("missing diagonal entry at row " + std::to_string(row));
            }
            if (std::fabs(diag[static_cast<std::size_t>(row)]) <= std::numeric_limits<double>::min()) {
                throw std::runtime_error("zero diagonal entry at row " + std::to_string(row));
            }
        }

        return diag;
    }

  private:
    template <typename T>
    static std::vector<T> read_array_file(const fs::path& path) {
        std::ifstream input(path);
        if (!input) {
            throw std::runtime_error("failed to open " + path.string());
        }

        std::vector<T> values;
        T value{};
        while (input >> value) {
            values.push_back(value);
        }

        if (values.empty()) {
            throw std::runtime_error("empty array file: " + path.string());
        }

        return values;
    }

    int n_ = 0;
    int nnz_ = 0;
    std::vector<int> row_ptr_;
    std::vector<int> col_idx_;
    std::vector<double> values_;
    std::vector<double> b_;
    std::vector<double> x0_;
};

inline double dot(const std::vector<double>& lhs, const std::vector<double>& rhs) {
    double acc = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        acc += lhs[index] * rhs[index];
    }
    return acc;
}

inline double l2_norm(const std::vector<double>& values) {
    return std::sqrt(dot(values, values));
}

}  // namespace project_xplus::cgsolver

#endif
