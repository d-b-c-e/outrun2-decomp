#pragma once
// The one game window: a Win32 window in the PC build (window_win32.cpp),
// the application's CoreWindow on Xbox (window_uwp.cpp). Both run
// or2_xbox_main (main.cpp) on the thread that owns the window.
#include <cstdint>
#include <string>
namespace outrun::platform { struct NativeInputState; struct NativeRuntimeContext; }
namespace outrun::xbox_runtime {
// Desktop builds (windows/) adjust the game input after the gamepad mapping:
// wheel, pedals and further devices. `runtime` is null until the game runs.
// Unset on the Xbox and in the PC test build.
using InputFilter=void(*)(outrun::platform::NativeInputState&,const outrun::platform::NativeRuntimeContext* runtime);
InputFilter& input_filter();
// Desktop builds draw their own overlay (the F6 panel, windows/source/desktop_panel) into the back buffer just before
// each frame is presented: the D3D11 device, its immediate context and the back buffer's render target view (void*, so
// this header stays free of graphics headers), and the swap chain's full size. Unset on the Xbox and in the PC test build.
using PresentOverlay=void(*)(void* device,void* context,void* back_buffer_view,unsigned width,unsigned height);
PresentOverlay& present_overlay();
// A latched (test) desktop process: home() is this run folder and the game folder is only read (data, the EXE image
// cache). Empty in every other process. Set by the desktop entry point before or2_xbox_main.
std::string& latched_home();
// The game folder the fence seeded from (latched processes only): the data root must be this folder.
std::string& latched_game_root();
struct NativeWindow {
    void* hwnd{};          // HWND (PC build)
    void* core_window{};   // ABI ICoreWindow* (UWP build)
    unsigned width{},height{};   // swap chain size in pixels
};
NativeWindow& native_window();
// Dispatches the pending window events; false once the window was closed.
bool pump_window_events();
// The folder the package or the executable was started from, and the
// folders the game data may be in, in search order (Xbox: removable drives,
// then the application's LocalState).
std::string launch_folder();
std::string local_state_folder();
// Switches the output to a refresh rate of at least `hz` when it offers one
// (Xbox: an HDMI mode at the current resolution; PC: the monitor's current
// rate is kept). Returns the rate the output runs at afterwards.
unsigned select_refresh_rate(unsigned hz);
}
int or2_xbox_main(int argc,char** argv);
