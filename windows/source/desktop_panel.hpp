#pragma once
// The F6 panel (docs/F6-PANEL.md; STD-030 / 004 / 017 / 023): the wheel toolkit's native panel (vendor/native/panel,
// Dear ImGui) drawn by this build's own renderer through xbox_runtime::present_overlay, not the toolkit's Present hook.
// Step 1: the frame and the Help page (version, UI scale, frame rate). F6 toggles, Esc closes; closed, every other key
// reaches the game untouched. Ordinary shared-Shim play also draws contextual live binding hints while closed.
//   - Ordinary process: the panel keeps [Panel] View / Scale / Page in OutRunDesktop.ini (the file Wheelkit reads).
//   - Latched (test) process: nothing is saved (no ini path); the panel opens only through the dev channel.
//   - Dev channel (unattended checks, no OS input): a file "OutRunDesktop.panel-dev.txt" beside the settings (a latched
//     run: in its run folder) whose text is "open <Page> [simple|advanced]" or "close"; one written before the process
//     started is ignored (the toolkit's rule).
#include <string>
namespace outrun::platform { struct NativeInputState; }
namespace outrun::desktop {
class Ini;
// While the panel is open the game gets a neutral input sample (UX-03): no buttons, centred axes, pedals released, the
// pad and wheel records at rest; only the presence flags (connected, the PC layer) stay as the backend set them. The
// keyboard's held keys are cleared whenever the panel opens or closes (its hook takes the key-ups meanwhile, which
// would otherwise leave a key held after the close).
void desktop_panel_neutral_input(platform::NativeInputState&);
// The panel's input filter after the current one (call after the backend, telemetry and FFB wrappers).
void desktop_panel_wrap_input_filter();
// Once, before the game runs (installs the present overlay). run_folder: the latched run's folder (empty otherwise).
// playback: a playback process (shim_input.hpp): the panel never opens (no F6 hook, no dev channel); only the badge.
void desktop_panel_configure(const Ini& settings,bool latched,const std::string& run_folder,bool playback=false);
// True while the panel is open (the input filter neutralises the game's input then).
bool desktop_panel_open();
// After the game: the ImGui backend and context are released.
void desktop_panel_shutdown();
}
