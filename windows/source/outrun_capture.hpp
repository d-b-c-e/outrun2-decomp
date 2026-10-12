#pragma once
// The OutRun side of a capture, moved from tests/queued_frames_spike/native.cpp (Astra 16553): one recorded tick is
// the Core frame plus the adapter's signals and the GameView it read (the NativeInputState it wrote is kept for
// replay comparisons, not recorded). Channels are the frame codec's plus "adapter.*" and "gameInput.*"; decoding is
// strict (finite, in range) and throws.
// Contract @2 (an ordinary recording, Play with Recording; romi 33134/33165) adds, per tick, the input the game
// consumed after the PC layer's arbitration at 453BB0 ("final.*": the NativeInputState there, with the record
// pc_device the race read) and the raw keyboard behind it ("keyboard.*", provenance only). The latched R1/R3 captures
// stay @1, byte for byte.
#include "capture.hpp"
#include "outrun_adapter.hpp"
#include "platform/runtime_input.hpp"
#include <vector>
namespace outrun::desktop {
namespace rec=dbce::shim::recorder;
constexpr const char* CaptureContract="dbce.shim.outrun-capture@1\n";   // bundle.meta of a desktop capture
constexpr const char* CaptureContract2="dbce.shim.outrun-capture@2\n";  // bundle.meta of an ordinary recording
// The raw keyboard behind a tick of an ordinary recording: the six menu keys, any key, and whether the keyboard drives
// the race (the adapter's keyboard_drives_). Provenance only: what the game consumed is final.*.
struct KeyboardProvenance { bool enter{},esc{},up{},down{},left{},right{},any{},drives{}; };

// tape_row: in an R3 replay run (outrun_replay.hpp) the tape row the adapter played, or -1; NotReplay elsewhere (no channel).
constexpr std::int64_t NotReplay=-2;
// The player car's pose and identity (@2 rows only; sana 35817): the car record 799D18 (a 0x10F0 vehicle object, the
// 7815A0 car works' layout: race_player_car.cpp 4A4D20 position +14, 487740 rotation +70, 40A270 translation +14),
// read in the race modes only; zero and not valid elsewhere, as gear and speed. vec20 is the raw +20..28 vector (the
// direction 4A4D20 reads, the source of the work velocity at 49FAD0): compared as raw values, not as a heading.
struct CarPose {
    bool valid{};
    std::array<float,3> position{},vec20{};
    std::array<float,9> rotation{};          // the 3x3 of the +70 matrix, row-major
    std::uint32_t model{},stage_key{},course_gate{};   // +11 (byte), +68 (the key stage_property reads), +5C
};
// consumed / keyboard: an ordinary recording's @2 fields, written when has_consumed (every row of an @2 capture).
struct Snapshot {
    dbce::shim::Frame frame; Signals signals; platform::NativeInputState sink; GameView view; std::int64_t tape_row=NotReplay;
    bool has_consumed{}; platform::NativeInputState consumed{}; KeyboardProvenance keyboard{};
    bool pc_drive{};   // the race update's PC-layer argument at the seam (runtime_input.hpp InputConsumedHook)
    CarPose pose{};
};

inline rec::Channels signal_channels(const Signals& s){
    rec::Channels c;rec::u64(c,"adapter.frame",s.frame_seq);
    c["adapter.mode"]=s.mode;c["adapter.inGame"]=s.in_gameplay;c["adapter.speed"]=s.speed;c["adapter.gear"]=s.gear;
    c["adapter.consumedSteer"]=s.consumed_steer;c["adapter.consumedAccel"]=s.consumed_accel;c["adapter.consumedBrake"]=s.consumed_brake;
    c["adapter.recordSteer"]=s.record_steer;c["adapter.recordAccel"]=s.record_accel;c["adapter.recordBrake"]=s.record_brake;
    c["adapter.recordButtons"]=s.record_buttons;c["adapter.nativeSuppressed"]=s.native_suppressed;c["adapter.gearbox"]=int(s.gearbox);
    return c;
}
// @2: what the game consumed this tick and the keyboard behind it.
inline rec::Channels consumed_channels(const Snapshot& s){
    rec::Channels c;const auto& f=s.consumed;
    c["final.steering"]=f.steering;c["final.accelerator"]=f.accelerator;c["final.brake"]=f.brake;
    c["final.shiftUp"]=f.shift_up;c["final.shiftDown"]=f.shift_down;
    c["final.menuLeft"]=f.menu_left;c["final.menuRight"]=f.menu_right;c["final.menuUp"]=f.menu_up;c["final.menuDown"]=f.menu_down;
    c["final.menuConfirm"]=f.menu_confirm;c["final.menuCancel"]=f.menu_cancel;c["final.start"]=f.start;
    c["final.featureMask"]=f.frontend.feature_mask;c["final.deviceHeld"]=f.frontend.device_held;
    for(std::size_t i=0;i<f.frontend.axes.size();++i)c["final.frontAxis"+std::to_string(i)]=f.frontend.axes[i];
    c["final.connected"]=f.connected;
    c["final.pcInputLayer"]=f.pc_input_layer;c["final.pcRecordUsed"]=f.pc_record_used;
    c["final.pcOverlay"]=f.pc_overlay;c["final.pcOverlayYield"]=f.pc_overlay_yield;
    c["final.pcButtons"]=f.pc_device.buttons_04;
    c["final.pcSteer"]=f.pc_device.axes_94[0x0e];c["final.pcAccel"]=f.pc_device.axes_94[0x0d];c["final.pcBrake"]=f.pc_device.axes_94[0x0c];
    // Additive (sana 35159): what consumed playback needs besides the 35 above. pcDrive is the race update's PC-layer
    // argument (it depends on the PC layer's own record); the four axes are the ones joystick configurations 2 and 3 read
    // (race_input.cpp 5A7B50: 0x17/0x18 and 0x01/0x03).
    c["final.pcDrive"]=s.pc_drive;
    c["final.pcAxis01"]=f.pc_device.axes_94[0x01];c["final.pcAxis03"]=f.pc_device.axes_94[0x03];
    c["final.pcAxis17"]=f.pc_device.axes_94[0x17];c["final.pcAxis18"]=f.pc_device.axes_94[0x18];
    // The pose observer (additive, @2 only): what the game made of the input, beside gear and speed.
    const auto& p=s.pose;
    c["gameInput.poseValid"]=p.valid;
    c["gameInput.posX"]=p.position[0];c["gameInput.posY"]=p.position[1];c["gameInput.posZ"]=p.position[2];
    c["gameInput.vec20X"]=p.vec20[0];c["gameInput.vec20Y"]=p.vec20[1];c["gameInput.vec20Z"]=p.vec20[2];
    for(std::size_t i=0;i<9;++i)c["gameInput.rot"+std::to_string(i/3)+std::to_string(i%3)]=p.rotation[i];
    c["gameInput.carModel"]=p.model;c["gameInput.stageKey"]=p.stage_key;c["gameInput.courseGate"]=p.course_gate;
    const auto& k=s.keyboard;
    c["keyboard.enter"]=k.enter;c["keyboard.esc"]=k.esc;c["keyboard.up"]=k.up;c["keyboard.down"]=k.down;
    c["keyboard.left"]=k.left;c["keyboard.right"]=k.right;c["keyboard.any"]=k.any;c["keyboard.drives"]=k.drives;
    return c;
}
// OutRun's own channels of a tick (the frame recorder appends them to the Core frame's).
inline rec::Channels game_channels(const Snapshot& s){
    auto c=signal_channels(s.signals);
    c["gameInput.mode"]=s.view.mode;c["gameInput.car"]=s.view.car;c["gameInput.gear"]=s.view.gear;c["gameInput.speed"]=s.view.speed;
    if(s.tape_row!=NotReplay)c["replay.tapeRow"]=double(s.tape_row);
    c["gameInput.consumedSteer"]=s.view.consumed_steer;c["gameInput.consumedAccel"]=s.view.consumed_accel;c["gameInput.consumedBrake"]=s.view.consumed_brake;
    if(s.has_consumed){const auto f=consumed_channels(s);c.insert(f.begin(),f.end());}
    return c;
}
inline rec::Channels channels(const Snapshot& s,std::size_t slots=0){   // slots: rec::encode's fixed sample slots
    auto c=rec::encode(s.frame,slots);const auto g=game_channels(s);c.insert(g.begin(),g.end());
    return c;
}
inline rec::Channels frame_channels(const rec::Channels& all){
    rec::Channels c;
    // Every OutRun-side prefix of @1 and the versioned @2 additions (romi 33147): only the Core frame codec's remain.
    for(const auto& v:all)if(v.first.rfind("adapter.",0)!=0&&v.first.rfind("gameInput.",0)!=0&&v.first.rfind("replay.",0)!=0&&
                             v.first.rfind("final.",0)!=0&&v.first.rfind("keyboard.",0)!=0)c.insert(v);
    return c;
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
// @2: the consumed input and the keyboard back from a row (strict, as the other decoders).
struct Consumed { platform::NativeInputState input; KeyboardProvenance keyboard; };
inline Consumed decoded_consumed(const rec::Channels& c){
    Consumed r;auto& f=r.input;
    const auto b=[&](const char* k){return rec::number(c,k,0,1)!=0;};
    f.steering=std::int32_t(rec::number(c,"final.steering",-128,127));
    f.accelerator=std::uint32_t(rec::number(c,"final.accelerator",0,255));f.brake=std::uint32_t(rec::number(c,"final.brake",0,255));
    f.shift_up=b("final.shiftUp");f.shift_down=b("final.shiftDown");
    f.menu_left=b("final.menuLeft");f.menu_right=b("final.menuRight");f.menu_up=b("final.menuUp");f.menu_down=b("final.menuDown");
    f.menu_confirm=b("final.menuConfirm");f.menu_cancel=b("final.menuCancel");f.start=b("final.start");
    f.frontend.feature_mask=std::uint32_t(rec::number(c,"final.featureMask",0,UINT32_MAX));
    f.frontend.device_held=std::uint32_t(rec::number(c,"final.deviceHeld",0,UINT32_MAX));
    for(std::size_t i=0;i<f.frontend.axes.size();++i)f.frontend.axes[i]=std::int16_t(rec::number(c,("final.frontAxis"+std::to_string(i)).c_str(),INT16_MIN,INT16_MAX));
    f.connected=b("final.connected");
    f.pc_input_layer=b("final.pcInputLayer");f.pc_record_used=b("final.pcRecordUsed");
    f.pc_overlay=b("final.pcOverlay");f.pc_overlay_yield=b("final.pcOverlayYield");
    f.pc_device.buttons_04=std::uint32_t(rec::number(c,"final.pcButtons",0,UINT32_MAX));
    f.pc_device.axes_94[0x0e]=std::int16_t(rec::number(c,"final.pcSteer",INT16_MIN,INT16_MAX));
    f.pc_device.axes_94[0x0d]=std::int16_t(rec::number(c,"final.pcAccel",INT16_MIN,INT16_MAX));
    f.pc_device.axes_94[0x0c]=std::int16_t(rec::number(c,"final.pcBrake",INT16_MIN,INT16_MAX));
    auto& k=r.keyboard;
    k.enter=b("keyboard.enter");k.esc=b("keyboard.esc");k.up=b("keyboard.up");k.down=b("keyboard.down");
    k.left=b("keyboard.left");k.right=b("keyboard.right");k.any=b("keyboard.any");k.drives=b("keyboard.drives");
    return r;
}
// Consumed playback (consumed_replay.hpp): a row's consumed input as the replay seam puts it back, with the race
// update's PC-layer argument. Strict, and stricter than decoded_consumed: the additive channels must be there (a tape
// recorded before the replay seam has no final.pcDrive and cannot be played), pcDrive exactly 0 or 1.
struct Replayed { platform::NativeInputState input; bool pc_drive{}; };
inline Replayed decoded_replay(const rec::Channels& c){
    Replayed r;r.input=decoded_consumed(c).input;
    if(!c.count("final.pcDrive"))throw std::runtime_error("the row has no final.pcDrive (recorded before the replay seam)");
    const double d=c.at("final.pcDrive");
    if(d!=0.0&&d!=1.0)throw std::runtime_error("final.pcDrive is not 0 or 1");
    r.pc_drive=d==1.0;
    auto& a=r.input.pc_device.axes_94;
    a[0x01]=std::int16_t(rec::number(c,"final.pcAxis01",INT16_MIN,INT16_MAX));a[0x03]=std::int16_t(rec::number(c,"final.pcAxis03",INT16_MIN,INT16_MAX));
    a[0x17]=std::int16_t(rec::number(c,"final.pcAxis17",INT16_MIN,INT16_MAX));a[0x18]=std::int16_t(rec::number(c,"final.pcAxis18",INT16_MIN,INT16_MAX));
    return r;
}
// The adapter signal fields that differ between two ticks (replay comparisons; empty when every field is equal).
inline std::vector<const char*> signal_differences(const Signals& a,const Signals& b){
    std::vector<const char*> d;
    if(a.frame_seq!=b.frame_seq)d.push_back("frame");
    if(a.mode!=b.mode)d.push_back("mode");
    if(a.in_gameplay!=b.in_gameplay)d.push_back("inGame");
    if(a.speed!=b.speed)d.push_back("speed");
    if(a.gear!=b.gear)d.push_back("gear");
    if(a.consumed_steer!=b.consumed_steer)d.push_back("consumedSteer");
    if(a.consumed_accel!=b.consumed_accel)d.push_back("consumedAccel");
    if(a.consumed_brake!=b.consumed_brake)d.push_back("consumedBrake");
    if(a.record_steer!=b.record_steer)d.push_back("recordSteer");
    if(a.record_accel!=b.record_accel)d.push_back("recordAccel");
    if(a.record_brake!=b.record_brake)d.push_back("recordBrake");
    if(a.record_buttons!=b.record_buttons)d.push_back("recordButtons");
    if(a.native_suppressed!=b.native_suppressed)d.push_back("nativeSuppressed");
    if(a.gearbox!=b.gearbox)d.push_back("gearbox");
    return d;
}
inline rec::Packet packet(const Snapshot& s,const std::vector<std::string>& names,std::size_t slots=0){
    return rec::packet_of(channels(s,slots),names,s.frame.seq,s.frame.tick_us);
}
}
