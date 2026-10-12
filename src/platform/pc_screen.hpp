#pragma once
// Port platform option: the half width (in 480-line PC pixels) of the view the
// game draws. 320 is the PC's 4:3 screen (6281C8 / 5DA2D4); the widescreen
// option (Y) sets 240*16/9 so the screen-bound visibility tests (4F02D0) keep
// objects until they leave the wider view.
#include <string>
namespace outrun::platform {
inline float g_pc_view_half_width=320.0f;
// The planar span (windows/docs/TRIPLE-SCREENS.md step 2): the window's aspect (width / height) when [Triple] Screens is
// not Off and the window is wider than 16:9, else 0. Then the PC screen the camera sees is 480 x this wide (Hor+, one
// wide frustum: not three views), the PC frame fills the window, and its 2D keeps the centred 4:3. Set once by the
// desktop window; 0 on every other platform.
inline float g_pc_wide_aspect=0.0f;
// The three-view spike (TRIPLE-SCREENS.md step 4; development only: OR2_TRIPLE_VIEWS=1 with a planar span window). Given
// the projection a back-buffer draw uses (D3D row-vector layout, 16 floats), writes the clip corrections of the left,
// centre and right panels (3 x 16 floats, row-major, row vectors; applied before the D3D9 pixel-centre fix) and returns
// true; false for a projection that is not a centred perspective (the draw then stays single, on the centre panel).
// Null: one view. While set, the game's camera is the centre panel's (16:9) and the PC device replicates its
// back-buffer draws into three viewports (d3d11_d3d9.cpp triple_bind).
using PcTripleCorrections=bool(*)(const float* projection,float* corrections);
inline PcTripleCorrections g_pc_triple_corrections=nullptr;
// [Graphics] ScreenEdgeCullFix (the Tweaks key, on by default there and here): the object visibility test 4F0E40
// (cones, course objects, gates) takes the same widened view as 4F02D0, so a widescreen screen edge no longer culls
// objects that are still on it. Off: 4F0E40 keeps the 4:3 bounds (4F02D0 is widened either way).
inline bool g_pc_screen_edge_cull_fix=true;
// [Bugfixes] FixParticleRendering (the Tweaks key, on by default there and here; hooks_bugfixes.cpp):
// - 418CF0's screen mapping is the Xbox code, whose [740C8C] / [740C90] were half the resolution; on the PC they are
//   the whole of it, so those particles (grass among them) land off screen. Tweaks halves the screen x, y and the
//   size at 419009 (the size before its clamp, rounded to float); race_particles_draw.cpp does the same there.
// - Metropolis fireworks: 44DE10 draws object 0x630001 with no node list (node 0: road barriers instead); Tweaks draws
//   0x1A000A with the node list of DrawDistanceBehind = 2 in its place (race_area.cpp).
inline bool g_pc_fix_particle_rendering=true;
// [Graphics] CarBaseShadowOpacity (the Tweaks key, 1.0 by default there and here; 0 = the PC's own: no base shadow;
// any finite value >= 0, as Tweaks reads it).
// The PC build nulled CalcPeraShadow (O2SP / C2C Xbox): its call sites call the bare RET 49A650 instead. Tweaks'
// RestoreCarBaseShadow puts it back: the flat shadow object drawn with DrawObjectAlpha (4056D0) under the car's
// own matrix (+B0) raised 0.05, its alpha the call's argument times this factor (C2C Xbox halved it, too faint on
// the PC). vehicle_model_draw.cpp: the race model draw 469600 (its 49A650 call at 469EB4) and the car-select draws
// 46A560 (46AC76) and 46AE70 (46B766), Tweaks' three sites.
inline float g_pc_car_base_shadow_opacity=1.0f;
// Port performance overlay (Minus): lines separated by '\n', drawn by the 2D
// flush 42D710 on top of the frame with the game's font 9. Empty = hidden.
inline std::string g_pc_overlay_text;
// Port option (options.ini car_shadows=0): the car stencil shadow volumes (422550 /
// 422740) are not drawn. Console bisect of the GPU cost of the car select screens.
inline bool g_pc_car_shadows=true;
// Port optimisation: a car draw list without the darkening leaf 422740 (the select
// screens' 46A560 calls 422550 twice and never 422740, checked in the EXE: the only
// 422740 calls are 4698D9 and 46A1AA) only fills a stencil nothing reads, so its
// volumes are skipped. Nothing on screen changes; on Switch those volumes cost about
// 45 ms of GPU per frame in the OutRun2SP car select.
inline bool g_pc_skip_unread_shadow_volumes=true;
// Port measurement: the draw group being recorded (0 = none), used by the Switch
// device to time each group's draws for the performance overlay. 1 tire marks,
// particles by draw function: 2 418CF0, 3 419DC0, 4 41A6F0, 5 41AE40, 6 others.
inline unsigned g_pc_gpu_zone=0;
}
