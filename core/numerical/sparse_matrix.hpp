#pragma once

#include <vector>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <memory>

namespace hypernova::numerical {

enum class StorageOrder { CSR, CSC };

struct Triplet {
    std::size_t row;
    std::size_t col;
    double value;

    Triplet() = default;
    Triplet(std::size_t r, std::size_t c, double v) : row(r), col(c), value(v) {}

    bool operator<(const Triplet& other) const {
        if (row != other.row) return row < other.row;
        return col < other.col;
    }
};

class SparseMatrix {
public:
    SparseMatrix() = default;
    SparseMatrix(std::size_t rows, std::size_t cols, StorageOrder order = StorageOrder::CSR)
        : rows_(rows), cols_(cols), order_(order) {}

    SparseMatrix(const SparseMatrix&) = default;
    SparseMatrix& operator=(const SparseMatrix&) = default;
    SparseMatrix(SparseMatrix&&) = default;
    SparseMatrix& operator=(SparseMatrix&&) = default;

    static SparseMatrix from_triplets(std::size_t rows, std::size_t cols,
                                       const std::vector<Triplet>& triplets,
                                       StorageOrder order = StorageOrder::CSR);

    std::size_t rows() const { return rows_; }
    std::size_t cols() const { return cols_; }
    std::size_t nnz() const { return values_.size(); }
    StorageOrder order() const { return order_; }

    const std::vector<double>& values() const { return values_; }
    const std::vector<std::size_t>& col_indices() const { return col_indices_; }
    const std::vector<std::size_t>& row_ptr() const { return row_ptr_; }

    std::vector<double>& mutable_values() { return values_; }
    std::vector<std::size_t>& mutable_col_indices() { return col_indices_; }
    std::vector<std::size_t>& mutable_row_ptr() { return row_ptr_; }
    std::size_t& mutable_rows() { return rows_; }
    std::size_t& mutable_cols() { return cols_; }

    double get(std::size_t row, std::size_t col) const;
    void set(std::size_t row, std::size_t col, double value);

    void transpose();
    SparseMatrix transposed() const;

    void scale_rows(const std::vector<double>& scales);
    void scale_cols(const std::vector<double>& scales);

    std::vector<double> multiply(const std::vector<double>& x) const;
    std::vector<double> transpose_multiply(const std::vector<double>& x) const;

    void sort_indices();
    void remove_zeros(double tol = 1e-15);
    void sum_duplicates();

    std::vector<Triplet> to_triplets() const;

    bool is_symmetric(double tol = 1e-12) const;

    void reserve(std::size_t nnz_estimate);

private:
    std::size_t rows_ = 0;
    std::size_t cols_ = 0;
    StorageOrder order_ = StorageOrder::CSR;
    std::vector<double> values_;
    std::vector<std::size_t> col_indices_;
    std::vector<std::size_t> row_ptr_;
};

class SparseMatrixBuilder {
public:
    SparseMatrixBuilder(std::size_t rows, std::size_t cols)
        : rows_(rows), cols_(cols) {}

    void add_entry(std::size_t row, std::size_t col, double value) {
        if (value != 0.0) {
            triplets_.emplace_back(row, col, value);
        }
    }

    SparseMatrix build(StorageOrder order = StorageOrder::CSR) {
        return SparseMatrix::from_triplets(rows_, cols_, triplets_, order);
    }

private:
    std::size_t rows_;
    std::size_t cols_;
    std::vector<Triplet> triplets_;
};

std::vector<double> spmv_csr(const SparseMatrix& A, const std::vector<double>& x);
std::vector<double> spmv_csc(const SparseMatrix& A, const std::vector<double>& x);
std::vector<double> spmv_csr_transpose(const SparseMatrix& A, const std::vector<double>& x);

} // namespace hypernova::numerical