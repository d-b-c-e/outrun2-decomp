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
}
