// The DirectInput 8 provider (dinput_provider.hpp; INPUT-SHIM.md section 6).
#include "dinput_provider.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
namespace dbce::shim {
namespace {
std::string narrow(const wchar_t* w){std::string s;for(;w&&*w;++w)s.push_back(*w<128?char(*w):'?');return s;}
constexpr DWORD AxisOffsets[8]{DIJOFS_X,DIJOFS_Y,DIJOFS_Z,DIJOFS_RX,DIJOFS_RY,DIJOFS_RZ,DIJOFS_SLIDER(0),DIJOFS_SLIDER(1)};
BOOL CALLBACK collect(const DIDEVICEINSTANCEW* d,void* user){
    static_cast<std::vector<DeviceInfo>*>(user)->push_back({guid_text(d->guidInstance),guid_text(d->guidProduct),narrow(d->tszProductName)});
    return DIENUM_CONTINUE;
}
}

std::string guid_text(const GUID& v){
    char t[40];
    std::snprintf(t,sizeof t,"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",v.Data1,v.Data2,v.Data3,
        v.Data4[0],v.Data4[1],v.Data4[2],v.Data4[3],v.Data4[4],v.Data4[5],v.Data4[6],v.Data4[7]);
    return t;
}
bool guid_parse(const std::string& s,GUID& g){
    unsigned long d1=0;unsigned d2=0,d3=0,b[8]{};
    if(s.size()!=38)return false;
    if(std::sscanf(s.c_str(),"{%8lx-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}",&d1,&d2,&d3,&b[0],&b[1],&b[2],&b[3],&b[4],&b[5],&b[6],&b[7])!=11)return false;
    g.Data1=d1;g.Data2=std::uint16_t(d2);g.Data3=std::uint16_t(d3);
    for(int i=0;i<8;++i)g.Data4[i]=std::uint8_t(b[i]);
    return guid_text(g)==s;   // exactly the canonical form, lower case
}

Call DInputProvider::classify(HRESULT hr){
    if(SUCCEEDED(hr))return Call::Ok;   // DI_OK, DI_NOEFFECT (S_FALSE), DI_POLLEDDEVICE, ...
    if(hr==DIERR_INPUTLOST||hr==DIERR_NOTACQUIRED)return Call::AcquisitionLost;
    return Call::Gone;                  // DIERR_UNPLUGGED and anything else
}

DInputProvider::DInputProvider(IDirectInput8W* input,HWND window):input_(input),window_(window){}
DInputProvider::~DInputProvider(){if(!shut_)shutdown(0);}

DInputProvider::Slot* DInputProvider::slot(Handle h)const{
    const auto index=h&0xffu;
    if(index<1||index>Handle(MaxSlots))return nullptr;
    auto* s=const_cast<Slot*>(&slots_[std::size_t(index-1)]);
    return s->used.load()&&s->generation.load()==std::uint32_t(h>>8)?s:nullptr;
}
bool DInputProvider::enter(Slot& s){
    int v=s.state.load();
    while(v>=0)if(s.state.compare_exchange_weak(v,v+1))return true;
    return false;
}
void DInputProvider::leave(Slot& s){s.state.fetch_sub(1);}

std::vector<DeviceInfo> DInputProvider::enumerate(){
    std::vector<DeviceInfo> out;
    if(input_)input_->EnumDevices(DI8DEVCLASS_GAMECTRL,collect,&out,DIEDFL_ATTACHEDONLY);
    return out;
}

OpenResult DInputProvider::open(const DeviceInfo& listed){
    OpenResult r;
    GUID instance{};
    if(!input_){r.why="no DirectInput";return r;}
    if(!guid_parse(listed.dev,instance)){r.why="not an instance GUID: "+listed.dev;return r;}
    IDirectInputDevice8W* d=nullptr;
    HRESULT hr=input_->CreateDevice(instance,&d,nullptr);
    if(FAILED(hr)||!d){r.why="CreateDevice failed";return r;}
    auto fail=[&](const std::string& why){d->Release();r=OpenResult{};r.why=why;return r;};
    if(FAILED(d->SetDataFormat(&c_dfDIJoystick2)))return fail("SetDataFormat failed");
    if(FAILED(d->SetCooperativeLevel(window_,DISCL_BACKGROUND|DISCL_NONEXCLUSIVE)))return fail("SetCooperativeLevel failed");
    // Requests only: what counts is what is read back below.
    DIPROPRANGE range{};range.diph.dwSize=sizeof range;range.diph.dwHeaderSize=sizeof range.diph;range.diph.dwHow=DIPH_DEVICE;
    range.lMin=0;range.lMax=65535;
    (void)d->SetProperty(DIPROP_RANGE,&range.diph);
    DIPROPDWORD zone{};zone.diph.dwSize=sizeof zone;zone.diph.dwHeaderSize=sizeof zone.diph;zone.diph.dwHow=DIPH_DEVICE;zone.dwData=0;
    (void)d->SetProperty(DIPROP_DEADZONE,&zone.diph);
    // Objects by offset, never by counting; each axis's range read back by offset.
    auto has=[&](DWORD offset){DIDEVICEOBJECTINSTANCEW oi{};oi.dwSize=sizeof oi;return SUCCEEDED(d->GetObjectInfo(&oi,offset,DIPH_BYOFFSET));};
    for(int a=0;a<8;++a){
        const auto i=std::size_t(a);
        r.caps.axis[i]=has(AxisOffsets[a]);
        if(!r.caps.axis[i])continue;
        DIPROPRANGE back{};back.diph.dwSize=sizeof back;back.diph.dwHeaderSize=sizeof back.diph;back.diph.dwHow=DIPH_BYOFFSET;back.diph.dwObj=AxisOffsets[a];
        if(SUCCEEDED(d->GetProperty(DIPROP_RANGE,&back.diph))&&back.lMax>back.lMin){
            r.caps.range_ok[i]=true;r.caps.min[i]=std::int32_t(back.lMin);r.caps.max[i]=std::int32_t(back.lMax);
        }
    }
    for(int b=0;b<128;++b)if(has(DWORD(DIJOFS_BUTTON(b))))r.caps.button.set(std::size_t(b));
    for(int p=0;p<4;++p)r.caps.pov[std::size_t(p)]=has(DWORD(DIJOFS_POV(p)));
    // The identity of the handle actually opened.
    DIDEVICEINSTANCEW di{};di.dwSize=sizeof di;
    if(FAILED(d->GetDeviceInfo(&di)))return fail("cannot read the opened device's identity");
    r.opened={guid_text(di.guidInstance),guid_text(di.guidProduct),narrow(di.tszProductName)};
    (void)d->Acquire();   // a refusal shows as NOTACQUIRED at the first read, and takes the reacquisition path
    for(int i=0;i<MaxSlots;++i){
        auto& s=slots_[std::size_t(i)];
        bool expected=false;
        if(!s.used.load()&&s.used.compare_exchange_strong(expected,true)){
            s.device=d;s.state.store(0);
            const auto generation=s.generation.fetch_add(1)+1;
            r.ok=true;r.handle=(Handle(generation)<<8)|Handle(i+1);
            return r;
        }
    }
    return fail("no free device slot");
}

Call DInputProvider::reacquire(Handle h){
    Slot* s=slot(h);
    if(!s||!enter(*s)){++misuse_;return Call::Gone;}
    const auto c=classify(s->device->Acquire());
    leave(*s);
    return c;
}

void DInputProvider::close(Handle h){
    Slot* s=slot(h);
    if(!s){++misuse_;return;}
    int idle=0;
    if(!s->state.compare_exchange_strong(idle,-1)){++misuse_;return;}   // a read is in progress (or already closed)
    s->device->Unacquire();
    s->device->Release();
    s->device=nullptr;
    s->used.store(false);   // reusable; this handle's generation no longer matches once the slot is reopened
}

Call DInputProvider::poll(Handle h){
    Slot* s=slot(h);
    if(!s||!enter(*s)){++misuse_;return Call::Gone;}
    const auto c=classify(s->device->Poll());
    leave(*s);
    return c;
}

Call DInputProvider::state(Handle h,RawState& out){
    Slot* s=slot(h);
    if(!s||!enter(*s)){++misuse_;return Call::Gone;}
    DIJOYSTATE2 st;
    std::memset(&st,0,sizeof st);
    for(auto& p:st.rgdwPOV)p=0xFFFFFFFFu;
    const auto c=classify(s->device->GetDeviceState(sizeof st,&st));
    leave(*s);
    if(c!=Call::Ok)return c;   // nothing is copied from a failed read
    const LONG* axes=&st.lX;
    out.axes[0]=std::int32_t(axes[0]);out.axes[1]=std::int32_t(axes[1]);out.axes[2]=std::int32_t(axes[2]);
    out.axes[3]=std::int32_t(axes[3]);out.axes[4]=std::int32_t(axes[4]);out.axes[5]=std::int32_t(axes[5]);
    out.axes[6]=std::int32_t(st.rglSlider[0]);out.axes[7]=std::int32_t(st.rglSlider[1]);
    std::memcpy(out.buttons,st.rgbButtons,128);
    for(int i=0;i<4;++i)out.povs[i]=st.rgdwPOV[i];
    return c;
}

std::int64_t DInputProvider::now_us(){
    static const LONGLONG f=[]{LARGE_INTEGER q;QueryPerformanceFrequency(&q);return q.QuadPart;}();
    LARGE_INTEGER c;QueryPerformanceCounter(&c);
    return std::int64_t(c.QuadPart/f*1000000+(c.QuadPart%f)*1000000/f);
}

DInputProvider::ShutdownReport DInputProvider::shutdown(int wait_ms){
    ShutdownReport rep;
    shut_=true;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(wait_ms);
    for(int i=0;i<MaxSlots;++i){
        auto& s=slots_[std::size_t(i)];
        if(!s.used.load()||s.state.load()<0)continue;
        for(;;){
            int idle=0;
            if(s.state.compare_exchange_strong(idle,-1)){s.device->Unacquire();s.device->Release();s.device=nullptr;++rep.released;break;}
            if(std::chrono::steady_clock::now()>=deadline){
                ++rep.leaked;
                rep.detail+="device slot "+std::to_string(i+1)+" is still being read; left unreleased (process exit reclaims it). ";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if(!rep.leaked&&input_){input_->Release();input_=nullptr;rep.input_released=true;}
    else if(rep.leaked)rep.detail+="DirectInput left unreleased while a device is held.";
    return rep;
}
}
