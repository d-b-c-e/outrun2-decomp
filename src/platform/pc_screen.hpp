#pragma once
// Port platform option: the half width (in 480-line PC pixels) of the view the
// game draws. 320 is the PC's 4:3 screen (6281C8 / 5DA2D4); the widescreen
// option (Y) sets 240*16/9 so the screen-bound visibility tests (4F02D0) keep
// objects until they leave the wider view.
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
// [Bugfixes] FixIncorrectShading (the Tweaks key, on by default there and here; hooks_bugfixes.cpp): materials with
// double-sided lighting (cutscene characters, some stage models) link the TWOSIDE lighting fragment, which only flips
// the light direction. Tweaks points the TWOSIDE_LAMBERT / TWOSIDE_SPECULAR entries of 40E140's lighting table
// (73F04C, 73F050) at the DOUBLE_LAMBERT / DOUBLE_SPECULAR fragments (61FB48, 61FC88); pc_vertex_shader_setup.cpp
// links those in their place. Only one side is then shaded correctly, as in Tweaks.
inline bool g_pc_fix_incorrect_shading=true;
// [Bugfixes] FixLensFlarePath (the Tweaks key, on by default there and here): 44A630 reads \common\lens_flare_offset.bin,
// but the game ships it in Media\, so the sun offsets stay zero. With the fix the Media\ copy is read when it exists
// (as Tweaks, which repoints the path only then), else the common\ one (race_course_passes.cpp).
inline bool g_pc_fix_lens_flare_path=true;
// [Bugfixes] FixPegasusClopping (the Tweaks key, on by default there and here; hooks_bugfixes.cpp): the Pegasus event
// 44BB30 starts the hoof clop as a loop (4249F0(SND_LOOP | 0x8D) = 0x88D), but its off callback SndOff_PEGA 44BCC0 stops
// only 0x8C (0x808C), so the clopping never ends. As Tweaks does after SndOff_PEGA, 44BCC0 then also requests
// SND_STOP | 0x8D (0x808D). Off: the PC's own single stop.
inline bool g_pc_fix_pegasus_clopping=true;
// 44BCC0's 4249F0 sound requests in order; returns how many (the race code issues exactly these).
inline int pc_pegasus_sound_off_44bcc0(bool fix,unsigned out[2]){
    out[0]=0x808cu;if(!fix)return 1;
    out[1]=0x808du;return 2;
}
// [Bugfixes] FixRightSideBunkiAnimations (the Tweaks key, on by default there and here): 44F6DC sets a culling flag
// on the mirrored right-side branch model. Tweaks skips that seven-byte store; race_area.cpp does the same when on.
// Off retains the PC store, and the left-side path is unchanged.
inline bool g_pc_fix_right_side_bunki_animations=true;
// [Bugfixes] HideOnlineSigninText (the Tweaks key, off by default there since its b3c1825 and here; hooks_bugfixes.cpp;
// Tweaks' code default is still true, its shipped INI sets false): the remnants of the online service on the menus.
// - 443256 (JNE to JMP): 443110 never opens the 4400D3 sign-in status box at +C48 (pc_signin_box_call_skipped,
//   native_runtime.cpp). The status text 441370 then returns at its first test (+C50 stays -1: 44327C is the only
//   open of +C48), which is what Tweaks' RET at 441370 does.
// - 446510 (a help-bar slot caption; frontend_slot_captions_446a50) and 446A80 / 446B20 (its icon; the r081 slot open
//   through skip_caption) do nothing for a slot whose caption is 664 (pc_hidden_signin_caption).
inline bool g_pc_hide_online_signin_text=false;
// 443110's calls that the 443256 jump skips: the 4400D3 box's 465860 open at 44327C and its 465970 commit at 443286.
inline bool pc_signin_box_call_skipped(bool hide,std::uint32_t site){return hide&&(site==0x44327cu||site==0x443286u);}
// The help-bar caption Tweaks' 446510 / 446A80 / 446B20 detours leave out: 664 (0x298, "SIGN-IN", the caption 446CF0
// keeps while not signed in; 0x297 "FRIENDS" is not hidden), or 0xFFFFFFFF (none) without the fix.
inline std::uint32_t pc_hidden_signin_caption(bool hide){return hide?0x298u:0xffffffffu;}
// [Misc] ShowOutRunMilesOnMenu (the Tweaks key, on by default there and here; hooks_misc.cpp): 441370's not signed in
// status (4413E2: 42CDD0 with string 0x474, "Not Signed In") prints Tweaks' "%s - %s %d" instead: that string, string
// 0x21D ("OutRun Miles:") and the active license's Miles (7C2404, +24 of 7C23E0; the float the showroom reads)
// truncated to an int (native_runtime.cpp native_state2_ui_services). The signed in branch (4413CE) is unchanged.
inline bool g_pc_show_outrun_miles_on_menu=true;
// True for the 42CDD0 call Tweaks replaces: the not signed in branch (7D68BC clear) drawing string 0x474.
inline bool pc_show_outrun_miles_4413e2(bool show,bool alternate,std::uint32_t text){return show&&!alternate&&text==0x474u;}
// Tweaks' "%s - %s %d" of the status, the label and (int) of the Miles (cvttss2si: the integer indefinite when the
// float is out of range or NaN).
inline std::string pc_outrun_miles_status(const std::string& status,const std::string& label,float miles){
    const std::int32_t n=(miles>-2147483904.0f&&miles<2147483648.0f)?std::int32_t(miles):std::int32_t(0x80000000u);
    return status+" - "+label+" "+std::to_string(n);
}
// [Graphics] UILetterboxing (the Tweaks key, 1 by default there and here; Tweaks' overlay/hooks_overlay.cpp D3DEndScene):
// black bars left and right of the centred 4:3 over the finished frame, under the desktop's overlay (the REC / PLAYBACK
// badge and the F6 panel stay on top; windows/source/desktop_panel.cpp). 0 none, 1 outside the race only, 2 always;
// only with UIScalingMode 1 or 2, as Tweaks, and only while the device keeps the 2D in that 4:3 (widescreen or a span).
inline int g_pc_ui_letterboxing=1;
// Tweaks' is_in_game: 78026C in GAME 0x10, SMPAUSEMENU 0x12, GOAL 0x13, TIMEUP 0x14, OUTRUNMILES 0x22 or TRYAGAIN 0x23,
// or START 0xD once its stage 8367A8 (NativeStartModeState::stage, native_runtime.cpp: 65 just before GAME) is 65.
inline bool pc_letterbox_in_race(std::uint32_t mode_78026c,std::uint32_t start_stage_8367a8){
    const std::uint32_t m=mode_78026c;
    return m==0x10u||m==0x12u||m==0x13u||m==0x14u||m==0x22u||m==0x23u||(m==0x0du&&start_stage_8367a8==65u);
}
// Tweaks' rule: the setting above 0 and UIScalingMode above 0, then always (2) or outside the race (1).
inline bool pc_letterbox_now(int setting,unsigned ui_scaling_mode,bool in_race){
    return setting>0&&ui_scaling_mode>0u&&(setting>=2||!in_race);
}
// The bars in back buffer pixels: [0, left_end) and [right_begin, width). They end at the device's own 4:3 edges
// (xbox/source/d3d11_d3d9.cpp apply_state: ox = floor((W - H * 4/3) / 2), the scissor's right edge lround(ox +
// H * 4/3) in float), so no bar covers a pixel of the 2D. Tweaks' half-pixel quads overlap one content pixel instead.
// No bars when the frame is not wider than 4:3.
struct PcLetterboxBars {
    std::int32_t left_end{},right_begin{},width{};
    bool any()const{return left_end>0||right_begin<width;}
};
inline PcLetterboxBars pc_letterbox_bars(std::int32_t w,std::int32_t h){
    if(w<=0||h<=0)return {};
    const double content=double(h)*4./3.;
    if(double(w)<=content)return {0,w,w};
    const double ox=std::floor((double(w)-content)/2.);
    const long right=std::lround(float(ox)+float(content));
    return {std::int32_t(ox),right<long(w)?std::int32_t(right):w,w};
}
// [Misc] DefaultManualTransmission (the Tweaks key, off by default there and here; hooks_misc.cpp): the transmission
// menu starts on Manual. Tweaks keeps the key in [Controls]; here that section is the ini-controls-1 contract, where
// any other key is an invalid binding (dbce_controls.hpp parseSection), so it lives in [Misc].
inline bool g_pc_default_manual_transmission=false;
// The transmission owner (key 16, 0x1F4 bytes; frontend_transmission.cpp): its carousel at +34 keeps the previous index
// at +E4 (carousel +B0) and the current at +E8 (+B4); 4DD250's carousel init zeroes both (Automatic).
inline constexpr std::size_t PcTransmissionPrevious=0xe4u,PcTransmissionCurrent=0xe8u;
// After 4DD250, as Tweaks' detour: +E4 = 0 (Automatic) and +E8 = 1 (Manual). Off, or a short object: nothing.
inline void pc_default_manual_transmission_4dd250(bool on,std::uint8_t* object,std::size_t size){
    if(!on||!object||size<PcTransmissionCurrent+4u)return;
    const std::uint32_t previous=0u,current=1u;
    std::memcpy(object+PcTransmissionPrevious,&previous,4);std::memcpy(object+PcTransmissionCurrent,&current,4);
}
// 4DD4A0's confirmation (action 0): the byte at +E8 into 830374 (48B170), 0 Automatic, 1 Manual.
inline std::uint8_t pc_transmission_confirmed_4dd4a0(const std::uint8_t* object){return object[PcTransmissionCurrent];}
// [Misc] HudToggleKey (the Tweaks key, unset by default there and here; hooks_input.cpp HudToggleUpdate): each release
// of the key flips NaviPub_Disp's display flag 8447F8 (Tweaks' 4447F8 above the image base), which the game sets to 1
// at the race HUD's init (4BC9E0) and to 0 at the race end (4B8E20); NaviPub_Disp (4BEB00) draws nothing while it is 0.
// The desktop posts each toggle here (windows/source/hud_toggle.hpp). The race HUD applies them on the game thread just
// before NaviPub_Disp and drops any posted before its init, so a press outside a race changes nothing (in Tweaks the
// next init sets the flag again). Nothing posts in a recording, playback or test process: the flag stays the game's.
inline std::atomic<std::uint32_t> g_pc_hud_toggle_requests{0};
inline constexpr std::uint32_t PcNaviDisplayFlag=0x8447f8u;
// Tweaks' flip, once per request: 0 becomes 1, any other value 0.
inline std::uint32_t pc_hud_toggle_apply(std::uint32_t flag,std::uint32_t requests){
    if(requests==0u)return flag;
    flag=flag==0u?1u:0u;
    return (requests-1u)%2u?(flag==0u?1u:0u):flag;
}
// [Graphics] UseHiDefCharacters (the Tweaks key, on by default there and here; hooks_graphics.cpp): Alberto, Jennifer
// and Clarissa in their hi-def character sets during gameplay (race_robots.hpp race_robot_hidef_driver / _passenger),
// and Clarissa's CHR_DR_GH00(_USA).bin read from Media\ when it exists (the Chr\ copies lack the hair animations;
// rob_disp.cpp 488B80).
inline bool g_pc_use_hidef_characters=true;
// [Misc] RestoreJPClarissa (the Tweaks key, off by default there and here; hooks_misc.cpp): Clarissa as the Japanese
// release has her, not the USA model the PC uses, in O2SP arcade mode and in the arcade ending. Gameplay: 487F50's
// non-console constants G00_USA 6 (488044) and GH00_USA 8 (48804E) become G00 5 (GH00 7 with hi-def characters) and
// GH00 7 (race_robots.hpp race_robot_hidef_passenger). Ending: race_ending.hpp ending_jp_motion_set /
// ending_heroine_resource and race_autoscene.cpp 4B5A80 (the 4B5AE6 force of 0xE to 0xF skipped).
inline bool g_pc_restore_jp_clarissa=false;
// [Misc] DisableCountdownTimer (the Tweaks key, off by default there and here; Tweaks clears the PC's
// Sumo_CountdownTimerEnable, .data 637911): the race clock does not count down. The byte's only two readers (450B03 /
// 450B4B) are race_manager.cpp's time_decrement_637911 tests; race_manager_runtime.cpp sets it from this.
inline bool g_pc_disable_countdown_timer=false;
// [Misc] AllowCharacterSelection (the Tweaks key, off by default there and here; hooks_misc.cpp, its hook on the license
// screen's 4DDC50): the license editor's menu (frontend_license_editor.cpp, 4DDC50's menu branch) skips its character
// item 1 on the PC, Down from the name and Up from the nationality going past it. On, those two moves select the
// character item (this +3C = 1, 4DD990(5)), whose own handling the PC kept: confirm opens the character grid.
inline bool g_pc_allow_character_selection=false;
// [Misc] EnableHollyCourse2 (the Tweaks key, hooks_misc.cpp; Tweaks' plugin default 1, its shipped INI 3, here 3 as the
// shipped INI): Holly's second mission set ("MIX 2 COURSE", from the PSP release) as the racer menu's group 7.
//   0 off (the PC: seven groups);
//   1 on, unlocked with Holly 1 (group 7 opens as group 6 does);
//   2 on, unlocked once Holly 1 itself reaches rank A (grade >= 4);
//   3 as 2, and the menu draws and reaches it only once it is unlocked (frontend_categories.cpp).
// Group 7 grades from the fourth course lane (frontend_profiles.hpp), requests its courses there (frontend_requests.cpp)
// and sets up as Holly with request course set 2 (Req_Course_MIX_PSP) instead of 3 (pc_common_control.cpp 4EEB50). The
// lane is optional: absent or malformed, group 7 stays locked and unrequestable in every mode. No save change: its results
// are the course types' nibbles at licence +36E like every championship's.
inline int g_pc_enable_holly_course2=3;
// [Audio] AllowHorn (the Tweaks key, on by default as there; hooks_audio.cpp AudioHooks_Update): while the contract's own
// "horn" action is held in GAME (mode 16), SE 0x1BC is queued looping every frame (0x1BC | SND_LOOP 0x800), and on release
// or on leaving GAME it is stopped (0x1BC | SND_STOP 0x8000). Tweaks reads the X switch (0x10); here the horn is its own
// action, because the desktop record's button behind that switch is shift-down (race_sound_runtime.cpp native_race_horn).
inline bool g_pc_allow_horn=true;
// [Graphics] UIScalingMode 1 (windows/docs/UI-SCALING.md step 2a): Tweaks' HUD shift in 640-wide UI units at the 4:3
// scale, 320 (1 - 4 / (3A)) for the 2D rectangle's frame of aspect A (80 at 16:9: three quarters of the margin, as
// Tweaks' AddSpriteSpacing lands; display_fit.hpp ui_scaling_rect .hud), set by the renderer each frame; 0 in modes 0
// and 2. layer_429350 moves a scoped footage layer by it (ui_spacing.hpp ui_spacing_shift), the put_scroll fixes too.
inline float g_pc_ui_spacing=0.0f;
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
