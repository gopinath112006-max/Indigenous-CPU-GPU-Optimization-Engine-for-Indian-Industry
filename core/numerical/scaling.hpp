#pragma once

#include "sparse_matrix.hpp"
#include <vector>
#include <cstddef>
#include <optional>

namespace hypernova::numerical {

enum class ScalingMethod {
    NONE,
    GEOMETRIC,
    CURTIS_REID,
    EQUILIBRATION
};

struct ScalingResult {
    std::vector<double> row_scales;
    std::vector<double> col_scales;
    double max_row_scale = 1.0;
    double max_col_scale = 1.0;
    double min_row_scale = 1.0;
    double min_col_scale = 1.0;
    int iterations = 0;
    bool converged = true;
};

class MatrixScaler {
public:
    explicit MatrixScaler(ScalingMethod method = ScalingMethod::GEOMETRIC)
        : method_(method) {}

    ScalingMethod method() const { return method_; }
    void set_method(ScalingMethod m) { method_ = m; }

    ScalingResult compute_scales(const SparseMatrix& A,
                                  const std::vector<double>& row_bounds = {},
                                  const std::vector<double>& col_bounds = {});

    void apply_scales(SparseMatrix& A,
                       std::vector<double>& rhs,
                       std::vector<double>& obj,
                       std::vector<double>& lb,
                       std::vector<double>& ub,
                       const ScalingResult& scales) const;

    void unscale_solution(const ScalingResult& scales,
                           std::vector<double>& primal,
                           std::vector<double>& dual) const;

    static ScalingResult geometric_scaling(const SparseMatrix& A, int max_iter = 10);
    static ScalingResult curtis_reid_scaling(const SparseMatrix& A, int max_iter = 10);
    static ScalingResult equilibration_scaling(const SparseMatrix& A, int max_iter = 20);

private:
    ScalingMethod method_;
};

ScalingResult geometric_scaling_impl(const SparseMatrix& A, int max_iter);
ScalingResult curtis_reid_scaling_impl(const SparseMatrix& A, int max_iter);
ScalingResult equilibration_scaling_impl(const SparseMatrix& A, int max_iter);

} // namespace hypernova::numerical