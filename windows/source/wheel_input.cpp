#include "wheel_input.hpp"
#include "desktop_bindings.hpp"
#include "wheel_axes.hpp"
#include "wheel_rescan.hpp"
#include "outrun_adapter.hpp"
#include "controls_store.hpp"
#include "desktop_settings.hpp"
#include "test_injection.hpp"
#include "native_window.hpp"
#include "desktop_keyboard.hpp"
#include "platform/native_runtime.hpp"
#include "platform/runtime_input.hpp"
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <memory>
#include <string>
#include <vector>
namespace outrun::desktop {
namespace {
// The game's buttons, by the controls-contract action that drives each.
enum Button : unsigned { Confirm, Back, Start, ShiftUp, ShiftDown, Camera, NavUp, NavDown, NavLeft, NavRight, Horn, ButtonCount };
constexpr const char* ButtonActions[ButtonCount]{"confirm","back","start","shiftUp","shiftDown","camera","navUp","navDown","navLeft","navRight","horn"};
constexpr const char* GearActions[6]{"gear1","gear2","gear3","gear4","gear5","gear6"};

struct Device {
    std::string dev,prod,name;      // instance and product GUIDs, lower case
    GUID guid{};
    DWORD type{},axes{},buttons{},povs{};
    bool ffb{};
    IDirectInputDevice8W* device{};
    DIJOYSTATE2 state{},previous{};
    bool connected{},present{},was_present{};
    std::uint64_t opens{},reads{};   // the F6 binding editor's epoch and read sequence
    AxisInventory axes_found{};      // read once per open (wheel_axes.hpp): the axes it has, and which read 0..65535
};
struct Bound { std::string action; ctl::Binding binding; Device* device{}; };

struct State {
    bool configured{},started{};
    std::string settings_path,folder;
    ctl::Section section;
    bool import_pending{};
    float sensitivity{1.f},dead_zone{0.f};
    std::deque<Device> found;                   // stable addresses for open and bound through later scans
    std::vector<Device*> open;                  // the opened ones (pointers into found)
    std::vector<Bound> bound;                   // applied bindings with their device
    IDirectInput8W* input{};
    std::uint32_t previous_buttons{};
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point next_scan;
    HPattern gearbox;
    bool keyboard_drives{};std::int32_t keyboard_anchor{};   // the race follows the keyboard (see wheel_input_filter)
    std::array<LONG,8> logged_axes{};
    unsigned rescans{};double rescan_ms_max{};   // after start (wheel_rescan.hpp): each one's reason and duration
};
State g;
std::atomic<bool> devices_changed{false};   // WM_DEVICECHANGE (wheel_input_devices_changed), taken by the next input sample

void print(const std::vector<std::string>& lines){for(const auto& l:lines)std::printf("%s\n",l.c_str());}
std::string lower_guid(const GUID& v){
    char t[40];std::snprintf(t,sizeof t,"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",v.Data1,v.Data2,v.Data3,
        v.Data4[0],v.Data4[1],v.Data4[2],v.Data4[3],v.Data4[4],v.Data4[5],v.Data4[6],v.Data4[7]);
    return t;
}
std::string narrow(const wchar_t* w){std::string s;for(;w&&*w;++w)s.push_back(*w<128?char(*w):'?');return s;}
BOOL CALLBACK collect(const DIDEVICEINSTANCEW* d,void* user){
    Device x;x.guid=d->guidInstance;x.dev=lower_guid(d->guidInstance);x.prod=lower_guid(d->guidProduct);
    x.name=narrow(d->tszProductName);x.type=GET_DIDEVICE_TYPE(d->dwDevType);
    static_cast<std::vector<Device>*>(user)->push_back(x);return DIENUM_CONTINUE;
}
Device* by_dev(const std::string& dev){
    for(auto& d:g.found)if(d.dev==dev)return &d;
    return nullptr;
}
Device* by_text(const std::string& text){   // a GUID as Tweaks writes it, any case
    const auto v=ctl::detail::guid(text);return v.empty()?nullptr:by_dev(v);
}
LONG axis(const DIJOYSTATE2& s,int index){
    switch(index){
    case 0:return s.lX;case 1:return s.lY;case 2:return s.lZ;case 3:return s.lRx;case 4:return s.lRy;case 5:return s.lRz;
    case 6:return s.rglSlider[0];case 7:return s.rglSlider[1];
    }
    return 0;
}
// The raw sample of a binding (axes rescaled from the 0..65535 the device was set to, into the binding's range).
long raw(const Bound& b){
    const auto& s=b.device->state;const auto& k=b.binding;
    switch(k.kind){
    case ctl::Kind::Axis:return ctl::rescale(axis(s,k.index),0,65535,k.min,k.max);
    case ctl::Kind::Button:return ctl::diButtonDown(s.rgbButtons[k.index])?1:0;
    case ctl::Kind::Hat:{const DWORD p=s.rgdwPOV[k.index];return (p&0xffffu)==0xffffu?-1:long(p);}
    default:return 0;
    }
}
const Bound* find(const char* action){
    for(const auto& b:g.bound)if(b.action==action&&b.device&&b.device->connected)return &b;
    return nullptr;
}
double value(const char* action){const auto* b=find(action);return b?ctl::normalize(action,b->binding,raw(*b)):0.0;}

bool open(Device& d,HWND hwnd){
    if(d.device)return true;
    IDirectInputDevice8W* x=nullptr;
    if(FAILED(g.input->CreateDevice(d.guid,&x,nullptr))||!x)return false;
    if(FAILED(x->SetDataFormat(&c_dfDIJoystick2))||FAILED(x->SetCooperativeLevel(hwnd,DISCL_BACKGROUND|DISCL_NONEXCLUSIVE))){x->Release();return false;}
    DIPROPRANGE range{};range.diph.dwSize=sizeof range;range.diph.dwHeaderSize=sizeof range.diph;range.diph.dwHow=DIPH_DEVICE;
    range.lMin=0;range.lMax=65535;x->SetProperty(DIPROP_RANGE,&range.diph);
    DIPROPDWORD zone{};zone.diph.dwSize=sizeof zone;zone.diph.dwHeaderSize=sizeof zone.diph;zone.diph.dwHow=DIPH_DEVICE;
    zone.dwData=0;x->SetProperty(DIPROP_DEADZONE,&zone.diph);
    x->Acquire();
    d.axes_found=axis_inventory(x);   // after the range request; an axis-less device (a shifter) still opens
    d.device=x;++d.opens;g.open.push_back(&d);
    std::printf("wheel input: opened %s %s\n",d.name.c_str(),d.dev.c_str());
    return true;
}
void poll(Device& d){
    d.previous=d.state;
    HRESULT r=d.device->Poll();
    if(FAILED(r)){d.device->Acquire();d.device->Poll();}
    r=d.device->GetDeviceState(sizeof d.state,&d.state);
    if(r==DIERR_INPUTLOST||r==DIERR_NOTACQUIRED){d.device->Acquire();r=d.device->GetDeviceState(sizeof d.state,&d.state);}
    const bool now=SUCCEEDED(r);
    if(now)++d.reads;
    std::vector<std::string> log;
    if(now)inject::deliver(d.dev,d.state,int(d.buttons),int(d.povs),log);   // before any binding reads the sample
    else inject::device_gone(d.dev,log);
    print(log);
    if(now!=d.connected)std::printf("wheel input: %s %s\n",d.name.c_str(),now?"connected":"lost");
    d.connected=now;
}
void scan_devices(){
    if(!g.input)return;
    std::vector<Device> attached;
    if(FAILED(g.input->EnumDevices(DI8DEVCLASS_GAMECTRL,collect,&attached,DIEDFL_ATTACHEDONLY))){
        std::printf("wheel input: device rescan failed; keeping current devices\n");return;
    }
    for(auto& d:g.found){d.was_present=d.present;d.present=false;}
    for(const auto& listed:attached){
        Device* d=by_dev(listed.dev);
        if(!d){g.found.push_back(listed);d=&g.found.back();}
        const bool arrived=!d->was_present;
        d->present=true;
        if(!arrived)continue;
        d->guid=listed.guid;d->prod=listed.prod;d->name=listed.name;d->type=listed.type;
        IDirectInputDevice8W* x=nullptr;
        if(SUCCEEDED(g.input->CreateDevice(d->guid,&x,nullptr))&&x){
            DIDEVCAPS caps{};caps.dwSize=sizeof caps;
            if(SUCCEEDED(x->GetCapabilities(&caps))){d->axes=caps.dwAxes;d->buttons=caps.dwButtons;d->povs=caps.dwPOVs;d->ffb=(caps.dwFlags&DIDC_FORCEFEEDBACK)!=0;}
            x->Release();
        }
        std::printf("wheel input: found dev=%s prod=%s \"%s\" type %02lX axes %lu buttons %lu hats %lu%s\n",d->dev.c_str(),d->prod.c_str(),d->name.c_str(),
            d->type,d->axes,d->buttons,d->povs,d->ffb?" force-feedback":"");
    }
    // A failed read can leave a stale handle even when the same instance GUID
    // is attached again. Reopen it at the scan boundary; bindings keep their
    // stable Device address and resume on the next successful sample.
    for(auto& d:g.found)if(d.device&&(!d.present||!d.connected)){
        std::vector<std::string> log;inject::device_gone(d.dev,log);print(log);
        if(d.connected)std::printf("wheel input: %s lost\n",d.name.c_str());
        d.connected=false;d.state={};d.previous={};
        d.device->Unacquire();d.device->Release();d.device=nullptr;
        g.open.erase(std::remove(g.open.begin(),g.open.end(),&d),g.open.end());
    }
}
void bind_present_devices(bool initial){
    const auto hwnd=static_cast<HWND>(xbox_runtime::native_window().hwnd);
    for(const auto& e:g.section.bound){
        if(unavailable_reason(e.action))continue;
        Device* d=by_dev(e.binding.dev);
        if(!d||!d->present){
            if(initial)std::printf("controls: %s: device %s (%s) is not connected\n",e.action.c_str(),e.binding.dev.c_str(),e.binding.name.c_str());
            continue;
        }
        if(!open(*d,hwnd)){if(initial)std::printf("controls: %s: cannot open %s\n",e.action.c_str(),d->name.c_str());continue;}
        const auto existing=std::find_if(g.bound.begin(),g.bound.end(),[&](const Bound& b){return b.action==e.action;});
        if(existing==g.bound.end())g.bound.push_back({e.action,e.binding,d});
    }
}
// The rescan policy's input (wheel_rescan.hpp): the applicable bindings, and each applied one's device handle and read.
bool bound_missing(){
    std::size_t applicable=0;
    for(const auto& e:g.section.bound)if(!unavailable_reason(e.action))++applicable;
    return wheel_bound_missing(applicable,g.bound,[](const Bound& b){return b.device&&b.device->device;},
                               [](const Bound& b){return b.device&&b.device->connected;});
}
void rescan(const char* reason){
    const auto t0=WheelClock::now();
    scan_devices();bind_present_devices(false);
    const double ms=std::chrono::duration<double,std::milli>(WheelClock::now()-t0).count();
    ++g.rescans;g.rescan_ms_max=std::max(g.rescan_ms_max,ms);
    if(g.rescans<=10)std::printf("wheel input: rescan %u (%s) %.1f ms\n",g.rescans,reason,ms);
    g.next_scan=WheelClock::now()+WheelMissingRescan;
}

// ---- the one-time import of the OutRun2006Tweaks [DirectInput] keys into [Controls]
LegacyControls legacy_controls(const Ini& ini){
    LegacyControls c;
    c.steer_axis=ini.integer("DirectInput","SteeringAxis",0);c.steer_invert=ini.flag("DirectInput","SteeringInvert",false);
    c.steer_calibration=ini.flag("DirectInput.Calibration","SteeringEnabled",false);
    c.steer_points={ini.text("DirectInput.Calibration","SteeringMinimum","0"),
                    ini.text("DirectInput.Calibration","SteeringCenter","32767.5"),
                    ini.text("DirectInput.Calibration","SteeringMaximum","65535")};
    c.accel_axis=ini.integer("DirectInput","AccelerationAxis",1);c.accel_invert=ini.flag("DirectInput","AccelerationInvert",false);
    c.brake_axis=ini.integer("DirectInput","BrakeAxis",2);c.brake_invert=ini.flag("DirectInput","BrakeInvert",false);
    const auto pedal_calibration=[&](const char* role,const char* deadzone){
        LegacyPedalCalibration calibration;
        calibration.enabled=ini.flag("DirectInput.Calibration",std::string(role)+"Enabled",false);
        calibration.points={ini.text("DirectInput.Calibration",std::string(role)+"Minimum","0"),
                            ini.text("DirectInput.Calibration",std::string(role)+"Center","32767.5"),
                            ini.text("DirectInput.Calibration",std::string(role)+"Maximum","65535")};
        calibration.deadzone=ini.text("DirectInput",deadzone,"0");
        return calibration;
    };
    c.accel_calibration=pedal_calibration("Throttle","AccelerationDeadzone");
    c.brake_calibration=pedal_calibration("Brake","BrakeDeadzone");
    static constexpr const char* Keys[13]{"ButtonA","ButtonB","ButtonX","ButtonY","ButtonStart","ButtonBack","ButtonGearUp",
                                          "ButtonGearDown","ButtonChangeView","ButtonSelUp","ButtonSelDown","ButtonSelLeft","ButtonSelRight"};
    static constexpr int Defaults[13]{0,1,2,3,7,6,4,5,8,-1,-1,-1,-1};   // Tweaks' own defaults
    for(unsigned i=0;i<13;++i){c.buttons[i]=ini.integer("DirectInput",Keys[i],Defaults[i]);c.aux_buttons[i]=ini.integer("DirectInput.Aux",Keys[i],-1);}
    c.hpattern=ini.text("DirectInput.Shifter","GearMode","sequential")=="hpattern";
    c.shifter_up=ini.integer("DirectInput.Shifter","ButtonGearUp",4);c.shifter_down=ini.integer("DirectInput.Shifter","ButtonGearDown",5);
    static constexpr const char* Gears[7]{"ButtonGear1","ButtonGear2","ButtonGear3","ButtonGear4","ButtonGear5","ButtonGear6","ButtonGearReverse"};
    for(unsigned i=0;i<7;++i)c.gears[i]=ini.integer("DirectInput.Shifter",Gears[i],-1);
    return c;
}
DeviceIdentity identity(const Device* d){return d?DeviceIdentity{d->dev,d->prod,d->name}:DeviceIdentity{};}
// Tweaks' "auto": no gamepads or axis-less devices; force feedback first, then the most axes.
Device* auto_wheel(){
    Device* best=nullptr;
    for(auto& d:g.found){
        if(!d.present)continue;
        if(d.type==DI8DEVTYPE_GAMEPAD||d.axes==0)continue;
        if(!best||(d.ffb&&!best->ffb)||(d.ffb==best->ffb&&d.axes>best->axes))best=&d;
    }
    return best;
}
bool import_legacy_bindings(){
    Ini own,tweaks,*source=&own;
    own.load(g.settings_path);
    if(!own.has_section("DirectInput")){tweaks.load(g.folder+"/OutRun2006Tweaks.ini");source=&tweaks;}
    const auto& src=*source;
    if(!src.flag("DirectInput","UseDirectInputRemap",true)){std::printf("controls: legacy import skipped: UseDirectInputRemap = false in %s\n",src.path().c_str());return false;}
    const auto wheel_text=src.text("DirectInput","DeviceGuid","auto");
    Device* wheel=(wheel_text.empty()||_stricmp(wheel_text.c_str(),"auto")==0)?auto_wheel():by_text(wheel_text);
    if(!wheel){std::printf("controls: legacy import waits: the wheel (%s) is not connected\n",wheel_text.c_str());return false;}
    LegacyDevices devices{identity(wheel)};
    auto optional=[&](const char* section,const char* key,DeviceIdentity& out){
        const auto t=src.text(section,key);if(t.empty())return true;
        const Device* d=by_text(t);
        if(!d){std::printf("controls: legacy import waits: [%s] %s %s is not connected\n",section,key,t.c_str());return false;}
        out=identity(d);return true;};
    if(!optional("DirectInput","ThrottleDeviceGuid",devices.throttle)||!optional("DirectInput","BrakeDeviceGuid",devices.brake)||
       !optional("DirectInput.Shifter","DeviceGuid",devices.shifter)||!optional("DirectInput.Aux","DeviceGuid",devices.aux))return false;
    std::vector<std::string> notes;
    const auto body=import_legacy(legacy_controls(src),devices,notes);
    // A durable copy of the file as it was, then the canonical section and the marker.
    const auto backup=g.settings_path+".pre-controls-import";
    if(GetFileAttributesA(backup.c_str())==INVALID_FILE_ATTRIBUTES&&!CopyFileA(g.settings_path.c_str(),backup.c_str(),TRUE)){
        std::printf("controls: legacy import stopped: cannot back up %s\n",g.settings_path.c_str());return false;}
    char date[32];const std::time_t t=std::time(nullptr);std::tm tm{};gmtime_s(&tm,&t);std::strftime(date,sizeof date,"%Y-%m-%dT%H:%M:%SZ",&tm);
    std::string error;
    const std::vector<std::string> marker{"From = "+src.path()+" [DirectInput]","Date = "+std::string(date),"Backup = "+backup};
    if(!write_section(g.settings_path,"Controls",body,error)||!write_section(g.settings_path,"ControlsImport",marker,error)){
        std::printf("controls: legacy import failed: %s\n",error.c_str());return false;}
    if(!own.has_section("Wheel")){
        float sensitivity=src.number("DirectInput","SteeringSensitivity",1.f),zone=src.number("Controls","SteeringDeadZone",0.f);
        if(!std::isfinite(sensitivity))sensitivity=1.f;   // never "nan" or "inf" into [Wheel] (romi 25900)
        if(!std::isfinite(zone))zone=0.f;
        char a[32],b[32];std::snprintf(a,sizeof a,"%g",double(sensitivity));std::snprintf(b,sizeof b,"%g",double(zone));
        write_section(g.settings_path,"Wheel",{std::string("SteeringSensitivity = ")+a,std::string("SteeringDeadZone = ")+b},error);
        const auto steering=wheel_steering(sensitivity,zone);
        g.sensitivity=steering.sensitivity;g.dead_zone=steering.dead_zone;
    }
    std::printf("controls: imported %zu binding line(s) from %s into [Controls] (backup %s)\n",body.size()-4,src.path().c_str(),backup.c_str());
    for(const auto& n:notes)std::printf("controls: import note: %s\n",n.c_str());
    Ini again;again.load(g.settings_path);
    g.section=ctl::parseSection(again.section_lines("Controls"),ControlsReads);
    return true;
}

void report(){
    const auto r=report_controls(g.section);
    std::printf("%s\n",r.start_line.c_str());
    for(const auto& n:r.notes)std::printf("%s\n",n.c_str());
}

// Enumerates the controllers, imports pending legacy bindings, opens every bound device, arms test injection.
void start(){
    g.started=true;g.start=std::chrono::steady_clock::now();
    if(FAILED(DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(&g.input),nullptr))||!g.input){
        std::printf("wheel input: DirectInput8Create failed\n");return;}
    scan_devices();
    g.next_scan=g.start+std::chrono::seconds(2);
    if(g.import_pending&&import_legacy_bindings()){g.import_pending=false;report();}   // canonical: reported at configure
    bind_present_devices(true);
    print(inject::init(g.section,false));   // this build sends no force
}

// First two minutes: buttons pressed and axes moved on every opened device, to find bindings.
void log_activity(){
    if(std::chrono::steady_clock::now()-g.start>std::chrono::minutes(2))return;
    for(const Device* d:g.open){
        if(!d->connected)continue;
        for(int i=0;i<128;++i)if((d->state.rgbButtons[i]&0x80u)&&!(d->previous.rgbButtons[i]&0x80u))std::printf("wheel input: %s button %d\n",d->name.c_str(),i);
        for(int h=0;h<4;++h)if(d->state.rgdwPOV[h]!=d->previous.rgdwPOV[h]&&(d->state.rgdwPOV[h]&0xffffu)!=0xffffu)
            std::printf("wheel input: %s hat %d at %lu\n",d->name.c_str(),h,d->state.rgdwPOV[h]);
    }
    if(g.open.empty()||!g.open.front()->connected)return;
    const auto& s=g.open.front()->state;
    bool moved=false;
    for(int a=0;a<8;++a)if(std::labs(axis(s,a)-g.logged_axes[std::size_t(a)])>4096)moved=true;
    if(!moved)return;
    for(int a=0;a<8;++a)g.logged_axes[std::size_t(a)]=axis(s,a);
    std::printf("wheel input: %s axes X %ld Y %ld Z %ld Rx %ld Ry %ld Rz %ld S0 %ld S1 %ld\n",g.open.front()->name.c_str(),g.logged_axes[0],
        g.logged_axes[1],g.logged_axes[2],g.logged_axes[3],g.logged_axes[4],g.logged_axes[5],g.logged_axes[6],g.logged_axes[7]);
}

// Native and shared input use the same consumed-gear validation, retry timing and pause handling.
std::uint32_t gear_pulses(const platform::NativeRuntimeContext* runtime){
    if(g.section.transmission!="H-pattern")return 0;
    bool gates[6];
    for(unsigned i=0;i<6;++i)gates[i]=value(GearActions[i])>=0.5;
    const std::uint32_t mode=runtime?runtime->mode_state.current:0u;
    const bool car=runtime&&(mode==16u||mode==18u);
    std::uint32_t gear=0;
    if(car)std::memcpy(&gear,runtime->event_function36.car_select.car_799d18.data()+0x208,4);
    const auto now=std::chrono::duration_cast<std::chrono::microseconds>(WheelClock::now()-g.start).count();
    const int dir=g.gearbox.step_mode(gates,mode,car,gear,now);
    return dir?1u<<(dir>0?ShiftUp:ShiftDown):0u;
}
}

std::string wheel_input_steer_device(){
    const Bound* b=g.open.empty()?nullptr:find("steer");
    return b?b->device->dev:std::string();
}

std::string wheel_input_steer_product(){
    const Bound* b=g.open.empty()?nullptr:find("steer");
    return b?b->device->prod:std::string();
}

std::string wheel_input_status_text(){
    if(!g.configured)return "the wheel input is not in use (another input backend)";
    if(g.section.schema==0)return "no [Controls] profile yet: bind the controls in Wheelkit";
    std::string s="profile \""+g.section.profile+"\""+(g.section.revision.empty()?"":" (revision "+g.section.revision+")")+", "+
        std::to_string(g.section.bound.size())+" bindings";
    const Bound* steer=nullptr;for(const auto& b:g.bound)if(b.action=="steer")steer=&b;
    if(!steer)return s+"; steering: no device bound or connected at start";
    return s+"; steering: "+steer->device->name+(steer->device->connected?" connected":" not connected");
}

void wheel_input_configure(const Ini& ini){
    g.settings_path=ini.path();
    const auto slash=g.settings_path.find_last_of("\\/");g.folder=slash==std::string::npos?".":g.settings_path.substr(0,slash);
    const auto steering=wheel_steering(ini);   // non-finite values are the defaults (romi 25900)
    g.sensitivity=steering.sensitivity;g.dead_zone=steering.dead_zone;
    g.section=ctl::parseSection(ini.section_lines("Controls"),ControlsReads);   // STD-037 (controls_store.hpp)
    // No canonical section yet: import the Tweaks bindings once (this file's [DirectInput], else the folder's
    // OutRun2006Tweaks.ini). The marker makes it once; a canonical [Controls] is never remapped from them.
    const bool canonical=g.section.error!="no Schema";
    if(!canonical){
        if(ini.has_section("ControlsImport"))std::printf("controls: nothing applied: [Controls] has no Schema (the legacy import ran before, see [ControlsImport])\n");
        else{
            Ini tweaks;
            g.import_pending=ini.has_section("DirectInput")||(tweaks.load(g.folder+"/OutRun2006Tweaks.ini")&&tweaks.has_section("DirectInput"));
            if(!g.import_pending)std::printf("controls: nothing applied: no [Controls] and no OutRun2006Tweaks bindings to import\n");
        }
    }
    g.configured=true;
    if(canonical)report();
}

void wheel_input_filter(platform::NativeInputState& out,const platform::NativeRuntimeContext* runtime){
    // The keyboard reaches the race only through the PC layer (406FA0), which native_runtime runs only while
    // pc_input_layer is set. The pad mapping sets it only for a connected gamepad (src/input/dual_sense.cpp returns
    // first without one), so on a rig without a pad a held accelerate key did nothing (perf run 1, nene 24264). The
    // desktop always has a keyboard. With no pad, pc_pad stays default: axes 0 (a centred stick) and the POV released.
    out.pc_input_layer=true;
    if(!g.configured)return;
    if(!g.started)start();
    if(g.input){   // a rescan stalls this frame: only on a device change, or every 2 s while a binding has no device
        const bool changed=devices_changed.exchange(false);
        if(wheel_rescan_due(changed,bound_missing(),WheelClock::now(),g.next_scan))rescan(changed?"device change":"bound device missing");
    }
    const std::uint32_t mode=runtime?runtime->mode_state.current:0u;
    const bool race=mode==16u||mode==18u;
    // Outside the race the menus read these edges, not the PC layer: the original default menu keys (61E678:
    // Enter, Esc, the arrows) count as the matching buttons. In the race the keyboard is the PC layer's own.
    std::uint32_t keys=0;
    if(!race){
        using outrun::desktop::desktop_key;
        if(desktop_key(0x1c))keys|=1u<<Confirm;if(desktop_key(0x01))keys|=1u<<Back;
        if(desktop_key(0xc8))keys|=1u<<NavUp;if(desktop_key(0xd0))keys|=1u<<NavDown;
        if(desktop_key(0xcb))keys|=1u<<NavLeft;if(desktop_key(0xcd))keys|=1u<<NavRight;
    }
    if(!g.open.empty()){inject::poll();for(Device* d:g.open)poll(*d);log_activity();}
    std::uint32_t held=keys;
    if(!g.open.empty())for(unsigned i=0;i<ButtonCount;++i)if(value(ButtonActions[i])>=0.5)held|=1u<<i;
    held|=gear_pulses(runtime);
    const std::uint32_t down=held&~g.previous_buttons;
    g.previous_buttons=held;
    auto on=[](std::uint32_t bits,Button b){return (bits&(1u<<b))!=0;};
    const Bound* steer_binding=g.open.empty()?nullptr:find("steer");
    std::int32_t steer=0;std::uint32_t accel=0,brake=0;
    if(held||steer_binding)out.connected=true;
    if(steer_binding){
        // The full-range steering word, pedals 0..255: the record carries the wheel itself (5A7B50[0]: steer
        // word 0x0E, accelerator 0x0D, brake 0x0C), not the pad through 406FA0.
        double n=value("steer");
        if(g.dead_zone>0.f){n=std::fabs(n)<g.dead_zone?0.0:(n>0?1.0:-1.0)*(std::fabs(n)-g.dead_zone)/(1.0-g.dead_zone);}
        n=std::clamp(n*double(g.sensitivity),-1.0,1.0);
        steer=std::int32_t(std::lround(n*32767.0));
        accel=std::uint32_t(std::lround(value("throttle")*255.0));
        brake=std::uint32_t(std::lround(value("brake")*255.0));
        // The PC layer stays on for the keyboard (its own device and key tables). The last device used drives the
        // race: a key pressed while the pedals and wheel buttons are at rest hands it to the layer's record, until
        // the wheel turns 3% from where it was then, a pedal is pressed or a wheel button is held. Without force
        // feedback nothing centres the wheel, so its resting position is not a test of idleness.
        const bool wheel_pressed=accel>=3u||brake>=3u||(held&~keys)!=0u;
        if(!g.keyboard_drives&&outrun::desktop::desktop_any_key()&&!wheel_pressed){g.keyboard_drives=true;g.keyboard_anchor=steer;}
        else if(g.keyboard_drives&&(wheel_pressed||std::abs(steer-g.keyboard_anchor)>983))g.keyboard_drives=false;
        out.pc_overlay=true;
        out.pc_overlay_yield=g.keyboard_drives;
        out.steering=std::clamp(steer/256,-128,127);
        out.accelerator=std::max(out.accelerator,accel);
        out.brake=std::max(out.brake,brake);
        auto& d=out.pc_device;
        d.axes_94[0x0e]=std::int16_t(steer);
        d.axes_94[0x0d]=std::int16_t(std::max<std::uint32_t>(std::uint16_t(d.axes_94[0x0d]),accel));
        d.axes_94[0x0c]=std::int16_t(std::max<std::uint32_t>(std::uint16_t(d.axes_94[0x0c]),brake));
    }
    auto& d=out.pc_device;
    if(on(held,Start))d.buttons_04|=0x1u;
    if(on(held,ShiftUp))d.buttons_04|=0x2u;
    if(on(held,Back))d.buttons_04|=0x4u;
    if(on(held,ShiftDown))d.buttons_04|=0x8u;
    if(on(held,Camera))d.buttons_04|=0x10u;
    // Outside the race: confirm answers the result prompts (0x2); left / right are the D-pad bits (0x80 / 0x100,
    // which would be digital steering in the race).
    if(!race){
        if(on(held,Confirm))d.buttons_04|=0x2u;
        if(on(held,NavRight))d.buttons_04|=0x80u;
        if(on(held,NavLeft))d.buttons_04|=0x100u;
    }
    out.shift_up|=on(down,ShiftUp);out.shift_down|=on(down,ShiftDown);
    out.menu_up|=on(down,NavUp);out.menu_down|=on(down,NavDown);
    out.menu_left|=on(down,NavLeft);out.menu_right|=on(down,NavRight);
    out.menu_confirm|=on(down,Confirm)||on(down,Start);out.menu_cancel|=on(down,Back);
    out.start|=on(down,Start);
    out.horn|=on(held,Horn);   // [Audio] AllowHorn: its own action, no record bit (native_race_horn)
    // Frontend features (48F5F0), as the pad adapter sets them from its button edges.
    auto& f=out.frontend.feature_mask;
    if(on(down,Start))f|=1u;
    if(on(down,Confirm))f|=4u;
    if(on(down,Back))f|=8u;
    if(on(down,NavUp))f|=0x400u;
    if(on(down,NavDown))f|=0x800u;
    if(on(down,NavLeft))f|=0x1000u;
    if(on(down,NavRight))f|=0x2000u;
    if(!race){if(on(down,ShiftDown))f|=0x10u;if(on(down,ShiftUp))f|=0x20u;}
    if(on(down,Camera))f|=0x40000u;
    if(inject::armed()){
        inject::ObserveData o{};
        o.gameMode=mode;o.inGameplay=race?1u:0u;
        auto raw_of=[](const char* a){const auto* b=find(a);return b?std::int32_t(raw(*b)):-1;};
        o.rawSteer=raw_of("steer");o.rawThrottle=raw_of("throttle");o.rawBrake=raw_of("brake");
        o.recordSteer=d.axes_94[0x0e];o.recordAccel=std::uint16_t(d.axes_94[0x0d]);o.recordBrake=std::uint16_t(d.axes_94[0x0c]);
        o.recordButtons=d.buttons_04;
        if(runtime){
            const auto& a=runtime->race.car_world.analog_7d6810.current;
            o.gameSteer=a[0];o.gameAccel=a[1];o.gameBrake=a[2];
            const auto* car=runtime->event_function36.car_select.car_799d18.data();
            std::memcpy(&o.speed,car+0x1c4,4);std::memcpy(&o.gear,car+0x208,4);
            std::memcpy(&o.car34,car+0x34,4);std::memcpy(&o.car38,car+0x38,4);
        }
        std::vector<std::string> log;inject::observe(o,log);print(log);
    }
}

bool wheel_input_binding_edit_allowed(){return g.configured&&g.started&&g.input&&!inject::armed();}
std::vector<dbce::controls::edit::AcceptedObservation> wheel_input_binding_observations(){
    if(!wheel_input_binding_edit_allowed())return {};
    std::vector<bindings::NativeSample> samples;
    for(const Device* d:g.open){
        if(!d->device||!d->connected)continue;   // this tick's read failed: no sample
        bindings::NativeSample s;
        s.dev=d->dev;s.prod=d->prod;s.name=d->name;s.epoch=d->opens;s.sequence=d->reads;s.buttons=d->buttons;s.povs=d->povs;
        s.axis_mask=d->axes_found.present;s.range_mask=d->axes_found.ranged;
        for(int i=0;i<8;++i)s.axes[std::size_t(i)]=axis(d->state,i);
        std::copy(std::begin(d->state.rgbButtons),std::end(d->state.rgbButtons),s.button_state.begin());
        std::copy(std::begin(d->state.rgdwPOV),std::end(d->state.rgdwPOV),s.pov_state.begin());
        samples.push_back(std::move(s));
    }
    try{return bindings::native_observations(samples,double(GetTickCount64())/1000.0);}
    catch(...){return {};}   // capture unavailability must not change ordinary controls/gameplay
}

void wheel_input_devices_changed(){devices_changed.store(true);}

void wheel_input_shutdown(){
    if(g.started)std::printf("wheel input: rescans after start %u, longest %.1f ms\n",g.rescans,g.rescan_ms_max);
    for(Device* d:g.open)if(d->device){d->device->Unacquire();d->device->Release();d->device=nullptr;}
    g.open.clear();
    if(g.input){g.input->Release();g.input=nullptr;}
}
}
