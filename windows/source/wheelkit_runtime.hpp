#pragma once
// The Wheelkit runtime in OutRunDesktop: wheelkit.dll beside the program, through its plain C interface (wheelkit.h,
// from the wheelkit repository's runtime/native/sdk; the build takes it from WHEELKIT_SDK). The runtime owns the wheel,
// pedals and shifter (the Wheelkit profile in [Controls]), the F6 panel, telemetry's packet and sender and the test
// switches; this program only loads it and joins it to the game at three seams: the input filter, the present overlay
// and the car state. Without the DLL the game runs with its keyboard and gamepad and says so in runtime.log.
#include <string>
namespace outrun::desktop {
class Ini;
// Once, before the game runs: loads and starts the runtime, gives it the F6 pages and installs the present overlay.
void wheelkit_configure(const Ini& settings);
// After every other input filter: the wheel snapshot into the game's input, telemetry, the panel's changes; while F6
// is open the game gets a neutral sample.
void wheelkit_wrap_input_filter();
// True while F6 is open (the game's own shortcuts stay quiet).
bool wheelkit_panel_open();
void wheelkit_shutdown();
}
