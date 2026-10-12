#pragma once
// The input shim in a latched (test) process: the first in-game loop (INPUT-SHIM.md section 15, CONTROL-JOURNAL.md).
// Ordinary play keeps the 35a8aa3 wheel path (wheel_input.hpp) unchanged; only a fenced, latched process uses this.
//   ProviderHost: real DirectInput through DInputProvider and Core, on the game's input thread.
//   Control worker: FileIo over the fence's inject.on / inject.txt, journal in the run folder.
//   OutRun adapter: at xbox_runtime::input_filter, after the pad adapter and before 453BB0 consumes the input.
// No recorder yet (NoCapture): capture and replay are the next slice.
#include "test_fence.hpp"
#include <string>
namespace outrun::platform { struct NativeInputState; struct NativeRuntimeContext; }
namespace outrun::desktop {
class Ini;
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
bool shim_backend_selected(const Ini& settings);
// Before the game starts: the exact [Controls] bytes, and the folder for the host's shutdown report (the game folder).
void shim_input_configure_ordinary(const Ini& settings,const std::string& folder);
// The input thread: opens at the first sample, then one Core frame per tick, applied with the desktop keyboard.
void shim_input_filter_ordinary(platform::NativeInputState&,const platform::NativeRuntimeContext* runtime);
// The input thread, after the game loop: the final neutral tick and the host stop. Never parks: an ordinary process exits.
void shim_input_shutdown_ordinary();
// For the F6 panel's Controls page: the ordinary shim backend's devices (open / not, each one's state); empty when this
// backend is not the one in use. Game thread (the input sample's thread).
std::string shim_input_status_text();
}
