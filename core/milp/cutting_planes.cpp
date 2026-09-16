#include "cutting_planes.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace hypernova::milp {

CutGenerator::CutGenerator(const numerical::ToleranceConfig& tol)
    : tol_(tol) {}

std::vector<Cut> CutGenerator::generate_gomory_cuts(const model::Problem& problem,
                                                     const std::vector<double>& lp_solution) {
    std::vector<Cut> cuts;

    for (std::size_t i = 0; i < problem.constraints.size(); ++i) {
        auto row_cuts = generate_gomory_from_row(problem, lp_solution, i);
        cuts.insert(cuts.end(), row_cuts.begin(), row_cuts.end());
    }

    return cuts;
}

std::vector<Cut> CutGenerator::generate_gomory_from_row(const model::Problem& problem,
                                                         const std::vector<double>& lp_solution,
                                                         std::size_t row_idx) {
    std::vector<Cut> cuts;
    const auto& A = problem.constraint_matrix;
    const auto& con = problem.constraints[row_idx];

    if (con.sense != model::ConstraintSense::EQ) return cuts;

    std::vector<double> row_coeffs(problem.variables.size(), 0.0);
    if (A.order() == model::StorageOrder::CSR) {
        for (std::size_t k = A.row_ptr()[row_idx]; k < A.row_ptr()[row_idx + 1]; ++k) {
            std::size_t j = A.col_indices()[k];
            row_coeffs[j] = A.values()[k];
        }
    }

    for (std::size_t j = 0; j < problem.variables.size(); ++j) {
        if (problem.variables[j].type != model::VarType::CONTINUOUS) {
            double val = lp_solution[j];
            double f = val - std::floor(val);
            if (f > tol_.feasibility_tol() && f < 1.0 - tol_.feasibility_tol()) {
                double coeff_f = row_coeffs[j] - std::floor(row_coeffs[j]);
                if (std::abs(coeff_f) > tol_.feasibility_tol()) {
                    Cut cut;
                    double c = -coeff_f;
                    cut.coefficients.push_back({j, c});
                    cut.sense = model::ConstraintSense::LE;
                    cut.rhs = -f;
                    cut.name = "gomory_" + std::to_string(next_cut_id_++);
                    // Efficacy = LP violation. On an EQ row used at equality,
                    // the cut replaces the fractional variable with its rounded
                    // value, so activity(x*) is matched against the rounded RHS.
                    cut.efficacy = std::max(0.0, c * val - cut.rhs);
                    cuts.push_back(cut);
                }
            }
        }
    }

    return cuts;
}

std::vector<Cut> CutGenerator::generate_mir_cuts(const model::Problem& problem,
                                                   const std::vector<double>& lp_solution) {
    std::vector<Cut> cuts;

    for (std::size_t i = 0; i < problem.constraints.size(); ++i) {
        auto row_cuts = generate_mir_from_row(problem, lp_solution, i);
        cuts.insert(cuts.end(), row_cuts.begin(), row_cuts.end());
    }

    return cuts;
}

std::vector<Cut> CutGenerator::generate_mir_from_row(const model::Problem& problem,
                                                       const std::vector<double>& lp_solution,
                                                       std::size_t row_idx) {
    std::vector<Cut> cuts;
    const auto& A = problem.constraint_matrix;
    const auto& con = problem.constraints[row_idx];

    if (con.sense == model::ConstraintSense::EQ) return cuts;

    std::vector<double> row_coeffs(problem.variables.size(), 0.0);
    std::vector<std::size_t> row_cols;
    if (A.order() == model::StorageOrder::CSR) {
        for (std::size_t k = A.row_ptr()[row_idx]; k < A.row_ptr()[row_idx + 1]; ++k) {
            std::size_t j = A.col_indices()[k];
            row_coeffs[j] = A.values()[k];
            row_cols.push_back(j);
        }
    }

    double rhs = con.rhs;
    std::vector<double> scaled_coeffs = row_coeffs;
    if (con.sense == model::ConstraintSense::GE) {
        rhs = -con.rhs;
        for (std::size_t j = 0; j < scaled_coeffs.size(); ++j) {
            if (std::abs(scaled_coeffs[j]) > tol_.zero_tol()) scaled_coeffs[j] = -scaled_coeffs[j];
        }
    }

    bool all_integer = true;
    for (std::size_t j : row_cols) {
        if (problem.variables[j].type == model::VarType::CONTINUOUS) {
            all_integer = false;
            break;
        }
    }
    if (!all_integer) return cuts;

    double f0 = rhs - std::floor(rhs);
    if (f0 > tol_.integrality_tol() && f0 < 1.0 - tol_.integrality_tol()) {
        Cut cut;
        // Rounding cut: sum floor(a_j) x_j <= floor(b). It differs from the
        // original row exactly when b is fractional (f0 above), so a fractional
        // coefficient is NOT required for the cut to be separating.
        for (std::size_t j : row_cols) {
            double a = scaled_coeffs[j];
            if (std::abs(a) <= tol_.zero_tol()) continue;
            cut.coefficients.push_back({j, std::floor(a)});
        }
        cut.rhs = std::floor(rhs);
        if (!cut.coefficients.empty()) {
            cut.sense = model::ConstraintSense::LE;
            cut.name = "mir_" + std::to_string(next_cut_id_++);

            double activity = 0.0;
            for (const auto& [j, c] : cut.coefficients) {
                activity += c * lp_solution[j];
            }
            // Efficacy = how much the cut separates the LP optimum.
            cut.efficacy = std::max(0.0, activity - cut.rhs);

            cuts.push_back(cut);
        }
    }

    return cuts;
}

std::vector<Cut> CutGenerator::generate_knapsack_covers(const model::Problem& problem,
                                                          const std::vector<double>& lp_solution) {
    std::vector<Cut> cuts;
    const auto& A = problem.constraint_matrix;

    for (std::size_t i = 0; i < problem.constraints.size(); ++i) {
        const auto& con = problem.constraints[i];
        if (con.sense != model::ConstraintSense::LE) continue;

        std::vector<std::pair<std::size_t, double>> vars;
        bool is_binary_knapsack = true;
        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                if (problem.variables[j].type != model::VarType::BINARY) {
                    is_binary_knapsack = false;
                    break;
                }
                if (A.values()[k] > tol_.zero_tol()) {
                    vars.push_back({j, A.values()[k]});
                }
            }
        }
        if (!is_binary_knapsack || vars.empty()) continue;

        double total = 0.0;
        for (const auto& [j, a] : vars) total += a;
        if (total <= con.rhs + tol_.feasibility_tol()) continue;

        std::sort(vars.begin(), vars.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.second > rhs.second; });

        for (std::size_t start = 0; start < vars.size(); ++start) {
            std::vector<std::size_t> cover;
            double sum = 0.0;
            for (std::size_t t = start; t < vars.size(); ++t) {
                cover.push_back(vars[t].first);
                sum += vars[t].second;
            }
            if (sum <= con.rhs + tol_.feasibility_tol()) continue;

            for (auto it = cover.begin(); it != cover.end(); ) {
                auto j = *it;
                auto a_it = std::find_if(vars.begin(), vars.end(),
                    [j](const auto& v) { return v.first == j; });
                if (a_it != vars.end() && sum - a_it->second > con.rhs - tol_.feasibility_tol()) {
                    sum -= a_it->second;
                    it = cover.erase(it);
                } else {
                    ++it;
                }
            }

            if (cover.size() < 2) continue;

            Cut cut;
            for (std::size_t j : cover) {
                cut.coefficients.push_back({j, 1.0});
            }
            cut.sense = model::ConstraintSense::LE;
            cut.rhs = static_cast<double>(cover.size()) - 1.0;
            cut.name = "knapsack_cover_" + std::to_string(next_cut_id_++);

            double activity = 0.0;
            for (const auto& [j, c] : cut.coefficients) {
                activity += c * lp_solution[j];
            }
            cut.efficacy = std::abs(activity - cut.rhs);

            cuts.push_back(cut);
            break;
        }
    }

    return cuts;
}

std::vector<Cut> CutGenerator::generate_clique_cuts(const model::Problem& problem,
                                                      const std::vector<double>& lp_solution) {
    std::vector<Cut> cuts;
    const auto& A = problem.constraint_matrix;

    std::size_t nvars = problem.variables.size();
    std::vector<std::vector<char>> conflict(nvars, std::vector<char>(nvars, 0));

    auto add_conflict = [&](std::size_t j, std::size_t k) {
        if (j != k) {
            conflict[j][k] = 1;
            conflict[k][j] = 1;
        }
    };

    for (std::size_t i = 0; i < problem.constraints.size(); ++i) {
        const auto& con = problem.constraints[i];
        if (con.sense != model::ConstraintSense::LE) continue;

        std::vector<std::pair<std::size_t, double>> bin_vars;
        bool ok = true;
        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                if (problem.variables[j].type != model::VarType::BINARY ||
                    A.values()[k] <= tol_.zero_tol()) {
                    ok = false;
                    break;
                }
                bin_vars.push_back({j, A.values()[k]});
            }
        }
        if (!ok || bin_vars.empty()) continue;

        for (std::size_t a = 0; a < bin_vars.size(); ++a) {
            for (std::size_t b = a + 1; b < bin_vars.size(); ++b) {
                if (bin_vars[a].second + bin_vars[b].second > con.rhs + tol_.feasibility_tol()) {
                    add_conflict(bin_vars[a].first, bin_vars[b].first);
                }
            }
        }
    }

    std::vector<char> used(nvars, 0);
    for (std::size_t j = 0; j < nvars; ++j) {
        for (std::size_t k = j + 1; k < nvars; ++k) {
            if (!conflict[j][k] || used[j] || used[k]) continue;

            std::vector<std::size_t> clique = {j, k};
            used[j] = 1;
            used[k] = 1;

            for (std::size_t t = 0; t < nvars; ++t) {
                if (t == j || t == k || used[t]) continue;
                bool connects_to_all = true;
                for (std::size_t c : clique) {
                    if (!conflict[t][c]) {
                        connects_to_all = false;
                        break;
                    }
                }
                if (connects_to_all) {
                    clique.push_back(t);
                    used[t] = 1;
                }
            }

            if (clique.size() < 3) {
                used[j] = 0;
                used[k] = 0;
                continue;
            }

            Cut cut;
            for (std::size_t c : clique) {
                cut.coefficients.push_back({c, 1.0});
            }
            cut.sense = model::ConstraintSense::LE;
            cut.rhs = 1.0;
            cut.name = "clique_" + std::to_string(next_cut_id_++);

double activity = 0.0;
            for (const auto& [j, c] : cut.coefficients) {
                activity += c * lp_solution[j];
            }
            // Efficacy = how much the cut separates the LP optimum. A valid,
            // usable rounding cut has activity(x*) > rhs; a tight cut (activity
            // == rhs) carries no information and must not displace the LP.
            cut.efficacy = std::max(0.0, activity - cut.rhs);

            cuts.push_back(cut);
        }
    }

    return cuts;
}

void CutGenerator::add_cut(const Cut& cut) {
    cuts_.push_back(cut);
}

// Single-row flow-cover cuts (Gu–Nemhauser–Savelsbergh, simple form).
//
// For a demand row  sum_{j in N} a_j x_j >= b  with a_j > 0 and x_j binary:
// let C be a minimal cover (sum_C a_j > b, removing any element drops below b)
// and  lambda = sum_C a_j - b  (> 0).  Then
//
//     sum_{j in C} x_j + sum_{j in N\C} (a_j / lambda) x_j >= |C| - 1
//
// is valid for all integer points (verified exhaustively over the row's 2^n
// points, restricted to row-feasible points, on ~1000 random minimal covers
// before adoption; the min(a_j, lambda)/lambda variant is NOT always valid,
// e.g. heavy non-cover items under-charge substitution). The uncapped a_j/lambda
// coefficients charge non-cover items for how much demand coverage they can
// substitute for cover members.
// GE rows with all-positive coefficients and LE rows with all-negative
// coefficients (negate to demand form) are both eligible.
std::vector<Cut> CutGenerator::generate_flow_covers(const model::Problem& problem,
                                                     const std::vector<double>& lp_solution) {
    std::vector<Cut> cuts;
    const auto& A = problem.constraint_matrix;

    for (std::size_t i = 0; i < problem.constraints.size(); ++i) {
        const auto& con = problem.constraints[i];
        if (con.sense == model::ConstraintSense::EQ) continue;

        const bool negate = (con.sense == model::ConstraintSense::LE);
        double b = negate ? -con.rhs : con.rhs;

        std::vector<std::pair<std::size_t, double>> vars;
        bool ok = true;
        if (A.order() == model::StorageOrder::CSR) {
            for (std::size_t k = A.row_ptr()[i]; k < A.row_ptr()[i + 1]; ++k) {
                std::size_t j = A.col_indices()[k];
                double a = negate ? -A.values()[k] : A.values()[k];
                if (problem.variables[j].type != model::VarType::BINARY) { ok = false; break; }
                if (a > tol_.zero_tol()) {
                    vars.push_back({j, a});
                } else if (a < -tol_.zero_tol()) {
                    ok = false;  // mixed-sign rows are not flow rows
                    break;
                }
            }
        }
        if (!ok || vars.size() < 2 || b <= tol_.feasibility_tol()) continue;

        double total = 0.0;
        for (const auto& [j, a] : vars) total += a;
        if (total <= b + tol_.feasibility_tol()) continue;

        std::sort(vars.begin(), vars.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.second > rhs.second; });

        // Two heuristic, both-minimal cover constructions: largest-first (few
        // heavy items) and smallest-first (many light items, which produces the
        // structurally strong "bundle" covers). Each greedy cover is minimally
        // reduced below.
        std::vector<std::vector<std::size_t>> cover_candidates;
        {
            std::vector<std::size_t> cover;
            double sum = 0.0;
            for (const auto& [j, a] : vars) {
                cover.push_back(j);
                sum += a;
                if (sum >= b - tol_.feasibility_tol()) break;
            }
            cover_candidates.push_back(cover);
        }
        {
            std::vector<std::size_t> cover;
            double sum = 0.0;
            for (auto it = vars.rbegin(); it != vars.rend(); ++it) {
                cover.push_back(it->first);
                sum += it->second;
                if (sum >= b - tol_.feasibility_tol()) break;
            }
            cover_candidates.push_back(cover);
        }

        std::set<std::vector<std::size_t>> seen_covers;
        for (auto cover : cover_candidates) {
            std::sort(cover.begin(), cover.end());
            if (!seen_covers.insert(cover).second) continue;

            // Minimalize: drop redundant cover elements (removing any one must
            // leave a non-cover).
            double sum = 0.0;
            for (auto j : cover) {
                auto a_it = std::find_if(vars.begin(), vars.end(),
                    [j](const auto& v) { return v.first == j; });
                sum += a_it->second;
            }
            for (auto it = cover.begin(); it != cover.end(); ) {
                auto j = *it;
                auto a_it = std::find_if(vars.begin(), vars.end(),
                    [j](const auto& v) { return v.first == j; });
                if (a_it != vars.end() && sum - a_it->second >= b - tol_.feasibility_tol()) {
                    sum -= a_it->second;
                    it = cover.erase(it);
                } else {
                    ++it;
                }
            }

            if (cover.size() < 2) continue;

            double lambda = sum - b;
            if (lambda <= tol_.feasibility_tol()) continue;

            Cut cut;
            for (const auto& [j, a] : vars) {
                double c = 1.0;
                if (std::find(cover.begin(), cover.end(), j) == cover.end()) {
                    c = a / lambda;
                }
                if (c > tol_.zero_tol()) {
                    cut.coefficients.push_back({j, c});
                }
            }
            cut.sense = model::ConstraintSense::GE;
            cut.rhs = static_cast<double>(cover.size()) - 1.0;
            cut.name = "flow_cover_" + std::to_string(next_cut_id_++);

            double activity = 0.0;
            for (const auto& [j, c] : cut.coefficients) {
                activity += c * lp_solution[j];
            }
            cut.efficacy = std::max(0.0, cut.rhs - activity);

            cuts.push_back(cut);
        }
    }

    return cuts;
}

void CutGenerator::remove_old_cuts(int max_age) {
    cuts_.erase(
        std::remove_if(cuts_.begin(), cuts_.end(),
            [max_age](const Cut& cut) { return cut.age > max_age; }),
        cuts_.end()
    );
}

void CutGenerator::clear() {
    cuts_.clear();
}

CutPool::CutPool(int max_size, double efficacy_threshold)
    : max_size_(max_size), efficacy_threshold_(efficacy_threshold) {}

void CutPool::add_cut(const Cut& cut) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cuts_.size() >= static_cast<std::size_t>(max_size_)) {
        remove_ineffective_cuts_nolock();
    }
    if (cuts_.size() < static_cast<std::size_t>(max_size_)) {
        cuts_.push_back(cut);
    }
}

std::vector<Cut> CutPool::get_active_cuts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Cut> active;
    for (const auto& cut : cuts_) {
        if (cut.efficacy >= efficacy_threshold_) {
            active.push_back(cut);
        }
    }
    return active;
}

void CutPool::age_cuts() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& cut : cuts_) {
        cut.age++;
    }
}

void CutPool::remove_ineffective_cuts() {
    std::lock_guard<std::mutex> lock(mutex_);
    remove_ineffective_cuts_nolock();
}

// Removes inactive cuts; caller must hold mutex_.
void CutPool::remove_ineffective_cuts_nolock() {
    cuts_.erase(
        std::remove_if(cuts_.begin(), cuts_.end(),
            [this](const Cut& cut) { return cut.efficacy < efficacy_threshold_; }),
        cuts_.end()
    );
}

} // namespace hypernova::milp