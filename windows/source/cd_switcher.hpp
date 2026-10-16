#pragma once
#include "input/dual_sense.hpp"
#include "platform/cd_switcher.hpp"
#include "runtime/host_platform.hpp"
#include <utility>
namespace outrun::desktop {
// The Xbox/desktop map_input path converts host PadButton bits to ps5::Button
// before DualSenseInput::update. NativeInputState's raw bits therefore have the
// latter layout. Normalize that actual sample for the switcher's host-layout
// combination parser; do not alter the native input record.
struct CdPadSample { std::uint64_t held{};bool connected{}; };
inline CdPadSample cd_switcher_pad_sample(const platform::NativeInputState& input){
    using namespace outrun::ps5;
    static constexpr std::pair<std::uint32_t,std::uint64_t> mapping[]={{Cross,runtime::PadA},{Circle,runtime::PadB},
        {Square,runtime::PadX},{Triangle,runtime::PadY},{StickL,runtime::PadStickL},{StickR,runtime::PadStickR},
        {L1,runtime::PadL},{R1,runtime::PadR},{L2,runtime::PadZL},{R2,runtime::PadZR},
        {Options,runtime::PadPlus},{Share,runtime::PadMinus},{Left,runtime::PadLeft},{Up,runtime::PadUp},
        {Right,runtime::PadRight},{Down,runtime::PadDown}};
    CdPadSample sample{0,input.connected};
    if(!sample.connected)return sample;
    for(const auto& entry:mapping)if(input.raw_buttons_held&entry.first)sample.held|=entry.second;
    // Tweaks hooks_input.cpp PadUpdate uses strict XInput deadzone comparisons.
    // The existing native axes already have XInput's sign/range (positive Y up).
    // Read them before wheel/shim filters; never poll, rescale or write input.
    struct Axis { std::int32_t value,deadzone;std::uint64_t positive,negative; };
    const Axis axes[]={{input.left_stick_x,7849,platform::CdLsRight,platform::CdLsLeft},
        {input.left_stick_y,7849,platform::CdLsUp,platform::CdLsDown},
        {input.right_stick_x,8689,platform::CdRsRight,platform::CdRsLeft},
        {input.right_stick_y,8689,platform::CdRsUp,platform::CdRsDown}};
    for(const auto& axis:axes){
        if(axis.value>axis.deadzone)sample.held|=axis.positive;
        if(axis.value< -axis.deadzone)sample.held|=axis.negative;
    }
    return sample;
}
class Ini;
void cd_switcher_configure(const Ini&,bool test_or_playback,bool recording);
void cd_switcher_wrap_input_filter();
}
