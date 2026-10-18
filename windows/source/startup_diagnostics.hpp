#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace outrun::desktop {
using DiagnosticEnvironment = std::vector<std::pair<std::string, std::string>>;
constexpr std::size_t StartupDiagnosticMaxEntries = 32;
constexpr std::size_t StartupDiagnosticMaxLine = 2048;
// Recognized production readers only; unknown OR2_/DBCE_ names are never enumerated or printed.
const std::vector<std::string_view>& startup_diagnostic_names();
// Pure, stable, single-line formatter. Presence is reported even for an empty or "0" value.
// Only exact boolean tokens are printable; paths, identifiers and other values are redacted.
std::string format_startup_diagnostics(const DiagnosticEnvironment& environment);
// Reads only the recognized names. Does not set/clear environment variables or configure gameplay.
std::string startup_diagnostics();
}
