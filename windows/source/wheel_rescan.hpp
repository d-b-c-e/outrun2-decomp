#pragma once
// When the native wheel path (wheel_input.cpp) rescans its DirectInput controllers. A rescan (EnumDevices, the opens,
// the closes) runs on the input thread and stalls that frame: the old unconditional rescan every 2 s put a 130..330 ms
// frame every ~2.15 s (perf-pair sample-2351, PresentMon). It now runs when Windows reports a device change
// (WM_DEVICECHANGE) and, every 2 s, only while an applicable binding has no open, connected device (absent at the last
// rescan, refused to open, or lost since), as the shim's provider does (INPUT-SHIM.md: arrival notifications, plus every
// 2 s while a bound device is missing). A rig whose bound
// devices are all open and connected never rescans; a lost device is neutralized by its failed read the same frame.
#include <chrono>
#include <cstddef>
#include <vector>
namespace outrun::desktop {
using WheelClock=std::chrono::steady_clock;
constexpr WheelClock::duration WheelMissingRescan=std::chrono::seconds(2);
// A bound action's device as the policy reads it: opened (a DirectInput handle) and connected (its last read succeeded).
struct WheelBoundDevice { bool opened{},connected{}; };
// applicable: the profile's bindings that this build can apply; bound: the applied ones (one each, opened at a rescan).
inline bool wheel_bound_missing(std::size_t applicable,const std::vector<WheelBoundDevice>& bound){
    if(bound.size()<applicable)return true;
    for(const auto& b:bound)if(!b.opened||!b.connected)return true;
    return false;
}
inline bool wheel_rescan_due(bool device_change,bool bound_missing,WheelClock::time_point now,WheelClock::time_point next){
    return device_change||(bound_missing&&now>=next);
}
}
