#pragma once

#include <string>
#include <vector>

namespace hypernova::benchmark {

// A single expected-result entry for a benchmark instance.
struct ReferenceEntry {
    std::string name;   // canonical instance name (compared case-insensitively)
    std::string status; // "min", "max", "inf", "unb"
    double objective = 0.0;
};

// Loads a reference table from CSV. Columns: name,status,objective.
// Lines starting with '#' are comments; a header row is skipped if detected.
// Returns the parsed entries (empty on missing/unreadable file).
std::vector<ReferenceEntry> load_reference_csv(const std::string& path);

// Built-in curated reference values for well-known Netlib AMPS instances.
// Objectives are the published optimal objective values from the Netlib
// AMPL default summary (all minimization, matching the MPS default MINIMIZE
// sense). These serve as a starter set; authoritative results can always be
// supplied explicitly via a reference CSV.
const std::vector<ReferenceEntry>& netlib_reference_table();

} // namespace hypernova::benchmark