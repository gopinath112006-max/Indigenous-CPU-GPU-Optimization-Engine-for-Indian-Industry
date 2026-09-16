#pragma once

#include "../model/problem.hpp"
#include "../numerical/tolerance.hpp"
#include "../lp/simplex.hpp"
#include <string>
#include <vector>

namespace hypernova::validation {

// A Farkas certificate for an infeasible LP relaxation:
// multipliers y_i (one per constraint, respecting the constraint sense) such that
// the canonical condition holds:
//   sign(y_i) consistent with the row sense (>= 0 for LE, <= 0 for GE, free for EQ),
//   N_j = sum_i y_i * a_ij >= 0 for every variable j,
//   value = sum_i y_i * b_i + sum_j (l_j * max(N_j,0) - u_j * max(-N_j,0)) < 0.
// The certificate is verified against the model and only reported when valid.
struct FarkasCertificate {
    bool valid = false;
    std::string message;
    std::vector<double> multipliers;
    double farkas_value = 0.0;
    double violation = 0.0;
};

// An unboundedness certificate: a recession direction d (one entry per variable)
// such that A*d stays feasible for every original constraint and bound, and the
// objective improves along d without limit. Verified against the model.
struct UnboundedRay {
    bool valid = false;
    std::string message;
    std::vector<double> direction;
    double objective_direction = 0.0;
    double violation = 0.0;
};

struct CertificateResult {
    model::ProblemStatus status = model::ProblemStatus::UNKNOWN;
    FarkasCertificate farkas;
    UnboundedRay ray;
    std::string message;
};

// Runs a raw (presolve-free) LP feasibility/optimality probe and, when the model
// is infeasible or unbounded, produces a verified certificate by solving small
// auxiliary LPs and re-checking the result against the model.
class CertificateAnalyzer {
public:
    explicit CertificateAnalyzer(const numerical::ToleranceConfig& tol =
                                     numerical::ToleranceConfig::industrial_defaults(),
                                 const lp::SimplexOptions& options = lp::SimplexOptions());
    ~CertificateAnalyzer() = default;

    CertificateResult analyze(const model::Problem& problem);

    // Independent canonical verification of a candidate certificate / ray.
    static FarkasCertificate verify_farkas(const model::Problem& problem,
                                           const std::vector<double>& y,
                                           const numerical::ToleranceConfig& tol);
    static UnboundedRay verify_ray(const model::Problem& problem,
                                   const std::vector<double>& d,
                                   const numerical::ToleranceConfig& tol);

private:
    numerical::ToleranceConfig tol_;
    lp::SimplexOptions options_;
};

} // namespace hypernova::validation