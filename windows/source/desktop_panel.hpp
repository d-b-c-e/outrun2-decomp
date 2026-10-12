#pragma once
// The F6 panel (docs/F6-PANEL.md; STD-030 / 004 / 017 / 023): the wheel toolkit's native panel (vendor/native/panel,
// Dear ImGui) drawn by this build's own renderer through xbox_runtime::present_overlay, not the toolkit's Present hook.
// Step 1: the frame and the Help page (version, UI scale, frame rate). F6 toggles, Esc closes; closed, every other key
// reaches the game untouched and the per-frame cost is the frame-rate note and the dev-channel poll.
//   - Ordinary process: the panel keeps [Panel] View / Scale / Page in OutRunDesktop.ini (the file Wheelkit reads).
//   - Latched (test) process: nothing is saved (no ini path); the panel opens only through the dev channel.
//   - Dev channel (unattended checks, no OS input): a file "OutRunDesktop.panel-dev.txt" beside the settings (a latched
//     run: in its run folder) whose text is "open <Page> [simple|advanced]" or "close"; one written before the process
//     started is ignored (the toolkit's rule).
#include <string>
namespace outrun::desktop {
class Ini;
// Once, before the game runs (installs the present overlay). run_folder: the latched run's folder (empty otherwise).
void desktop_panel_configure(const Ini& settings,bool latched,const std::string& run_folder);
// True while the panel is open (the input filter neutralises the game's input then).
bool desktop_panel_open();
// After the game: the ImGui backend and context are released.
void desktop_panel_shutdown();
}
