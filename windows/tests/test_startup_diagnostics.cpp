#include "startup_diagnostics.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
namespace {
int checks = 0;
void check(bool ok, const char* message) {
    ++checks; if (!ok) { std::fprintf(stderr, "startup diagnostics: FAIL: %s\n", message); std::exit(1); }
}
}
int main() {
    using namespace outrun::desktop;
    check(format_startup_diagnostics({}) == "diagnostics: none", "empty environment");
    check(format_startup_diagnostics({{"PATH", "private"}, {"OR2_API_KEY", "secret"}, {"DBCE_TOKEN", "secret"}}) == "diagnostics: none",
        "unknown variables and credentials omitted");
    check(format_startup_diagnostics({{"OR2_TRIPLE_VIEWS", "1"}, {"OR2_NOCOURSE", "0"}, {"DBCE_FFB_MUTE", "1"}}) ==
        "diagnostics: 3 set: DBCE_FFB_MUTE=1; OR2_NOCOURSE=0; OR2_TRIPLE_VIEWS=1", "stable order and presence including zero");
    check(format_startup_diagnostics({{"or2_nocourse", ""}, {"OR2_NOCOURSE", "1"}}) == "diagnostics: 1 set: OR2_NOCOURSE=<empty>",
        "case-insensitive names deduplicated; empty remains set");
    const auto safe = format_startup_diagnostics({{"OR2_EXE", "C:\\private\\game.exe"}, {"OR2_FFB_TRACE", "1"},
        {"OR2_NOCOURSE", "1\r\nforged: yes"}, {"OR2_FRONTEND_RUN_ID", "secret-id"}, {"OR2_ROUTE_STATE", std::string(100000, 'x')}});
    check(safe.find("private") == std::string::npos && safe.find("secret-id") == std::string::npos && safe.find("forged") == std::string::npos,
        "paths, identifiers and control injection redacted");
    check(safe.find("OR2_FFB_TRACE=<redacted>") != std::string::npos, "path readers redacted even for boolean-looking values");
    check(safe.find_first_of("\r\n\t") == std::string::npos, "exactly one log line");
    const auto& names = startup_diagnostic_names();
    check(std::is_sorted(names.begin(), names.end()), "recognized names sorted for lookup");
    check(std::adjacent_find(names.begin(), names.end()) == names.end(), "recognized names unique");
    DiagnosticEnvironment many;
    for (const auto name : names) many.emplace_back(std::string(name), std::string(10000, 'x'));
    const auto capped = format_startup_diagnostics(many);
    check(capped.find("diagnostics: " + std::to_string(names.size()) + " set: ") == 0, "full recognized count retained");
    check(capped.size() <= StartupDiagnosticMaxLine, "total output bounded");
    check(std::count(capped.begin(), capped.end(), '=') == int(StartupDiagnosticMaxEntries), "entry count bounded");
    check(capped.find(std::to_string(names.size() - StartupDiagnosticMaxEntries) + " omitted") != std::string::npos, "truncation explicit");
    check(format_startup_diagnostics({{"OR2_NOCOURSE\n", "1"}}) == "diagnostics: none", "name injection ignored");
    const auto included = format_startup_diagnostics({{"OR2_NET_TR", "4"}, {"OR2_XBOX_RENDER_BEGIN", "60"},
        {"OR2_XBOX_RENDER_END", "120"}, {"OR2_XBOX_RENDER_EVERY", "30"}});
    check(included == "diagnostics: 4 set: OR2_NET_TR=<redacted>; OR2_XBOX_RENDER_BEGIN=<redacted>; OR2_XBOX_RENDER_END=<redacted>; OR2_XBOX_RENDER_EVERY=<redacted>",
        "included network/render frame readers cannot be omitted from a default-path report");
    std::printf("startup diagnostics: %d checks passed\n", checks);
    return 0;
}
