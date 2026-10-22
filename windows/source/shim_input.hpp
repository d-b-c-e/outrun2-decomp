#pragma once
// The input shim in a latched (test) process: the first in-game loop (INPUT-SHIM.md section 15, CONTROL-JOURNAL.md).
// Ordinary play keeps the 35a8aa3 wheel path (wheel_input.hpp) unchanged; only a fenced, latched process uses this.
//   ProviderHost: real DirectInput through DInputProvider and Core, on the game's input thread.
//   Control worker: FileIo over the fence's inject.on / inject.txt, journal in the run folder.
//   OutRun adapter: at xbox_runtime::input_filter, after the pad adapter and before 453BB0 consumes the input.
// A latched run captures @1 (ShimRecorder); ordinary play captures @2 only with a record request (OrdinaryRecorder).
#include "desktop_settings.hpp"
#include "session_request.hpp"
#include "test_fence.hpp"
#include <cstdint>
#include <string>
#include <cstring>   // _stricmp
#include <vector>
namespace dbce::controls::edit { struct AcceptedObservation; }
namespace outrun::platform { struct NativeInputState; struct NativeRuntimeContext; }
namespace outrun::desktop {
// Before the game starts: keeps the plan and the exact [Controls] bytes. Nothing is opened.
void shim_input_configure(const fence::Plan& plan,const Ini& settings);
// The input thread: opens at the first sample (the game window exists by then), then one Core frame per tick.
void shim_input_filter(platform::NativeInputState&,const platform::NativeRuntimeContext* runtime);
// The input thread, after the game loop: close admission, the final neutral tick, then the host stop. An unresolved
// stop parks for the supervisor (P21); a resolved one returns.
void shim_input_shutdown();

// Ordinary play on the shim backend: [Input] Backend = Shim in OutRunDesktop.ini, an unlatched process only. The same Core,
// DInputProvider and [Controls] profile (ini-controls-1, the form Wheelkit writes) as DRIVE's shim, with no control
// worker, no injection files and no capture. The native wheel path (wheel_input) is not started, so the profile's
// devices are read by the shim alone. That is the isolation: no device identity is compared or guessed. The pad
// (Windows.Gaming.Input) and the desktop keyboard keep their own native paths, the keyboard exactly as with the native
// wheel path (Adapter::apply with a Keyboard). The default stays Native.
inline bool shim_backend_selected(const Ini& settings){return _stricmp(settings.text("Input","Backend","Native").c_str(),"Shim")==0;}
// Before the game starts: the exact [Controls] bytes, and the folder for the host's shutdown report (the game folder).
void shim_input_configure_ordinary(const Ini& settings,const std::string& folder);
// Play with Recording (ordinary_recording.hpp), before shim_input_configure_ordinary: the record request the start
// admitted (session::take) and the running exe's SHA-256. configure arms the recorder with the [Controls] bytes it reads;
// the capture follows the input the game consumed after 453BB0 (platform::native_input_consumed_hook).
void shim_input_arm_recording(const session::Request& request,const std::string& exe_sha256);
// The REC badge for the present thread: empty unless this session records.
std::string shim_input_recording_badge();
// The input thread: opens at the first sample, then one Core frame per tick, applied with the desktop keyboard.
void shim_input_filter_ordinary(platform::NativeInputState&,const platform::NativeRuntimeContext* runtime);
// The input thread, after the game loop: the final neutral tick and the host stop. Never parks: an ordinary process exits.
void shim_input_shutdown_ordinary();
// Playback (an admitted playback request, session::Intake::Kind::Playback; sana 37135/37176): no shim host, no device
// opened. The consumed-input seam plays the request's tape from the first input tick (consumed_replay.hpp), then holds
// neutral; the replay's own @2 capture (OrdinaryRecorder, the request's recording\capture, replay.tapeRow on every row)
// stops itself TapeEndRows rows after the tape's last row ("tape-end"). The [Controls] bytes are the recording's.
// false: the tape cannot be played (the reason logged, status.json failed); the caller ends the process.
constexpr std::uint64_t TapeEndRows=60;
bool shim_input_configure_playback(const session::Intake& intake,const Ini& settings);
void shim_input_filter_playback(platform::NativeInputState&,const platform::NativeRuntimeContext* runtime);
void shim_input_shutdown_playback();
std::string shim_input_playback_badge();   // the present thread: "PLAYBACK" while the tape plays, then the save's state
// For the F6 panel's Controls page: the ordinary shim backend's devices (open / not, each one's state); empty when this
// backend is not the one in use. Game thread (the input sample's thread).
std::string shim_input_status_text();
// Input-thread copies from the ordinary host's already accepted tick and adopted device reports.
// No tick/read/open/enumeration occurs here. Recording, tests, playback and stopped readers are excluded.
bool shim_input_binding_edit_allowed();
std::vector<dbce::controls::edit::AcceptedObservation> shim_input_binding_observations();
// The steering wheel as the ordinary shim backend reads it, for force feedback (INPUT-SHIM.md section 6, one owner):
// the [Controls] steer binding's device instance and product, from the profile bytes Core parses (no second mapping
// store), joined to Core's live report of that instance. `opened` only when that report is open and its product (Core
// checks it against the listing and the opened handle) is the binding's; `epoch` is the report's (a reopen is a new
// epoch, 0 before the first open). `live`: this tick's steer action is applied from a valid, fresh, not injected sample
// of that device in that epoch (an open handle can still be pending, failed, stale or range-unqualified; romi 31707).
// `dev` is empty when steer is not bound or this backend is not in use; `status` is Core's device status, or why there
// is none. `inverted` is that same parsed binding's input inversion. Game thread (the input sample's thread).
struct ShimSteer { std::string dev,prod,status; bool opened{},live{}; std::uint32_t epoch{}; bool inverted{}; };
ShimSteer shim_input_steer();
}
