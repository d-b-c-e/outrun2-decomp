#include "camera_controls.hpp"
#include "mounted_camera_rotation.hpp"
#include "desktop_settings.hpp"
#include "wheelkit_runtime.hpp"
#include "hud_toggle.hpp"
#include "platform/native_runtime.hpp"
#include "platform/pc_scene_renderer.hpp"
#include "platform/pc_screen.hpp"
#include "driving/pc_d3dx.hpp"
#include "enhancements/frame_rate.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
namespace outrun::desktop {
namespace {
// Native slots: bumper 0, in-car 1, chase 2. Logical Bonnet 3 uses slot 0 with a model-derived render transform; the
// native cinematic slots (3 and up) are not in the list and stay exactly the game's.
const char* const ViewNames[4]{"Bumper","Cockpit","Chase","Bonnet"};
struct State {
    std::string path;
    float physical{};
    unsigned view{255},model{255};
    int last_camera{-1};
    MountedCameraRotation rotation;const void* rotation_car{};
    bool configured{},bonnet{},race{};
    WheelkitCamera set{};   // what the runtime attaches to set.view, taken each tick
} h;
std::mutex camera_mutex;
bool triples(){return platform::g_pc_triple_corrections!=nullptr;}
bool save_last(unsigned view){
    std::string error;
    if(write_keys(h.path,"Camera",{{"LastCamera",std::string(ViewNames[view])}},error))return true;
    std::printf("camera: LastCamera not saved: %s\n",error.c_str());return false;
}
float adjust(std::uint8_t view,float game){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    if(view==1||view==2)h.bonnet=false;
    const unsigned logical=view==0&&h.bonnet?3u:view<=2?view:unsigned(view)+4u;
    if(h.view!=logical){h.view=logical;h.rotation={};}
    driving::camera_fov_context().near_z=logical==0||logical==3?0.05f:0.f;
    if(logical>3||h.set.view!=int(logical))return game;
    const float base=h.set.realistic&&h.physical>0.f?h.physical:game;
    if(!std::isfinite(base)||!std::isfinite(h.set.fov_offset)||!(base>0.f&&base<3.14159265f))return game;
    // The player's offset is unrestricted; only the resulting perspective must stay finite and nondegenerate.
    return float(std::clamp(double(base)+double(h.set.fov_offset)*0.017453292519943295,0.001,3.140592653589793));
}
// The game's cycle with the added view: bumper, bonnet, cockpit, chase.
std::uint8_t cycle(std::uint8_t previous){
    const std::lock_guard<std::mutex> lock(camera_mutex);
    const unsigned logical=previous==0&&h.bonnet?3u:previous;
    const unsigned next=logical==0?3u:logical==3?1u:logical==1?2u:0u;
    h.bonnet=next==3;
    if(h.last_camera!=int(next)&&save_last(next))h.last_camera=int(next);
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
    const auto old_model=h.model;
    const int model=car.i8(0x11);h.model=model>=0&&model<30?unsigned(model):255;
    if(old_model!=h.model||h.rotation_car!=car.data()){h.rotation={};h.rotation_car=car.data();}
    if(h.view>3||h.model>=30)return;
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
    // The runtime's position offset for this car, view and framing (centimetres along the camera's own axes): render
    // only, never written to the authored or smoothed eye, target or angle fields.
    if(h.set.view==int(h.view)&&(h.set.right_cm!=0.f||h.set.up_cm!=0.f||h.set.forward_cm!=0.f)){
        std::array<std::uint8_t,64> base{};std::memcpy(base.data(),s.current().data(),64);
        driving::pc_matrix_identity(s);
        driving::pc_matrix_translate_vector(s,{-h.set.right_cm*.01f,-h.set.up_cm*.01f,h.set.forward_cm*.01f});
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
}
void camera_controls_tick(const platform::NativeRuntimeContext* rt){
    int view=-1;char car[16]="";
    {
        const std::lock_guard<std::mutex> lock(camera_mutex);
        if(!h.configured)return;
        const bool race=rt&&(driving::race_camera_mode_48618d(std::int32_t(rt->mode_state.current))||rt->mode_state.current==19u);
        if(race&&h.view<=3){view=int(h.view);if(h.model<30)std::snprintf(car,sizeof car,"Car%u",h.model);}
    }
    const auto set=wheelkit_camera(view,*car?car:nullptr,triples());
    const std::lock_guard<std::mutex> lock(camera_mutex);
    h.set=set;
}
void camera_controls_configure(const Ini& ini,float realistic_fov){
    h.path=ini.path();h.physical=realistic_fov;h.configured=true;
    h.last_camera=-1;h.bonnet=false;h.rotation={};h.rotation_car=nullptr;
    const auto last=hud_toggle_setting_name(ini.text("Camera","LastCamera"));
    for(unsigned view=0;view<4;++view)if(last==hud_toggle_setting_name(ViewNames[view]))h.last_camera=int(view);
    if(!last.empty()&&last!="DEFAULT"&&h.last_camera<0)std::printf("camera: LastCamera = %.32s refused; native stage-start preference retained\n",last.c_str());
    platform::camera_fov_adjust()=adjust;
    platform::camera_view_cycle()=cycle;platform::camera_view_restore()=restore_view;platform::camera_mount_adjust()=adjust_view;
    platform::camera_body_hidden()=body_hidden;
}
}
