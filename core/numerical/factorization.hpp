#pragma once

#include "sparse_matrix.hpp"
#include "tolerance.hpp"
#include <vector>
#include <cstddef>
#include <memory>
#include <functional>
#include <optional>

namespace hypernova::numerical {

enum class FactorizationType { LU, Cholesky, LDLT };

struct FactorizationStats {
    std::size_t nnz_L = 0;
    std::size_t nnz_U = 0;
    double condition_estimate = 0.0;
    double determinant = 0.0;
    int rank = 0;
    bool singular = false;
    double factorization_time_ms = 0.0;
};

class SparseFactorization {
public:
    virtual ~SparseFactorization() = default;

    virtual void solve(std::vector<double>& x) const = 0;
    virtual void solve_transpose(std::vector<double>& x) const = 0;
    virtual void solve_multiple(std::vector<std::vector<double>>& X) const = 0;

    virtual FactorizationStats stats() const = 0;
    virtual FactorizationType type() const = 0;

    // Diagonal factors for symmetric factorizations (Cholesky/LDLT). Returns
    // an empty vector for incompatible types (e.g. LU).
    virtual const std::vector<double>& D_values() const { return empty_D_; }

    virtual std::unique_ptr<SparseFactorization> clone() const = 0;

    // Incremental column replacement for basis updates (revised simplex).
    // direction must satisfy  direction = B^{-1} * a_new  where column `column`
    // of the current factorized matrix B is being replaced by a_new. Returns
    // false when the update cannot be applied (invalid input / singular
    // direction), leaving the factorization state undefined and requiring a
    // full refactorization. Eta-matrix updates keep the factorization
    // consistent with meaning: the resulting system solves the updated basis.
    virtual bool apply_eta(int column, const std::vector<double>& direction) {
        (void)column;
        (void)direction;
        return false;
    }
    virtual void clear_eta_chain() {}
    virtual std::size_t eta_chain_size() const { return 0; }

    // Cooperative interruption for long-running numeric factorizations. When the
    // callback is set and returns true, factorization aborts early; the result is
    // then unusable and the caller must re-check its own deadline/abort state.
    void set_interrupt_callback(std::function<bool()> cb) { interrupt_cb_ = std::move(cb); }

protected:
    bool interrupted() const { return interrupt_cb_ && interrupt_cb_(); }
    std::function<bool()> interrupt_cb_;

private:
    static const std::vector<double> empty_D_;
};

class SparseLU : public SparseFactorization {
public:
    struct SymbolicFactorization {
        std::vector<std::size_t> row_perm;
        std::vector<std::size_t> col_perm;
        std::vector<std::size_t> L_row_ptr;
        std::vector<std::size_t> L_col_indices;
        std::vector<std::size_t> U_row_ptr;
        std::vector<std::size_t> U_col_indices;
        std::vector<std::size_t> L_col_ptr;
        std::vector<std::size_t> L_row_indices;
        std::vector<double> L_csc_values;
        std::vector<std::size_t> U_col_ptr;
        std::vector<std::size_t> U_row_indices;
        std::vector<double> U_csc_values;
        std::vector<std::size_t> etree;
    };

    SparseLU() = default;
    SparseLU(const SparseMatrix& A, const ToleranceConfig& tol = ToleranceConfig::industrial_defaults());

    void analyze(const SparseMatrix& A);
    void factorize(const SparseMatrix& A, const ToleranceConfig& tol = ToleranceConfig::industrial_defaults());
    void refactorize(const SparseMatrix& A, const ToleranceConfig& tol = ToleranceConfig::industrial_defaults());

    void solve(std::vector<double>& x) const override;
    void solve_transpose(std::vector<double>& x) const override;
    void solve_multiple(std::vector<std::vector<double>>& X) const override;

    FactorizationStats stats() const override;
    FactorizationType type() const override { return FactorizationType::LU; }

    std::unique_ptr<SparseFactorization> clone() const override;

    bool apply_eta(int column, const std::vector<double>& direction) override;
    void clear_eta_chain() override;
    std::size_t eta_chain_size() const override { return eta_chain_.size(); }

    const std::vector<double>& L_values() const { return L_values_; }
    const std::vector<double>& U_values() const { return U_values_; }
    const std::vector<std::size_t>& L_row_ptr() const { return symbolic_.L_row_ptr; }
    const std::vector<std::size_t>& L_col_indices() const { return symbolic_.L_col_indices; }
    const std::vector<std::size_t>& U_row_ptr() const { return symbolic_.U_row_ptr; }
    const std::vector<std::size_t>& U_col_indices() const { return symbolic_.U_col_indices; }
    const std::vector<std::size_t>& row_perm() const { return symbolic_.row_perm; }
    const std::vector<std::size_t>& col_perm() const { return symbolic_.col_perm; }

    void update_column(std::size_t col, const std::vector<double>& new_col);
    void update_row(std::size_t row, const std::vector<double>& new_row);

private:
    struct EtaUpdate {
        int column = 0;
        std::vector<std::size_t> rows;
        std::vector<double> vals;
    };

    void reconstruct_factor_space_matrix(std::vector<double>& M) const;
    void refactorize_from_dense(const std::vector<double>& M);
    void apply_eta_forward(std::vector<double>& x) const;
    void apply_eta_transpose(std::vector<double>& x) const;
    SymbolicFactorization symbolic_;
    std::vector<double> L_values_;
    std::vector<double> U_values_;
    std::vector<double> reciprocals_;
    std::vector<double> dense_L_;
    std::vector<double> dense_U_;
    std::size_t dense_n_ = 0;
    std::vector<EtaUpdate> eta_chain_;
    FactorizationStats stats_;
    ToleranceConfig tol_;

    void symbolic_analysis(const SparseMatrix& A);
    void numeric_factorization(const SparseMatrix& A);
    void numeric_factorization_dense(const SparseMatrix& A);
    void numeric_factorization_sparse(const SparseMatrix& A);
    void publish_factors(const SparseMatrix& A,
                         std::size_t rank,
                         bool singular,
                         const std::vector<std::vector<std::size_t>>& Lcol_rows,
                         const std::vector<std::vector<double>>& Lcol_vals,
                         const std::vector<std::vector<std::size_t>>& Urow_cols,
                         const std::vector<std::vector<double>>& Urow_vals);
    void compute_etree();
    void apply_row_perm(std::vector<double>& x) const;
    void apply_col_perm(std::vector<double>& x) const;
    void apply_row_perm_inv(std::vector<double>& x) const;
    void apply_col_perm_inv(std::vector<double>& x) const;
};

class SparseCholesky : public SparseFactorization {
public:
    struct SymbolicFactorization {
        std::vector<std::size_t> perm;
        std::vector<std::size_t> inv_perm;
        std::vector<std::size_t> L_row_ptr;
        std::vector<std::size_t> L_col_indices;
        std::vector<std::size_t> parent;
        std::vector<std::size_t> postorder;
    };

    SparseCholesky() = default;
    SparseCholesky(const SparseMatrix& A, const ToleranceConfig& tol = ToleranceConfig::industrial_defaults());

    void analyze(const SparseMatrix& A);
    void factorize(const SparseMatrix& A, const ToleranceConfig& tol = ToleranceConfig::industrial_defaults());
    void refactorize(const SparseMatrix& A, const ToleranceConfig& tol = ToleranceConfig::industrial_defaults());

    void solve(std::vector<double>& x) const override;
    void solve_transpose(std::vector<double>& x) const override;
    void solve_multiple(std::vector<std::vector<double>>& X) const override;

    FactorizationStats stats() const override;
    FactorizationType type() const override { return FactorizationType::Cholesky; }

    std::unique_ptr<SparseFactorization> clone() const override;

    const std::vector<double>& D_values() const override { return D_values_; }
    const std::vector<double>& L_values() const { return L_values_; }
    const std::vector<std::size_t>& L_row_ptr() const { return symbolic_.L_row_ptr; }
    const std::vector<std::size_t>& L_col_indices() const { return symbolic_.L_col_indices; }
    const std::vector<std::size_t>& perm() const { return symbolic_.perm; }

private:
    SymbolicFactorization symbolic_;
    std::vector<double> L_values_;
    std::vector<double> D_values_;
    FactorizationStats stats_;
    ToleranceConfig tol_;

    void symbolic_analysis(const SparseMatrix& A);
    void numeric_factorization(const SparseMatrix& A);
    void compute_elimination_tree();
    void compute_postorder();
    void apply_perm(std::vector<double>& x) const;
    void apply_perm_inv(std::vector<double>& x) const;
};

class SparseLDLT : public SparseFactorization {
public:
    struct SymbolicFactorization {
        std::vector<std::size_t> perm;
        std::vector<std::size_t> inv_perm;
        std::vector<std::size_t> L_row_ptr;
        std::vector<std::size_t> L_col_indices;
        std::vector<std::size_t> parent;
        std::vector<std::size_t> postorder;
    };

    SparseLDLT() = default;
    SparseLDLT(const SparseMatrix& A, const ToleranceConfig& tol = ToleranceConfig::industrial_defaults());

    void analyze(const SparseMatrix& A);
    void factorize(const SparseMatrix& A, const ToleranceConfig& tol = ToleranceConfig::industrial_defaults());
    void refactorize(const SparseMatrix& A, const ToleranceConfig& tol = ToleranceConfig::industrial_defaults());

    void solve(std::vector<double>& x) const override;
    void solve_transpose(std::vector<double>& x) const override;
    void solve_multiple(std::vector<std::vector<double>>& X) const override;

    FactorizationStats stats() const override;
    FactorizationType type() const override { return FactorizationType::LDLT; }

    std::unique_ptr<SparseFactorization> clone() const override;

    const std::vector<double>& L_values() const { return L_values_; }
    const std::vector<double>& D_values() const { return D_values_; }
    const std::vector<std::size_t>& L_row_ptr() const { return symbolic_.L_row_ptr; }
    const std::vector<std::size_t>& L_col_indices() const { return symbolic_.L_col_indices; }
    const std::vector<std::size_t>& perm() const { return symbolic_.perm; }

private:
    SymbolicFactorization symbolic_;
    std::vector<double> L_values_;
    std::vector<double> D_values_;
    FactorizationStats stats_;
    ToleranceConfig tol_;

    void symbolic_analysis(const SparseMatrix& A);
    void numeric_factorization(const SparseMatrix& A);
    void compute_elimination_tree();
    void compute_postorder();
    void apply_perm(std::vector<double>& x) const;
    void apply_perm_inv(std::vector<double>& x) const;
};

std::unique_ptr<SparseFactorization> create_factorization(FactorizationType type,
                                                           const SparseMatrix& A,
                                                           const ToleranceConfig& tol = ToleranceConfig::industrial_defaults());

} // namespace hypernova::numerical