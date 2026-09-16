#include "SessionManager.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <ctime>

namespace hypernova::cli {

using json = nlohmann::json;

void to_json(json& j, const VariableSpec& v) {
    j = json{
        {"name", v.name},
        {"type", static_cast<int>(v.type)},
        {"lower_bound", v.lower_bound},
        {"upper_bound", v.upper_bound},
        {"objective_coeff", v.objective_coeff}
    };
}

void from_json(const json& j, VariableSpec& v) {
    j.at("name").get_to(v.name);
    int type_val;
    j.at("type").get_to(type_val);
    v.type = static_cast<model::VarType>(type_val);
    j.at("lower_bound").get_to(v.lower_bound);
    j.at("upper_bound").get_to(v.upper_bound);
    j.at("objective_coeff").get_to(v.objective_coeff);
}

void to_json(json& j, const ConstraintSpec& c) {
    json coeffs = json::array();
    for (const auto& e : c.coefficients) {
        coeffs.push_back({{"index", e.index}, {"value", e.value}});
    }
    j = json{
        {"name", c.name},
        {"sense", static_cast<int>(c.sense)},
        {"rhs", c.rhs},
        {"coefficients", coeffs}
    };
}

void from_json(const json& j, ConstraintSpec& c) {
    j.at("name").get_to(c.name);
    int sense_val;
    j.at("sense").get_to(sense_val);
    c.sense = static_cast<model::ConstraintSense>(sense_val);
    j.at("rhs").get_to(c.rhs);
    c.coefficients.clear();
    for (const auto& e : j.at("coefficients")) {
        CoefficientEntry entry;
        e.at("index").get_to(entry.index);
        e.at("value").get_to(entry.value);
        c.coefficients.push_back(entry);
    }
}

void to_json(json& j, const QuadraticTermSpec& q) {
    j = json{{"row", q.row}, {"col", q.col}, {"coeff", q.coeff}};
}

void from_json(const json& j, QuadraticTermSpec& q) {
    j.at("row").get_to(q.row);
    j.at("col").get_to(q.col);
    j.at("coeff").get_to(q.coeff);
}

void to_json(json& j, const SOSMember& m) {
    j = json{{"index", m.index}, {"weight", m.weight}};
}

void from_json(const json& j, SOSMember& m) {
    j.at("index").get_to(m.index);
    j.at("weight").get_to(m.weight);
}

void to_json(json& j, const SOSConstraintSpec& s) {
    j = json{{"type", s.type}, {"members", s.members}};
}

void from_json(const json& j, SOSConstraintSpec& s) {
    j.at("type").get_to(s.type);
    j.at("members").get_to(s.members);
}

void to_json(json& j, const SolverOptionsSpec& o) {
    j = json{
        {"engine", o.engine},
        {"time_limit_seconds", o.time_limit_seconds},
        {"mip_gap", o.mip_gap},
        {"threads", o.threads},
        {"use_gpu", o.use_gpu}
    };
}

void from_json(const json& j, SolverOptionsSpec& o) {
    j.at("engine").get_to(o.engine);
    j.at("time_limit_seconds").get_to(o.time_limit_seconds);
    j.at("mip_gap").get_to(o.mip_gap);
    j.at("threads").get_to(o.threads);
    j.at("use_gpu").get_to(o.use_gpu);
}

bool SessionManager::save_session(const SessionState& state, const std::string& filepath) {
    try {
        json j;
        j["version"] = state.version;
        j["problem_name"] = state.problem_name;
        j["obj_sense"] = static_cast<int>(state.obj_sense);
        j["variables"] = state.variables;
        j["constraints"] = state.constraints;
        j["quadratic_terms"] = state.quadratic_terms;
        j["sos_constraints"] = state.sos_constraints;
        j["solver_options"] = state.solver_options;
        j["created_at"] = state.created_at;
        j["updated_at"] = state.updated_at;

        std::ofstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        file << j.dump(2) << "\n";
        return true;

    } catch (...) {
        return false;
    }
}

std::optional<SessionState> SessionManager::load_session(const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return std::nullopt;
        }

        json j;
        file >> j;

        SessionState state;
        j.at("version").get_to(state.version);
        j.at("problem_name").get_to(state.problem_name);
        int obj_sense_val;
        j.at("obj_sense").get_to(obj_sense_val);
        state.obj_sense = static_cast<model::ObjectiveSense>(obj_sense_val);
        j.at("variables").get_to(state.variables);
        j.at("constraints").get_to(state.constraints);
        j.at("quadratic_terms").get_to(state.quadratic_terms);
        j.at("sos_constraints").get_to(state.sos_constraints);
        j.at("solver_options").get_to(state.solver_options);
        j.at("created_at").get_to(state.created_at);
        j.at("updated_at").get_to(state.updated_at);

        return state;

    } catch (...) {
        return std::nullopt;
    }
}

std::string SessionManager::generate_session_path(const std::string& problem_name,
                                                     const std::string& directory) {
    std::string safe_name = sanitize_filename(problem_name);
    if (safe_name.empty()) {
        safe_name = "session";
    }

    std::string timestamp = timestamp_now();
    std::string filename = safe_name + "_" + timestamp + ".json";

    std::filesystem::path dir(directory);
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }

    return (dir / filename).string();
}

std::string SessionManager::timestamp_now() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
    return oss.str();
}

std::string SessionManager::sanitize_filename(const std::string& name) {
    std::string result;
    for (char c : name) {
        if (std::isalnum(c) || c == '_' || c == '-') {
            result += c;
        } else if (c == ' ') {
            result += '_';
        }
    }
    return result;
}

} // namespace hypernova::cli
