#pragma once
// Game-side input update of the PC frame loop (0x453BB0 for player 0):
// 0x453640 switch record 7D6770 (held/previous/pressed/released feature bits)
// and 0x453860 analogue channels 7D6810 (+4 current, +8 previous, pedal
// edge flags +1C/+2C) with the steering filter 7D6884 and the digital
// steering smoother 0x4537C0. The platform device record (0x8999C0: +4 button
// bits, +94 signed 16-bit axis words) is what the Switch adapter fills; the
// configuration records are the EXE tables 5A7B30 (keyboard) / 5A7B50[n]
// (joystick): steer/accel/brake axis indices, shift-up/down/view masks, flags.
#include <array>
#include <cmath>
#include <cstdint>
namespace outrun::platform {
struct PcInputDevice {                         // 0x8999C0 view
    std::uint32_t buttons_04{};
    std::array<std::int16_t,0x30> axes_94{};   // +0x94 + index*2
};
struct PcInputConfig {                         // 0x1C-byte record
    std::uint32_t steer_axis{},accel_axis{},brake_axis{};
    std::uint32_t shift_up_mask{},shift_down_mask{},view_mask{},flags{};
};
// EXE tables: keyboard 5A7B30 and the four joystick configurations 5A7B50.
PcInputConfig pc_input_keyboard_config();
PcInputConfig pc_input_joystick_config(unsigned index);
struct PcInputSwitchRecord { std::uint32_t held{},previous{},pressed{},released{}; }; // 7D6770 + player*0x10
struct PcInputAnalog {                                                               // 7D6810 + player*0x70
    std::array<std::int32_t,7> current{},previous{};   // +4/+8 of each 0x10 channel
    std::int32_t accel_edge_1c{},brake_edge_2c{};
};
// [Bugfixes] FixFullPedalChecks (the Tweaks key, on by default there and here; Tweaks hooks_bugfixes.cpp): the game's
// full-press checks (the backfire among them) compare the accelerator / brake volume with 255, and a real pedal scaled
// to 0..255 (lround(value*255) in both input backends) rarely reaches it. As Tweaks' GetVolume / GetVolumeOld hooks do,
// 453720 / 453750 report 250..254 as 255 on channels 1 (accelerator) and 2 (brake) only. The stored 7D6810 values, and
// so the pedal edges, the consumed-input capture and replays, stay as written. Off: the PC's own 255-only checks.
inline bool g_pc_fix_full_pedal_checks=true;
constexpr std::int32_t pc_input_volume_read(std::int32_t stored,unsigned channel,bool fix){
    return fix&&(channel==1u||channel==2u)&&stored>=250&&stored<255?255:stored;
}
inline std::int32_t pc_input_volume_current(const PcInputAnalog& a,unsigned channel){   // 453720
    return pc_input_volume_read(a.current[channel],channel,g_pc_fix_full_pedal_checks);
}
inline std::int32_t pc_input_volume_previous(const PcInputAnalog& a,unsigned channel){  // 453750
    return pc_input_volume_read(a.previous[channel],channel,g_pc_fix_full_pedal_checks);
}
// [Misc] FineSteering (not a Tweaks key; off by default; ROADMAP step 6): 453860 truncates the steering to -127..127
// (current[0], cvttss2si(steer*127)) and 49FAD0 writes the car's +0x202 as that << 8, so a 16-bit wheel reaches the
// physics in 255 steps. On, on the analogue path only (a joystick configuration, flags bit 0; the keyboard table's
// digital smoother 4537C0 is unchanged), +0x202 takes lround(steer * 0x7F00) of the same steering value before the
// step, clamped to full lock +-0x7F00; at k/127 that is the PC's k << 8. The readers of +0x202 (pc_steering.cpp,
// pc_common_control.cpp's counter-steer and steering history) are continuous in it. current[0], and so the consumed
// capture, telemetry and the other volume(0) readers, stay as written. race_player_car.cpp applies it in GAME where
// 49FAD0 wrote the word. A latched (test) process keeps it off; a recording's startup INI names it for its playback.
inline bool g_pc_fine_steering=false;
constexpr std::int32_t PcFineSteerFullLock=0x7f00;
inline std::int16_t pc_input_fine_steer_202(float steer){
    if(std::isnan(steer))return 0;                                               // neutral
    const long v=std::lround(double(steer)*double(PcFineSteerFullLock));
    return std::int16_t(v<-PcFineSteerFullLock?-PcFineSteerFullLock:v>PcFineSteerFullLock?PcFineSteerFullLock:v);
}
void pc_input_switch_453640(PcInputSwitchRecord&,const PcInputDevice&,const PcInputConfig&);
// filter_7d6884: per-player steering filter; option_7c24cb: steering speed
// option (race table 5A7C30 row); game_mode_78026c selects 5A7C30 (16) or 5A7CF0.
// fine_steer (FineSteering): on the analogue path, the steering value before its -127..127 step; the digital path
// leaves it untouched. Null (the default) for the PC's own update.
void pc_input_analog_453860(PcInputAnalog&,float& filter_7d6884,const PcInputDevice&,const PcInputConfig&,
                            std::int8_t option_7c24cb,std::int32_t game_mode_78026c,float* fine_steer=nullptr);
std::int32_t pc_input_digital_steer_4537c0(std::int32_t target,std::int32_t previous,std::int8_t option_7c24cb);
}
