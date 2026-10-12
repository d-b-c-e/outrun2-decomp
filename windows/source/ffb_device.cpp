// OutRun force feedback, F4 (ffb_device.hpp): the gates between the force stream and the wheel.
#define DIRECTINPUT_VERSION 0x0800
#include "ffb_device.hpp"
#include <dinput.h>
#include <algorithm>
#include <cstdio>
namespace outrun::desktop::ffbdev {
namespace {
int hex(char c){
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
// The HRESULTs the mod treats as transient (focus or acquisition lost, the library recreating its effect): no release
// yet, but an accepted zero is due.
bool transient(long hr){
    return hr==DIERR_NOTEXCLUSIVEACQUIRED||hr==DIERR_INPUTLOST||hr==DIERR_NOTACQUIRED||hr==E_HANDLE||hr==DIERR_NOTDOWNLOADED;
}
std::string hresult(long hr){char t[16];std::snprintf(t,sizeof t,"0x%08lX",static_cast<unsigned long>(hr));return t;}
}

bool parse_guid(const std::string& s,GUID& out){
    if(s.size()!=38||s.front()!='{'||s.back()!='}'||s[9]!='-'||s[14]!='-'||s[19]!='-'||s[24]!='-')return false;
    unsigned char b[16];unsigned n=0;
    for(std::size_t i=1;i<37;){
        if(s[i]=='-'){++i;continue;}
        const int hi=hex(s[i]),lo=hex(s[i+1]);
        if(hi<0||lo<0)return false;
        b[n++]=static_cast<unsigned char>(hi*16+lo);i+=2;
    }
    if(n!=16)return false;
    out.Data1=(unsigned long)b[0]<<24|(unsigned long)b[1]<<16|(unsigned long)b[2]<<8|b[3];
    out.Data2=static_cast<unsigned short>(b[4]<<8|b[5]);
    out.Data3=static_cast<unsigned short>(b[6]<<8|b[7]);
    for(unsigned k=0;k<8;++k)out.Data4[k]=b[8+k];
    return true;
}

std::string load_refusal(const Config& c){
    if(!c.output)return "[FFB] Output is not wheel (the default: no force output)";
    if(c.muted)return "DBCE_FFB_MUTE=1";
    if(c.latched)return "a latched (test) process";
    if(c.steer_dev.empty())return "[Controls] steer names no device";
    GUID g;
    if(!parse_guid(c.steer_dev,g))return "[Controls] steer dev= is not an instance GUID";
    return {};
}

const char* phase_name(Phase p){
    switch(p){
    case Phase::Off:return "off";
    case Phase::Waiting:return "waiting";
    case Phase::Open:return "open";
    case Phase::Released:return "released";
    case Phase::Stopped:return "stopped";
    }
    return "?";
}

bool Delivery::start(const WheelFfbApi& api,const Config& cfg){
    if(phase_!=Phase::Off||native_){why_="started twice";return false;}   // never discard a started lifecycle
    api_=api;cfg_=cfg;why_.clear();
    if(auto why=load_refusal(cfg);!why.empty()){why_=why;return false;}
    parse_guid(cfg.steer_dev,guid_);
    const struct { const void* fn; const char* name; } used[]{
        {reinterpret_cast<const void*>(api.GetWheelFfbVersion),"GetWheelFfbVersion"},
        {reinterpret_cast<const void*>(api.SetPreferredDevice),"SetPreferredDevice"},
        {reinterpret_cast<const void*>(api.SetPreferredDeviceIndex),"SetPreferredDeviceIndex"},
        {reinterpret_cast<const void*>(api.SetPreferredDeviceGuid),"SetPreferredDeviceGuid"},
        {reinterpret_cast<const void*>(api.SetStrictDeviceSelection),"SetStrictDeviceSelection"},
        {reinterpret_cast<const void*>(api.EnumerateDevices),"EnumerateDevices"},
        {reinterpret_cast<const void*>(api.GetDeviceGuid),"GetDeviceGuid"},
        {reinterpret_cast<const void*>(api.InitDirectInput),"InitDirectInput"},
        {reinterpret_cast<const void*>(api.InstallExitGuards),"InstallExitGuards"},
        {reinterpret_cast<const void*>(api.SetHoldTimeoutMs),"SetHoldTimeoutMs"},
        {reinterpret_cast<const void*>(api.SetDeviceForcesXY),"SetDeviceForcesXY"},
        {reinterpret_cast<const void*>(api.GetLastHResult),"GetLastHResult"},
        {reinterpret_cast<const void*>(api.PanicStop),"PanicStop"},
        {reinterpret_cast<const void*>(api.FreeDirectInput),"FreeDirectInput"}};
    for(const auto& u:used)if(!u.fn){why_=std::string("WheelFfb.dll has no ")+u.name;phase_=Phase::Released;return false;}
    // The shim reads the wheel through the library's read slot while the library holds it (wheelffb_bridge.hpp).
    const struct { const void* fn; const char* name; } shim[]{
        {reinterpret_cast<const void*>(api.EnumerateAllDevices),"EnumerateAllDevices"},
        {reinterpret_cast<const void*>(api.GetAnyDeviceGuid),"GetAnyDeviceGuid"},
        {reinterpret_cast<const void*>(api.OpenReadDevice),"OpenReadDevice"},
        {reinterpret_cast<const void*>(api.ReadDeviceStateWithPov),"ReadDeviceStateWithPov"},
        {reinterpret_cast<const void*>(api.CloseReadDevices),"CloseReadDevices"},
        {reinterpret_cast<const void*>(api.GetReadSlotCaps),"GetReadSlotCaps"}};
    if(cfg.shim)for(const auto& u:shim)if(!u.fn){
        why_=std::string("WheelFfb.dll has no ")+u.name+" (0.11.0 or newer is needed with Input = Shim)";phase_=Phase::Released;return false;}
    if(const int v=api_.GetWheelFfbVersion();v<MinVersion){
        why_="WheelFfb.dll "+std::to_string(v)+" is older than "+std::to_string(MinVersion);phase_=Phase::Released;return false;}
    phase_=Phase::Waiting;
    return true;
}

bool Delivery::same_device(const std::string& dev)const{
    GUID g{};
    return parse_guid(dev,g)&&IsEqualGUID(g,guid_);
}

// Stops what the library holds: PanicStop for an opened device, then FreeDirectInput (effects stopped, the exit
// guards' window subclass removed, the hold watchdog joined) whenever the library was given a selection. With an
// Owner the input stops reading through the library before FreeDirectInput and takes the wheel back after it; a
// failed open hands it back only once the library has freed it, so two owners never hold the wheel at once.
// The library gate (owner mode): entered once per sample or release, re-entrant within it.
struct Delivery::Gate {
    Delivery& d;bool entered{};
    explicit Gate(Delivery& x):d(x){}
    bool enter(){
        if(!d.owner_||d.in_library_)return true;
        if(!d.owner_->enter_library())return false;
        d.in_library_=entered=true;return true;
    }
    ~Gate(){if(entered){d.in_library_=false;d.owner_->leave_library();}}
};

void Delivery::quiesce(){
    Gate gate(*this);
    if(owner_&&(opened_||native_||held_)&&!gate.enter()){
        // The input's worker is inside the library: calling it would wait for that worker. The library keeps the
        // wheel (its hold watchdog silences it); the input stops reading through it but does not take it back.
        if(held_&&!retained_)owner_->begin_release();
        if(!retained_)why_="the wheel is left to the library's hold watchdog: a shim call is still inside the library";
        retained_=true;
        return;
    }
    retained_=false;
    if(opened_)api_.PanicStop();
    if(owner_&&held_)owner_->begin_release();
    if(native_)api_.FreeDirectInput();
    if(owner_&&held_)owner_->end_release();
    if(owner_&&(promoting_||handing_over_))owner_->end_open(false);
    native_=opened_=handing_over_=promoting_=held_=shared_=false;
    silent_=true;
}

void Delivery::set_owner(Owner* o){if(phase_==Phase::Waiting||phase_==Phase::Off)owner_=o;}

void Delivery::release(const std::string& why){
    quiesce();
    phase_=Phase::Released;
    if(!retained_)why_=why;else why_=why+"; "+why_;
}

void Delivery::open_device(const Sample& s){
    native_=true;   // the selection below may create the library's DirectInput state: FreeDirectInput is owed
    api_.SetPreferredDevice(nullptr);
    api_.SetPreferredDeviceIndex(-1);
    api_.SetPreferredDeviceGuid(&guid_);
    api_.SetStrictDeviceSelection(1);
    bool listed=false;
    const int n=api_.EnumerateDevices();
    for(int i=0;i<n&&!listed;++i){GUID g{};listed=api_.GetDeviceGuid(i,&g)&&IsEqualGUID(g,guid_);}
    if(!listed){release("the steering device "+cfg_.steer_dev+" is not one of the force-feedback devices");return;}
    // The ABI takes the window as an int; a window handle is 32 bits wide by design, also in 64-bit Windows.
    if(!api_.InitDirectInput(static_cast<int>(reinterpret_cast<std::intptr_t>(s.hwnd)))){
        release("the steering device refused the open (HRESULT "+hresult(api_.GetLastHResult())+")");return;}
    opened_=true;
    if(!api_.InstallExitGuards()){release("the exit guards could not be installed on the game window");return;}
    api_.SetHoldTimeoutMs(HoldMs);
    // Start through an accepted neutral command, never a retained effect's parameters; a refused zero is a failed open.
    if(api_.SetDeviceForcesXY(0,0)!=1){
        release("the wheel refused the starting zero (HRESULT "+hresult(api_.GetLastHResult())+")");return;}
    ++counts_.zeroes;
    silent_=true;need_zero_=unacked_=false;last_value_=0;last_send_ms_=s.ms;
    if(owner_){owner_->end_open(true);promoting_=false;held_=true;shared_=false;opened_ms_=s.ms;}
    phase_=Phase::Open;why_.clear();
}

// A refused SetDeviceForcesXY: the wheel may still hold the previous force. Transient refusals leave an accepted zero
// due (tried every sample, SilenceMs at most); any other releases.
bool Delivery::refused(const Sample& s){
    const long hr=api_.GetLastHResult();
    if(!transient(hr)){release("the wheel refused output (HRESULT "+hresult(hr)+")");return false;}
    ++counts_.transient;
    silent_=false;need_zero_=true;
    if(!unacked_){unacked_=true;unacked_ms_=s.ms;}
    why_="output interrupted (HRESULT "+hresult(hr)+"); waiting for an accepted zero";
    return false;
}

bool Delivery::zero(const Sample& s){
    if(unacked_&&s.ms-unacked_ms_>=SilenceMs){release("no accepted zero within 2 s");return false;}
    if(api_.SetDeviceForcesXY(0,0)!=1){refused(s);return false;}
    if(unacked_)++counts_.recoveries;
    ++counts_.zeroes;
    silent_=true;need_zero_=unacked_=false;last_value_=0;last_send_ms_=s.ms;why_.clear();
    return true;
}

void Delivery::frame(const Sample& s,std::int32_t command){
    if(phase_!=Phase::Waiting&&phase_!=Phase::Open)return;
    Gate gate(*this);
    if(!gate.enter())return;   // the input's worker is inside the library: no library call this sample
    if(phase_==Phase::Waiting){
        if(!s.race||!s.front||!same_device(s.steer_dev)){
            // Out of the race or the front: a hand-over in progress is cancelled (the input reads the wheel again).
            if(handing_over_){owner_->end_open(false);handing_over_=false;}
            if(s.race&&s.front)why_="waiting: the input layer does not read "+cfg_.steer_dev+" as the steering device";
            return;
        }
        if(cfg_.shim&&!owner_){why_="waiting: the shim input has no owner hand-over for the wheel";return;}
        if(owner_){
            if(!handing_over_){
                if(!s.steer_live){why_="waiting: the steering input is not live";return;}
                e0_=s.steer_epoch;handing_over_=true;   // the hand-over itself ends this epoch: liveness is not asked again
            }
            if(!owner_->begin_open()){why_="waiting: the shim is handing the wheel over";return;}
            handing_over_=false;promoting_=true;          // the input's own handle is closed: the open goes on
        }
        open_device(s);
        if(phase_!=Phase::Open)return;
    }
    // One device epoch: the opened instance must stay the input layer's steering device.
    if(!same_device(s.steer_dev)){release("the steering device changed or was lost");return;}
    bool drive=s.race&&s.front;
    if(owner_){
        // The force follows the steering read through the force handle: a new epoch after the open, then that one only.
        if(!shared_){
            if(s.steer_live&&s.steer_epoch!=e0_){shared_=true;e1_=s.steer_epoch;}
            else if(s.ms-opened_ms_>=SilenceMs){release("the steering input did not resume through the force device within 2 s");return;}
        }else if(s.steer_epoch!=e1_){release("the steering device's input epoch changed");return;}
        drive=drive&&shared_&&s.steer_live;
    }
    // A held force older than the hold (since the last accepted command, whatever the sampling did): the library's
    // watchdog may have stopped the wheel, so it restarts through an accepted zero (romi 29717).
    const bool expired=last_value_!=0&&s.ms-last_send_ms_>std::uint64_t(HoldMs);
    if(expired||!drive)need_zero_=true;
    if(!drive){
        if(!silent_||unacked_)zero(s);    // until accepted, or released after SilenceMs
        return;
    }
    if(need_zero_&&!zero(s))return;
    const std::int32_t c=std::clamp<std::int32_t>(command,-10000,10000);
    const bool repeat=c==last_value_;
    if(repeat&&(c==0||s.ms-last_send_ms_<KeepAliveMs))return;   // a held zero needs no refresh (the watchdog acts on force)
    if(api_.SetDeviceForcesXY(c,0)!=1){refused(s);return;}
    ++counts_.sent;if(repeat)++counts_.keepalive;
    last_value_=c;last_send_ms_=s.ms;silent_=c==0;
}

void Delivery::shutdown(){
    quiesce();
    if(phase_!=Phase::Off)phase_=Phase::Stopped;
}
}
