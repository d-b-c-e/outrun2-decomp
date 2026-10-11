#pragma once
// The OutRun side of a capture, moved from tests/queued_frames_spike/native.cpp (Astra 16553): one recorded tick is
// the Core frame plus the adapter's signals and the GameView it read (the NativeInputState it wrote is kept for
// replay comparisons, not recorded). Channels are the frame codec's plus "adapter.*" and "gameInput.*"; decoding is
// strict (finite, in range) and throws.
#include "capture.hpp"
#include "outrun_adapter.hpp"
#include "platform/runtime_input.hpp"
#include <vector>
namespace outrun::desktop {
namespace rec=dbce::shim::recorder;

struct Snapshot { dbce::shim::Frame frame; Signals signals; platform::NativeInputState sink; GameView view; };

inline rec::Channels signal_channels(const Signals& s){
    rec::Channels c;rec::u64(c,"adapter.frame",s.frame_seq);
    c["adapter.mode"]=s.mode;c["adapter.inGame"]=s.in_gameplay;c["adapter.speed"]=s.speed;c["adapter.gear"]=s.gear;
    c["adapter.consumedSteer"]=s.consumed_steer;c["adapter.consumedAccel"]=s.consumed_accel;c["adapter.consumedBrake"]=s.consumed_brake;
    c["adapter.recordSteer"]=s.record_steer;c["adapter.recordAccel"]=s.record_accel;c["adapter.recordBrake"]=s.record_brake;
    c["adapter.recordButtons"]=s.record_buttons;c["adapter.nativeSuppressed"]=s.native_suppressed;c["adapter.gearbox"]=int(s.gearbox);
    return c;
}
inline rec::Channels channels(const Snapshot& s,std::size_t slots=0){   // slots: rec::encode's fixed sample slots
    auto c=rec::encode(s.frame,slots);const auto a=signal_channels(s.signals);c.insert(a.begin(),a.end());
    c["gameInput.mode"]=s.view.mode;c["gameInput.car"]=s.view.car;c["gameInput.gear"]=s.view.gear;c["gameInput.speed"]=s.view.speed;
    c["gameInput.consumedSteer"]=s.view.consumed_steer;c["gameInput.consumedAccel"]=s.view.consumed_accel;c["gameInput.consumedBrake"]=s.view.consumed_brake;
    return c;
}
inline rec::Channels frame_channels(const rec::Channels& all){
    rec::Channels c;for(const auto& v:all)if(v.first.rfind("adapter.",0)!=0&&v.first.rfind("gameInput.",0)!=0)c.insert(v);return c;
}
inline GameView decoded_view(const rec::Channels& c){
    GameView v;v.mode=std::uint32_t(rec::number(c,"gameInput.mode",0,UINT32_MAX));v.car=rec::number(c,"gameInput.car",0,1)!=0;
    v.gear=std::uint32_t(rec::number(c,"gameInput.gear",0,UINT32_MAX));v.speed=float(c.at("gameInput.speed"));
    if(!std::isfinite(v.speed))throw std::runtime_error("finite input speed");
    v.consumed_steer=std::int32_t(rec::number(c,"gameInput.consumedSteer",INT32_MIN,INT32_MAX));
    v.consumed_accel=std::int32_t(rec::number(c,"gameInput.consumedAccel",INT32_MIN,INT32_MAX));
    v.consumed_brake=std::int32_t(rec::number(c,"gameInput.consumedBrake",INT32_MIN,INT32_MAX));
    return v;
}
inline Signals decoded_signals(const rec::Channels& c){
    Signals s;s.frame_seq=rec::u64(c,"adapter.frame");s.mode=std::uint32_t(rec::number(c,"adapter.mode",0,UINT32_MAX));
    s.in_gameplay=rec::number(c,"adapter.inGame",0,1)!=0;s.gear=std::uint32_t(rec::number(c,"adapter.gear",0,UINT32_MAX));
    s.speed=float(c.at("adapter.speed"));if(!std::isfinite(s.speed))throw std::runtime_error("finite adapter speed");
    s.consumed_steer=std::int32_t(rec::number(c,"adapter.consumedSteer",INT32_MIN,INT32_MAX));
    s.consumed_accel=std::int32_t(rec::number(c,"adapter.consumedAccel",INT32_MIN,INT32_MAX));
    s.consumed_brake=std::int32_t(rec::number(c,"adapter.consumedBrake",INT32_MIN,INT32_MAX));
    s.record_steer=std::int32_t(rec::number(c,"adapter.recordSteer",INT32_MIN,INT32_MAX));
    s.record_accel=std::int32_t(rec::number(c,"adapter.recordAccel",INT32_MIN,INT32_MAX));
    s.record_brake=std::int32_t(rec::number(c,"adapter.recordBrake",INT32_MIN,INT32_MAX));
    s.record_buttons=std::uint32_t(rec::number(c,"adapter.recordButtons",0,UINT32_MAX));
    s.native_suppressed=std::uint32_t(rec::number(c,"adapter.nativeSuppressed",0,UINT32_MAX));
    s.gearbox=HPattern::State(rec::number(c,"adapter.gearbox",0,int(HPattern::State::AtTarget)));
    return s;
}
inline rec::Packet packet(const Snapshot& s,const std::vector<std::string>& names,std::size_t slots=0){
    return rec::packet_of(channels(s,slots),names,s.frame.seq,s.frame.tick_us);
}
}
