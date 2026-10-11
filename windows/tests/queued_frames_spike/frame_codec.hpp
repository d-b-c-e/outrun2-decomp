#pragma once
// Fixture side of the codec: the production codec is windows/host/recorder/frame_channels.hpp (Astra 16553); this file
// keeps the fixture includes and the replay comparators. Historical session_spike remains unchanged.
#include "session_writer.h"
#define main upstream_adapter_tests_main
#include "test_outrun_adapter.cpp"
#undef main
#include <filesystem>
#include <fstream>
#include <limits>
#include <tuple>
namespace fs=std::filesystem;

#include "frame_channels.hpp"
using namespace dbce::shim::recorder;

void same_sink(const NativeInputState& a,const NativeInputState& b){
    auto fields=[](const NativeInputState& v){return std::tie(v.frontend.axes,v.frontend.feature_mask,v.frontend.device_held,
        v.pc_device.buttons_04,v.pc_device.axes_94,v.pc_pad.axes,v.pc_pad.pov,v.pc_pad.buttons,
        v.pc_input_layer,v.pc_record_used,v.connected,v.steering,v.accelerator,v.brake,v.shift_up,v.shift_down,
        v.menu_left,v.menu_right,v.menu_up,v.menu_down,v.menu_confirm,v.menu_cancel,v.menu_preview,v.menu_start_probe,
        v.start,v.exit_requested,v.raw_buttons_held,v.raw_buttons_down,v.raw_buttons_up,
        v.left_stick_x,v.left_stick_y,v.right_stick_x,v.right_stick_y);};
    check(fields(a)==fields(b),"action replay matches every NativeInputState field");
}
void same_signals(const Signals& a,const Signals& b){
    auto fields=[](const Signals& v){return std::tie(v.frame_seq,v.mode,v.in_gameplay,v.speed,v.gear,
        v.consumed_steer,v.consumed_accel,v.consumed_brake,v.record_steer,v.record_accel,v.record_brake,
        v.record_buttons,v.native_suppressed,v.gearbox);};
    check(fields(a)==fields(b),"action replay matches every adapter signal");
}
