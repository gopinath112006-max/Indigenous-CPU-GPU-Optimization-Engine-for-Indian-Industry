#include "scaling.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace hypernova::numerical {

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
        std::fill(row_max.begin(), row_max.end(), 0.0);
        std::fill(col_max.begin(), col_max.end(), 0.0);

        if (A.order() == StorageOrder::CSR) {
            for (std::size_t i = 0; i < A.rows(); ++i) {
                for (std::size_t j = A.row_ptr()[i]; j < A.row_ptr()[i + 1]; ++j) {
                    double val = std::abs(A.values()[j]);
                    row_max[i] = std::max(row_max[i], val);
                    col_max[A.col_indices()[j]] = std::max(col_max[A.col_indices()[j]], val);
                }
            }
        } else {
            for (std::size_t j = 0; j < A.cols(); ++j) {
                for (std::size_t k = A.row_ptr()[j]; k < A.row_ptr()[j + 1]; ++k) {
                    double val = std::abs(A.values()[k]);
                    col_max[j] = std::max(col_max[j], val);
                    row_max[A.col_indices()[k]] = std::max(row_max[A.col_indices()[k]], val);
                }
            }
        }

        bool changed = false;
        for (std::size_t i = 0; i < A.rows(); ++i) {
            if (row_max[i] > 0.0) {
                double scale = 1.0 / std::sqrt(row_max[i]);
                result.row_scales[i] *= scale;
                changed = true;
            }
        }
        for (std::size_t j = 0; j < A.cols(); ++j) {
            if (col_max[j] > 0.0) {
                double scale = 1.0 / std::sqrt(col_max[j]);
                result.col_scales[j] *= scale;
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

    for (int iter = 0; iter < max_iter; ++iter) {
        bool changed = false;
        
        std::vector<double> row_max(A.rows(), 0.0);
        if (A.order() == StorageOrder::CSR) {
            for (std::size_t i = 0; i < A.rows(); ++i) {
                for (std::size_t j = A.row_ptr()[i]; j < A.row_ptr()[i + 1]; ++j) {
                    double val = std::abs(A.values()[j]) * result.row_scales[i] * result.col_scales[A.col_indices()[j]];
                    row_max[i] = std::max(row_max[i], val);
                }
            }
        } else {
            for (std::size_t j = 0; j < A.cols(); ++j) {
                for (std::size_t k = A.row_ptr()[j]; k < A.row_ptr()[j + 1]; ++k) {
                    std::size_t i = A.col_indices()[k];
                    double val = std::abs(A.values()[k]) * result.row_scales[i] * result.col_scales[j];
                    row_max[i] = std::max(row_max[i], val);
                }
            }
        }

        for (std::size_t i = 0; i < A.rows(); ++i) {
            if (row_max[i] > 0.0 && std::abs(row_max[i] - 1.0) > 1e-4) {
                result.row_scales[i] *= 1.0 / std::sqrt(row_max[i]);
                changed = true;
            }
        }

        std::vector<double> col_max(A.cols(), 0.0);
        if (A.order() == StorageOrder::CSR) {
            for (std::size_t i = 0; i < A.rows(); ++i) {
                for (std::size_t j = A.row_ptr()[i]; j < A.row_ptr()[i + 1]; ++j) {
                    double val = std::abs(A.values()[j]) * result.row_scales[i] * result.col_scales[A.col_indices()[j]];
                    col_max[A.col_indices()[j]] = std::max(col_max[A.col_indices()[j]], val);
                }
            }
        } else {
            for (std::size_t j = 0; j < A.cols(); ++j) {
                for (std::size_t k = A.row_ptr()[j]; k < A.row_ptr()[j + 1]; ++k) {
                    std::size_t i = A.col_indices()[k];
                    double val = std::abs(A.values()[k]) * result.row_scales[i] * result.col_scales[j];
                    col_max[j] = std::max(col_max[j], val);
                }
            }
        }

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

    for (int iter = 0; iter < max_iter; ++iter) {
        std::vector<double> row_max(A.rows(), 0.0);
        std::vector<double> col_max(A.cols(), 0.0);

        if (A.order() == StorageOrder::CSR) {
            for (std::size_t i = 0; i < A.rows(); ++i) {
                for (std::size_t j = A.row_ptr()[i]; j < A.row_ptr()[i + 1]; ++j) {
                    double val = std::abs(A.values()[j]);
                    row_max[i] = std::max(row_max[i], val);
                    col_max[A.col_indices()[j]] = std::max(col_max[A.col_indices()[j]], val);
                }
            }
        }

        bool changed = false;
        for (std::size_t i = 0; i < A.rows(); ++i) {
            if (row_max[i] > 0.0) {
                double target = 1.0;
                double scale = target / row_max[i];
                if (std::abs(scale - 1.0) > 1e-6) {
                    result.row_scales[i] *= scale;
                    changed = true;
                }
            }
        }
        for (std::size_t j = 0; j < A.cols(); ++j) {
            if (col_max[j] > 0.0) {
                double target = 1.0;
                double scale = target / col_max[j];
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