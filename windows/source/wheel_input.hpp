#pragma once
// Wheels, pedals, shifters and button boxes through DirectInput (windows/),
// bound by the canonical [Controls] of OutRunDesktop.ini (controls_store.hpp;
// Wheelkit writes it), merged into the game input after the gamepad mapping
// (xbox_runtime::input_filter). Devices are matched by instance GUID only. In
// the race the device record of 453BB0 carries the full-range steering word and
// the 0..255 pedals directly; in the menus the bound buttons drive the
// frontend. Development test injection and its observer: test_injection.hpp.
#include <string>
#include <vector>
namespace dbce::controls::edit { struct AcceptedObservation; }
namespace outrun::platform { struct NativeInputState; struct NativeRuntimeContext; }
namespace outrun::desktop {
class Ini;
// Reads the settings and logs the controls start line (after the one-time
// import, at the first input sample, when one is pending). The devices open
// with the first input sample (the game window exists by then).
void wheel_input_configure(const Ini&);
void wheel_input_filter(platform::NativeInputState&,const platform::NativeRuntimeContext* runtime);
void wheel_input_shutdown();
// Any thread (the window procedure's WM_DEVICECHANGE): the controllers changed, so the next input sample rescans them
// (wheel_rescan.hpp). Without it the native path rescans only while a binding has no open, connected device.
void wheel_input_devices_changed();
// The instance GUID ("{...}", lower case) of the device the steer binding reads, while it is connected; "" otherwise
// (no binding, the device absent or lost, or this backend not in use). The force output's device identity (ffb_device.hpp).
std::string wheel_input_steer_device();
// That device's product GUID ("{...}", lower case, from its DirectInput listing), under the same conditions; "" otherwise.
// The force output's measured direction is per product (ffb_device.hpp wheel_direction).
std::string wheel_input_steer_product();
// For the F6 panel's Controls page: the profile and revision, how many bindings apply, and the steering device's state.
std::string wheel_input_status_text();
// For the F6 binding editor, on the input thread after wheel_input_filter: whether this Native process can feed capture,
// and this tick's reads of the devices it already opened. No new device, open or enumeration.
bool wheel_input_binding_edit_allowed();
std::vector<dbce::controls::edit::AcceptedObservation> wheel_input_binding_observations();
}
