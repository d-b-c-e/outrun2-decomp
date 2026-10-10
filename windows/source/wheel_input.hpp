#pragma once
// Wheel, pedals, shifter and button box through DirectInput (windows/): up to
// five devices (wheel, separate throttle and brake, shifter, aux) bound with
// the OutRun2006Tweaks [DirectInput] settings, merged into the game input
// after the gamepad mapping (xbox_runtime::input_filter). In the race the
// device record of 453BB0 carries the full-range steering word and the 0..255
// pedals directly; in the menus the bound buttons drive the frontend.
#include <cstdint>
namespace outrun::platform { struct NativeInputState; }
namespace outrun::desktop {
class Ini;
// Reads the settings; the devices open with the first input sample (the
// game window exists by then).
void wheel_input_configure(const Ini&);
void wheel_input_filter(platform::NativeInputState&,std::uint32_t mode);
void wheel_input_shutdown();
}
