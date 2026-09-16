#include "sparse_matrix.hpp"
#include <algorithm>
#include <numeric>
#include <unordered_map>

namespace hypernova::numerical {

SparseMatrix SparseMatrix::from_triplets(std::size_t rows, std::size_t cols,
                                          const std::vector<Triplet>& triplets,
                                          StorageOrder order) {
    SparseMatrix mat(rows, cols, order);

    if (triplets.empty()) {
        mat.row_ptr_.assign(rows + 1, 0);
        return mat;
    }

    std::vector<Triplet> sorted = triplets;
    if (order == StorageOrder::CSR) {
        std::sort(sorted.begin(), sorted.end(),
                  [](const Triplet& a, const Triplet& b) {
                      if (a.row != b.row) return a.row < b.row;
                      return a.col < b.col;
                  });
    } else {
        std::sort(sorted.begin(), sorted.end(),
                  [](const Triplet& a, const Triplet& b) {
                      if (a.col != b.col) return a.col < b.col;
                      return a.row < b.row;
                  });
    }

    std::size_t major_dim = (order == StorageOrder::CSR) ? rows : cols;
    mat.row_ptr_.assign(major_dim + 1, 0);

    std::size_t current_major = 0;
    for (const auto& t : sorted) {
        std::size_t major = (order == StorageOrder::CSR) ? t.row : t.col;
        while (current_major < major) {
            mat.row_ptr_[current_major + 1] = mat.values_.size();
            ++current_major;
        }
        mat.values_.push_back(t.value);
        mat.col_indices_.push_back((order == StorageOrder::CSR) ? t.col : t.row);
    }
    while (current_major < major_dim) {
        mat.row_ptr_[current_major + 1] = mat.values_.size();
        ++current_major;
    }

    mat.sum_duplicates();
    return mat;
}

double SparseMatrix::get(std::size_t row, std::size_t col) const {
    if (order_ != StorageOrder::CSR) {
        throw std::logic_error("get() only supported for CSR format");
    }
    if (row >= rows_ || col >= cols_) return 0.0;

    for (std::size_t i = row_ptr_[row]; i < row_ptr_[row + 1]; ++i) {
        if (col_indices_[i] == col) return values_[i];
    }
    return 0.0;
}

void SparseMatrix::set(std::size_t row, std::size_t col, double value) {
    if (order_ != StorageOrder::CSR) {
        throw std::logic_error("set() only supported for CSR format");
    }
    if (row >= rows_ || col >= cols_) return;

    for (std::size_t i = row_ptr_[row]; i < row_ptr_[row + 1]; ++i) {
        if (col_indices_[i] == col) {
            values_[i] = value;
            return;
        }
    }
    if (value != 0.0) {
        throw std::logic_error("Cannot insert new non-zero in fixed structure. Use builder.");
    }
}

void SparseMatrix::transpose() {
    std::vector<Triplet> triplets = to_triplets();
    for (auto& t : triplets) {
        std::swap(t.row, t.col);
    }
    *this = from_triplets(cols_, rows_, triplets, order_);
}

SparseMatrix SparseMatrix::transposed() const {
    SparseMatrix result = *this;
    result.transpose();
    return result;
}

void SparseMatrix::scale_rows(const std::vector<double>& scales) {
    if (scales.size() != rows_) throw std::invalid_argument("Scale vector size mismatch");

    if (order_ == StorageOrder::CSR) {
        for (std::size_t i = 0; i < rows_; ++i) {
            double s = scales[i];
            if (s == 1.0) continue;
            for (std::size_t j = row_ptr_[i]; j < row_ptr_[i + 1]; ++j) {
                values_[j] *= s;
            }
        }
    } else {
        for (std::size_t j = 0; j < nnz(); ++j) {
            std::size_t row = col_indices_[j];
            values_[j] *= scales[row];
        }
    }
}

void SparseMatrix::scale_cols(const std::vector<double>& scales) {
    if (scales.size() != cols_) throw std::invalid_argument("Scale vector size mismatch");

    if (order_ == StorageOrder::CSR) {
        for (std::size_t j = 0; j < nnz(); ++j) {
            std::size_t col = col_indices_[j];
            values_[j] *= scales[col];
        }
    } else {
        for (std::size_t i = 0; i < cols_; ++i) {
            double s = scales[i];
            if (s == 1.0) continue;
            for (std::size_t j = row_ptr_[i]; j < row_ptr_[i + 1]; ++j) {
                values_[j] *= s;
            }
        }
    }
}

std::vector<double> SparseMatrix::multiply(const std::vector<double>& x) const {
    if (x.size() != cols_) throw std::invalid_argument("Vector size mismatch");

    std::vector<double> result(rows_, 0.0);

    if (order_ == StorageOrder::CSR) {
        for (std::size_t i = 0; i < rows_; ++i) {
            double sum = 0.0;
            for (std::size_t j = row_ptr_[i]; j < row_ptr_[i + 1]; ++j) {
                sum += values_[j] * x[col_indices_[j]];
            }
            result[i] = sum;
        }
    } else {
        for (std::size_t j = 0; j < cols_; ++j) {
            double xj = x[j];
            if (xj == 0.0) continue;
            for (std::size_t k = row_ptr_[j]; k < row_ptr_[j + 1]; ++k) {
                result[col_indices_[k]] += values_[k] * xj;
            }
        }
    }
    return result;
}

std::vector<double> SparseMatrix::transpose_multiply(const std::vector<double>& x) const {
    if (x.size() != rows_) throw std::invalid_argument("Vector size mismatch");

    std::vector<double> result(cols_, 0.0);

    if (order_ == StorageOrder::CSR) {
        for (std::size_t i = 0; i < rows_; ++i) {
            double xi = x[i];
            if (xi == 0.0) continue;
            for (std::size_t j = row_ptr_[i]; j < row_ptr_[i + 1]; ++j) {
                result[col_indices_[j]] += values_[j] * xi;
            }
        }
    } else {
        for (std::size_t j = 0; j < cols_; ++j) {
            double sum = 0.0;
            for (std::size_t k = row_ptr_[j]; k < row_ptr_[j + 1]; ++k) {
                sum += values_[k] * x[col_indices_[k]];
            }
            result[j] = sum;
        }
    }
    return result;
}

void SparseMatrix::sort_indices() {
    if (order_ != StorageOrder::CSR) return;

    for (std::size_t i = 0; i < rows_; ++i) {
        std::size_t start = row_ptr_[i];
        std::size_t end = row_ptr_[i + 1];
        if (end <= start + 1) continue;

        std::vector<std::pair<std::size_t, double>> entries;
        entries.reserve(end - start);
        for (std::size_t j = start; j < end; ++j) {
            entries.emplace_back(col_indices_[j], values_[j]);
        }
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (std::size_t j = 0; j < entries.size(); ++j) {
            col_indices_[start + j] = entries[j].first;
            values_[start + j] = entries[j].second;
        }
    }
}

void SparseMatrix::remove_zeros(double tol) {
    std::size_t write_idx = 0;
    std::size_t major_dim = (order_ == StorageOrder::CSR) ? rows_ : cols_;
    std::vector<std::size_t> new_row_ptr(major_dim + 1, 0);

    for (std::size_t i = 0; i < major_dim; ++i) {
        new_row_ptr[i] = write_idx;
        for (std::size_t j = row_ptr_[i]; j < row_ptr_[i + 1]; ++j) {
            if (std::abs(values_[j]) > tol) {
                values_[write_idx] = values_[j];
                col_indices_[write_idx] = col_indices_[j];
                ++write_idx;
            }
        }
    }
    new_row_ptr[major_dim] = write_idx;

    values_.resize(write_idx);
    col_indices_.resize(write_idx);
    row_ptr_ = std::move(new_row_ptr);
}

void SparseMatrix::sum_duplicates() {
    if (order_ != StorageOrder::CSR) return;

    std::size_t write_idx = 0;
    std::vector<std::size_t> new_row_ptr(rows_ + 1, 0);

    for (std::size_t i = 0; i < rows_; ++i) {
        new_row_ptr[i] = write_idx;
        std::size_t start = row_ptr_[i];
        std::size_t end = row_ptr_[i + 1];

        std::size_t j = start;
        while (j < end) {
            std::size_t col = col_indices_[j];
            double sum = values_[j];
            ++j;
            while (j < end && col_indices_[j] == col) {
                sum += values_[j];
                ++j;
            }
            if (sum != 0.0) {
                values_[write_idx] = sum;
                col_indices_[write_idx] = col;
                ++write_idx;
            }
        }
    }
    new_row_ptr[rows_] = write_idx;

    values_.resize(write_idx);
    col_indices_.resize(write_idx);
    row_ptr_ = std::move(new_row_ptr);
}

std::vector<Triplet> SparseMatrix::to_triplets() const {
    std::vector<Triplet> triplets;
    triplets.reserve(nnz());

    if (order_ == StorageOrder::CSR) {
        for (std::size_t i = 0; i < rows_; ++i) {
            for (std::size_t j = row_ptr_[i]; j < row_ptr_[i + 1]; ++j) {
                triplets.emplace_back(i, col_indices_[j], values_[j]);
            }
        }
    } else {
        for (std::size_t j = 0; j < cols_; ++j) {
            for (std::size_t k = row_ptr_[j]; k < row_ptr_[j + 1]; ++k) {
                triplets.emplace_back(col_indices_[k], j, values_[k]);
            }
        }
    }
    return triplets;
}

bool SparseMatrix::is_symmetric(double tol) const {
    if (rows_ != cols_) return false;
    if (order_ != StorageOrder::CSR) return false;

    for (std::size_t i = 0; i < rows_; ++i) {
        for (std::size_t j = row_ptr_[i]; j < row_ptr_[i + 1]; ++j) {
            std::size_t col = col_indices_[j];
            double val = values_[j];
            double sym_val = get(col, i);
            if (std::abs(val - sym_val) > tol) return false;
        }
    }
    return true;
}

void SparseMatrix::reserve(std::size_t nnz_estimate) {
    values_.reserve(nnz_estimate);
    col_indices_.reserve(nnz_estimate);
}

std::vector<double> spmv_csr(const SparseMatrix& A, const std::vector<double>& x) {
    return A.multiply(x);
}

std::vector<double> spmv_csc(const SparseMatrix& A, const std::vector<double>& x) {
    return A.multiply(x);
}

std::vector<double> spmv_csr_transpose(const SparseMatrix& A, const std::vector<double>& x) {
    return A.transpose_multiply(x);
}

} // namespace hypernova::numerical