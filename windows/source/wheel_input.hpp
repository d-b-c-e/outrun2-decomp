#pragma once
// Wheels, pedals, shifters and button boxes through DirectInput (windows/),
// bound by the canonical [Controls] of OutRunDesktop.ini (controls_store.hpp;
// Wheelkit writes it), merged into the game input after the gamepad mapping
// (xbox_runtime::input_filter). Devices are matched by instance GUID only. In
// the race the device record of 453BB0 carries the full-range steering word and
// the 0..255 pedals directly; in the menus the bound buttons drive the
// frontend. Development test injection and its observer: test_injection.hpp.
#include <string>
namespace outrun::platform { struct NativeInputState; struct NativeRuntimeContext; }
namespace outrun::desktop {
class Ini;
// Reads the settings and logs the controls start line (after the one-time
// import, at the first input sample, when one is pending). The devices open
// with the first input sample (the game window exists by then).
void wheel_input_configure(const Ini&);
void wheel_input_filter(platform::NativeInputState&,const platform::NativeRuntimeContext* runtime);
void wheel_input_shutdown();
// The instance GUID ("{...}", lower case) of the device the steer binding reads, while it is connected; "" otherwise
// (no binding, the device absent or lost, or this backend not in use). The force output's device identity (ffb_device.hpp).
std::string wheel_input_steer_device();
// For the F6 panel's Controls page: the profile and revision, how many bindings apply, and the steering device's state.
std::string wheel_input_status_text();
}
