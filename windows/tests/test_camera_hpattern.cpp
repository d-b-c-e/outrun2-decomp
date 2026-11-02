// Scripted shim provider -> real adapter -> camera filter -> native 453BB0
// arbitration/switch records -> actual manual transmission. No device/window/game.
#include "camera_controls.hpp"
#include "desktop_keyboard.hpp"
#include "desktop_settings.hpp"
#include "outrun_adapter.hpp"
#include "shim_fake_provider.hpp"
#include "native_window.hpp"
#include "platform/native_runtime.hpp"
#include "platform/pc_input_devices.hpp"
#include "platform/pc_scene_renderer.hpp"
#include "driving/pc_transmission.hpp"
#include "system/exe_image.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace {bool panel_open;unsigned checks;}
namespace outrun::desktop {bool desktop_panel_open(){return panel_open;}}
namespace outrun::xbox_runtime {InputFilter& input_filter(){static InputFilter value=nullptr;return value;}}
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("camera H-pattern line "+std::to_string(__LINE__)+": " #x);}while(0)
struct Fixture {
    std::filesystem::path dir=std::filesystem::current_path()/("camera-hpattern-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    bool owned{};
    Fixture(){owned=std::filesystem::create_directory(dir);CHECK(owned);std::ofstream out(dir/"fixture.ini");out<<"[Camera]\nBumperRealistic=false\nBonnetRealistic=false\n[FFB]\nOutput=none\n";}
    ~Fixture(){if(owned){std::error_code ec;std::filesystem::remove(dir/"fixture.ini",ec);std::filesystem::remove(dir,ec);}}
};
int main(){try{
    using namespace outrun;using namespace desktop;namespace sh=dbce::shim;namespace fk=sh::fake;
    if(!platform::exe_image_autoload()){std::printf("SKIP: set OR2_EXE to the game's verified cache for the native keyboard path\n");return 77;}
    Fixture fixture;Ini ini;CHECK(ini.load((fixture.dir/"fixture.ini").string()));camera_controls_configure(ini,false,.586f);camera_controls_wrap_input_filter();
    platform::g_pc_triple_corrections=[](const float*,float*){return false;};(void)platform::camera_fov_adjust()(0,1.1f);
    auto rt=std::make_unique<platform::NativeRuntimeContext>();rt->mode_state.current=16;
    platform::native_pc_keyboard_source()=[](std::uint8_t* keys){std::memcpy(keys,desktop_keys().data(),256);};
    CHECK(platform::native_pc_input_create_403de0(*rt)&&platform::native_pc_input_profile_4040f0(*rt));
    const std::string wheel="{d71b8350-61b7-11f1-8001-444553540000}",shift="{d9b1d890-6f3f-11f1-8002-444553540000}",product="{0006346e-0000-0000-0000-504944564944}";
    fk::Provider provider;fk::Device w;w.listed={wheel,product,"Scripted wheel"};w.caps=fk::wheel_caps();w.state.axes[0]=32768;const auto wi=provider.add(w);
    fk::Device s;s.listed={shift,product,"Scripted shifter"};s.caps=fk::wheel_caps();const auto si=provider.add(s);
    std::string profile="[Controls]\nSchema=1\nProfile=Fixture\nRevision=1\nTransmission=H-pattern\nsteer = axis 0 dev="+wheel+" prod="+product+" range=0..65535 rest=32768 travel=+1\nthrottle = axis 2 dev="+wheel+" prod="+product+" range=0..65535 rest=0 travel=+1\n";
    for(unsigned i=1;i<=6;++i)profile+="gear"+std::to_string(i)+" = button "+std::to_string(i-1)+" dev="+shift+" prod="+product+"\n";
    sh::CoreConfig config;config.capability=outrun_capability();sh::Core core(provider,config);core.set_profile(sh::read_profile(profile));Adapter adapter("H-pattern");
    std::array<std::uint8_t,0x1000> car_bytes{},work_bytes{};std::array<std::uint8_t,0x1700> param_bytes{};
    driving::Bytes car(car_bytes.data(),car_bytes.size()),work(work_bytes.data(),work_bytes.size()),params(param_bytes.data(),param_bytes.size());
    car.put32(0x208,3);params.put32(0x10a0,6);params.putf(0xb94,1.f);params.putf(0x1644,9000.f);
    const auto tick=[&]{
        core.service_devices();provider.advance(16667);const auto& frame=core.tick();GameView view;view.mode=rt->mode_state.current;view.car=true;view.gear=car.u32(0x208);
        Keyboard kb;kb.any=desktop_any_driving_key();platform::NativeInputState out;adapter.apply(frame,view,out,&kb);
        xbox_runtime::input_filter()(out,rt.get());rt->input_state=out;platform::native_input_step_453bb0(*rt);
        const auto& buttons=rt->race.car_world.switch_7d6770;
        if(rt->mode_state.current==16)driving::manual_transmission(car,work,params,false,(buttons.pressed&0x80u)!=0,(buttons.pressed&0x40u)!=0);
    };
    for(unsigned i=0;i<8;++i)tick();provider.at(si).state.buttons[2]=0x80;provider.at(wi).state.axes[0]=45000;
    for(unsigned i=0;i<4;++i)tick();CHECK(car.u32(0x208)==3&&adapter.gearbox().state()==HPattern::State::AtTarget);
    const auto steering=rt->race.car_world.analog_7d6810.current[0];CHECK(steering!=0&&!rt->input_state.pc_overlay_yield);
    desktop_keys()[0x41]=0x80;tick();
    std::printf("F7 held with gate3: keyboard yield %d, native keyboard record %d, consumed steer %d, gear %u\n",int(rt->input_state.pc_overlay_yield),int(rt->input_state.pc_record_used),rt->race.car_world.analog_7d6810.current[0],car.u32(0x208));
    CHECK(!rt->input_state.pc_overlay_yield&&!rt->input_state.pc_record_used&&rt->race.car_world.analog_7d6810.current[0]==steering&&car.u32(0x208)==3);
    desktop_keys()[0x41]=0;tick();CHECK(camera_controls_label().find("Realistic FOV")!=std::string::npos&&car.u32(0x208)==3);
    const auto base=platform::camera_fov_adjust()(0,1.1f);desktop_keys()[0x4e]=0x80;tick();desktop_keys()[0x4e]=0;tick();
    CHECK(platform::camera_fov_adjust()(0,1.1f)<base&&car.u32(0x208)==3&&!rt->input_state.pc_overlay_yield);
    // A held gate still changes gear through the actual pressed bits and transmission.
    provider.at(si).state.buttons[2]=0;provider.at(si).state.buttons[3]=0x80;
    for(unsigned i=0;i<5;++i)tick();CHECK(car.u32(0x208)==4&&adapter.gearbox().state()==HPattern::State::AtTarget);
    for(unsigned i=0;i<30;++i)tick();CHECK(car.u32(0x208)==4&&(rt->race.car_world.switch_7d6770.pressed&0xc0u)==0);
    provider.at(si).state.buttons[3]=0;provider.at(si).state.buttons[1]=0x80;
    for(unsigned i=0;i<10;++i)tick();CHECK(car.u32(0x208)==2&&adapter.gearbox().state()==HPattern::State::AtTarget);
    // Paused held gates spend no retry or shift, then resume from the observed gear.
    rt->mode_state.current=18;provider.at(si).state.buttons[1]=0;provider.at(si).state.buttons[4]=0x80;
    for(unsigned i=0;i<40;++i)tick();CHECK(car.u32(0x208)==2&&adapter.gearbox().state()!=HPattern::State::Stopped);
    rt->mode_state.current=16;tick();CHECK(car.u32(0x208)==2);
    for(unsigned i=0;i<12;++i)tick();CHECK(car.u32(0x208)==5&&adapter.gearbox().state()==HPattern::State::AtTarget);
    // Every default camera key and dedicated desktop control stays out of the
    // handover request, without clearing the raw keyboard state.
    for(const unsigned key:{0x47u,0x48u,0x49u,0x4bu,0x4cu,0x4du,0x4fu,0x50u,0x51u,0x4eu,0x4au,0x40u,0x42u,0x57u}){
        desktop_keys()[key]=0x80;CHECK(desktop_any_key()&&!desktop_any_driving_key()&&desktop_key(key));desktop_keys()[key]=0;}
    // A custom binding owns its new key; restoring defaults relinquishes it.
    camera_controls_set_key(9,"F9");desktop_keys()[0x43]=0x80;tick();desktop_keys()[0x43]=0;tick();CHECK(!rt->input_state.pc_overlay_yield&&car.u32(0x208)==5);
    desktop_keys()[0x4e]=0x80;CHECK(desktop_any_driving_key());desktop_keys()[0x4e]=0;camera_controls_default_keys();
    desktop_keys()[0x43]=0x80;CHECK(desktop_any_driving_key());desktop_keys()[0x43]=0;
    // Ordinary keyboard driving is unchanged and a genuine wheel movement
    // takes control back, even with a shifter resting in its gate.
    desktop_keys()[0xc8]=0x80;tick();CHECK(rt->input_state.pc_overlay_yield&&rt->input_state.pc_record_used);
    desktop_keys()[0xc8]=0;provider.at(wi).state.axes[0]=65000;tick();CHECK(!rt->input_state.pc_overlay_yield&&!rt->input_state.pc_record_used&&car.u32(0x208)==5);
    desktop_keys_clear();platform::native_pc_keyboard_source()=nullptr;platform::g_pc_triple_corrections=nullptr;
    std::printf("camera H-pattern: %u checks\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
