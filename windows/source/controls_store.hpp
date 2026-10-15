#pragma once
// The desktop build's canonical control bindings (wheel toolkit STD-033): the
// [Controls] section of OutRunDesktop.ini in the toolkit's ini-controls-1 form
// (vendor/controls/dbce_controls.hpp), which Wheelkit and the F6 panel edit.
// It is the game's only binding store. Pure (no Windows headers): which
// actions this build maps into the game, the start line Wheelkit reads back,
// and the one-time import of OutRun2006Tweaks [DirectInput] keys.
#include "dbce_controls.hpp"
#include <array>
#include <string>
#include <vector>
namespace outrun::desktop {
namespace ctl=dbce::controls;

// The highest controls schema this build reads (STD-037): 2, the `cal=` measured endpoints. Every reader of [Controls]
// uses it together (the input readers wheel_input / shim_input with Core, and the force target ffb_settings), so no
// reader applies a section another refuses. The capture tools that re-read a tape's profile do the same.
inline constexpr int ControlsReads=2;

// nullptr when this build maps the action into the game, else why it does not.
const char* unavailable_reason(const std::string& action);

struct ControlsReport {
    bool ok{};                               // Schema 1: the supported bindings apply
    std::vector<std::string> applied,unsupported,invalid;
    std::string start_line;                  // controls: profile "..." revision ... via ini-controls-1: applied n (...); ...
    std::vector<std::string> notes;          // "controls: <id> unavailable: <why>" / "controls: <id> invalid: <reason>"
};
ControlsReport report_controls(const ctl::Section&);

// A device as the import resolved it: instance and product GUIDs ("{...}", lower case), display name.
struct DeviceIdentity { std::string dev,prod,name; bool present()const{return !dev.empty();} };
// The OutRun2006Tweaks keys the import reads. Buttons in Tweaks order (Button<Name>): A B X Y Start Back GearUp
// GearDown ChangeView SelUp SelDown SelLeft SelRight; 0..127, 128 + hat*4 + direction for a POV, -1 unbound.
struct LegacyControls {
    int steer_axis{0},accel_axis{1},brake_axis{2};
    bool steer_invert{},accel_invert{},brake_invert{};
    // Tweaks [DirectInput.Calibration], ignored unless SteeringEnabled is true.
    bool steer_calibration{};
    std::array<std::string,3> steer_points{"0","32767.5","65535"};
    std::array<int,13> buttons{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    std::array<int,13> aux_buttons{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    bool hpattern{};
    int shifter_up{-1},shifter_down{-1};
    std::array<int,7> gears{-1,-1,-1,-1,-1,-1,-1};   // ButtonGear1..6, ButtonGearReverse
};
struct LegacyDevices { DeviceIdentity wheel,throttle,brake,shifter,aux; };
// The [Controls] body (Schema, Profile, Revision, Transmission, one line per action, canonical text) and, in
// `notes`, what could not be imported and why.
std::vector<std::string> import_legacy(const LegacyControls&,const LegacyDevices&,std::vector<std::string>& notes);
}
