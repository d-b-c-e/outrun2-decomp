// Real desktop camera controller + INI writer, inert keyboard/runtime records only.
// The window input-chain boundary and panel-open flag are stand-ins; no window/device/game is created.
#include "camera_controls.hpp"
#include "camera_keys.hpp"
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
namespace {bool panel_open;unsigned checks;}
namespace outrun::desktop {bool desktop_panel_open(){return panel_open;}}
namespace outrun::xbox_runtime {InputFilter& input_filter(){static InputFilter filter=nullptr;return filter;}}
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"camera controls line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(int argc,char** argv){
    using namespace outrun;using namespace desktop;
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
    CHECK(ini.load(path)&&ini.number("Camera","CockpitRealisticFovOffset",0.f)==-2.f);
    const auto narrowed=fov(1);release(0x41);CHECK(fov(1)==1.1f); // F7 changes only this camera's set
    CHECK(fov(0)==0.586f&&fov(2)==1.1f);
    (void)fov(1);release(0x4a);CHECK(fov(1)>1.1f);
    release(0x41);CHECK(fov(1)==narrowed);release(0x4c);CHECK(fov(1)==0.586f);
    CHECK(camera_controls_label().find("(Custom)")==std::string::npos&&camera_controls_notice().find("calculated FOV restored")!=std::string::npos);
    const auto before=read();desktop_keys()[0x2a]=0x80;release(0x41);desktop_keys()[0x2a]=0;
    CHECK(read()==before); // Shift+F7 must never persist the ordinary set toggle
    desktop_keys()[0x4e]=0x80;poll();desktop_keys_clear();poll();CHECK(read()==before); // focus clear is not a release
    panel_open=true;release(0x4e);panel_open=false;poll();CHECK(read()==before);
    const auto real=settings_publish();settings_publish()=[](const std::string&,const std::string&){return false;};
    camera_controls_zoom(1.f);settings_publish()=real;
    CHECK(read()==before&&fov(1)==0.586f&&camera_controls_notice().find("not saved")!=std::string::npos);
    platform::g_pc_triple_corrections=nullptr;
    CHECK(fov(1)>1.1f); // single-screen uses this camera's Game offset, never the Realistic base
    const auto single=read();release(0x41);CHECK(read()==single);
    CHECK(read().rfind(original,0)==0); // controls, force and owner comments retained
    // Explicit legacy global choice seeds absent per-camera keys; explicit new choice wins.
    Ini legacy;legacy.load_text("[Triple]\nMatchGameFov = true\n[Camera]\nBumperRealistic = true\n",path);
    camera_controls_configure(legacy,false,0.586f);platform::g_pc_triple_corrections=[](const float*,float*){return false;};
    CHECK(fov(0)==0.586f&&fov(1)==1.1f&&fov(2)==1.1f);
    // Bonnet is a logical fourth view on the native bumper slot; the native cockpit remains distinct.
    CHECK(platform::camera_view_cycle()(0)==0&&fov(0)==1.1f&&camera_controls_label().find("Bonnet Camera")!=std::string::npos);
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
    Ini posed;posed.load_text("[Camera]\nBonnetRealistic=false\nCar0.BonnetGameSideOffset=0.2\nCar0.BonnetGameUpOffset=0.1\n",path);
    camera_controls_configure(posed,false,.586f);(void)platform::camera_view_cycle()(0);(void)fov(0);
    const auto tuned=mount();CHECK(std::fabs(tuned[0]-.2f)<1e-5f&&std::fabs(tuned[1]-1.f)<1e-5f);
    car.put8(0x11,1);CHECK(std::fabs(mount()[0])<1e-5f);car.put8(0x11,0);(void)mount();
    camera_controls_toggle_set();(void)fov(0);CHECK(std::fabs(mount()[0])<1e-5f);
    camera_controls_toggle_set();(void)fov(0);CHECK(std::fabs(mount()[0]-.2f)<1e-5f);
    camera_controls_reset();CHECK(std::fabs(mount()[0])<1e-5f&&std::fabs(mount()[1]-.9f)<1e-5f);
    CHECK(ini.load(path)&&ini.number("Camera","Car0.BonnetGameSideOffset",999.f)==0.f);
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
    platform::camera_fov_adjust()=nullptr;platform::camera_mount_adjust()=nullptr;platform::camera_view_cycle()=nullptr;platform::camera_view_restore()=nullptr;
    platform::g_pc_triple_corrections=nullptr;driving::camera_fov_context()={};
    std::printf("camera controls: %u checks\n",checks);
}
