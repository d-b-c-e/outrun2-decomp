#pragma once
// The Wheelkit runtime in OutRunDesktop: wheelkit.dll beside the program, through its plain C interface (wheelkit.h,
// from the wheelkit repository's runtime/native/sdk; the build takes it from WHEELKIT_SDK). The runtime owns the wheel,
// pedals and shifter (the Wheelkit profile in [Controls]), the F6 panel, telemetry's packet and sender and the test
// switches; this program only loads it and joins it to the game at three seams: the input filter, the present overlay
// and the car state. Without the DLL the game runs with its keyboard and gamepad and says so in runtime.log.
#include <string>
namespace outrun::desktop {
class Ini;
// Once, before the window is created: loads and starts the runtime, gives it the F6 pages and the game's camera views,
// and installs the present overlay.
void wheelkit_configure(const Ini& settings);
// Triple screens (wk_triples_get): where the borderless window opens, whether screens are on at all (wide), whether
// the window spans three screens and whether their measurements can be used (rig). on is false without the runtime:
// the caller then opens on the primary display, one screen.
struct WheelkitLayout { bool on{},wide{},span{},rig{}; long x{},y{},width{},height{}; float realistic_fov{}; std::string text; };
WheelkitLayout wheelkit_layout();
// The three views' clip corrections for the centre projection the game used (wk_triples_views; pc_screen.hpp
// PcTripleCorrections: 3 x 16 floats). False: that draw stays single.
bool wheelkit_view_corrections(const float* projection,float* out);
// Curved screens ([Triple] CurveRadiusMm above 0): for one screen (0 left, 1 centre, 2 right), where each point of a
// (columns + 1) x (rows + 1) grid on its curved glass, seen from the seat, takes its picture from in that screen's flat
// view; u, v pairs row by row from the top (wk_triples_curve_grid, the runtime's warp). The seat's eye, whatever the
// camera's framing: a view drawn for another eye (Game framing) keeps the seat's curve correction (Wheelkit STD-041).
// radius_mm gets the screens' curve. False: flat screens, no usable measurements or no runtime.
bool wheelkit_curve_grid(int screen,int columns,int rows,float* uv,double& radius_mm);
// The camera on screen (wk_cameras_frame): view is the index in the list given at start (Bumper, Cockpit, Chase,
// Bonnet) or -1 when not driving. What the runtime attaches to it; view is -1 in the answer when nothing applies.
struct WheelkitCamera { int view{-1}; bool realistic{}; float fov_offset{},right_cm{},up_cm{},forward_cm{}; };
WheelkitCamera wheelkit_camera(int view,const char* car,bool projected_views);
// After every other input filter: the wheel snapshot into the game's input, telemetry, the panel's changes; while F6
// is open the game gets a neutral sample.
void wheelkit_wrap_input_filter();
// True while F6 is open (the game's own shortcuts stay quiet).
bool wheelkit_panel_open();
// One line into the runtime's log (wheelkit.log) and runtime.log; runtime.log alone without the runtime.
void wheelkit_log(const char* line);
void wheelkit_shutdown();
}
