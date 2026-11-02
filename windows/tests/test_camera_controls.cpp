// Real desktop camera controller + INI writer, inert keyboard/runtime records only.
// The window input-chain boundary and panel-open flag are stand-ins; no window/device/game is created.
#include "camera_controls.hpp"
#include "camera_keys.hpp"
#include "mounted_camera_rotation.hpp"
#include "desktop_settings.hpp"
#include "desktop_keyboard.hpp"
#include "native_window.hpp"
#include "platform/native_runtime.hpp"
#include "platform/pc_scene_renderer.hpp"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmisleading-indentation" // existing compressed software-device headers
#endif
#include "platform/pc_soft_d3d9.hpp"
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include "platform/retail_asset_store.hpp"
#include "enhancements/frame_rate.hpp"
#include "platform/pc_screen.hpp"
#include "platform/pc_curve_scene.hpp"
#include "driving/pc_d3dx.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <map>
#include <limits>
namespace {bool panel_open;unsigned checks;float profile_scale=1;unsigned profile_calls;bool profile_refused;
bool profile_corrections(const float*,float* out){
    ++profile_calls;if(profile_refused)return false;
    for(unsigned panel=0;panel<3;++panel)for(unsigned k=0;k<16;++k)out[panel*16+k]=k%5==0?(k==0?profile_scale:1.f):0.f;
    return true;
}}

namespace outrun::desktop {bool desktop_panel_open(){return panel_open;}}
namespace outrun::xbox_runtime {InputFilter& input_filter(){static InputFilter filter=nullptr;return filter;}}
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"camera controls line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(int argc,char** argv){
    using namespace outrun;using namespace desktop;
    {
        const auto yaw=[](float degrees){const float half=degrees*.008726646259971648f;return CameraQuaternion{0,std::sin(half),0,std::cos(half)};};
        MountedCameraRotation a,b;a.step(yaw(0),0);b.step(yaw(0),0);a.step(yaw(10),.06f);b.step(yaw(10),.03f);b.step(yaw(10),.03f);
        CHECK(std::abs(camera_rotation_dot(a.current,b.current))>.999999f);
        CHECK(std::abs(2.f*std::atan2(a.current[1],a.current[3])*57.295779513f-10.f*(1.f-std::exp(-1.f)))<.0001f);
        const auto saved=a.current;a.step(yaw(10),0);CHECK(a.current==saved);a.step(yaw(50),1.f/60.f);CHECK(std::abs(camera_rotation_dot(a.current,yaw(50)))>.999999f&&a.previous==a.current);
        // Shortest arc across quaternion sign/180 degrees, round-trip yaw/pitch/roll, rigid translation.
        for(unsigned axis=0;axis<3;++axis)for(const float angle:{10.f,179.f,-179.f,180.f}){
            CameraQuaternion q{0,0,0,std::cos(angle*.008726646259971648f)};q[axis]=std::sin(angle*.008726646259971648f);
            driving::PcMatrix16 position{};position[12]=12;position[13]=34;position[14]=56;const auto world=camera_rotation_world(q,position);
            CHECK(std::abs(camera_rotation_dot(q,camera_rotation_from_world(world)))>.999999f&&world[12]==12&&world[13]==34&&world[14]==56);
        }
        a={};a.step(yaw(179),0);a.step(yaw(-179),1.f/60.f);CHECK(std::abs(camera_rotation_dot(a.current,yaw(179)))>.9999f);
        const auto tick=a.current;for(unsigned i=0;i<8;++i)(void)a.display(float(i)/8.f);CHECK(a.current==tick);
    }
    {
        std::map<std::string,std::string> saved;
        const auto load=[&]{return camera_keys_load([&](const char* key){return saved[key];},[&](const char* key){return saved.count(key)!=0;});};
        CHECK(load().names==CameraKeys{}.names&&!load().migrated);
        const char* old[]{"Keypad8","Keypad2","Keypad9","Keypad7","Keypad4","Keypad6","Keypad1","Keypad3","KeypadPlus","KeypadMinus","Keypad0"};
        for(unsigned i=0;i<11;++i)saved[CameraKeyFields[i]]=old[i];
        const auto bytes=saved;CHECK(load().migrated&&load().names==CameraKeys{}.names&&saved==bytes);
        saved["KeyLeft"]="F9";CHECK(!load().migrated&&load().names[4]=="F9"&&load().names[8]=="ADD");
        saved.erase("KeyUp");CHECK(!load().migrated&&load().names[0]=="NUMPAD9");
        saved.clear();for(unsigned i=0;i<11;++i)saved[CameraKeyFields[i]]=CameraKeys{}.names[i];CHECK(!load().migrated);
        saved["KeyFovUp"]="ADD";saved["KeyFovDown"]="SUBTRACT";CHECK(load().migrated&&load().names[9]=="ADD");
        saved["KeyLeft"]="NumpadDivide";saved["KeyRight"]="NumpadMultiply";saved["KeyPitchDown"]="Numpad6";saved["KeyPitchUp"]="Numpad4";
        CHECK(load().migrated&&load().names==CameraKeys{}.names);
        CameraKeyRepeat repeat;
        CHECK(!repeat.step(true,false,100,true)&&repeat.step(false,false,200,true));
        CHECK(!repeat.step(true,false,300,true)&&!repeat.step(true,false,649,true)&&repeat.step(true,false,650,true));
        CHECK(!repeat.step(true,false,651,true)&&repeat.step(true,false,750,true)&&!repeat.step(false,false,800,true));
        CHECK(!repeat.step(true,true,900,true)&&!repeat.step(false,false,1000,true));
        CHECK(!repeat.step(true,false,1100,false)&&!repeat.step(true,false,3000,false)&&repeat.step(false,false,3100,false));
    }
    const auto dir=std::filesystem::current_path()/("camera-controls-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    CHECK(std::filesystem::create_directory(dir));const auto path=(dir/"fixture.ini").string();
    const std::string original="; owner\r\n[Controls]\r\nSchema = 1\r\nunknown = retained\r\n[FFB]\r\nOutput = none\r\n";
    {std::ofstream out(path,std::ios::binary);out<<original;}
    const auto read=[&]{std::ifstream in(path,std::ios::binary);std::ostringstream out;out<<in.rdbuf();return out.str();};
    Ini ini;CHECK(ini.load(path));
    camera_controls_configure(ini,true,0.586f);camera_controls_wrap_input_filter();
    CHECK(!platform::camera_fov_adjust()&&!xbox_runtime::input_filter()&&read()==original);
    camera_controls_configure(ini,false,0.586f);camera_controls_wrap_input_filter();
    CHECK(platform::camera_fov_adjust()&&xbox_runtime::input_filter()&&read()==original);
    platform::g_pc_triple_corrections=[](const float*,float*){return false;}; // presence selects angled policy, never invoked here
    const auto fov=[&](unsigned view){return platform::camera_fov_adjust()(std::uint8_t(view),1.1f);};
    CHECK(fov(0)==0.586f&&fov(1)==0.586f&&fov(2)==1.1f&&read()==original);
    auto rt=std::make_unique<platform::NativeRuntimeContext>();rt->mode_state.current=16;
    platform::NativeInputState input{};
    const auto poll=[&]{xbox_runtime::input_filter()(input,rt.get());};
    const auto release=[&](unsigned key){desktop_keys()[key]=0x80;poll();desktop_keys()[key]=0;poll();};
    (void)fov(1);poll();desktop_keys()[0x20]=0x80; // unrelated key remains held throughout camera actions
    release(0x4e);CHECK(fov(1)<0.586f&&camera_controls_label().find("(Custom)")!=std::string::npos);
    CHECK(camera_controls_notice().find("Cockpit Camera - Realistic FOV (Custom)")!=std::string::npos&&camera_controls_notice().find("Numpad + / Numpad - zoom; Numpad 5 reset; F7 Game set")!=std::string::npos);
    CHECK(ini.load(path)&&ini.number("Camera","CockpitRealisticFovOffset",0.f)==-2.f);
    const auto narrowed=fov(1);release(0x41);CHECK(fov(1)==1.1f); // F7 changes only this camera's set
    CHECK(camera_controls_notice().find("Cockpit Camera - Game FOV")!=std::string::npos&&camera_controls_notice().find("F7 Realistic set")!=std::string::npos);
    CHECK(fov(0)==0.586f&&fov(2)==1.1f);
    (void)fov(1);release(0x4a);CHECK(fov(1)>1.1f);
    release(0x41);CHECK(fov(1)==narrowed);release(0x4c);CHECK(fov(1)==0.586f);
    CHECK(camera_controls_label().find("(Custom)")==std::string::npos&&camera_controls_notice().find("calculated FOV restored")!=std::string::npos);
    const auto calculated=camera_controls_fov();CHECK(calculated.valid&&std::fabs(calculated.calculated-.586f*57.295779513f)<1e-4f&&calculated.offset==0.f);
    camera_controls_set_fov(40.f);CHECK(std::fabs(fov(1)-40.f*.017453292519943295f)<1e-5f&&std::fabs(camera_controls_fov().current-40.f)<1e-4f);
    CHECK(ini.load(path)&&std::fabs(ini.number("Camera","CockpitRealisticFovOffset",0.f)-(40.f-calculated.calculated))<1e-4f);
    const auto valid_fov_bytes=read();for(const float invalid:{0.f,-1.f,180.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})camera_controls_set_fov(invalid);
    CHECK(read()==valid_fov_bytes&&std::fabs(camera_controls_fov().current-40.f)<1e-4f);
    camera_controls_reset();(void)fov(1);
    platform::g_pc_wide_aspect=5.25f;
    const auto before=read();desktop_keys()[0x2a]=0x80;release(0x41);desktop_keys()[0x2a]=0;
    CHECK(read()==before); // Shift+F7 never persists the ordinary set toggle
    CHECK(platform::pc_comparison_original()&&!platform::g_pc_triple_corrections&&platform::g_pc_wide_aspect==0.f&&platform::pc_configured_aspect()==5.25f);
    CHECK(fov(1)==.586f&&camera_controls_triples()&&camera_controls_label().find("Realistic FOV")!=std::string::npos);
    CHECK(camera_controls_comparison_notice().find("Original game view")!=std::string::npos&&camera_controls_comparison_notice().find("not enabled")!=std::string::npos);
    const auto shifted=[&]{desktop_keys()[0x36]=0x80;release(0x41);desktop_keys()[0x36]=0;};
    shifted();CHECK(platform::g_pc_screen_comparison==platform::PcScreenComparison::angle&&platform::g_pc_triple_corrections&&platform::g_pc_wide_aspect==5.25f&&read()==before);
    shifted();CHECK(platform::pc_comparison_original()&&read()==before); // unavailable Curve skipped
    // Resource readiness is an inert published record; no backend or device is initialized.
    platform::g_pc_curve_mesh_provider=[](unsigned,unsigned,platform::PcCurveMeshView&){return false;};
    platform::g_pc_curve_radius_mm=1500;platform::pc_curve_publish(platform::PcCurveMode::active);
    shifted();shifted();CHECK(platform::g_pc_screen_comparison==platform::PcScreenComparison::curve&&read()==before&&fov(1)==.586f);
    shifted();CHECK(platform::pc_comparison_original()&&read()==before);
    for(const unsigned mode:{18u,32u,19u,13u}){
        rt->mode_state.current=mode;poll();CHECK(platform::g_pc_screen_comparison==platform::PcScreenComparison::normal&&platform::g_pc_triple_corrections&&read()==before);
        rt->mode_state.current=16;poll();shifted();CHECK(platform::pc_comparison_original());
    }
    panel_open=true;poll();CHECK(!platform::pc_comparison_original()&&read()==before);panel_open=false;poll();
    shifted();CHECK(platform::pc_comparison_original());camera_controls_end_comparison();CHECK(!platform::pc_comparison_original()&&read()==before);
    shifted();release(0x41);CHECK(!platform::pc_comparison_original()&&fov(1)>1.1f&&camera_controls_label().find("Game FOV")!=std::string::npos);release(0x41);CHECK(fov(1)==.586f); // plain F7 restores first, then owns its usual saved toggle
    const auto after_plain=read();shifted();desktop_keys()[0x41]=0x80;poll();desktop_keys_clear();poll();CHECK(platform::pc_comparison_original()&&read()==after_plain);
    camera_controls_end_comparison();poll();platform::g_pc_curve_mesh_provider=nullptr;platform::g_pc_curve_radius_mm=0;platform::pc_curve_publish(platform::PcCurveMode::unavailable);
    // Ordinary settings updates end comparison even when publication fails.
    shifted();const auto before_fail=read();const auto publish_before=settings_publish();settings_publish()=[](const std::string&,const std::string&){return false;};camera_controls_zoom(1);settings_publish()=publish_before;
    CHECK(!platform::pc_comparison_original()&&read()==before_fail);
    CHECK(read()==before); // comparison never changed bytes; the two ordinary F7 toggles restore the saved choice

    desktop_keys()[0x4e]=0x80;poll();desktop_keys_clear();poll();CHECK(read()==before); // focus clear is not a release
    panel_open=true;release(0x4e);panel_open=false;poll();CHECK(read()==before);
    const auto real=settings_publish();settings_publish()=[](const std::string&,const std::string&){return false;};
    camera_controls_zoom(1.f);settings_publish()=real;
    CHECK(read()==before&&fov(1)==0.586f&&camera_controls_notice().find("not saved")!=std::string::npos);
    settings_publish()=[](const std::string&,const std::string&){return false;};camera_controls_set_fov(60.f);settings_publish()=real;
    CHECK(read()==before&&fov(1)==.586f&&camera_controls_notice().find("not saved")!=std::string::npos);
    platform::g_pc_triple_corrections=nullptr;platform::g_pc_wide_aspect=0;
    CHECK(fov(1)>1.1f); // single-screen uses this camera's Game offset, never the Realistic base
    const auto single=read();release(0x41);CHECK(read()==single);
    CHECK(read().rfind(original,0)==0); // controls, force and owner comments retained
    // Explicit legacy global choice seeds absent per-camera keys; explicit new choice wins.
    Ini legacy;legacy.load_text("[Triple]\nMatchGameFov = true\n[Camera]\nBumperRealistic = true\n",path);
    camera_controls_configure(legacy,false,0.586f);platform::g_pc_triple_corrections=[](const float*,float*){return false;};
    CHECK(fov(0)==0.586f&&fov(1)==1.1f&&fov(2)==1.1f);
    // Bonnet is a logical fourth view on the native bumper slot; the native cockpit remains distinct.
    CHECK(platform::camera_view_cycle()(0)==0&&fov(0)==1.1f&&camera_controls_label().find("Bonnet Camera")!=std::string::npos);
    CHECK(!platform::camera_body_hidden()());
    CHECK(camera_controls_notice().find("Bonnet Camera - Game FOV")!=std::string::npos);
    std::array<std::uint8_t,0xf00> car_bytes{};driving::Bytes car(car_bytes.data(),car_bytes.size());
    std::array<std::uint8_t,0x400> cam_bytes{};driving::Bytes camera(cam_bytes.data(),cam_bytes.size());
    for(unsigned i=0;i<16;++i)car.putf(0xb0+i*4,i%5==0?1.f:0.f);
    camera.putf(0xf8,5.f);camera.putf(0xbc,.1f);const auto untouched=cam_bytes;
    platform::CameraMountFit fit;fit.valid=true;fit.bonnet={0,.9f,1.2f};fit.bumper={0,.4f,2.2f};
    std::array<std::uint8_t,256> arena{};driving::PcMatrixStack stack{driving::Bytes(arena.data(),arena.size()),0,0,4};
    driving::pc_matrix_identity(stack);platform::camera_mount_adjust()(camera,car,nullptr,stack);
    CHECK(camera_controls_label().find("native placement")!=std::string::npos);
    const auto mount=[&]{driving::pc_matrix_identity(stack);platform::camera_mount_adjust()(camera,car,&fit,stack);
        driving::PcMatrix16 view{},inverse{};std::memcpy(view.data(),stack.current().data(),64);CHECK(driving::pc_d3dx_matrix_inverse(inverse,view));
        return driving::pc_d3dx_vec3_transform_coord({0,0,0},inverse);};
    const auto bonnet_eye=mount();CHECK(std::fabs(bonnet_eye[1]-.9f)<1e-5f&&std::fabs(bonnet_eye[2]+1.2f)<1e-5f&&car.u32(4)==0x4000u);
    CHECK(cam_bytes==untouched&&mount()==bonnet_eye); // render-only placement never compounds in authored camera state
    CHECK(platform::camera_view_cycle()(0)==1&&fov(1)==1.1f);
    CHECK(platform::camera_view_cycle()(1)==2&&platform::camera_view_cycle()(2)==0);
    (void)fov(0);const auto bumper_eye=mount();CHECK(std::fabs(bumper_eye[2]+2.2f)<1e-5f&&car.u32(4)==0u);
    CHECK(platform::camera_body_hidden()());
    // The actual consumer damps rotation only. Translation follows the displayed
    // car immediately; pause/replay cannot integrate an extra filter step.
    const auto set_car=[&](float degrees,float x){const float half=degrees*.008726646259971648f;
        driving::PcMatrix16 p{};p[12]=x;const auto m=camera_rotation_world({0,std::sin(half),0,std::cos(half)},p);for(unsigned i=0;i<16;++i)car.putf(0xb0+i*4,m[i]);return m;};
    const auto target_car=set_car(10,10);const auto rigid=driving::pc_d3dx_vec3_transform_coord({0,.4f,-2.2f},target_car);const auto lagged_eye=mount();
    CHECK(std::fabs(lagged_eye[0]-rigid[0])<1e-5f&&std::fabs(lagged_eye[2]-rigid[2])<1e-5f);
    driving::PcMatrix16 damped_view{},damped_world{};std::memcpy(damped_view.data(),stack.current().data(),64);CHECK(driving::pc_d3dx_matrix_inverse(damped_world,damped_view));
    const auto damped=camera_rotation_from_world(damped_world);CHECK(damped[1]>0.f&&damped[1]<std::sin(5.f*.017453292519943295f));
    driving::camera_fov_context().paused=true;const auto paused_car=set_car(15,20);const auto paused_eye=mount();const auto paused_rigid=driving::pc_d3dx_vec3_transform_coord({0,.4f,-2.2f},paused_car);
    driving::PcMatrix16 frozen_view{},frozen_world{};std::memcpy(frozen_view.data(),stack.current().data(),64);CHECK(driving::pc_d3dx_matrix_inverse(frozen_world,frozen_view));
    CHECK(std::abs(camera_rotation_dot(damped,camera_rotation_from_world(frozen_world)))>.999999f&&std::fabs(paused_eye[0]-paused_rigid[0])<1e-5f&&std::fabs(paused_eye[2]-paused_rigid[2])<1e-5f);
    driving::camera_fov_context().display_replay=true;for(unsigned i=0;i<5;++i)(void)mount();
    std::memcpy(frozen_view.data(),stack.current().data(),64);CHECK(driving::pc_d3dx_matrix_inverse(frozen_world,frozen_view)&&std::abs(camera_rotation_dot(damped,camera_rotation_from_world(frozen_world)))>.999999f);
    driving::camera_fov_context()={};const auto cut=set_car(60,0);(void)mount();std::memcpy(frozen_view.data(),stack.current().data(),64);
    CHECK(driving::pc_d3dx_matrix_inverse(frozen_world,frozen_view)&&std::abs(camera_rotation_dot(camera_rotation_from_world(cut),camera_rotation_from_world(frozen_world)))>.999999f);
    (void)set_car(0,0);(void)mount();
    // Cockpit uses its native eye and target rotation; chase is entirely unfiltered.
    (void)fov(1);(void)mount();
    CHECK(!platform::camera_body_hidden()());
    const auto native_view=[&](float degrees){driving::PcMatrix16 p{};p[12]=3;p[13]=4;p[14]=5;
        const float half=degrees*.008726646259971648f;driving::PcMatrix16 view{};CHECK(driving::pc_d3dx_matrix_inverse(view,camera_rotation_world({0,std::sin(half),0,std::cos(half)},p)));
        std::memcpy(stack.current().data(),view.data(),64);return view;};
    (void)native_view(10);platform::camera_mount_adjust()(camera,car,&fit,stack);std::memcpy(frozen_view.data(),stack.current().data(),64);
    CHECK(driving::pc_d3dx_matrix_inverse(frozen_world,frozen_view)&&frozen_world[12]>2.9999f&&frozen_world[12]<3.0001f&&camera_rotation_from_world(frozen_world)[1]>.0f&&camera_rotation_from_world(frozen_world)[1]<.087f);
    (void)fov(2);const auto chase_view=native_view(10);platform::camera_mount_adjust()(camera,car,&fit,stack);
    CHECK(std::memcmp(chase_view.data(),stack.current().data(),64)==0);
    Ini posed;posed.load_text("[Camera]\nBonnetRealistic=false\nCar0.BonnetGameSideOffset=0.2\nCar0.BonnetGameUpOffset=0.1\n",path);
    camera_controls_configure(posed,false,.586f);(void)platform::camera_view_cycle()(0);(void)fov(0);
    const auto tuned=mount();CHECK(std::fabs(tuned[0]-.2f)<1e-5f&&std::fabs(tuned[1]-1.f)<1e-5f);
    poll();const auto pose_comparison_bytes=read();const auto pose_comparison=camera_controls_pose();const auto pose_fov=fov(0);
    shifted();CHECK(platform::pc_comparison_original()&&camera_controls_pose()==pose_comparison&&fov(0)==pose_fov&&mount()==tuned&&!platform::camera_body_hidden()()&&read()==pose_comparison_bytes);
    shifted();CHECK(platform::g_pc_screen_comparison==platform::PcScreenComparison::angle&&camera_controls_pose()==pose_comparison&&mount()==tuned&&read()==pose_comparison_bytes);
    camera_controls_end_comparison();CHECK(camera_controls_pose()==pose_comparison&&mount()==tuned&&read()==pose_comparison_bytes);

    car.put8(0x11,1);CHECK(std::fabs(mount()[0])<1e-5f);car.put8(0x11,0);(void)mount();
    camera_controls_toggle_set();(void)fov(0);CHECK(std::fabs(mount()[0])<1e-5f);
    camera_controls_toggle_set();(void)fov(0);CHECK(std::fabs(mount()[0]-.2f)<1e-5f);
    camera_controls_reset();CHECK(std::fabs(mount()[0])<1e-5f&&std::fabs(mount()[1]-.9f)<1e-5f);
    CHECK(ini.load(path)&&ini.number("Camera","Car0.BonnetGameSideOffset",999.f)==0.f);
    // F6's commands use the exact keyboard action dispatch and persistence path.
    for(unsigned action=0;action<8;++action){
        camera_controls_reset();camera_controls_activate(action);const auto pose=camera_controls_pose();
        const unsigned axes[]{1,1,2,2,0,0,3,3};const float signs[]{1,-1,1,-1,-1,1,1,-1};
        for(unsigned axis=0;axis<4;++axis)CHECK(std::fabs(pose[axis]-(axis==axes[action]?signs[action]*camera_controls_step(action<6?0:1):0.f))<1e-6f);
    }
    const auto before_invalid=camera_controls_pose();camera_controls_activate(11);camera_controls_activate(~0u);CHECK(camera_controls_pose()==before_invalid);
    camera_controls_activate(10);for(const auto value:camera_controls_pose())CHECK(value==0.f);
    CHECK(fov(3)==1.1f&&driving::camera_fov_context().near_z==0.f); // native cinematic 3 is not logical Bonnet
    // The actual native view-switch branch reaches the desktop four-view cycle.
    (void)fov(1);camera.put8(0x34a,2);camera.put16(0x340,0);camera.put16(0x344,0);
    driving::camera_fov_context().cycle=platform::camera_view_cycle();
    driving::race_camera_view_switch_484a40(camera,0x40000u,0);CHECK(camera.u8(0x34a)==0);
    driving::race_camera_view_switch_484a40(camera,0x40000u,0);CHECK(camera.u8(0x34a)==0&&fov(0)==1.1f&&camera_controls_label().find("Bonnet")!=std::string::npos);
    driving::camera_fov_context()={};
    // Every family shortcut reaches a visible pose/FOV consumer; unrelated keys stay held.
    (void)fov(1);(void)platform::camera_view_cycle()(0);(void)fov(0);(void)mount();poll();camera_controls_reset();
    desktop_keys()[0x20]=0x80;
    release(0x48);CHECK(std::fabs(camera_controls_pose()[2]-.02f)<1e-6f); // 8 forward
    CHECK(camera_controls_notice().find("Bonnet Camera - Game FOV (Custom)")!=std::string::npos);
    release(0x50);CHECK(camera_controls_pose()[2]==0.f); // 2 back
    release(0x49);CHECK(std::fabs(camera_controls_pose()[1]-.02f)<1e-6f); // 9 up
    release(0x51);CHECK(camera_controls_pose()[1]==0.f); // 3 down
    release(0x4b);CHECK(std::fabs(camera_controls_pose()[0]+.02f)<1e-6f); // 4 left
    release(0x4d);CHECK(camera_controls_pose()[0]==0.f); // 6 right
    release(0x47);(void)mount();CHECK(camera_controls_pose()[3]==1.f&&driving::pc_inverse_vector(stack.current(),{0,0,-1}).y<0.f); // 7 looks down
    release(0x4f);CHECK(camera_controls_pose()[3]==0.f); // 1 looks up
    release(0x48);release(0x4e);release(0x4c);CHECK((camera_controls_pose()==std::array<float,4>{}&&fov(0)==1.1f));
    camera_controls_set_key(2,"F9");poll();const auto rebound=read();release(0x48);CHECK(read()==rebound);release(0x43);CHECK(camera_controls_pose()[2]>.019f);
    const auto bound=read();camera_controls_set_key(2,"F7");CHECK(read()==bound&&camera_controls_key_names()[2]=="F9");
    camera_controls_set_key(2,"NUMPAD9");CHECK(read()==bound); // conflicts with up
    camera_controls_set_step(0,.04f);poll();release(0x43);CHECK(camera_controls_pose()[2]>.059f);
    camera_controls_default_steps();CHECK(camera_controls_step(0)==.02f&&camera_controls_step(1)==1.f&&camera_controls_step(2)==2.f);
    const auto pose_before_defaults=camera_controls_pose();camera_controls_default_keys();poll();
    CHECK(camera_controls_key_names()==CameraKeys{}.names&&camera_controls_pose()==pose_before_defaults);
    const auto active_fov=camera_controls_fov();camera_controls_set_fov(70.f);
    CHECK(camera_controls_pose()==pose_before_defaults&&std::fabs(camera_controls_fov().current-70.f)<1e-4f);
    (void)platform::camera_fov_adjust()(0,1.2f);CHECK(std::fabs(camera_controls_fov().offset-(70.f-active_fov.calculated))<1e-4f&&std::fabs(camera_controls_fov().current-(70.f+.1f*57.295779513f))<1e-4f);
    (void)fov(0);CHECK(std::fabs(camera_controls_fov().current-70.f)<1e-4f); // dynamic game base moves, saved offset never compounds
    camera_controls_reset();
    // Migrate only complete untouched historical sets; an explicit edit saves the
    // migrated set atomically, so a reload cannot produce a mixture of old/new defaults.
    std::string old_text=original+"[Camera]\n";
    const char* old_keys[]{"Keypad8","Keypad2","Keypad9","Keypad7","Keypad4","Keypad6","Keypad1","Keypad3","KeypadPlus","KeypadMinus","Keypad0"};
    for(unsigned i=0;i<11;++i)old_text+=std::string(CameraKeyFields[i])+"="+old_keys[i]+"\n";
    {std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<old_text;}
    CHECK(ini.load(path));camera_controls_configure(ini,false,.586f);
    CHECK(camera_controls_keys_migrated()&&camera_controls_key_names()==CameraKeys{}.names&&read()==old_text);
    camera_controls_set_key(2,"F10");CHECK(ini.load(path));camera_controls_configure(ini,false,.586f);
    CHECK(!camera_controls_keys_migrated()&&camera_controls_key_names()[2]=="F10"&&camera_controls_key_names()[0]=="NUMPAD9");
    auto custom_text=old_text;const auto left=custom_text.find("KeyLeft=Keypad4");custom_text.replace(left,std::string("KeyLeft=Keypad4").size(),"KeyLeft=F9");
    {std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<custom_text;}
    CHECK(ini.load(path));camera_controls_configure(ini,false,.586f);
    CHECK(!camera_controls_keys_migrated()&&camera_controls_key_names()[4]=="F9"&&camera_controls_key_names()[0]=="NUMPAD8"&&read()==custom_text);
    camera_controls_default_keys();poll();camera_controls_reset();
    // Last-camera memory is independent of framing/pose. Absence preserves the
    // game's license choice, and loading/restoring never rewrites the file.
    CHECK(platform::camera_view_restore()(2)==2);
    (void)fov(1);CHECK(platform::camera_view_cycle()(0)==0);(void)fov(0);
    CHECK(ini.load(path)&&ini.text("Camera","LastCamera")=="Bonnet");
    const auto saved_camera=read();camera_controls_configure(ini,false,.586f);
    CHECK(platform::camera_view_restore()(2)==0&&fov(0)==1.1f&&camera_controls_label().find("Bonnet Camera")!=std::string::npos&&read()==saved_camera);
    const auto publish=settings_publish();settings_publish()=[](const std::string&,const std::string&){return false;};
    CHECK(platform::camera_view_cycle()(0)==1);(void)fov(1);settings_publish()=publish;
    CHECK(read()==saved_camera&&camera_controls_notice().find("last camera not saved")!=std::string::npos);
    CHECK(platform::camera_view_restore()(2)==0&&fov(0)==1.1f);
    if(argc==2){
        CHECK(platform::exe_image_autoload());
        platform::RetailAssetStore store;std::string error;CHECK(platform::retail_asset_store_open(store,argv[1],&error));
        std::vector<std::uint8_t> bytes;
        CHECK(platform::retail_asset_read_relative_inflated(store,platform::VehicleResourcePaths[0],bytes,128u<<20,&error));
        const auto expected=platform::camera_mount_fit(assets::inspect_pc_pmt(assets::ByteView(bytes)),0);
        platform::PcSoftD3D9Device soft(640,480);platform::PcSceneRenderer scene(soft);
        CHECK(scene.load_bank(platform::VehicleResourceIds[0],std::move(bytes),error));
        scene.vehicle_camera=[&](driving::Bytes& vehicle,driving::Bytes& cam){vehicle=car;cam=camera;return true;};
        scene.set_screen(1280,720);CHECK(scene.invoke(0x484ee0u,0,385,32,0)&&scene.camera_unanswered==0);
        { // A same-callback profile change must refresh retained culling without a new camera projection.
            const auto saved_provider=platform::g_pc_triple_corrections;
            platform::g_pc_triple_corrections=profile_corrections;platform::pc_triple_profile_changed();
            CHECK(scene.render_view().panels.valid&&profile_calls==1);
            const auto initial=scene.render_view().panels.panels[1].planes;const auto native_projection=soft.transform_proj;
            CHECK(profile_calls==1);
            profile_scale=.5f;platform::pc_triple_profile_changed();(void)scene.queue_context();
            CHECK(profile_calls==2&&scene.render_view().panels.valid&&scene.render_view().panels.panels[1].planes!=initial);
            CHECK(soft.transform_proj==native_projection&&profile_calls==2);
            profile_refused=true;platform::pc_triple_profile_changed();CHECK(!scene.render_view().panels.valid&&profile_calls==3);
            (void)scene.queue_context();CHECK(profile_calls==3);
            platform::g_pc_triple_corrections=nullptr;CHECK(!scene.render_view().panels.valid&&profile_calls==3);
            profile_refused=false;platform::g_pc_triple_corrections=profile_corrections;CHECK(scene.render_view().panels.valid&&profile_calls==4);
            profile_scale=1;platform::pc_triple_profile_changed();CHECK(scene.render_view().panels.panels[1].planes==initial&&profile_calls==5);
            CHECK(soft.transform_proj==native_projection);
            // The actual sun near-plane scope saves/restores the original cache as well as its planes.
            driving::PcEventControlState events{};events.slots[387].flags=2;
            scene.course_effects=[](unsigned& work,unsigned& flags){work=0x780280;flags=2;};
            bool visited=false;scene.leaf_hook=[&](unsigned pc,unsigned){if(pc==0x40cbc0){visited=true;profile_scale=.75f;platform::pc_triple_profile_changed();(void)scene.queue_context();}return true;};
            const bool old_z=platform::fix_znear();platform::fix_znear()=true;scene.render_frame(events,16);platform::fix_znear()=old_z;
            CHECK(visited&&scene.render_view().panels.valid&&scene.render_view().panels.panels[1].planes!=initial);
            profile_scale=1;platform::pc_triple_profile_changed();CHECK(scene.render_view().panels.panels[1].planes==initial);
            scene.course_effects=nullptr;scene.leaf_hook=nullptr;
            platform::g_pc_triple_corrections=saved_provider;platform::pc_triple_profile_changed();
        }
        scene.pause_780248=1;camera.put8(0x34a,0);(void)fov(1); // native bumper, no Bonnet flag
        const auto base_eye=camera.f32(0xf8),base_near=camera.f32(0xbc);
        enhancements::set_display_rate(120);enhancements::display_before_tick();
        CHECK(scene.invoke(0x485fe0u,0,385,16,0)&&scene.camera_unanswered==0);
        const auto rendered=scene.render_view();
        CHECK(std::fabs(rendered.eye[1]-expected.bumper[1])<1e-5f&&std::fabs(rendered.eye[2]+expected.bumper[2])<1e-5f);
        CHECK(rendered.frustum_95bf40[4]==0.05f&&camera.f32(0xbc)==base_near&&camera.f32(0xf8)==base_eye);
        enhancements::display_after_ticks(.5f,0);
        CHECK(scene.render_view().view_95d860==rendered.view_95d860&&scene.render_view().frustum_95bf40==rendered.frustum_95bf40);
        enhancements::set_display_rate(60);
        for(unsigned i=0;i<3;++i)CHECK(scene.invoke(0x485fe0u,0,385,16,0)&&scene.render_view().view_95d860==rendered.view_95d860&&camera.f32(0xf8)==base_eye);
        // Restore at the actual native single-player START preference point.
        scene.pause_780248=0;camera.put8(0x34a,0x1e);
        scene.race_camera_inputs=[](driving::PcRaceCameraInputs& in){in.timer_8367bc=0x12bu;in.byte_7c24b8=2;return true;};
        CHECK(scene.invoke(0x485fe0u,0,385,13,0)&&scene.camera_unanswered==0&&camera.u8(0x34a)==0);
        scene.race_camera_inputs=[](driving::PcRaceCameraInputs& in){in.timer_8367bc=0x12cu;in.byte_7c24b8=2;return true;};
        CHECK(scene.invoke(0x485fe0u,0,385,13,0)&&scene.camera_unanswered==0&&camera_controls_label().find("Bonnet Camera")!=std::string::npos);
        CHECK(std::fabs(scene.render_view().eye[2]+expected.bonnet[2])<1e-5f&&read()==saved_camera);
        // A profile without LastCamera keeps its native chase default, not the previous session's Bonnet flag.
        Ini fresh;fresh.load_text("[Camera]\n",path);camera_controls_configure(fresh,false,.586f);
        camera.put8(0x34a,0x1e);scene.race_camera_inputs=[](driving::PcRaceCameraInputs& in){in.timer_8367bc=0x12bu;in.byte_7c24b8=2;return true;};
        CHECK(scene.invoke(0x485fe0u,0,385,13,0)&&camera.u8(0x34a)==2&&read()==saved_camera);
        // The LAN START variant reaches the same restore seam; no LAN/game process is started.
        CHECK(ini.load(path));camera_controls_configure(ini,false,.586f);
        driving::PcRaceCameraInputs lan{};lan.car=car;lan.timer_8367bc=0x167u;lan.byte_7c24b8=2;
        driving::PcCameraDevice lan_device;camera.put8(0x34a,0x1e);
        driving::camera_fov_context().restore=platform::camera_view_restore();
        driving::race_camera_start_lan_485300(camera,stack,lan_device,{1280,720},{0,0,0,1.f},lan);
        CHECK(camera.u8(0x34a)==0&&fov(0)==1.1f&&read()==saved_camera);driving::camera_fov_context()={};
    }
    CHECK(std::filesystem::remove(path)&&std::filesystem::remove(dir));
    platform::camera_fov_adjust()=nullptr;platform::camera_mount_adjust()=nullptr;platform::camera_view_cycle()=nullptr;platform::camera_view_restore()=nullptr;platform::camera_body_hidden()=nullptr;
    platform::g_pc_triple_corrections=nullptr;driving::camera_fov_context()={};
    std::printf("camera controls: %u checks\n",checks);
}
