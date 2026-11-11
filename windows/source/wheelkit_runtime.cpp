// The Wheelkit runtime in OutRunDesktop (wheelkit_runtime.hpp): the loader and the call sites.
#include "wheelkit_runtime.hpp"
#include "wheelkit.h"
#include "camera_controls.hpp"
#include "desktop_keyboard.hpp"
#include "desktop_settings.hpp"
#include "hpattern.hpp"
#include "letterbox.hpp"
#include "native_window.hpp"
#include "telemetry_forza.hpp"
#include "platform/native_runtime.hpp"
#include "platform/runtime_input.hpp"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
namespace outrun::desktop {
namespace {
struct Api {
#define X(ret,name,args) ret (WK_CALL *name) args=nullptr;
    WK_FUNCTIONS(X)
#undef X
} wk;
const char kDescriptor[]=
#include "wheelkit_panel.inc"
;
// The rows the game saves itself: one key of OutRunDesktop.ini each. values: the saved text of each choice ("|"),
// "%" for a percent saved as a fraction, "" for a toggle (true / false).
struct Row { const char *id,*section,*key,*values; };
constexpr Row Rows[]{
    {"controls.sensitivity","Wheel","SteeringSensitivity","%"},{"controls.deadzone","Wheel","SteeringDeadZone","%"},
    {"controls.manual","Misc","DefaultManualTransmission",""},{"controls.horn","Audio","AllowHorn",""},
    {"display.screens","Triple","Screens","Off|Surround|Separate monitors"},{"display.latency","Window","FrameLatency","0|1|2|3"},
    {"display.ui-scaling","Graphics","UIScalingMode","0|1|2"},{"display.letterbox","Graphics","UILetterboxing","0|1|2"},
    {"display.views","Triple","Views","Planar|Angled"},{"display.borderless","Window","WindowedBorderless",""},
    {"display.cursor","Window","WindowedHideMouseCursor",""},{"display.aniso","Graphics","AnisotropicFiltering","0|2|4|8|16"},
    {"display.reflection","Graphics","ReflectionResolution","128|256|512|1024|2048"},{"display.lods","Graphics","DisableVehicleLODs",""},
    {"display.culling","Graphics","DisableStageCulling",""},{"display.transparency","Graphics","TransparencySupersampling",""},
    {"display.scene-textures","Graphics","SceneTextureReplacement",""},{"display.ui-textures","Graphics","UITextureReplacement",""},
    {"telemetry.rpm","Telemetry","EngineRpm","Synthetic|Game"}};
// The game's buttons, by the contract action that drives each.
enum Button : unsigned { Confirm,Back,Start,ShiftUp,ShiftDown,Camera,NavUp,NavDown,NavLeft,NavRight,Horn,ButtonCount };
constexpr int ButtonActions[ButtonCount]{WK_ACTION_CONFIRM,WK_ACTION_BACK,WK_ACTION_START,WK_ACTION_SHIFT_UP,WK_ACTION_SHIFT_DOWN,
    WK_ACTION_CAMERA,WK_ACTION_NAV_UP,WK_ACTION_NAV_DOWN,WK_ACTION_NAV_LEFT,WK_ACTION_NAV_RIGHT,WK_ACTION_HORN};
struct State {
    bool on{},attached{},was_open{},game_rpm{};
    xbox_runtime::InputFilter inner{};
    WheelSteering steering;
    HPattern gearbox;
    std::chrono::steady_clock::time_point t0;
    bool keyboard_drives{};std::int32_t keyboard_anchor{};
    std::uint32_t previous{};
    int camera_index[11]{};
    std::string camera_note;
};
State s;
void WK_CALL log_line(const char* line,void*){std::printf("%s\n",line);std::fflush(stdout);}
std::string piece(const char* values,int n){   // the n-th "|"-separated value
    std::string v(values);std::size_t a=0;
    for(int i=0;i<n;++i){a=v.find('|',a);if(a==std::string::npos)return {};++a;}
    return v.substr(a,v.find('|',a)-a);
}
void show_values(const Ini& ini){
    for(const auto& r:Rows){
        const auto text=ini.text(r.section,r.key);if(text.empty())continue;
        if(!*r.values)wk.wk_panel_set_toggle(r.id,ini.flag(r.section,r.key,false));
        else if(r.values[0]=='%')wk.wk_panel_set_number(r.id,double(ini.number(r.section,r.key,0.f))*100.0);
        else for(int i=0;!piece(r.values,i).empty();++i)if(_stricmp(piece(r.values,i).c_str(),text.c_str())==0)wk.wk_panel_set_choice(r.id,i);
    }
    const char* profile=wk.wk_input_profile();
    wk.wk_panel_set_text("controls.profile",*profile?profile:"No profile from Wheelkit: the keyboard and gamepad work",*profile?WK_TONE_GOOD:WK_TONE_WARNING);
    wk.wk_panel_set_text("help.runtime",wk.wk_runtime_version(),WK_TONE_NORMAL);
}
void panel_events(){
    wk_panel_event e{};e.size=sizeof e;
    while(wk.wk_panel_poll_event(&e)){
        for(const auto& r:Rows){
            if(e.type!=WK_PANEL_CHANGED||std::strcmp(e.id,r.id)!=0)continue;
            char number[32];std::snprintf(number,sizeof number,"%g",e.number/100.0);
            const std::string value=!*r.values?(e.toggle?"true":"false"):r.values[0]=='%'?std::string(number):piece(r.values,e.choice);
            const bool saved=!value.empty()&&wk.wk_settings_set(r.section,r.key,value.c_str());
            wk.wk_panel_toast(saved?"Saved":"Not saved",saved?"It applies at the next start":wk.wk_last_error(),2500);
        }
        e=wk_panel_event{};e.size=sizeof e;
    }
}
// The present overlay: the UILetterboxing bars, then the runtime's panel on the game's own device and back buffer.
void overlay(void* device,void* context,void* view,unsigned width,unsigned height){
    letterbox_draw(static_cast<ID3D11DeviceContext*>(context),static_cast<ID3D11RenderTargetView*>(view),width,height);
    if(!s.on)return;
    if(!s.attached&&device)s.attached=wk.wk_panel_d3d11_attach(device,xbox_runtime::native_window().hwnd)!=0;
    if(s.attached)wk.wk_panel_d3d11_frame(view);
}
void telemetry(const platform::NativeRuntimeContext* rt){
    wk_car c{};c.size=sizeof c;
    c.rpm_max=TelemetryMaxRpm;c.rpm_idle=c.rpm=TelemetryIdleRpm;c.gear=1;
    const std::uint32_t mode=rt?rt->mode_state.current:0u;
    if(rt&&telemetry_in_gameplay(mode)){
        const auto& cs=rt->event_function36.car_select;
        const auto car=telemetry_car(cs.car_799d18.data(),cs.car_799d18.size(),cs.parameters.data(),cs.parameters.size());
        if(!std::isfinite(car.hud_kmh_1f8)||car.hud_kmh_1f8<0.f)return;   // never a confident zero
        c.race_on=1;c.speed=car.hud_kmh_1f8/3.6f;c.gear=int(car.gear_208);
        if(!(s.game_rpm&&telemetry_game_rpm(car,c.rpm,c.rpm_idle,c.rpm_max)))c.rpm=telemetry_synthetic_rpm(car);
        if(mode==16u){const auto& a=rt->race.car_world.analog_7d6810.current;   // what the game consumed
            c.steer=float(std::clamp(a[0],-127,127))/127.f;c.throttle=float(std::clamp(a[1],0,255))/255.f;c.brake=float(std::clamp(a[2],0,255))/255.f;}
    }
    wk.wk_telemetry_frame(&c);
}
void filter(platform::NativeInputState& out,const platform::NativeRuntimeContext* rt){
    if(s.inner)s.inner(out,rt);
    // The keyboard reaches the race only through the PC layer (406FA0), set only for a connected pad otherwise.
    out.pc_input_layer=true;
    letterbox_note(rt);
    if(!s.on)return;
    panel_events();
    telemetry(rt);
    wk_input in{};in.size=sizeof in;
    if(!wk.wk_input_poll(&in))return;
    const bool open=(in.flags&WK_INPUT_PANEL)!=0;
    if(open!=s.was_open){desktop_keys_clear();s.was_open=open;}
    if(open){   // the panel has the input: a neutral sample, the presence flags kept
        camera_controls_end_comparison();
        const bool connected=out.connected;out=platform::NativeInputState{};out.connected=connected;out.pc_input_layer=true;return;
    }
    const auto camera_note=camera_controls_notice();   // a camera change is a toast on the runtime's panel
    if(camera_note!=s.camera_note){s.camera_note=camera_note;if(!camera_note.empty())wk.wk_panel_toast(camera_note.c_str(),nullptr,1500);}
    const std::uint32_t mode=rt?rt->mode_state.current:0u;
    const bool race=mode==16u||mode==18u;
    // Outside the race the menus read these edges: the game's own default menu keys (61E678) count as the buttons.
    std::uint32_t held=0;
    if(!race){if(desktop_key(0x1c))held|=1u<<Confirm;if(desktop_key(0x01))held|=1u<<Back;if(desktop_key(0xc8))held|=1u<<NavUp;
              if(desktop_key(0xd0))held|=1u<<NavDown;if(desktop_key(0xcb))held|=1u<<NavLeft;if(desktop_key(0xcd))held|=1u<<NavRight;}
    const std::uint32_t keys=held;
    for(unsigned i=0;i<ButtonCount;++i)if(in.held[ButtonActions[i]])held|=1u<<i;
    std::array<CameraResolvedAction,11> cameras{};
    for(unsigned i=0;i<cameras.size();++i)cameras[i]={s.camera_index[i]>=0&&in.held[s.camera_index[i]]!=0,(in.flags&WK_INPUT_PROFILE)!=0};
    camera_controls_resolved_actions(cameras,mode!=16u,GetTickCount64());
    if(in.transmission==WK_TRANSMISSION_H_PATTERN){   // OutRun shifts sequentially: pulses toward the held gate
        bool gates[6];for(int i=0;i<6;++i)gates[i]=in.held[WK_ACTION_GEAR1+i]!=0;
        const bool car=rt&&race;std::uint32_t gear=0;
        if(car)std::memcpy(&gear,rt->event_function36.car_select.car_799d18.data()+0x208,4);
        const auto now=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-s.t0).count();
        if(const int dir=s.gearbox.step_mode(gates,mode,car,gear,now))held|=1u<<(dir>0?ShiftUp:ShiftDown);
    }
    const std::uint32_t down=held&~s.previous;s.previous=held;
    auto on=[](std::uint32_t bits,Button b){return (bits&(1u<<b))!=0;};
    auto& d=out.pc_device;
    if(held)out.connected=true;
    if(in.bound&WK_BOUND_STEER){
        // The record carries the wheel itself (5A7B50[0]: steer word 0x0E, accelerator 0x0D, brake 0x0C).
        out.connected=true;
        double n=in.steer;const double z=s.steering.dead_zone;
        if(z>0.0)n=std::fabs(n)<z?0.0:(n>0?1.0:-1.0)*(std::fabs(n)-z)/(1.0-z);
        const auto steer=std::int32_t(std::lround(std::clamp(n*double(s.steering.sensitivity),-1.0,1.0)*32767.0));
        const auto accel=std::uint32_t(std::lround(in.throttle*255.0)),brake=std::uint32_t(std::lround(in.brake*255.0));
        // The last device used drives: a key pressed while the pedals and wheel buttons rest hands the race to the
        // keyboard until the wheel turns 3%, a pedal is pressed or a wheel button is held.
        const bool wheel_pressed=accel>=3u||brake>=3u||(held&~keys)!=0u;
        if(!s.keyboard_drives&&desktop_any_driving_key()&&!wheel_pressed){s.keyboard_drives=true;s.keyboard_anchor=steer;}
        else if(s.keyboard_drives&&(wheel_pressed||std::abs(steer-s.keyboard_anchor)>983))s.keyboard_drives=false;
        out.pc_overlay=true;out.pc_overlay_yield=s.keyboard_drives;
        out.steering=std::clamp(steer/256,-128,127);
        out.accelerator=std::max(out.accelerator,accel);out.brake=std::max(out.brake,brake);
        d.axes_94[0x0e]=std::int16_t(steer);
        d.axes_94[0x0d]=std::int16_t(std::max<std::uint32_t>(std::uint16_t(d.axes_94[0x0d]),accel));
        d.axes_94[0x0c]=std::int16_t(std::max<std::uint32_t>(std::uint16_t(d.axes_94[0x0c]),brake));
    }
    if(on(held,Start))d.buttons_04|=0x1u;if(on(held,ShiftUp))d.buttons_04|=0x2u;if(on(held,Back))d.buttons_04|=0x4u;
    if(on(held,ShiftDown))d.buttons_04|=0x8u;if(on(held,Camera))d.buttons_04|=0x10u;
    if(!race){if(on(held,Confirm))d.buttons_04|=0x2u;if(on(held,NavRight))d.buttons_04|=0x80u;if(on(held,NavLeft))d.buttons_04|=0x100u;}
    out.shift_up|=on(down,ShiftUp);out.shift_down|=on(down,ShiftDown);
    out.menu_up|=on(down,NavUp);out.menu_down|=on(down,NavDown);out.menu_left|=on(down,NavLeft);out.menu_right|=on(down,NavRight);
    out.menu_confirm|=on(down,Confirm)||on(down,Start);out.menu_cancel|=on(down,Back);out.start|=on(down,Start);
    out.horn|=on(held,Horn);
    auto& f=out.frontend.feature_mask;   // frontend features (48F5F0), as the pad adapter sets them from its edges
    if(on(down,Start))f|=1u;if(on(down,Confirm))f|=4u;if(on(down,Back))f|=8u;if(on(down,NavUp))f|=0x400u;
    if(on(down,NavDown))f|=0x800u;if(on(down,NavLeft))f|=0x1000u;if(on(down,NavRight))f|=0x2000u;
    if(!race){if(on(down,ShiftDown))f|=0x10u;if(on(down,ShiftUp))f|=0x20u;}
    if(on(down,Camera))f|=0x40000u;
}
}

void wheelkit_configure(const Ini& settings){
    s=State{};s.t0=std::chrono::steady_clock::now();
    s.steering=wheel_steering(settings);
    xbox_runtime::present_overlay()=overlay;   // the letterbox bars with or without the runtime
    const auto rpm=settings.text("Telemetry","EngineRpm");s.game_rpm=_stricmp(rpm.c_str(),"Game")==0;
    wchar_t exe[MAX_PATH];const DWORD n=GetModuleFileNameW(nullptr,exe,MAX_PATH);
    std::wstring dll(exe,n);dll=dll.substr(0,dll.find_last_of(L"\\/")+1)+L"wheelkit.dll";
    HMODULE m=LoadLibraryW(dll.c_str());
#define X(ret,name,args) wk.name=m?reinterpret_cast<ret (WK_CALL *) args>(GetProcAddress(m,#name)):nullptr;
    WK_FUNCTIONS(X)
#undef X
    bool complete=m!=nullptr;
#define X(ret,name,args) complete=complete&&wk.name!=nullptr;
    WK_FUNCTIONS(X)
#undef X
    if(!complete||wk.wk_abi_version()!=WK_ABI_VERSION){
        std::printf("wheelkit: %s; the keyboard and gamepad work, no wheel, F6 panel or telemetry\n",
            !m?"wheelkit.dll not found beside the game":"wheelkit.dll is another version (its interface differs)");
        std::fflush(stdout);return;
    }
    const std::string ini=settings.path();
    wk_identity id{};id.size=sizeof id;id.game_id="outrun-2006";id.game_name="OutRun 2006 Coast 2 Coast";id.game_version="0.2.0";
    id.settings_file=ini.c_str();id.window=xbox_runtime::native_window().hwnd;id.log=log_line;
    if(!wk.wk_start(&id)){std::printf("wheelkit: not started: %s\n",wk.wk_last_error());std::fflush(stdout);return;}
    s.on=true;
    for(unsigned i=0;i<CameraControlActions.size();++i){
        s.camera_index[i]=-1;for(int a=0;a<WK_ACTION_COUNT;++a)if(std::strcmp(wk.wk_action_name(a),CameraControlActions[i])==0)s.camera_index[i]=a;}
    if(!wk.wk_panel_configure(kDescriptor,sizeof kDescriptor-1)){std::printf("wheelkit: F6 pages refused: %s\n",wk.wk_last_error());return;}
    show_values(settings);
}

void wheelkit_wrap_input_filter(){
    auto& f=xbox_runtime::input_filter();
    if(f==filter)return;
    s.inner=f;f=filter;
}

bool wheelkit_panel_open(){return s.on&&wk.wk_panel_wants_input();}

void wheelkit_shutdown(){
    xbox_runtime::present_overlay()=nullptr;
    letterbox_release();
    if(!s.on)return;
    wk.wk_panel_d3d11_detach();
    wk.wk_stop();
    s.on=false;
}
}
