#pragma once
// OutRun force feedback, F2: the force model (ffb_outrun.hpp, "outrun-legacy@1") computed in the game for a trace, with
// NO device output. There is no device code here at all: nothing is opened, acquired or sent. A later layer adds the
// device (the toolkit's WheelFfb) with its own gates and an attended test.
//   - Settings: [FFB] in OutRunDesktop.ini with the OutRun 2006 mod's key names (FFBSpringStrength, ...), over the
//     owner's accepted values (ffb::owner_accepted). Parsed strictly and validated once; refused values are logged.
//   - Computed only with OR2_FFB_TRACE=<file> (a development switch), and never in a latched test process. One row
//     per race tick (GAME, mode 16) to that CSV, its header naming the model, the settings, the periodic mode (the
//     CF fallback, since there is no device) and the stage source (unknown: the surface LUT's stage-dependent water
//     and 0x400000 cases use their defaults until the race area exposes the player's stage).
//   - runtime.log: the state at start, and the tick and force counts at exit.
#include "desktop_settings.hpp"
#include "ffb_outrun.hpp"
#include <string>
namespace outrun::desktop {
// The settings as the game reads them: the owner's accepted values, then each [FFB] key present. A key whose value
// is not a number (or a boolean, for the two switches) keeps the default, noted in `notes`.
ffb::LegacySettings ffb_settings(const Ini& settings,std::string& notes);
void ffb_configure(const Ini& settings,bool latched);
// Installs the per-tick computation after the current input filter (call after it is chosen). No-op when off.
void ffb_wrap_input_filter();
void ffb_shutdown();
}
