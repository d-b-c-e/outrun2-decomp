#include "wheel_input.hpp"
#include "desktop_settings.hpp"
#include "native_window.hpp"
#include "platform/runtime_input.hpp"
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
namespace outrun::desktop {
namespace {
// Logical buttons, in the order of the Tweaks keys Button<Name>.
enum Button : unsigned { A, B, X, Y, Start, Back, GearUp, GearDown, View, SelUp, SelDown, SelLeft, SelRight, ButtonCount };
constexpr const char* ButtonKeys[ButtonCount]{"ButtonA","ButtonB","ButtonX","ButtonY","ButtonStart","ButtonBack","ButtonGearUp",
                                              "ButtonGearDown","ButtonChangeView","ButtonSelUp","ButtonSelDown","ButtonSelLeft","ButtonSelRight"};
using Bindings=std::array<int,ButtonCount>;
constexpr int PovBase=128,PovLast=PovBase+15;   // 128 + hat*4 + direction (0 up, 1 right, 2 down, 3 left)

struct Settings {
    bool enabled{true};
    std::string device{"auto"},throttle_device,brake_device,shifter_device,aux_device;
    int steer_axis{0},accel_axis{1},brake_axis{2};
    bool steer_invert{},accel_invert{},brake_invert{};
    float sensitivity{1.f},steer_dead_zone{0.f};
    Bindings primary{0,1,2,3,7,6,4,5,8,-1,-1,-1,-1},aux{};
    bool hpattern{};
    int shift_up{4},shift_down{5};
    std::array<int,7> gears{-1,-1,-1,-1,-1,-1,-1};   // 1..6, reverse
};
struct Slot {
    const char* role{};
    IDirectInputDevice8W* device{};
    GUID guid{};
    std::string name;
    DIJOYSTATE2 state{},previous{};
    bool connected{};
};
struct Candidate { DIDEVICEINSTANCEW instance{}; DWORD axes{},buttons{}; bool ffb{}; };

Settings g_settings;
bool g_configured{},g_started{};
IDirectInput8W* g_input{};
Slot g_wheel{"wheel"},g_throttle{"throttle"},g_brake{"brake"},g_shifter{"shifter"},g_aux{"aux"};
Slot* const g_slots[]{&g_wheel,&g_throttle,&g_brake,&g_shifter,&g_aux};
std::uint32_t g_previous_buttons{};
std::chrono::steady_clock::time_point g_start;
std::array<LONG,8> g_logged_axes{};

std::string narrow(const wchar_t* w){
    std::string s;for(;w&&*w;++w)s.push_back(*w<128?char(*w):'?');return s;
}
std::string guid_text(const GUID& g){
    char t[40];std::snprintf(t,sizeof t,"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",g.Data1,g.Data2,g.Data3,
        g.Data4[0],g.Data4[1],g.Data4[2],g.Data4[3],g.Data4[4],g.Data4[5],g.Data4[6],g.Data4[7]);
    return t;
}
bool parse_guid(const std::string& s,GUID& g){
    unsigned long d1;unsigned d2,d3,b[8];
    if(std::sscanf(s.c_str(),"{%8lx-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}",&d1,&d2,&d3,&b[0],&b[1],&b[2],&b[3],&b[4],&b[5],&b[6],&b[7])!=11)return false;
    g.Data1=d1;g.Data2=std::uint16_t(d2);g.Data3=std::uint16_t(d3);for(int i=0;i<8;++i)g.Data4[i]=std::uint8_t(b[i]);
    return true;
}
Bindings read_bindings(const Ini& ini,const char* section,const Bindings& fallback){
    Bindings b{};for(unsigned i=0;i<ButtonCount;++i)b[i]=ini.integer(section,ButtonKeys[i],fallback[i]);return b;
}

BOOL CALLBACK collect(const DIDEVICEINSTANCEW* d,void* user){
    static_cast<std::vector<Candidate>*>(user)->push_back({*d});return DIENUM_CONTINUE;
}
// DIJOYSTATE axis 0..7: lX lY lZ lRx lRy lRz slider0 slider1 (0..65535 after DIPROP_RANGE).
LONG axis(const DIJOYSTATE2& s,int index){
    switch(index){
    case 0:return s.lX;case 1:return s.lY;case 2:return s.lZ;case 3:return s.lRx;case 4:return s.lRy;case 5:return s.lRz;
    case 6:return s.rglSlider[0];case 7:return s.rglSlider[1];
    }
    return 32767;
}
bool pressed(const Slot& s,int binding){
    if(!s.connected||binding<0)return false;
    if(binding<128)return (s.state.rgbButtons[binding]&0x80u)!=0;
    if(binding>PovLast)return false;
    const DWORD pov=s.state.rgdwPOV[(binding-PovBase)/4];
    if((pov&0xffffu)==0xffffu)return false;
    long d=std::labs(long(pov%36000u)-long((binding-PovBase)%4)*9000);
    if(d>18000)d=36000-d;
    return d<=4500;   // a diagonal presses both neighbours
}

bool open(Slot& slot,const DIDEVICEINSTANCEW& instance,HWND hwnd){
    IDirectInputDevice8W* d=nullptr;
    if(FAILED(g_input->CreateDevice(instance.guidInstance,&d,nullptr))||!d)return false;
    if(FAILED(d->SetDataFormat(&c_dfDIJoystick2))||FAILED(d->SetCooperativeLevel(hwnd,DISCL_BACKGROUND|DISCL_NONEXCLUSIVE))){d->Release();return false;}
    DIPROPRANGE range{};range.diph.dwSize=sizeof range;range.diph.dwHeaderSize=sizeof range.diph;range.diph.dwHow=DIPH_DEVICE;
    range.lMin=0;range.lMax=65535;d->SetProperty(DIPROP_RANGE,&range.diph);
    DIPROPDWORD zone{};zone.diph.dwSize=sizeof zone;zone.diph.dwHeaderSize=sizeof zone.diph;zone.diph.dwHow=DIPH_DEVICE;
    zone.dwData=0;d->SetProperty(DIPROP_DEADZONE,&zone.diph);
    d->Acquire();
    slot.device=d;slot.guid=instance.guidInstance;slot.name=narrow(instance.tszProductName);
    std::printf("wheel input: %s = %s %s\n",slot.role,slot.name.c_str(),guid_text(slot.guid).c_str());
    return true;
}
void poll(Slot& s){
    if(!s.device)return;
    s.previous=s.state;
    HRESULT r=s.device->Poll();
    if(FAILED(r)){s.device->Acquire();s.device->Poll();}
    r=s.device->GetDeviceState(sizeof s.state,&s.state);
    if(r==DIERR_INPUTLOST||r==DIERR_NOTACQUIRED){s.device->Acquire();r=s.device->GetDeviceState(sizeof s.state,&s.state);}
    const bool now=SUCCEEDED(r);
    if(now!=s.connected)std::printf("wheel input: %s %s\n",s.role,now?"connected":"lost");
    s.connected=now;
}

// Enumerates the controllers, logs them and opens every configured role.
void start(){
    g_started=true;g_start=std::chrono::steady_clock::now();
    const auto hwnd=static_cast<HWND>(xbox_runtime::native_window().hwnd);
    if(FAILED(DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(&g_input),nullptr))||!g_input){
        std::printf("wheel input: DirectInput8Create failed\n");return;}
    std::vector<Candidate> found;
    g_input->EnumDevices(DI8DEVCLASS_GAMECTRL,collect,&found,DIEDFL_ATTACHEDONLY);
    for(auto& c:found){
        IDirectInputDevice8W* d=nullptr;
        if(SUCCEEDED(g_input->CreateDevice(c.instance.guidInstance,&d,nullptr))&&d){
            DIDEVCAPS caps{};caps.dwSize=sizeof caps;
            if(SUCCEEDED(d->GetCapabilities(&caps))){c.axes=caps.dwAxes;c.buttons=caps.dwButtons;c.ffb=(caps.dwFlags&DIDC_FORCEFEEDBACK)!=0;}
            d->Release();
        }
        std::printf("wheel input: found %s %s type %02X axes %lu buttons %lu%s\n",guid_text(c.instance.guidInstance).c_str(),
            narrow(c.instance.tszProductName).c_str(),unsigned(GET_DIDEVICE_TYPE(c.instance.dwDevType)),c.axes,c.buttons,c.ffb?" force-feedback":"");
    }
    auto by_guid=[&](const std::string& text)->const Candidate*{
        GUID g{};if(!parse_guid(text,g))return nullptr;
        for(const auto& c:found)if(IsEqualGUID(c.instance.guidInstance,g))return &c;
        return nullptr;};
    // auto: no gamepads or axis-less devices; force feedback first, then the most axes.
    const Candidate* wheel=nullptr;
    if(g_settings.device.empty()||_stricmp(g_settings.device.c_str(),"auto")==0){
        for(const auto& c:found){
            if(GET_DIDEVICE_TYPE(c.instance.dwDevType)==DI8DEVTYPE_GAMEPAD||c.axes==0)continue;
            if(!wheel||(c.ffb&&!wheel->ffb)||(c.ffb==wheel->ffb&&c.axes>wheel->axes))wheel=&c;
        }
    }else if(!(wheel=by_guid(g_settings.device)))std::printf("wheel input: DeviceGuid %s is not connected\n",g_settings.device.c_str());
    if(wheel)open(g_wheel,wheel->instance,hwnd);
    else std::printf("wheel input: no wheel; the gamepad drives\n");
    const std::pair<Slot*,const std::string*> extra[]{{&g_throttle,&g_settings.throttle_device},{&g_brake,&g_settings.brake_device},
                                                      {&g_shifter,&g_settings.shifter_device},{&g_aux,&g_settings.aux_device}};
    for(const auto& [slot,text]:extra){
        if(text->empty())continue;
        if(const auto* c=by_guid(*text))open(*slot,c->instance,hwnd);
        else std::printf("wheel input: %s %s is not connected\n",slot->role,text->c_str());
    }
}

const Slot& pedal_slot(const Slot& own){return own.device?own:g_wheel;}
std::int32_t steering_word(){
    if(g_settings.steer_axis<0||!g_wheel.connected)return 0;
    float v=(float(axis(g_wheel.state,g_settings.steer_axis))-32767.5f)/32767.5f;
    if(g_settings.steer_invert)v=-v;
    const float dz=g_settings.steer_dead_zone;
    if(dz>0.f){if(std::fabs(v)<dz)return 0;v=(v>0.f?1.f:-1.f)*(std::fabs(v)-dz)/(1.f-dz);}
    v=std::clamp(v*g_settings.sensitivity,-1.f,1.f);
    return std::int32_t(std::lround(v*32767.f));
}
std::uint32_t pedal(const Slot& s,int index,bool invert){
    if(index<0||!s.connected)return 0;
    float v=float(axis(s.state,index))/65535.f;
    if(invert)v=1.f-v;
    return std::uint32_t(std::clamp(v*255.f,0.f,255.f));
}
std::uint32_t buttons_held(){
    std::uint32_t held=0;
    for(unsigned i=0;i<ButtonCount;++i)if(pressed(g_wheel,g_settings.primary[i])||pressed(g_aux,g_settings.aux[i]))held|=1u<<i;
    if(g_shifter.device&&!g_settings.hpattern){
        if(pressed(g_shifter,g_settings.shift_up))held|=1u<<GearUp;
        if(pressed(g_shifter,g_settings.shift_down))held|=1u<<GearDown;
    }
    return held;
}
// First two minutes: every button pressed and the moved axes, to fill in the settings.
void log_activity(){
    if(std::chrono::steady_clock::now()-g_start>std::chrono::minutes(2))return;
    for(const Slot* s:g_slots){
        if(!s->connected)continue;
        for(int i=0;i<128;++i)if((s->state.rgbButtons[i]&0x80u)&&!(s->previous.rgbButtons[i]&0x80u))std::printf("wheel input: %s button %d\n",s->role,i);
        for(int h=0;h<4;++h)if(s->state.rgdwPOV[h]!=s->previous.rgdwPOV[h]&&(s->state.rgdwPOV[h]&0xffffu)!=0xffffu)
            std::printf("wheel input: %s hat %d at %lu\n",s->role,h,s->state.rgdwPOV[h]);
    }
    if(g_wheel.connected){
        bool moved=false;
        for(int a=0;a<8;++a)if(std::labs(axis(g_wheel.state,a)-g_logged_axes[size_t(a)])>4096)moved=true;
        if(moved){
            for(int a=0;a<8;++a)g_logged_axes[size_t(a)]=axis(g_wheel.state,a);
            std::printf("wheel input: wheel axes X %ld Y %ld Z %ld Rx %ld Ry %ld Rz %ld S0 %ld S1 %ld\n",g_logged_axes[0],g_logged_axes[1],
                g_logged_axes[2],g_logged_axes[3],g_logged_axes[4],g_logged_axes[5],g_logged_axes[6],g_logged_axes[7]);
        }
    }
}
}

void wheel_input_configure(const Ini& ini){
    auto& s=g_settings;
    s.enabled=ini.flag("DirectInput","UseDirectInputRemap",true);
    s.device=ini.text("DirectInput","DeviceGuid","auto");
    s.steer_axis=ini.integer("DirectInput","SteeringAxis",0);
    s.steer_invert=ini.flag("DirectInput","SteeringInvert",false);
    s.sensitivity=ini.number("DirectInput","SteeringSensitivity",1.f);
    s.accel_axis=ini.integer("DirectInput","AccelerationAxis",1);
    s.accel_invert=ini.flag("DirectInput","AccelerationInvert",false);
    s.brake_axis=ini.integer("DirectInput","BrakeAxis",2);
    s.brake_invert=ini.flag("DirectInput","BrakeInvert",false);
    s.throttle_device=ini.text("DirectInput","ThrottleDeviceGuid");
    s.brake_device=ini.text("DirectInput","BrakeDeviceGuid");
    s.steer_dead_zone=std::clamp(ini.number("Controls","SteeringDeadZone",0.f),0.f,0.9f);
    s.primary=read_bindings(ini,"DirectInput",s.primary);
    s.shifter_device=ini.text("DirectInput.Shifter","DeviceGuid");
    s.hpattern=ini.text("DirectInput.Shifter","GearMode","sequential")=="hpattern";
    s.shift_up=ini.integer("DirectInput.Shifter","ButtonGearUp",4);
    s.shift_down=ini.integer("DirectInput.Shifter","ButtonGearDown",5);
    static constexpr const char* GearKeys[7]{"ButtonGear1","ButtonGear2","ButtonGear3","ButtonGear4","ButtonGear5","ButtonGear6","ButtonGearReverse"};
    for(unsigned i=0;i<7;++i)s.gears[i]=ini.integer("DirectInput.Shifter",GearKeys[i],-1);
    s.aux_device=ini.text("DirectInput.Aux","DeviceGuid");
    Bindings none;none.fill(-1);
    s.aux=read_bindings(ini,"DirectInput.Aux",none);
    g_configured=true;
    std::printf("wheel input: %s, device %s, axes steer %d accel %d brake %d\n",s.enabled?"on":"off",s.device.c_str(),s.steer_axis,s.accel_axis,s.brake_axis);
    if(s.hpattern)std::printf("wheel input: H-pattern shifting is not implemented yet; use the paddles\n");
}

void wheel_input_filter(platform::NativeInputState& out,std::uint32_t mode){
    if(!g_configured||!g_settings.enabled)return;
    if(!g_started)start();
    if(!g_wheel.device)return;
    for(Slot* s:g_slots)poll(*s);
    log_activity();
    if(!g_wheel.connected)return;
    const std::uint32_t held=buttons_held(),down=held&~g_previous_buttons;
    g_previous_buttons=held;
    auto on=[](std::uint32_t bits,Button b){return (bits&(1u<<b))!=0;};
    const bool race=mode==16u||mode==18u;
    const std::int32_t steer=steering_word();
    const std::uint32_t accel=pedal(pedal_slot(g_throttle),g_settings.accel_axis,g_settings.accel_invert);
    const std::uint32_t brake=pedal(pedal_slot(g_brake),g_settings.brake_axis,g_settings.brake_invert);
    // The direct mapping (menus, diagnostics) and the race record of 453BB0.
    out.connected=true;
    out.steering=std::clamp(steer/256,-128,127);
    out.accelerator=std::max(out.accelerator,accel);
    out.brake=std::max(out.brake,brake);
    out.shift_up|=on(down,GearUp);out.shift_down|=on(down,GearDown);
    out.menu_up|=on(down,SelUp);out.menu_down|=on(down,SelDown);
    out.menu_left|=on(down,SelLeft);out.menu_right|=on(down,SelRight);
    out.menu_confirm|=on(down,A)||on(down,Start);out.menu_cancel|=on(down,B);
    out.start|=on(down,Start);
    // Race: the record carries the wheel itself (5A7B50[0]: steer word 0x0E, accelerator
    // 0x0D, brake 0x0C, shift up 0x2, shift down 0x8, view 0x10), not the pad through 406FA0.
    out.pc_input_layer=false;
    auto& d=out.pc_device;
    d.axes_94[0x0e]=std::int16_t(steer);
    d.axes_94[0x0d]=std::int16_t(std::max<std::uint32_t>(std::uint16_t(d.axes_94[0x0d]),accel));
    d.axes_94[0x0c]=std::int16_t(std::max<std::uint32_t>(std::uint16_t(d.axes_94[0x0c]),brake));
    if(on(held,Start))d.buttons_04|=0x1u;
    if(on(held,GearUp))d.buttons_04|=0x2u;
    if(on(held,B))d.buttons_04|=0x4u;
    if(on(held,GearDown))d.buttons_04|=0x8u;
    if(on(held,View))d.buttons_04|=0x10u;
    // Outside the race: A confirms the result prompts (0x2) and the select buttons are
    // the D-pad (0x80 / 0x100 would be digital steering in the race).
    if(!race){
        if(on(held,A))d.buttons_04|=0x2u;
        if(on(held,SelRight))d.buttons_04|=0x80u;
        if(on(held,SelLeft))d.buttons_04|=0x100u;
    }
    // Frontend features (48F5F0), as the pad adapter sets them from its button edges.
    auto& f=out.frontend.feature_mask;
    if(on(down,Start))f|=1u;
    if(on(down,A))f|=4u;
    if(on(down,B))f|=8u;
    if(on(down,SelUp))f|=0x400u;
    if(on(down,SelDown))f|=0x800u;
    if(on(down,SelLeft))f|=0x1000u;
    if(on(down,SelRight))f|=0x2000u;
    if(!race){if(on(down,GearDown))f|=0x10u;if(on(down,GearUp))f|=0x20u;}
    if(on(down,X))f|=0x8000u|0x40000u;
    if(on(down,Y))f|=0x4000u;
    if(on(down,View))f|=0x40000u;
}

void wheel_input_shutdown(){
    for(Slot* s:g_slots)if(s->device){s->device->Unacquire();s->device->Release();s->device=nullptr;}
    if(g_input){g_input->Release();g_input=nullptr;}
}
}
