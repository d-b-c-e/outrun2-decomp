#pragma once
// When the native wheel path (wheel_input.cpp) rescans its DirectInput controllers. A rescan (EnumDevices, the opens,
// the closes) runs on the input thread and stalls that frame. The old unconditional rescan every 2 s is the candidate
// for the 130..330 ms frame every ~2.15 s in perf-pair sample-2351 (PresentMon): the period fits 2 s plus a scan, but
// the scan's duration was not measured there (each rescan now logs it). It now runs when Windows reports a device change
// (WM_DEVICECHANGE) and, every 2 s, only while an applicable binding has no open, connected device (absent at the last
// rescan, refused to open, or lost since), as the shim's provider does (INPUT-SHIM.md: arrival notifications, plus every
// 2 s while a bound device is missing). A rig whose bound devices are all open and connected rescans only on device
// changes; a lost device is neutralized by its failed read the same frame.
#include <chrono>
#include <cstddef>
namespace outrun::desktop {
using WheelClock=std::chrono::steady_clock;
constexpr WheelClock::duration WheelMissingRescan=std::chrono::seconds(2);
// applicable: the profile's bindings this build can apply; bound: the applied ones (one each, opened at a rescan), read
// through opened (a DirectInput handle) and connected (its last read succeeded). No copy: the input thread calls it
// every sample.
template<class Bound,class Opened,class Connected>
bool wheel_bound_missing(std::size_t applicable,const Bound& bound,Opened opened,Connected connected){
    std::size_t applied=0;
    for(const auto& b:bound){++applied;if(!opened(b)||!connected(b))return true;}
    return applied<applicable;
}
inline bool wheel_rescan_due(bool device_change,bool bound_missing,WheelClock::time_point now,WheelClock::time_point next){
    return device_change||(bound_missing&&now>=next);
}
}
