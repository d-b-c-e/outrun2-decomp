#include "camera_controls.hpp"
#include "camera_framing.hpp"
#include "camera_keys.hpp"
#include "mounted_camera_rotation.hpp"
#include "desktop_settings.hpp"
#include "desktop_keyboard.hpp"
#include "desktop_panel.hpp"
#include "hud_toggle.hpp"
#include "native_window.hpp"
#include "platform/native_runtime.hpp"
#include "platform/pc_scene_renderer.hpp"
#include "platform/pc_screen.hpp"
#include "driving/pc_d3dx.hpp"
#include "enhancements/frame_rate.hpp"
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
    float physical{},game_fov{};
    unsigned view{255},model{255};
    int last_camera{-1};bool cycle_save_failed{};
    MountedCameraRotation rotation;const void* rotation_car{};
    std::uint32_t clears{},mode{};
    CameraKeys keys;std::array<HudToggleKey,11> bindings{};std::array<CameraKeyRepeat,11> edges{};
    HudToggleEdge set_edge;
    std::array<float,3> steps{{.02f,1.f,2.f}};
    bool configured{},shift_set{},bonnet{},fit_available{};
    ULONGLONG notice_until{};
    xbox_runtime::InputFilter inner{};
} h;
std::mutex camera_mutex;
bool triples(){return platform::g_pc_triple_corrections!=nullptr;}
std::string label(){
    if(!CameraFraming::tunable(h.view))return "Camera "+std::to_string(h.view>=4?h.view-4:h.view);
    const bool seat=h.framing.seat(h.view,triples());
    bool custom=h.framing.offsets[h.view][seat?1:0]!=0.f;
    if(h.model<30){const auto& p=h.poses[h.model][h.view][seat?1:0];custom=custom||p.side!=0.f||p.up!=0.f||p.forward!=0.f||p.pitch!=0.f;}
    return std::string(CameraFraming::prefix(h.view))+" Camera - "+(seat?"Realistic FOV":"Game FOV")+
        (custom?" (Custom)":"")+
        ((h.view==0||h.view==3)&&h.model<30&&!h.fit_available?" (native placement)":"");
}
void notify(const std::string& text){h.notice=text;h.notice_until=GetTickCount64()+3000;}
float adjust(std::uint8_t view,float game){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    h.game_fov=game; // unmodified authored/dynamic FOV, not the previous adjusted result
    if(view==1||view==2)h.bonnet=false;
    const unsigned logical=view==0&&h.bonnet?3u:view<=2?view:unsigned(view)+4u;
    if(h.view!=logical){h.view=logical;h.rotation={};notify(label()+(h.cycle_save_failed?" - last camera not saved":""));h.cycle_save_failed=false;}
    driving::camera_fov_context().near_z=logical==0||logical==3?0.05f:0.f;
    return h.framing.fov(logical,game,h.physical,triples());
}
bool save(const std::string&,const std::string&);
std::uint8_t cycle(std::uint8_t previous){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    const unsigned logical=previous==0&&h.bonnet?3u:previous;
    const unsigned next=logical==0?3u:logical==3?1u:logical==1?2u:0u;
    h.bonnet=next==3;
    if(h.last_camera!=int(next)){
        if(save("LastCamera",CameraFraming::prefix(next)))h.last_camera=int(next);
        else h.cycle_save_failed=true; // the view still changes, but a failed preference write is never reported saved
    }
    return std::uint8_t(h.bonnet?0:next);
}
std::uint8_t restore_view(std::uint8_t native_default){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    h.bonnet=h.last_camera==3;
    h.rotation={};
    return h.last_camera<0?native_default:std::uint8_t(h.bonnet?0:h.last_camera);
}
bool body_hidden(){const std::lock_guard<std::mutex> lock(camera_mutex);return h.configured&&h.view==0;}
void adjust_view(driving::Bytes camera,driving::Bytes car,const platform::CameraMountFit* fit,driving::PcMatrixStack& s){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    const auto old_model=h.model;const bool old_fit=h.fit_available;
    const int model=car.i8(0x11);h.model=model>=0&&model<30?unsigned(model):255;h.fit_available=fit&&fit->valid;
    if(old_model!=h.model||h.rotation_car!=car.data()){h.rotation={};h.rotation_car=car.data();}
    if((old_model!=h.model||old_fit!=h.fit_available)&&(h.view==0||h.view==3)&&h.notice.find("not saved")==std::string::npos)notify(label());
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
        driving::pc_matrix_identity(s);driving::pc_matrix_rotate_x(s,std::remainder(p.pitch,360.f)*0.017453292519943295f); // positive offset looks down
        driving::pc_matrix_translate_vector(s,{-p.side,-p.up,p.forward});
        driving::pc_matrix_multiply_current(s,driving::Bytes(base.data(),base.size()));
    }
    if(h.view==0||h.view==1||h.view==3){
        driving::PcMatrix16 view{},world{};std::memcpy(view.data(),s.current().data(),64);
        if(driving::pc_d3dx_matrix_inverse(world,view)){
            const auto& context=driving::camera_fov_context();const auto target=camera_rotation_from_world(world);
            if(!context.display_replay){
                if(!h.rotation.started||!context.paused)h.rotation.step(target,1.f/60.f);
            }
            const auto rotation=context.display_replay&&!context.paused&&h.rotation.started?h.rotation.display(enhancements::display_blend()):h.rotation.current;
            if(h.rotation.started&&driving::pc_d3dx_matrix_inverse(view,camera_rotation_world(rotation,world)))std::memcpy(s.current().data(),view.data(),64);
        }else h.rotation={};
    }else h.rotation={};
    if(h.view==0)car.put32(4,car.u32(4)&~0xc000u); // stock visibility bits: hide own body/wheels in bumper
    else if(h.view==3)car.put32(4,(car.u32(4)&~0xc000u)|0x4000u); // bonnet retains the exterior
    (void)camera;
}
bool save_edits(const SettingsEdits& edits){
    std::string error;
    if(write_keys(h.path,"Camera",edits,error))return true;
    notify("Camera setting not saved: "+error);
    std::printf("camera: setting not saved: %s\n",error.c_str());return false;
}
bool save(const std::string& key,const std::string& value){return save_edits({{key,value}});}
std::array<HudToggleKey,11> parse_keys(const CameraKeys& keys){
    std::array<HudToggleKey,11> out;
    for(unsigned i=0;i<11;++i){
        out[i]=parse_hud_toggle_key(keys.names[i],[](char c){return MapVirtualKeyW(unsigned(static_cast<unsigned char>(c)),MAPVK_VK_TO_VSC)&0xffu;});
        for(const auto scan:out[i].keys)if(scan==0x41u||scan==0x01u){out[i].why="F7 and Escape are reserved";break;}
    }
    for(unsigned i=0;i<11;++i)for(unsigned j=i+1;j<11;++j)
        for(const auto scan:out[i].keys)if(std::find(out[j].keys.begin(),out[j].keys.end(),scan)!=out[j].keys.end()){
            out[i].why=std::string("Conflicts with ")+CameraKeyLabels[j];out[j].why=std::string("Conflicts with ")+CameraKeyLabels[i];
        }
    for(auto& binding:out)if(!binding.why.empty())binding.keys.clear();return out;
}
void install_keys(const CameraKeys& keys){
    h.keys=keys;h.bindings=parse_keys(keys);
    for(auto& edge:h.edges){edge={};edge.edge.wait=true;}
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
    // Release waits inspect only this action's bindings. A held shifter cannot block camera keys.
    const bool set_down=desktop_key(0x41);
    if(set_down)h.shift_set=h.shift_set||desktop_key(0x2a)||desktop_key(0x36);
    if(h.set_edge.step(set_down,blocked)){if(!h.shift_set)camera_controls_toggle_set();h.shift_set=false;}
    if(blocked)h.shift_set=false;
    std::array<bool,11> actions{};
    {
        const std::lock_guard<std::mutex> lock(camera_mutex);
        for(unsigned i=0;i<11;++i){bool down=false;for(const auto key:h.bindings[i].keys)down=down||desktop_key(key);
            actions[i]=h.edges[i].step(down,blocked,GetTickCount64(),i!=10);}
    }
    for(unsigned i=0;i<11;++i)if(actions[i]){
        const float move=camera_controls_step(0),tilt=camera_controls_step(1),fov=camera_controls_step(2);
        if(i<2)camera_controls_move(1,i==0?move:-move);
        else if(i<4)camera_controls_move(2,i==2?move:-move);
        else if(i<6)camera_controls_move(0,i==4?-move:move);
        else if(i<8)camera_controls_move(3,i==6?tilt:-tilt);
        else if(i<10)camera_controls_zoom(i==8?fov:-fov);
        else camera_controls_reset();
    }
}
}
void camera_controls_configure(const Ini& ini,bool disabled,float physical){
    if(disabled)return; // test, recording and playback retain their existing presentation contract; never save
    h.path=ini.path();h.physical=physical;h.clears=desktop_key_clears();h.configured=true;
    h.last_camera=-1;h.bonnet=false;h.cycle_save_failed=false;h.rotation={};h.rotation_car=nullptr;
    const auto last=hud_toggle_setting_name(ini.text("Camera","LastCamera"));
    for(unsigned view=0;view<4;++view)if(last==hud_toggle_setting_name(CameraFraming::prefix(view)))h.last_camera=int(view);
    if(!last.empty()&&last!="DEFAULT"&&h.last_camera<0)std::printf("camera: LastCamera = %.32s refused; native stage-start preference retained\n",last.c_str());
    install_keys(camera_keys_load([&](const char* key){return ini.text("Camera",key);},[&](const char* key){return ini.has_value("Camera",key);}));
    if(h.keys.migrated)std::printf("camera: untouched earlier shortcut defaults migrated for this session; custom sets and file bytes retained\n");
    const char* step_keys[]{"MoveStepMetres","TiltStepDegrees","FovStepDegrees"};
    const float defaults[]{.02f,1.f,2.f},minimum[]{.005f,.1f,.5f},maximum[]{.25f,10.f,10.f};
    for(unsigned i=0;i<3;++i){const float value=ini.number("Camera",step_keys[i],defaults[i]);h.steps[i]=std::isfinite(value)?std::clamp(value,minimum[i],maximum[i]):defaults[i];}
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
    platform::camera_view_cycle()=cycle;platform::camera_view_restore()=restore_view;platform::camera_mount_adjust()=adjust_view;
    platform::camera_body_hidden()=body_hidden;
}
void camera_controls_wrap_input_filter(){
    if(!h.configured)return;
    auto& filter=xbox_runtime::input_filter();if(filter==input_filter)return;
    h.inner=filter;filter=input_filter;
}
bool camera_controls_tunable(){const std::lock_guard<std::mutex> lock(camera_mutex);return h.configured&&CameraFraming::tunable(h.view);}
bool camera_controls_available(){const std::lock_guard<std::mutex> lock(camera_mutex);return h.configured;}
bool camera_controls_triples(){return triples();}
std::string camera_controls_label(){const std::lock_guard<std::mutex> lock(camera_mutex);return h.configured&&h.view!=255?label():"Camera available during a race";}
std::string camera_controls_notice(){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    if(!h.configured||GetTickCount64()>=h.notice_until)return {};
    if(!CameraFraming::tunable(h.view))return h.notice;
    return h.notice+"\n"+camera_key_display(h.keys.names[9])+" / "+camera_key_display(h.keys.names[8])+" zoom; "+
        camera_key_display(h.keys.names[10])+" reset"+(triples()?h.framing.seat(h.view,true)?"; F7 Game set":"; F7 Realistic set":"");
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
CameraFovValues camera_controls_fov(){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    if(!h.configured||!CameraFraming::tunable(h.view))return {};
    const bool seat=h.framing.seat(h.view,triples());const float base=seat?h.physical:h.game_fov;
    if(!std::isfinite(base)||!(base>0.f&&base<3.14159265f))return {};
    return {base*57.295779513f,h.framing.fov(h.view,h.game_fov,h.physical,triples())*57.295779513f,h.framing.offsets[h.view][seat?1:0],true};
}
void camera_controls_set_fov(float degrees){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    if(!h.configured||!CameraFraming::tunable(h.view)||!std::isfinite(degrees)||!(degrees>0.f&&degrees<180.f))return;
    const bool seat=h.framing.seat(h.view,triples());const float base=seat?h.physical:h.game_fov;
    if(!std::isfinite(base)||!(base>0.f&&base<3.14159265f))return;
    const float offset=degrees-base*57.295779513f;char value[48];std::snprintf(value,sizeof value,"%.9g",double(offset));
    if(save(CameraFraming::offset_key(h.view,seat),value)){h.framing.offsets[h.view][seat?1:0]=offset;notify(label());}
}
void camera_controls_reset(){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    if(!h.configured||!CameraFraming::tunable(h.view))return;
    const bool seat=h.framing.seat(h.view,triples());
    SettingsEdits edits{{CameraFraming::offset_key(h.view,seat),"0"}};
    if(h.model<30)for(const char* axis:{"Side","Up","Forward","Pitch"})edits.push_back({camera_pose_key(h.model,h.view,seat,axis),"0"});
    if(save_edits(edits)){
        h.framing.offsets[h.view][seat?1:0]=0.f;if(h.model<30)h.poses[h.model][h.view][seat?1:0]={};
        notify(label()+" - calculated FOV restored; placement reset");
    }
}
void camera_controls_move(unsigned axis,float amount){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    if(!h.configured||h.model>=30||!CameraFraming::tunable(h.view)||axis>=4||!std::isfinite(amount))return;
    const bool seat=h.framing.seat(h.view,triples());auto& pose=h.poses[h.model][h.view][seat?1:0];
    float* fields[]{&pose.side,&pose.up,&pose.forward,&pose.pitch};const char* names[]{"Side","Up","Forward","Pitch"};
    const float next=*fields[axis]+amount;if(!std::isfinite(next))return;
    char value[48];std::snprintf(value,sizeof value,"%.9g",double(next));
    if(save(camera_pose_key(h.model,h.view,seat,names[axis]),value)){*fields[axis]=next;notify(label()+" - "+names[axis]+" offset "+value);}
}
bool camera_controls_pose_available(){const std::lock_guard<std::mutex> lock(camera_mutex);return h.configured&&h.model<30&&CameraFraming::tunable(h.view);}
std::array<float,4> camera_controls_pose(){
    const std::lock_guard<std::mutex> lock(camera_mutex);if(h.model>=30||!CameraFraming::tunable(h.view))return {};
    const auto& pose=h.poses[h.model][h.view][h.framing.seat(h.view,triples())?1:0];return {pose.side,pose.up,pose.forward,pose.pitch};
}
std::array<std::string,11> camera_controls_key_names(){const std::lock_guard<std::mutex> lock(camera_mutex);return h.keys.names;}
std::array<std::string,11> camera_controls_key_errors(){const std::lock_guard<std::mutex> lock(camera_mutex);std::array<std::string,11> out;
    for(unsigned i=0;i<11;++i)out[i]=h.bindings[i].why;return out;}
bool camera_controls_keys_migrated(){const std::lock_guard<std::mutex> lock(camera_mutex);return h.keys.migrated;}
void camera_controls_set_key(unsigned index,const std::string& name){
    const std::lock_guard<std::mutex> lock(camera_mutex);if(!h.configured||index>=11)return;
    auto next=h.keys;next.names[index]=camera_key_name(name);const auto bindings=parse_keys(next);
    if(!bindings[index].why.empty()){notify("Camera binding not saved: "+bindings[index].why);return;}
    SettingsEdits edits;
    for(unsigned i=0;i<11;++i)if(h.keys.migrated||i==index)edits.push_back({CameraKeyFields[i],next.names[i]});
    if(save_edits(edits)){next.migrated=false;install_keys(next);notify("Camera binding saved");}
}
void camera_controls_default_keys(){
    const std::lock_guard<std::mutex> lock(camera_mutex);if(!h.configured)return;
    CameraKeys next;SettingsEdits edits;for(unsigned i=0;i<11;++i)edits.push_back({CameraKeyFields[i],next.names[i]});
    if(save_edits(edits)){install_keys(next);notify("Camera numpad defaults restored");}
}
float camera_controls_step(unsigned index){const std::lock_guard<std::mutex> lock(camera_mutex);return h.steps[index<3?index:0];}
void camera_controls_set_step(unsigned index,float value){
    const std::lock_guard<std::mutex> lock(camera_mutex);if(!h.configured||index>=3||!std::isfinite(value))return;
    const char* keys[]{"MoveStepMetres","TiltStepDegrees","FovStepDegrees"};
    const float minimum[]{.005f,.1f,.5f},maximum[]{.25f,10.f,10.f};value=std::clamp(value,minimum[index],maximum[index]);
    char text[48];std::snprintf(text,sizeof text,"%.9g",double(value));if(save(keys[index],text)){h.steps[index]=value;notify("Camera adjustment step saved");}
}
void camera_controls_default_steps(){
    const std::lock_guard<std::mutex> lock(camera_mutex);if(!h.configured)return;
    if(save_edits({{"MoveStepMetres","0.02"},{"TiltStepDegrees","1"},{"FovStepDegrees","2"}})){h.steps={.02f,1.f,2.f};notify("Camera adjustment steps restored");}
}
}
