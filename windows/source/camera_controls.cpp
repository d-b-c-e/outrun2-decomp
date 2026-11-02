#include "camera_controls.hpp"
#include "camera_framing.hpp"
#include "desktop_settings.hpp"
#include "desktop_keyboard.hpp"
#include "desktop_panel.hpp"
#include "hud_toggle.hpp"
#include "native_window.hpp"
#include "platform/native_runtime.hpp"
#include "platform/pc_scene_renderer.hpp"
#include "platform/pc_screen.hpp"
#include <windows.h>
#include <cstdio>
#include <mutex>
namespace outrun::desktop {
namespace {
struct State {
    CameraFraming framing;
    std::string path,notice;
    float physical{};
    unsigned view{255};
    std::uint32_t clears{},mode{};
    std::array<HudToggleEdge,4> edges{};
    bool configured{},shift_set{};
    ULONGLONG notice_until{};
    xbox_runtime::InputFilter inner{};
} h;
std::mutex camera_mutex;
bool triples(){return platform::g_pc_triple_corrections!=nullptr;}
std::string label(){
    if(!CameraFraming::tunable(h.view))return "Camera "+std::to_string(h.view);
    const bool seat=h.framing.seat(h.view,triples());
    return std::string(CameraFraming::prefix(h.view))+" Camera - "+(seat?"Realistic FOV":"Game FOV")+
        (h.framing.offsets[h.view][seat?1:0]!=0.f?" (Custom)":"");
}
void notify(const std::string& text){h.notice=text;h.notice_until=GetTickCount64()+3000;}
float adjust(std::uint8_t view,float game){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    if(h.view!=view){h.view=view;notify(label());}
    return h.framing.fov(view,game,h.physical,triples());
}
bool save(const std::string& key,const std::string& value){
    std::string error;
    if(write_key(h.path,"Camera",key,value,error))return true;
    notify("Camera setting not saved: "+error);
    std::printf("camera: setting not saved: %s\n",error.c_str());return false;
}
void input_filter(platform::NativeInputState& out,const platform::NativeRuntimeContext* rt){
    if(h.inner)h.inner(out,rt);
    const bool race=rt&&(driving::race_camera_mode_48618d(std::int32_t(rt->mode_state.current))||rt->mode_state.current==19u);
    const bool blocked=!race||desktop_panel_open()||h.clears!=desktop_key_clears();
    h.clears=desktop_key_clears();
    const unsigned mode=rt?rt->mode_state.current:0;
    if(h.mode!=mode){
        h.mode=mode;
        const std::lock_guard<std::mutex> lock(camera_mutex);
        if(race&&CameraFraming::tunable(h.view))notify(label());
    }
    // Release waits inspect only this action's key. A held shifter or driving key cannot block it.
    const unsigned keys[]{0x41,0x4e,0x4a,0x4c}; // F7, numpad +, -, 5
    for(unsigned k=0;k<4;++k){
        const bool down=desktop_key(keys[k]);
        if(k==0&&down)h.shift_set=h.shift_set||desktop_key(0x2a)||desktop_key(0x36);
        const bool released=h.edges[k].step(down,blocked);
        if(k==0&&blocked)h.shift_set=false;
        if(!released)continue;
        if(k==0){if(!h.shift_set)camera_controls_toggle_set();h.shift_set=false;}
        else if(k==1)camera_controls_zoom(-1.f);
        else if(k==2)camera_controls_zoom(1.f);
        else camera_controls_reset();
    }
}
}
void camera_controls_configure(const Ini& ini,bool disabled,float physical){
    if(disabled)return; // test, recording and playback retain their existing presentation contract; never save
    h.path=ini.path();h.physical=physical;h.clears=desktop_key_clears();h.configured=true;
    for(unsigned view=0;view<3;++view){
        // Explicit old global tuning seeds absent per-camera choices; loading never rewrites owner settings.
        const bool fallback=ini.has_value("Triple","MatchGameFov")?!ini.flag("Triple","MatchGameFov",true):h.framing.realistic[view];
        h.framing.realistic[view]=ini.flag("Camera",CameraFraming::set_key(view),fallback);
        for(unsigned set=0;set<2;++set){
            const float offset=ini.number("Camera",CameraFraming::offset_key(view,set!=0),0.f);
            h.framing.offsets[view][set]=std::isfinite(offset)?offset:0.f;
        }
    }
    platform::camera_fov_adjust()=adjust;
}
void camera_controls_wrap_input_filter(){
    if(!h.configured)return;
    auto& filter=xbox_runtime::input_filter();if(filter==input_filter)return;
    h.inner=filter;filter=input_filter;
}
bool camera_controls_tunable(){const std::lock_guard<std::mutex> lock(camera_mutex);return h.configured&&CameraFraming::tunable(h.view);}
bool camera_controls_triples(){return triples();}
std::string camera_controls_label(){const std::lock_guard<std::mutex> lock(camera_mutex);return h.configured&&h.view!=255?label():"Camera available during a race";}
std::string camera_controls_notice(){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    if(!h.configured||GetTickCount64()>=h.notice_until)return {};
    return h.notice+"\nNumpad + / - zoom; 5 reset"+(triples()?"; F7 Game / Realistic":"");
}
void camera_controls_toggle_set(){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    if(!h.configured||!triples()||!CameraFraming::tunable(h.view))return;
    auto& r=h.framing.realistic[h.view];
    if(save(CameraFraming::set_key(h.view),r?"false":"true")){r=!r;notify(label());}
}
void camera_controls_zoom(float degrees){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    if(!h.configured||!CameraFraming::tunable(h.view)||!std::isfinite(degrees))return;
    const bool seat=h.framing.seat(h.view,triples());auto& offset=h.framing.offsets[h.view][seat?1:0];
    const float next=offset+degrees;if(!std::isfinite(next))return;
    char value[48];std::snprintf(value,sizeof value,"%.9g",double(next));
    if(save(CameraFraming::offset_key(h.view,seat),value)){offset=next;notify(label());}
}
void camera_controls_reset(){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    if(!h.configured||!CameraFraming::tunable(h.view))return;
    const bool seat=h.framing.seat(h.view,triples());
    if(save(CameraFraming::offset_key(h.view,seat),"0")){
        h.framing.offsets[h.view][seat?1:0]=0.f;notify(label()+" - calculated FOV restored");
    }
}
}
