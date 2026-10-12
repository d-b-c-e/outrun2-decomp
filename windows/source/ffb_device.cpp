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
// The HRESULTs the mod treats as transient (focus or acquisition lost, the library recreating its effect): silence and
// wait for an accepted zero, never a release.
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
    case Phase::Recovering:return "recovering";
    case Phase::Released:return "released";
    case Phase::Stopped:return "stopped";
    }
    return "?";
}

bool Delivery::start(const WheelFfbApi& api,const Config& cfg){
    *this=Delivery{};
    api_=api;cfg_=cfg;
    if(auto why=load_refusal(cfg);!why.empty()){why_=why;return false;}
    parse_guid(cfg.steer_dev,guid_);
    const struct { const void* fn; const char* name; } used[]{
        {reinterpret_cast<const void*>(api.GetWheelFfbVersion),"GetWheelFfbVersion"},
        {reinterpret_cast<const void*>(api.SetHoldTimeoutMs),"SetHoldTimeoutMs"},
        {reinterpret_cast<const void*>(api.SetPreferredDevice),"SetPreferredDevice"},
        {reinterpret_cast<const void*>(api.SetPreferredDeviceIndex),"SetPreferredDeviceIndex"},
        {reinterpret_cast<const void*>(api.SetPreferredDeviceGuid),"SetPreferredDeviceGuid"},
        {reinterpret_cast<const void*>(api.SetStrictDeviceSelection),"SetStrictDeviceSelection"},
        {reinterpret_cast<const void*>(api.EnumerateDevices),"EnumerateDevices"},
        {reinterpret_cast<const void*>(api.GetDeviceGuid),"GetDeviceGuid"},
        {reinterpret_cast<const void*>(api.InitDirectInput),"InitDirectInput"},
        {reinterpret_cast<const void*>(api.InstallExitGuards),"InstallExitGuards"},
        {reinterpret_cast<const void*>(api.SetDeviceForcesXY),"SetDeviceForcesXY"},
        {reinterpret_cast<const void*>(api.GetLastHResult),"GetLastHResult"},
        {reinterpret_cast<const void*>(api.ZeroForces),"ZeroForces"},
        {reinterpret_cast<const void*>(api.PanicStop),"PanicStop"},
        {reinterpret_cast<const void*>(api.FreeDirectInput),"FreeDirectInput"}};
    for(const auto& u:used)if(!u.fn){why_=std::string("WheelFfb.dll has no ")+u.name;phase_=Phase::Released;return false;}
    if(const int v=api_.GetWheelFfbVersion();v<MinVersion){
        why_="WheelFfb.dll "+std::to_string(v)+" is older than "+std::to_string(MinVersion);phase_=Phase::Released;return false;}
    api_.SetHoldTimeoutMs(HoldMs);
    phase_=Phase::Waiting;
    return true;
}

void Delivery::zero_best_effort(){
    if(!silent_){api_.ZeroForces();silent_=true;++counts_.zeroes;}
}

void Delivery::release(const std::string& why){
    if(opened_){api_.ZeroForces();api_.FreeDirectInput();opened_=false;}
    silent_=true;phase_=Phase::Released;why_=why;
}

// A refused command: transient ones wait for an accepted zero, any other releases the device.
bool Delivery::accept(int result){
    if(result==1)return true;
    const long hr=api_.GetLastHResult();
    if(!transient(hr)){release("the wheel refused output (HRESULT "+hresult(hr)+")");return false;}
    ++counts_.transient;
    api_.ZeroForces();silent_=true;
    if(phase_!=Phase::Recovering){phase_=Phase::Recovering;recovering_clock_=false;}
    why_="output interrupted (HRESULT "+hresult(hr)+"); waiting for an accepted zero";
    return false;
}

void Delivery::open(const Sample& s){
    api_.SetPreferredDevice(nullptr);
    api_.SetPreferredDeviceIndex(-1);
    api_.SetPreferredDeviceGuid(&guid_);
    api_.SetStrictDeviceSelection(1);
    bool listed=false;
    const int n=api_.EnumerateDevices();
    for(int i=0;i<n&&!listed;++i){GUID g{};listed=api_.GetDeviceGuid(i,&g)&&IsEqualGUID(g,guid_);}
    if(!listed){release("the steering device "+cfg_.steer_dev+" is not one of the force-feedback devices");return;}
    opened_=true;   // FreeDirectInput is owed from here, also after a refused open
    if(!api_.InitDirectInput(static_cast<int>(reinterpret_cast<std::intptr_t>(s.hwnd)))){
        release("the steering device refused the open (HRESULT "+hresult(api_.GetLastHResult())+")");return;}
    api_.InstallExitGuards();
    // Start through an accepted neutral command, never a retained effect's parameters; a refused zero is a failed open.
    if(api_.SetDeviceForcesXY(0,0)!=1){
        release("the wheel refused the starting zero (HRESULT "+hresult(api_.GetLastHResult())+")");return;}
    ++counts_.zeroes;silent_=true;need_zero_=false;
    phase_=Phase::Open;why_.clear();
}

void Delivery::frame(const Sample& s,std::int32_t command){
    if(phase_!=Phase::Waiting&&phase_!=Phase::Open&&phase_!=Phase::Recovering)return;
    if(phase_==Phase::Waiting){
        if(!s.race||!s.front)return;
        if(s.steer_dev!=cfg_.steer_dev){why_="waiting: the input layer does not read "+cfg_.steer_dev+" as the steering device";return;}
        open(s);
        if(phase_!=Phase::Open)return;
    }
    // One device epoch: the opened instance must stay the input layer's steering device.
    if(s.steer_dev!=cfg_.steer_dev){release("the steering device changed or was lost");return;}
    if(!s.race||!s.front){
        zero_best_effort();
        need_zero_=true;recovering_clock_=false;
        return;
    }
    if(phase_==Phase::Recovering){
        if(!recovering_clock_){recovering_clock_=true;recovery_start_=s.ms;}
        if(s.ms-recovery_start_>=RecoveryMs){release("no accepted zero within 2 s of an interrupted output");return;}
        if(!accept(api_.SetDeviceForcesXY(0,0)))return;
        phase_=Phase::Open;recovering_clock_=false;need_zero_=false;silent_=true;why_.clear();
        ++counts_.recoveries;++counts_.zeroes;
    }else if(need_zero_){
        if(!accept(api_.SetDeviceForcesXY(0,0)))return;
        need_zero_=false;silent_=true;++counts_.zeroes;
    }
    const std::int32_t c=std::clamp<std::int32_t>(command,-10000,10000);
    if(!accept(api_.SetDeviceForcesXY(c,0)))return;
    ++counts_.sent;silent_=c==0;
}

void Delivery::shutdown(){
    if(opened_){api_.PanicStop();api_.FreeDirectInput();opened_=false;}
    if(phase_!=Phase::Off)phase_=Phase::Stopped;
}
}
