#pragma once
// One runtime.log line when the main camera's effective race view changes (sana 70042): the game mode (78026C), race
// variant (780258), view (+34A), the projection 484BD0 actually used (+A0 / +A4 FOV, +BC near, +B8 aspect), the render
// size, and whether the [Triple] MatchGameFov = false FOV override or Tweaks' FixZBufferPrecision near plane was armed.
// Read only: it never changes the camera. A line is written only when the mode, the view or the override changes, so
// repeated frames add nothing. Outside race and goal modes the view is not tracked (menus and attract cameras would
// otherwise log every switch); only the mode change is.
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace outrun::platform {

struct RaceViewSample {
    std::uint32_t mode_78026c{};
    std::int32_t variant_780258{};
    bool race{};                 // race_camera_mode_48618d(mode) or a goal view: the main camera's race views
    std::uint8_t view_34a{};
    float fov_y_a0{}, fov_x_a4{}, znear_bc{}, aspect_b8{};   // radians / world units, as 484BD0 left them
    float width{}, height{};     // the render the camera projects for (PcCameraScreen)
    bool fov_override{};         // camera_fov_context().fov_y > 0 while this control ran
    bool znear_fix{};            // camera_znear_context().armed while this control ran
};

class RaceViewNote {
public:
    // The line to log for this control, or nothing when the tracked state is unchanged.
    std::optional<std::string> observe(const RaceViewSample& s) {
        const int view = s.race ? int(s.view_34a) : -1;
        if (any_ && s.mode_78026c == mode_ && view == view_ && s.fov_override == override_) return std::nullopt;
        any_ = true; mode_ = s.mode_78026c; view_ = view; override_ = s.fov_override;
        constexpr double deg = 57.29577951308232;
        char line[320];
        if (!s.race) {
            std::snprintf(line, sizeof line, "race view: none (mode 0x%x, variant %d; front end or other camera)",
                          unsigned(s.mode_78026c), int(s.variant_780258));
        } else {
            std::snprintf(line, sizeof line,
                          "race view: mode 0x%x variant %d view %d; FOV %.1f deg vertical / %.1f deg horizontal, near %.2f, "
                          "aspect %.3f, render %.0fx%.0f; FOV override %s, near-plane fix %s",
                          unsigned(s.mode_78026c), int(s.variant_780258), view, double(s.fov_y_a0) * deg,
                          double(s.fov_x_a4) * deg, double(s.znear_bc), double(s.aspect_b8), double(s.width),
                          double(s.height), s.fov_override ? "on ([Triple] MatchGameFov = false)" : "off",
                          s.znear_fix ? "on (FixZBufferPrecision)" : "off");
        }
        return std::string(line);
    }

private:
    bool any_{};
    std::uint32_t mode_{};
    int view_{-1};
    bool override_{};
};

} // namespace outrun::platform
