#include "gamepad_vibration_live.hpp"
#include "enhancements/gamepad_vibration.hpp"
#include "native_window.hpp"
#include "desktop_panel.hpp"
#include "ffb_live.hpp"
#include "platform/native_runtime.hpp"
#include "platform/race_area_runtime.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace outrun::desktop {
namespace {
namespace en=enhancements;
en::PadVibrationSettings settings;
en::PadVibrationModel model;
xbox_runtime::InputFilter inner{};
std::uint64_t generation=0;
bool blocked=false,ordinary_process=false;
bool muted(){const auto* value=std::getenv("DBCE_FFB_MUTE");return value&&*value&&std::strcmp(value,"0")!=0;}
template<class T> T field(const std::uint8_t* p,std::size_t offset){T value;std::memcpy(&value,p+offset,sizeof value);return value;}
en::PadVibrationCar car_signals(const platform::NativeRuntimeContext& rt){
    const auto& bytes=rt.event_function36.car_select.car_799d18;const auto* b=bytes.data();en::PadVibrationCar c;
    c.flags=field<std::uint32_t>(b,4);c.state=field<std::uint32_t>(b,8);c.brake=field<std::int32_t>(b,0x38);
    c.speed=field<float>(b,0x1c4);c.prior_speed=field<float>(b,0x178);c.delta=field<float>(b,0x1d0);c.prior_delta=field<float>(b,0x1d4);
    c.lift_a=field<float>(b,0x1dc);c.lift_b=field<float>(b,0x2dc);c.suspension_a=field<float>(b,0x1e0);c.suspension_b=field<float>(b,0x1e4);
    c.prior_gear=field<std::uint32_t>(b,0x1d8);c.gear=field<std::uint32_t>(b,0x208);
    c.road_left=field<float>(b,0x264);c.road_right=field<float>(b,0x268);c.slip=field<std::int16_t>(b,0xd34);
    const auto stage=platform::native_race_area_player_stage_unique(rt);std::uint32_t water=0;
    for(unsigned k=0;k<4;++k)c.roughness=std::max(c.roughness,float(ffb::surface_roughness(
        field<std::uint32_t>(b,0x24c+k*4u),int(field<std::uint32_t>(b,0x5c)),stage,&water)));
    c.water=water!=0;
    const auto other=field<std::uint32_t>(b,0xdd4);
    using Manager=platform::NativeRaceManager;
    if(other>=Manager::CarWorkFirst&&other<Manager::CarWorkFirst+Manager::CarWorkCount&&(rt.event_state.slots[other].flags&2u)){
        const auto offset=std::size_t(other-Manager::CarWorkFirst)*Manager::CarWorkStride;
        const auto& cars=rt.race.manager.car_works_7815a0;
        if(offset+0x1c8u<=cars.size()){c.other_speed=field<float>(cars.data()+offset,0x1c4);c.other_known=true;}
    }
    return c;
}
void filter(platform::NativeInputState& out,const platform::NativeRuntimeContext* rt){
    if(inner)inner(out,rt);
    if(!settings.allowed||settings.mode==0u)return;
    const auto current=xbox_runtime::gamepad_vibration_generation();
    if(current!=generation){model.reset();generation=current;}
    en::PadVibrationGate gate;gate.ordinary=ordinary_process;gate.muted=muted();
    gate.focused=ffb_front_check()(xbox_runtime::native_window().hwnd);gate.panel=desktop_panel_open();
    gate.connected=xbox_runtime::gamepad_vibration_available();gate.mode=rt?rt->mode_state.current:0;
    gate.pause_depth=rt?rt->event_state.pause_depth:1;gate.car_ready=rt&&(rt->event_state.slots[8].flags&2u)&&rt->event_function36.car_select.car_799d18.size()>=0xdd8u;
    if(!gate.eligible()||blocked){gamepad_vibration_stop();return;}
    const auto motors=en::map_pad_motors(model.tick(car_signals(*rt),settings.strength),settings);
    if(!xbox_runtime::gamepad_vibration_write(motors)){blocked=true;gamepad_vibration_stop();std::printf("gamepad vibration: output failed; stopped for this process\n");}
}
}
void gamepad_vibration_configure(const Ini& ini,bool ordinary){
    gamepad_vibration_stop();ordinary_process=ordinary;blocked=false;
    settings=enhancements::pad_vibration_settings(ini.text("Controls","VibrationMode","0"),ini.text("Controls","VibrationStrength","7"),
        ini.text("Controls","ImpulseVibrationMode","0"),ini.text("Controls","ImpulseVibrationLeftMultiplier","0.25"),
        ini.text("Controls","ImpulseVibrationRightMultiplier","0.25"),ini.text("Controls","VibrationControllerId","0"),ordinary,muted());
    std::printf("gamepad vibration: %s; selected native gamepad only, trigger commands may be approximated by its driver\n",
        settings.allowed&&settings.mode?"opt-in configured (runtime unqualified)":"off (default, excluded, muted, invalid setting or unsupported controller index)");
}
void gamepad_vibration_wrap_input_filter(){auto& filter_slot=xbox_runtime::input_filter();if(filter_slot!=filter){inner=filter_slot;filter_slot=filter;}}
void gamepad_vibration_stop(){model.reset();if(!xbox_runtime::gamepad_vibration_stop())blocked=true;}
void gamepad_vibration_shutdown(){gamepad_vibration_stop();settings.allowed=false;}
const char* gamepad_vibration_status_text(){
    if(!settings.mode)return "off (default or saved Off)";
    if(!settings.allowed)return "off (excluded, muted, invalid setting or unsupported controller index)";
    if(blocked)return "stopped after an output failure; restart required";
    if(muted())return "muted";
    if(!xbox_runtime::gamepad_vibration_available())return "waiting for the selected native gamepad";
    return "configured; gameplay/focus/F6/pause gates apply (physical output unqualified)";
}
}
