#pragma once
#include "platform/race_input.hpp"
#include "platform/pc_input_devices.hpp"
#include <cstdint>
#include "platform/title_owner.hpp"

namespace outrun::platform {

// Platform-neutral controller sample consumed at the recovered PC ReadIO
// boundary (0x453BB0).  Pedals retain the game's 0..255 domain and steering
// retains the signed byte domain later shifted into event+0x202.
struct NativeInputState {
    FrontendInputSnapshot frontend{};
    // Platform device record 0x8999C0 for the game-side 453BB0 update (a
    // Switch pad presented as the PC joystick of configuration 5A7B50[0]).
    PcInputDevice pc_device{};
    // The pad as the PC DirectInput joystick (pc_input_devices.hpp) and whether
    // the race reads it through the PC layer (406FA0) instead of pc_device.
    PcDirectInputPad pc_pad{};
    bool pc_input_layer{},pc_record_used{};
    // Desktop: pc_device carries the wheel (the PC layer still runs for the keyboard). The race reads the PC layer's
    // record instead only in a frame where the wheel yields (it is idle and a key is held).
    bool pc_overlay{},pc_overlay_yield{};
    bool connected{};
    std::int32_t steering{};       // -128..127
    std::uint32_t accelerator{};   // 0..255
    std::uint32_t brake{};         // 0..255
    bool shift_up{};
    bool shift_down{};
    bool menu_left{};
    bool menu_right{};
    bool menu_up{};
    bool menu_down{};
    bool menu_confirm{};
    bool menu_cancel{};
    bool menu_preview{}; // explicit diagnostic GAME shortcut
    bool menu_start_probe{}; // explicit START probe, not the original menu path
    bool start{};
    bool exit_requested{};
    std::uint64_t raw_buttons_held{};
    std::uint64_t raw_buttons_down{};
    std::uint64_t raw_buttons_up{};
    std::int32_t left_stick_x{};
    std::int32_t left_stick_y{};
    std::int32_t right_stick_x{};
    std::int32_t right_stick_y{};
};

using NativeInputSample = void(*)(void* user,NativeInputState& state);

struct NativeRuntimeInput {
    void* user{};
    NativeInputSample sample{};
    std::uint32_t sample_calls{};
};

inline bool runtime_input_ready(const NativeRuntimeInput& input){
    return input.sample!=nullptr;
}

// After the game consumed this tick's input: called at 453BB0 once the PC layer's arbitration (406FA0) chose the
// record and race_input_update_453bb0 read it, with that consumed input (pc_device the record used) and pc_drive, the
// PC-layer argument the race update took (pc_input_layer && pc_record_used && the record's 1D0 word is 0: it depends
// on the PC layer's own record, not only on the input). Null by default; the desktop's recorder sets it
// (windows/source/shim_input.cpp). runtime_input_sample has this one caller, so each sample is followed by exactly one
// call.
struct NativeRuntimeContext;
using InputConsumedHook=void(*)(const NativeInputState& consumed,const NativeRuntimeContext& context,bool pc_drive);
InputConsumedHook& native_input_consumed_hook();
// Consumed playback (an admitted latched replay only; windows/source/consumed_replay.hpp): called at 453BB0 after the
// arbitration and before race_input_update_453bb0, it may replace this tick's input and the race update's PC-layer
// argument with a recorded tick's. Null by default (the ordinary and live paths never replay).
using InputReplayHook=void(*)(NativeInputState& in,bool& pc_drive,const NativeRuntimeContext& context);
InputReplayHook& native_input_replay_hook();

inline void runtime_input_sample(NativeRuntimeInput& input,NativeInputState& state){
    if(!runtime_input_ready(input)){state=NativeInputState{};return;}
    NativeInputState next{};
    input.sample(input.user,next);
    state=next;
    ++input.sample_calls;
}

} // namespace outrun::platform
