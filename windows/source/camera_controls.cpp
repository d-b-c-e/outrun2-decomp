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
#include "driving/pc_d3dx.hpp"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <mutex>
namespace outrun::desktop {
namespace {
struct State {
    CameraFraming framing;
    std::array<std::array<std::array<CameraPoseOffset,2>,4>,30> poses{};
    std::string path,notice;
    float physical{};
    unsigned view{255},model{255};
    std::uint32_t clears{},mode{};
    std::array<HudToggleEdge,4> edges{};
    bool configured{},shift_set{},bonnet{},fit_available{};
    ULONGLONG notice_until{};
    xbox_runtime::InputFilter inner{};
} h;
std::mutex camera_mutex;
bool triples(){return platform::g_pc_triple_corrections!=nullptr;}
std::string label(){
    if(!CameraFraming::tunable(h.view))return "Camera "+std::to_string(h.view>=4?h.view-4:h.view);
    const bool seat=h.framing.seat(h.view,triples());
    return std::string(CameraFraming::prefix(h.view))+" Camera - "+(seat?"Realistic FOV":"Game FOV")+
        (h.framing.offsets[h.view][seat?1:0]!=0.f?" (Custom)":"")+
        ((h.view==0||h.view==3)&&h.model<30&&!h.fit_available?" (native placement)":"");
}
void notify(const std::string& text){h.notice=text;h.notice_until=GetTickCount64()+3000;}
float adjust(std::uint8_t view,float game){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    if(view==1||view==2)h.bonnet=false;
    const unsigned logical=view==0&&h.bonnet?3u:view<=2?view:unsigned(view)+4u;
    if(h.view!=logical){h.view=logical;notify(label());}
    driving::camera_fov_context().near_z=logical==0||logical==3?0.05f:0.f;
    return h.framing.fov(logical,game,h.physical,triples());
}
std::uint8_t cycle(std::uint8_t previous){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    const unsigned logical=previous==0&&h.bonnet?3u:previous;
    const unsigned next=logical==0?3u:logical==3?1u:logical==1?2u:0u;
    h.bonnet=next==3;return std::uint8_t(h.bonnet?0:next);
}
void adjust_view(driving::Bytes camera,driving::Bytes car,const platform::CameraMountFit* fit,driving::PcMatrixStack& s){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    const auto old_model=h.model;const bool old_fit=h.fit_available;
    const int model=car.i8(0x11);h.model=model>=0&&model<30?unsigned(model):255;h.fit_available=fit&&fit->valid;
    if((old_model!=h.model||old_fit!=h.fit_available)&&(h.view==0||h.view==3))notify(label());
    if(!CameraFraming::tunable(h.view)||h.model>=30)return;
    const bool mounted=h.view==0||h.view==3;
    if(mounted&&fit&&fit->valid){
        const auto& mount=h.view==0?fit->bumper:fit->bonnet;
        driving::PcMatrix16 parent{};std::memcpy(parent.data(),car.sub(0xb0,64).data(),64);
        driving::PcMatrix16 inverse{};
        if(driving::pc_d3dx_matrix_inverse(inverse,parent)){
            const auto eye=driving::pc_d3dx_vec3_transform_coord({mount[0],mount[1],-mount[2]},parent);
            const auto target=driving::pc_d3dx_vec3_transform_coord({mount[0],mount[1],-mount[2]-1.f},parent);
            const auto up=driving::pc_d3dx_vec3_transform_normal({0,1,0},parent);
            driving::pc_d3dx_look_at_rh(s.current(),eye,target,up);
        }
    }
    // Render-only offsets never alter the authored/smoothed eye, target or angle fields.
    const auto& p=h.poses[h.model][h.view][h.framing.seat(h.view,triples())?1:0];
    if(p.side!=0.f||p.up!=0.f||p.forward!=0.f||p.pitch!=0.f){
        std::array<std::uint8_t,64> base{};std::memcpy(base.data(),s.current().data(),64);
        driving::pc_matrix_identity(s);driving::pc_matrix_rotate_x(s,-p.pitch*0.017453292519943295f);
        driving::pc_matrix_translate_vector(s,{-p.side,-p.up,p.forward});
        driving::pc_matrix_multiply_current(s,driving::Bytes(base.data(),base.size()));
    }
    if(h.view==0)car.put32(4,car.u32(4)&~0xc000u); // stock visibility bits: hide own body/wheels in bumper
    else if(h.view==3)car.put32(4,(car.u32(4)&~0xc000u)|0x4000u); // bonnet retains the exterior
    (void)camera;
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
    for(unsigned view=0;view<4;++view){
        // Explicit old global tuning seeds absent per-camera choices; loading never rewrites owner settings.
        const bool fallback=view<3&&ini.has_value("Triple","MatchGameFov")?!ini.flag("Triple","MatchGameFov",true):h.framing.realistic[view];
        h.framing.realistic[view]=ini.flag("Camera",CameraFraming::set_key(view),fallback);
        for(unsigned set=0;set<2;++set){
            const float offset=ini.number("Camera",CameraFraming::offset_key(view,set!=0),0.f);
            h.framing.offsets[view][set]=std::isfinite(offset)?offset:0.f;
        }
        for(unsigned model=0;model<30;++model)for(unsigned set=0;set<2;++set){
            auto& pose=h.poses[model][view][set];
            float* fields[]{&pose.side,&pose.up,&pose.forward,&pose.pitch};const char* axes[]{"Side","Up","Forward","Pitch"};
            for(unsigned axis=0;axis<4;++axis){
                const float value=ini.number("Camera",camera_pose_key(model,view,set!=0,axes[axis]),0.f);
                *fields[axis]=std::isfinite(value)?value:0.f;
            }
        }
    }
    platform::camera_fov_adjust()=adjust;
    platform::camera_view_cycle()=cycle;platform::camera_mount_adjust()=adjust_view;
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
    SettingsEdits edits{{CameraFraming::offset_key(h.view,seat),"0"}};
    if(h.model<30)for(const char* axis:{"Side","Up","Forward","Pitch"})edits.push_back({camera_pose_key(h.model,h.view,seat,axis),"0"});
    std::string error;
    if(write_keys(h.path,"Camera",edits,error)){
        h.framing.offsets[h.view][seat?1:0]=0.f;if(h.model<30)h.poses[h.model][h.view][seat?1:0]={};
        notify(label()+" - calculated FOV restored; placement reset");
    }else{notify("Camera setting not saved: "+error);std::printf("camera: setting not saved: %s\n",error.c_str());
    }
}
}
