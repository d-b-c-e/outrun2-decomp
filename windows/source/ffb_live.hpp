#pragma once
// OutRun force feedback, F2: the force model (ffb_outrun.hpp, "outrun-legacy@1") computed in the game as a software
// force stream, with NO device output. There is no device code here at all: nothing is opened, acquired or sent. A
// later layer adds the device (the toolkit's WheelFfb) with its own gates and an attended test.
//   - Settings: [FFB] in OutRunDesktop.ini with the OutRun 2006 mod's key names (FFBSpringStrength, ...), over the
//     mod's compiled defaults. [FFB] FFBPreset = owner-2026-10-08 starts from the owner's accepted values instead (an
//     explicit, labelled comparison preset; never applied silently). Parsed strictly and validated once.
//   - Where: a latched test process always computes, into its run folder (ffb-trace.csv beside its capture), so a
//     recorded session's playback yields its software force stream. Ordinary play computes only with
//     OR2_FFB_TRACE=<file> (a development switch).
//   - Lifecycle (ffb_race_epochs): computed in GAME (mode 16) only. A race start (GAME entered from anything but the
//     pause menu, 18) resets the model and opens a new epoch; a pause keeps its state. The mod's production path never
//     resets (state carries across pauses and races); its signal capture resets when a new local car starts, which is
//     what the decomp follows.
//   - Rows: the raw mode, epoch, race tick, the game frame, the inputs (round-trip floats), their validity, the surface
//     (stage unknown), sent and the held command (the deadband holds the last one), FFBGlobalStrength and the delivery
//     intent (held x global), which a device layer would send.
#include "desktop_settings.hpp"
#include "ffb_outrun.hpp"
#include <cstdint>
#include <string>
namespace outrun::desktop {
// The settings as the game reads them. `source` names what they start from ("mod-defaults" or the preset).
ffb::LegacySettings ffb_settings(const Ini& settings,std::string& notes,std::string& source);

// The computation's lifecycle, one call per game tick with the current mode: true when this tick computes; a race
// start resets (reset_now) and counts an epoch.
struct RaceEpochs {
    std::uint32_t last_mode{},epoch{};
    bool reset_now{};
    bool step(std::uint32_t mode);
};

// latched_run_folder: the fence's run folder in a latched process (empty otherwise).
void ffb_configure(const Ini& settings,bool latched,const std::string& latched_run_folder);
// Installs the per-tick computation after the current input filter (call after it is chosen). No-op when off.
void ffb_wrap_input_filter();
void ffb_shutdown();
}
