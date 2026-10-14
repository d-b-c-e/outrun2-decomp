#pragma once
// OutRun force feedback: the default legacy model and explicit producer@2 selection. Both use the gated F4 delivery
// (ffb_device.hpp), OFF unless [FFB] Output = wheel, never in a
// latched process or under DBCE_FFB_MUTE, and with WheelFfb.dll not even loaded then.
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
#include "ffb_device.hpp"
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

// The wheel output's configuration (pure): [FFB] Output = wheel | none (default none; anything else is noted and is
// none), the mute (`mute_env`, DBCE_FFB_MUTE: any value but empty or 0 mutes), the process kind, the input backend
// ([Input] Backend = Shim: the shim hands the wheel over, ffbdev::Owner) and the device instance [Controls] steer reads
// (the force goes to the wheel the player steers with, never one chosen by name or index).
ffbdev::Config ffb_output_config(const Ini& settings,bool latched,const char* mute_env,std::string& notes);

// latched_run_folder: the fence's run folder in a latched process (empty otherwise).
void ffb_configure(const Ini& settings,bool latched,const std::string& latched_run_folder);
// Installs the per-tick computation after the current input filter (call after it is chosen). No-op when off.
void ffb_wrap_input_filter();
void ffb_shutdown();
// For the F6 panel: the wheel output's state ("off", with the shim backend why it is off and its steering wheel as the
// shim reads it (shim_input_steer), or the delivery's phase and why), whether it was started,
// and Stop FFB (F8, STD-004): the device quiesced and released for this process, then the DLL unloaded.
std::string ffb_status_text();
bool ffb_output_running();
void ffb_stop_output();
// For the F6 panel's next-start setting: WheelFfb.dll (the force add-on) is beside the game. A file check only; it never
// loads the library.
bool ffb_library_present();
// "The game window is this process's and the foreground window" (default: GetForegroundWindow). A test seam, so a test
// never has to take the focus on the desktop.
using FfbFrontCheck=bool(*)(void* hwnd);
FfbFrontCheck& ffb_front_check();
// The shim backend's hand-over (INPUT-SHIM.md section 6): the loaded library while the wheel output waits for its open
// with Input = Shim (null otherwise), for the shim's WheelFfb bridge; and the bridge's Owner, set before the open (game
// thread). Without an Owner the output never opens with Input = Shim.
const WheelFfbApi* ffb_shim_wheel_api();
void ffb_set_shim_owner(ffbdev::Owner*);
}
