// Real desktop camera controller + INI writer, inert keyboard/runtime records only.
// The window input-chain boundary and panel-open flag are stand-ins; no window/device/game is created.
#include "camera_controls.hpp"
#include "desktop_settings.hpp"
#include "desktop_keyboard.hpp"
#include "native_window.hpp"
#include "platform/native_runtime.hpp"
#include "platform/pc_scene_renderer.hpp"
#include "platform/pc_screen.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cmath>
namespace {bool panel_open;unsigned checks;}
namespace outrun::desktop {bool desktop_panel_open(){return panel_open;}}
namespace outrun::xbox_runtime {InputFilter& input_filter(){static InputFilter filter=nullptr;return filter;}}
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"camera controls line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    using namespace outrun;using namespace desktop;
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
    CHECK(ini.load(path)&&ini.number("Camera","CockpitRealisticFovOffset",0.f)==-1.f);
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
    CHECK(std::filesystem::remove(path)&&std::filesystem::remove(dir));
    platform::camera_fov_adjust()=nullptr;platform::g_pc_triple_corrections=nullptr;
    std::printf("camera controls: %u checks\n",checks);
}
