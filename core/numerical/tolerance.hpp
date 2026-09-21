#pragma once

#include <cstddef>
#include <cmath>
#include <limits>
#include <string>

namespace hypernova::numerical {

class ToleranceConfig {
public:
    static constexpr double DEFAULT_FEASIBILITY_TOL = 1e-9;
    static constexpr double DEFAULT_OPTIMALITY_TOL = 1e-9;
    static constexpr double DEFAULT_COMPLEMENTARITY_TOL = 1e-6;
    static constexpr double DEFAULT_PIVOT_TOL = 1e-12;
    static constexpr double DEFAULT_INTEGRALITY_TOL = 1e-6;
    static constexpr double DEFAULT_IPM_CONVERGENCE_TOL = 1e-8;
    static constexpr double DEFAULT_IPM_COMPLEMENTARITY_TOL = 1e-8;
    static constexpr double DEFAULT_MIP_GAP_TOL = 1e-4;
    static constexpr double DEFAULT_SINGULAR_TOL = 1e-14;
    static constexpr double DEFAULT_ZERO_TOL = 1e-15;
    static constexpr double DEFAULT_DEGENERACY_TOL = 1e-10;
    static constexpr double DEFAULT_MARKOWITZ_TOL = 0.01;

    ToleranceConfig() = default;

    ToleranceConfig& feasibility_tol(double v) { feasibility_tol_ = v; return *this; }
    ToleranceConfig& optimality_tol(double v) { optimality_tol_ = v; return *this; }
    ToleranceConfig& complementarity_tol(double v) { complementarity_tol_ = v; return *this; }
    ToleranceConfig& pivot_tol(double v) { pivot_tol_ = v; return *this; }
    ToleranceConfig& integrality_tol(double v) { integrality_tol_ = v; return *this; }
    ToleranceConfig& ipm_convergence_tol(double v) { ipm_convergence_tol_ = v; return *this; }
    ToleranceConfig& ipm_complementarity_tol(double v) { ipm_complementarity_tol_ = v; return *this; }
    ToleranceConfig& mip_gap_tol(double v) { mip_gap_tol_ = v; return *this; }
    ToleranceConfig& singular_tol(double v) { singular_tol_ = v; return *this; }
    ToleranceConfig& zero_tol(double v) { zero_tol_ = v; return *this; }
    ToleranceConfig& degeneracy_tol(double v) { degeneracy_tol_ = v; return *this; }
    ToleranceConfig& markowitz_tol(double v) { markowitz_tol_ = v; return *this; }

    double feasibility_tol() const { return feasibility_tol_; }
    double optimality_tol() const { return optimality_tol_; }
    double complementarity_tol() const { return complementarity_tol_; }
    double pivot_tol() const { return pivot_tol_; }
    double integrality_tol() const { return integrality_tol_; }
    double ipm_convergence_tol() const { return ipm_convergence_tol_; }
    double ipm_complementarity_tol() const { return ipm_complementarity_tol_; }
    double mip_gap_tol() const { return mip_gap_tol_; }
    double singular_tol() const { return singular_tol_; }
    double zero_tol() const { return zero_tol_; }
    double degeneracy_tol() const { return degeneracy_tol_; }
    double markowitz_tol() const { return markowitz_tol_; }

    bool is_zero(double v) const { return std::abs(v) <= zero_tol_; }
    bool is_feasible(double violation) const { return violation <= feasibility_tol_; }
    bool is_optimal(double reduced_cost) const { return std::abs(reduced_cost) <= optimality_tol_; }
    bool is_integral(double v) const { return std::abs(v - std::round(v)) <= integrality_tol_; }
    bool is_singular(double pivot) const { return std::abs(pivot) <= singular_tol_; }
    bool is_degenerate(double step) const { return step <= degeneracy_tol_; }

    static ToleranceConfig industrial_defaults() {
        ToleranceConfig cfg;
        cfg.feasibility_tol_ = 1e-9;
        cfg.optimality_tol_ = 1e-9;
        cfg.complementarity_tol_ = 1e-6;
        cfg.pivot_tol_ = 1e-12;
        cfg.integrality_tol_ = 1e-6;
        cfg.ipm_convergence_tol_ = 1e-8;
        cfg.ipm_complementarity_tol_ = 1e-8;
        cfg.mip_gap_tol_ = 1e-4;
        cfg.singular_tol_ = 1e-14;
        cfg.zero_tol_ = 1e-15;
        cfg.degeneracy_tol_ = 1e-10;
        cfg.markowitz_tol_ = 0.01;
        return cfg;
    }

    static ToleranceConfig loose_defaults() {
        ToleranceConfig cfg;
        cfg.feasibility_tol_ = 1e-7;
        cfg.optimality_tol_ = 1e-7;
        cfg.complementarity_tol_ = 1e-5;
        cfg.pivot_tol_ = 1e-10;
        cfg.integrality_tol_ = 1e-5;
        cfg.ipm_convergence_tol_ = 1e-6;
        cfg.ipm_complementarity_tol_ = 1e-6;
        cfg.mip_gap_tol_ = 1e-3;
        cfg.singular_tol_ = 1e-12;
        cfg.zero_tol_ = 1e-12;
        cfg.degeneracy_tol_ = 1e-8;
        cfg.markowitz_tol_ = 0.1;
        return cfg;
    }

    static ToleranceConfig tight_defaults() {
        ToleranceConfig cfg;
        cfg.feasibility_tol_ = 1e-11;
        cfg.optimality_tol_ = 1e-11;
        cfg.complementarity_tol_ = 1e-8;
        cfg.pivot_tol_ = 1e-14;
        cfg.integrality_tol_ = 1e-8;
        cfg.ipm_convergence_tol_ = 1e-10;
        cfg.ipm_complementarity_tol_ = 1e-10;
        cfg.mip_gap_tol_ = 1e-6;
        cfg.singular_tol_ = 1e-16;
        cfg.zero_tol_ = 1e-18;
        cfg.degeneracy_tol_ = 1e-12;
        cfg.markowitz_tol_ = 0.001;
        return cfg;
    }

    std::string to_string() const;

private:
    double feasibility_tol_ = DEFAULT_FEASIBILITY_TOL;
    double optimality_tol_ = DEFAULT_OPTIMALITY_TOL;
    double complementarity_tol_ = DEFAULT_COMPLEMENTARITY_TOL;
    double pivot_tol_ = DEFAULT_PIVOT_TOL;
    double integrality_tol_ = DEFAULT_INTEGRALITY_TOL;
    double ipm_convergence_tol_ = DEFAULT_IPM_CONVERGENCE_TOL;
    double ipm_complementarity_tol_ = DEFAULT_IPM_COMPLEMENTARITY_TOL;
    double mip_gap_tol_ = DEFAULT_MIP_GAP_TOL;
    double singular_tol_ = DEFAULT_SINGULAR_TOL;
    double zero_tol_ = DEFAULT_ZERO_TOL;
    double degeneracy_tol_ = DEFAULT_DEGENERACY_TOL;
    double markowitz_tol_ = DEFAULT_MARKOWITZ_TOL;
};

inline std::string ToleranceConfig::to_string() const {
    return "ToleranceConfig{feas=" + std::to_string(feasibility_tol_) +
           ", opt=" + std::to_string(optimality_tol_) +
           ", comp=" + std::to_string(complementarity_tol_) +
           ", pivot=" + std::to_string(pivot_tol_) +
           ", int=" + std::to_string(integrality_tol_) +
           ", ipm_conv=" + std::to_string(ipm_convergence_tol_) +
           ", ipm_comp=" + std::to_string(ipm_complementarity_tol_) +
           ", mip_gap=" + std::to_string(mip_gap_tol_) +
           ", singular=" + std::to_string(singular_tol_) +
           ", zero=" + std::to_string(zero_tol_) +
           ", degen=" + std::to_string(degeneracy_tol_) +
           ", markowitz=" + std::to_string(markowitz_tol_) + "}";
}

} // namespace hypernova::numerical