#include "scaling.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace hypernova::numerical {

namespace {

// Row and column maxima of the *effectively scaled* matrix |R*A*C|} under the
// current accumulated row/column scales. A Ruiz-style pass must measure the
// current iteration's working matrix; recomputing maxima from the raw A every
// pass multiplies the same factor into the accumulated scales forever and
// never converges to a fixed point.
void effective_maxima(const SparseMatrix& A,
                      const std::vector<double>& row_scales,
                      const std::vector<double>& col_scales,
                      std::vector<double>& row_max,
                      std::vector<double>& col_max) {
    std::fill(row_max.begin(), row_max.end(), 0.0);
    std::fill(col_max.begin(), col_max.end(), 0.0);
    if (A.order() == StorageOrder::CSR) {
        for (std::size_t i = 0; i < A.rows(); ++i) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                const std::size_t j = A.col_indices()[k];
                const double val = std::abs(A.values()[k]) * row_scales[i] * col_scales[j];
                if (val > row_max[i]) row_max[i] = val;
                if (val > col_max[j]) col_max[j] = val;
            }
        }
    } else {
        for (std::size_t j = 0; j < A.cols(); ++j) {
            for (std::size_t k = A.row_ptr()[j]; k < A.row_ptr()[j + 1]; ++k) {
                const std::size_t i = A.col_indices()[k];
                const double val = std::abs(A.values()[k]) * row_scales[i] * col_scales[j];
                if (val > row_max[i]) row_max[i] = val;
                if (val > col_max[j]) col_max[j] = val;
            }
        }
    }
}

} // namespace

ScalingResult MatrixScaler::compute_scales(const SparseMatrix& A,
                                            const std::vector<double>& /*row_bounds*/,
                                            const std::vector<double>& /*col_bounds*/) {
    switch (method_) {
        case ScalingMethod::GEOMETRIC:
            return geometric_scaling_impl(A, 10);
        case ScalingMethod::CURTIS_REID:
            return curtis_reid_scaling_impl(A, 10);
        case ScalingMethod::EQUILIBRATION:
            return equilibration_scaling_impl(A, 20);
        case ScalingMethod::NONE:
        default: {
            ScalingResult result;
            result.row_scales.assign(A.rows(), 1.0);
            result.col_scales.assign(A.cols(), 1.0);
            result.max_row_scale = result.min_row_scale = 1.0;
            result.max_col_scale = result.min_col_scale = 1.0;
            result.converged = true;
            return result;
        }
    }
}

void MatrixScaler::apply_scales(SparseMatrix& A,
                                 std::vector<double>& rhs,
                                 std::vector<double>& obj,
                                 std::vector<double>& lb,
                                 std::vector<double>& ub,
                                 const ScalingResult& scales) const {
    if (scales.row_scales.size() != A.rows() || scales.col_scales.size() != A.cols()) {
        return;
    }

    A.scale_rows(scales.row_scales);
    A.scale_cols(scales.col_scales);

    if (!rhs.empty() && rhs.size() == A.rows()) {
        for (std::size_t i = 0; i < rhs.size(); ++i) {
            rhs[i] *= scales.row_scales[i];
        }
    }

    if (!obj.empty() && obj.size() == A.cols()) {
        for (std::size_t j = 0; j < obj.size(); ++j) {
            obj[j] *= scales.col_scales[j];
        }
    }

    if (!lb.empty() && lb.size() == A.cols()) {
        for (std::size_t j = 0; j < lb.size(); ++j) {
            if (lb[j] > -std::numeric_limits<double>::infinity()) {
                lb[j] *= scales.col_scales[j];
            }
        }
    }

    if (!ub.empty() && ub.size() == A.cols()) {
        for (std::size_t j = 0; j < ub.size(); ++j) {
            if (ub[j] < std::numeric_limits<double>::infinity()) {
                ub[j] *= scales.col_scales[j];
            }
        }
    }
}

void MatrixScaler::unscale_solution(const ScalingResult& scales,
                                     std::vector<double>& primal,
                                     std::vector<double>& dual) const {
    if (!primal.empty() && primal.size() == scales.col_scales.size()) {
        for (std::size_t j = 0; j < primal.size(); ++j) {
            primal[j] /= scales.col_scales[j];
        }
    }

    if (!dual.empty() && dual.size() == scales.row_scales.size()) {
        for (std::size_t i = 0; i < dual.size(); ++i) {
            dual[i] /= scales.row_scales[i];
        }
    }
}

ScalingResult geometric_scaling_impl(const SparseMatrix& A, int max_iter) {
    ScalingResult result;
    result.row_scales.assign(A.rows(), 1.0);
    result.col_scales.assign(A.cols(), 1.0);

    std::vector<double> row_max(A.rows(), 0.0);
    std::vector<double> col_max(A.cols(), 0.0);

    for (int iter = 0; iter < max_iter; ++iter) {
        bool changed = false;

        // Row phase: measure after the current scales, then re-measure for the
        // column phase so each pass works on the freshly scaled matrix
        // (Gauss-Seidel style) -- simultaneous row/col scaling from one stale
        // snapshot oscillates and never converges.
        effective_maxima(A, result.row_scales, result.col_scales, row_max, col_max);
        for (std::size_t i = 0; i < A.rows(); ++i) {
            if (row_max[i] > 0.0 && std::abs(row_max[i] - 1.0) > 1e-4) {
                result.row_scales[i] *= 1.0 / std::sqrt(row_max[i]);
                changed = true;
            }
        }

        effective_maxima(A, result.row_scales, result.col_scales, row_max, col_max);
        for (std::size_t j = 0; j < A.cols(); ++j) {
            if (col_max[j] > 0.0 && std::abs(col_max[j] - 1.0) > 1e-4) {
                result.col_scales[j] *= 1.0 / std::sqrt(col_max[j]);
                changed = true;
            }
        }

        result.iterations = iter + 1;
        if (!changed) {
            result.converged = true;
            break;
        }
    }

    result.max_row_scale = *std::max_element(result.row_scales.begin(), result.row_scales.end());
    result.min_row_scale = *std::min_element(result.row_scales.begin(), result.row_scales.end());
    result.max_col_scale = *std::max_element(result.col_scales.begin(), result.col_scales.end());
    result.min_col_scale = *std::min_element(result.col_scales.begin(), result.col_scales.end());

    return result;
}

ScalingResult curtis_reid_scaling_impl(const SparseMatrix& A, int max_iter) {
    ScalingResult result;
    result.row_scales.assign(A.rows(), 1.0);
    result.col_scales.assign(A.cols(), 1.0);

    std::vector<double> row_max(A.rows(), 0.0);
    std::vector<double> col_max(A.cols(), 0.0);

    for (int iter = 0; iter < max_iter; ++iter) {
        bool changed = false;

        effective_maxima(A, result.row_scales, result.col_scales, row_max, col_max);
        for (std::size_t i = 0; i < A.rows(); ++i) {
            if (row_max[i] > 0.0 && std::abs(row_max[i] - 1.0) > 1e-4) {
                result.row_scales[i] *= 1.0 / std::sqrt(row_max[i]);
                changed = true;
            }
        }

        effective_maxima(A, result.row_scales, result.col_scales, row_max, col_max);
        for (std::size_t j = 0; j < A.cols(); ++j) {
            if (col_max[j] > 0.0 && std::abs(col_max[j] - 1.0) > 1e-4) {
                result.col_scales[j] *= 1.0 / std::sqrt(col_max[j]);
                changed = true;
            }
        }

        result.iterations = iter + 1;
        if (!changed) {
            result.converged = true;
            break;
        }
    }

    result.max_row_scale = *std::max_element(result.row_scales.begin(), result.row_scales.end());
    result.min_row_scale = *std::min_element(result.row_scales.begin(), result.row_scales.end());
    result.max_col_scale = *std::max_element(result.col_scales.begin(), result.col_scales.end());
    result.min_col_scale = *std::min_element(result.col_scales.begin(), result.col_scales.end());

    return result;
}

ScalingResult equilibration_scaling_impl(const SparseMatrix& A, int max_iter) {
    ScalingResult result;
    result.row_scales.assign(A.rows(), 1.0);
    result.col_scales.assign(A.cols(), 1.0);

    std::vector<double> row_max(A.rows(), 0.0);
    std::vector<double> col_max(A.cols(), 0.0);

    for (int iter = 0; iter < max_iter; ++iter) {
        bool changed = false;

        effective_maxima(A, result.row_scales, result.col_scales, row_max, col_max);
        for (std::size_t i = 0; i < A.rows(); ++i) {
            if (row_max[i] > 0.0) {
                const double scale = 1.0 / row_max[i];
                if (std::abs(scale - 1.0) > 1e-6) {
                    result.row_scales[i] *= scale;
                    changed = true;
                }
            }
        }

        effective_maxima(A, result.row_scales, result.col_scales, row_max, col_max);
        for (std::size_t j = 0; j < A.cols(); ++j) {
            if (col_max[j] > 0.0) {
                const double scale = 1.0 / col_max[j];
                if (std::abs(scale - 1.0) > 1e-6) {
                    result.col_scales[j] *= scale;
                    changed = true;
                }
            }
        }

        result.iterations = iter + 1;
        if (!changed) {
            result.converged = true;
            break;
        }
    }

    result.max_row_scale = *std::max_element(result.row_scales.begin(), result.row_scales.end());
    result.min_row_scale = *std::min_element(result.row_scales.begin(), result.row_scales.end());
    result.max_col_scale = *std::max_element(result.col_scales.begin(), result.col_scales.end());
    result.min_col_scale = *std::min_element(result.col_scales.begin(), result.col_scales.end());

    return result;
}

ScalingResult MatrixScaler::geometric_scaling(const SparseMatrix& A, int max_iter) {
    return geometric_scaling_impl(A, max_iter);
}

ScalingResult MatrixScaler::curtis_reid_scaling(const SparseMatrix& A, int max_iter) {
    return curtis_reid_scaling_impl(A, max_iter);
}

ScalingResult MatrixScaler::equilibration_scaling(const SparseMatrix& A, int max_iter) {
    return equilibration_scaling_impl(A, max_iter);
}

} // namespace hypernova::numerical