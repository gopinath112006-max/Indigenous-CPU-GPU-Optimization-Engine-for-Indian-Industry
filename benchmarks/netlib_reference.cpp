#include "netlib_reference.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace hypernova::benchmark {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;
    for (char ch : line) {
        if (ch == '"') {
            in_quotes = !in_quotes;
        } else if (ch == ',' && !in_quotes) {
            fields.push_back(current);
            current.clear();
        } else {
            current += ch;
        }
    }
    fields.push_back(current);
    return fields;
}

} // namespace

std::vector<ReferenceEntry> load_reference_csv(const std::string& path) {
    std::vector<ReferenceEntry> entries;
    std::ifstream in(path);
    if (!in.is_open()) {
        return entries;
    }

    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        auto fields = split_csv_line(line);
        if (fields.size() < 2) {
            continue;
        }
        // Skip header row if detected (name,status[,objective]).
        std::string status = to_lower(fields[1]);
        if (first && (fields[0] == "name" || status == "status")) {
            first = false;
            continue;
        }
        first = false;
        if (status != "min" && status != "max" && status != "inf" && status != "unb") {
            continue;
        }
        ReferenceEntry entry;
        entry.name = to_lower(fields[0]);
        entry.status = status;
        if (status != "inf" && status != "unb" && fields.size() >= 3) {
            try {
                entry.objective = std::stod(fields[2]);
            } catch (...) {
                continue;
            }
        }
        entries.push_back(entry);
    }
    return entries;
}

const std::vector<ReferenceEntry>& netlib_reference_table() {
    static const std::vector<ReferenceEntry> entries = {
        {"afiro",    "min",  -464.7531419},
        {"adlittle", "min",  225494.9632},
        {"bandm",    "min",  -158.628006},
        {"blend",    "min",  -30.81215084},
        {"dfl001",   "min",  11113286.6516},
        {"israel",   "min",  -896644.8258},
        {"kb2",      "min",  -1749.900254},
        {"recipe",   "min",  -266.616},
        {"sc105",    "min",  -52.20206121},
        {"sc205",    "min",  -52.20206121},
        {"sc50a",    "min",  -64.57507706},
        {"sc50b",    "min",  -70.0},
        {"share1b",  "min",  -76589.31824},
        {"share2b",  "min",  -415.732241},
        {"shell",    "min",  1208826925.6},
        {"stair",    "min",  -251.266951},
        {"sctap1",   "min",  1412.25},
        {"sctap2",   "min",  1724.8071},
        {"sctap3",   "min",  1424.0},
        {"tuff",     "min",  0.2921477651},
        {"25fv47",   "min",  5501.845889},
    };
    return entries;
}

} // namespace hypernova::benchmark