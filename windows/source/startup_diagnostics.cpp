#include "startup_diagnostics.hpp"
#include <algorithm>
#include <cstdlib>
#include <map>

namespace outrun::desktop {
const std::vector<std::string_view>& startup_diagnostic_names() {
    // Literal environment readers in windows/source, windows/shim, xbox/source and src at a187a947.
    static const std::vector<std::string_view> names{
        "DBCE_FFB_MUTE", "OR2_680AD4_OLD", "OR2_680AD4_TRACE", "OR2_BLOB_DEBUG", "OR2_BULK_DEBUG",
        "OR2_COURSE_DEBUG", "OR2_CRT_PRINTF", "OR2_CUBE_DEBUG", "OR2_D3D11_CPU_VERTEX", "OR2_DBG_DRAWSTATS",
        "OR2_DBG_ONLY", "OR2_DEV_SCRIPTED_RECOVERY", "OR2_DIFF_VERBOSE", "OR2_EXE", "OR2_EXE_TRACE", "OR2_FE_DUMP",
        "OR2_FFB_PRODUCER2_SHADOW_TRACE", "OR2_FFB_TRACE", "OR2_FRAME_LATENCY", "OR2_FRONTEND_RUN_ID",
        "OR2_FRONTEND_STATE_TRACE", "OR2_GHOST_TRACE", "OR2_HEADLIGHT_FORCE", "OR2_HEART_DEBUG", "OR2_HOST_ALL_EVENTS",
        "OR2_HOST_AREA", "OR2_HOST_AREA_TRACE", "OR2_HOST_CAMERA", "OR2_HOST_CARDEBUG", "OR2_HOST_DISPLAY",
        "OR2_HOST_DRAWHASH_DUMP", "OR2_HOST_EXE_STATE", "OR2_HOST_FE_DUMP", "OR2_HOST_FIXEDCLOCK", "OR2_HOST_FRAME_HZ",
        "OR2_HOST_GLYPHS", "OR2_HOST_GOAL", "OR2_HOST_GOAL_COURSE", "OR2_HOST_GOAL_SCORE", "OR2_HOST_LIVE",
        "OR2_HOST_MODE", "OR2_HOST_OVERLAY", "OR2_HOST_OWNERDEBUG", "OR2_HOST_PCM", "OR2_HOST_POOL", "OR2_HOST_RACE",
        "OR2_HOST_RACERS", "OR2_HOST_SKIP_ENDING", "OR2_HOST_SOUND", "OR2_HOST_STALL", "OR2_HOST_START_EVENTS",
        "OR2_HOST_TRACE", "OR2_HOST_UNLOCK", "OR2_HOST_VARIANT", "OR2_ICS_TRACE", "OR2_INPUT_DEBUG",
        "OR2_KEEP_TEXTURE_RGBA", "OR2_LEAVE_DEBUG", "OR2_MATRIX_TRACE", "OR2_MEMORY_BUILD_CHECK", "OR2_NET_DEBUG",
        "OR2_NET_DIFF", "OR2_NET_SCREENS", "OR2_NET_TEXT", "OR2_NET_TR", "OR2_NET_TRACE", "OR2_NET_VLAN", "OR2_NOCOURSE",
        "OR2_NOCUBE", "OR2_NO_OBJECTS", "OR2_OBJECTS_TRACE", "OR2_PAUSE_DEBUG", "OR2_ROUTE_STATE", "OR2_SHADOW_DEBUG",
        "OR2_SHADOW_OFF", "OR2_SMOKE_TAIL", "OR2_TELEMETRY", "OR2_TELEMETRY_PORT", "OR2_TRIPLE_AHEAD",
        "OR2_TRIPLE_BEHIND", "OR2_TRIPLE_TRACE", "OR2_TRIPLE_VIEWS", "OR2_TR_COUNT", "OR2_TR_STACK",
        "OR2_UNMAPPED_TRACE", "OR2_XBOX_DRAW_STATS", "OR2_XBOX_REFRESH_HZ", "OR2_XBOX_RENDER_BEGIN",
        "OR2_XBOX_RENDER_END", "OR2_XBOX_RENDER_EVERY", "OR2_XBOX_SCREENSHOT_DIR"
    };
    return names;
}
namespace {
std::string value_for_log(std::string_view name, const std::string& value) {
    // Path/identifier readers stay redacted even when their value happens to be a boolean token.
    constexpr std::string_view private_values[]{"OR2_EXE", "OR2_EXE_TRACE", "OR2_FFB_TRACE",
        "OR2_FFB_PRODUCER2_SHADOW_TRACE", "OR2_FRONTEND_RUN_ID", "OR2_FRONTEND_STATE_TRACE",
        "OR2_ROUTE_STATE", "OR2_XBOX_SCREENSHOT_DIR", "OR2_NET_VLAN"};
    for (const auto key : private_values) if (name == key) return "<redacted>";
    if (value.empty()) return "<empty>";
    if (value == "0" || value == "1" || value == "true" || value == "false" || value == "on" || value == "off") return value;
    return "<redacted>";
}
}
std::string format_startup_diagnostics(const DiagnosticEnvironment& environment) {
    std::map<std::string, std::string> set;
    const auto& recognized = startup_diagnostic_names();
    for (const auto& entry : environment) {
        auto name = entry.first;
        for (auto& c : name) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
        if (!std::binary_search(recognized.begin(), recognized.end(), std::string_view(name))) continue;
        set.emplace(name, value_for_log(name, entry.second));
    }
    if (set.empty()) return "diagnostics: none";
    std::string line = "diagnostics: " + std::to_string(set.size()) + " set: ";
    std::size_t shown = 0;
    for (const auto& entry : set) {
        const auto text = (shown ? "; " : "") + entry.first + "=" + entry.second;
        if (shown == StartupDiagnosticMaxEntries || line.size() + text.size() > StartupDiagnosticMaxLine - 64) break;
        line += text; ++shown;
    }
    if (shown < set.size()) line += "; ... " + std::to_string(set.size() - shown) + " omitted";
    return line;
}
std::string startup_diagnostics() {
    DiagnosticEnvironment environment;
    for (const auto name : startup_diagnostic_names()) {
        // Reader names are string literals. Copy at most 25 bytes; values cannot expand the report.
        if (const char* value = std::getenv(name.data())) {
            std::size_t size = 0; while (size < 25 && value[size]) ++size;
            environment.emplace_back(std::string(name), std::string(value, size));
        }
    }
    return format_startup_diagnostics(environment);
}
}
