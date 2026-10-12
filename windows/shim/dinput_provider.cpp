// The DirectInput 8 provider (dinput_provider.hpp; INPUT-SHIM.md section 6).
#include "dinput_provider.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <thread>
namespace dbce::shim {
namespace {
std::string narrow(const wchar_t* w){std::string s;for(;w&&*w;++w)s.push_back(*w<128?char(*w):'?');return s;}
// Identity must not use narrow()'s lossy '?' replacement. Read only this owned handle; unavailable metadata never
// prevents legacy input, but an adapter must not infer a RawInput match from an empty path.
std::string interface_path(IDirectInputDevice8W* device){
    DIPROPGUIDANDPATH value{};value.diph.dwSize=sizeof value;value.diph.dwHeaderSize=sizeof value.diph;value.diph.dwHow=DIPH_DEVICE;
    if(FAILED(device->GetProperty(DIPROP_GUIDANDPATH,&value.diph)))return {};
    const auto end=std::find(std::begin(value.wszPath),std::end(value.wszPath),L'\0');
    if(end==std::begin(value.wszPath)||end==std::end(value.wszPath))return {};
    const int length=int(end-std::begin(value.wszPath));
    const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.wszPath,length,nullptr,0,nullptr,nullptr);
    if(bytes<=0)return {};
    std::string result(std::size_t(bytes),'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.wszPath,length,result.data(),bytes,nullptr,nullptr)!=bytes)return {};
    return result;
}
constexpr DWORD AxisOffsets[8]{DIJOFS_X,DIJOFS_Y,DIJOFS_Z,DIJOFS_RX,DIJOFS_RY,DIJOFS_RZ,DIJOFS_SLIDER(0),DIJOFS_SLIDER(1)};
BOOL CALLBACK collect(const DIDEVICEINSTANCEW* d,void* user){
    static_cast<std::vector<DeviceInfo>*>(user)->push_back({guid_text(d->guidInstance),guid_text(d->guidProduct),narrow(d->tszProductName)});
    return DIENUM_CONTINUE;
}
// A slot's word: generation << 32 | low; low = CLOSED | RESERVED | readers.
constexpr std::uint32_t Closed=1u<<31,Reserved=1u<<30,Readers=Reserved-1u;
constexpr std::uint64_t pack(std::uint32_t generation,std::uint32_t low){return (std::uint64_t(generation)<<32)|low;}
constexpr std::uint32_t generation_of(std::uint64_t w){return std::uint32_t(w>>32);}
constexpr std::uint32_t low_of(std::uint64_t w){return std::uint32_t(w);}
std::atomic<DInputProvider::AdmissionHook> g_hook{nullptr};
std::atomic<void*> g_hook_user{nullptr};
std::atomic<DInputProvider::OpenHook> g_open_hook{nullptr};
std::atomic<void*> g_open_hook_user{nullptr};
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
    if(hr==E_PENDING)return Call::Pending;   // "data not yet available": transient, the device is not lost
    if(hr==DIERR_INPUTLOST||hr==DIERR_NOTACQUIRED)return Call::AcquisitionLost;
    return Call::Gone;                  // DIERR_UNPLUGGED and anything else
}

struct DInputProvider::State {
    struct Slot {
        std::atomic<std::uint64_t> word{pack(0,Closed)};   // free
        IDirectInputDevice8W* device{};                     // written only while the slot is RESERVED
    };
    IDirectInput8W* input{};
    HWND window{};
    std::array<Slot,MaxSlots> slots;
    std::atomic<bool> stopped{false};
    std::atomic<int> worker_ops{0};                         // listings and opens using DirectInput
    std::atomic<int> misuse{0},refused{0};
    // The last reference: no call can be inside any more, so whatever shutdown had to leave is released now.
    ~State(){
        for(auto& s:slots)if(s.device){s.device->Unacquire();s.device->Release();s.device=nullptr;}
        if(input){input->Release();input=nullptr;}
    }
    // A handle is generation << 8 | slot. Anything else is misuse: a slot out of range, or bits above the 32-bit
    // generation, which would otherwise be truncated into a valid one (slice 3.1 review, nene 10389).
    static bool decode(Handle h,std::size_t& index,std::uint32_t& generation){
        const auto slot=h&0xffu;
        if(slot<1||slot>Handle(MaxSlots)||(h>>40)!=0)return false;
        index=std::size_t(slot-1);generation=std::uint32_t(h>>8);
        return true;
    }
    // One compare-exchange checks the generation and the open state and adds a reader.
    Slot* enter(Handle h){
        std::size_t index{};std::uint32_t generation{};
        if(!decode(h,index,generation)){++misuse;return nullptr;}
        auto& s=slots[index];
        for(;;){
            std::uint64_t w=s.word.load(std::memory_order_acquire);
            if(const auto hook=g_hook.load())hook(g_hook_user.load());
            if(generation_of(w)!=generation||(low_of(w)&(Closed|Reserved))){++misuse;return nullptr;}
            if(stopped.load()){++refused;return nullptr;}
            if(s.word.compare_exchange_weak(w,w+1,std::memory_order_acq_rel))break;
        }
        if(stopped.load()){leave(s);++refused;return nullptr;}   // the stop raced this admission: back out
        return &s;
    }
    static void leave(Slot& s){s.word.fetch_sub(1,std::memory_order_acq_rel);}
    // A worker call that uses DirectInput itself (listing, open): counted so shutdown keeps DirectInput for it.
    bool begin_op(){
        worker_ops.fetch_add(1);
        if(stopped.load()){worker_ops.fetch_sub(1);++refused;return false;}
        return true;
    }
    void end_op(){worker_ops.fetch_sub(1);}
    // Releases an idle open slot (no reader); false when it is busy, free or closing.
    bool close_idle(Slot& s,std::uint64_t w){
        if((low_of(w)&(Closed|Reserved|Readers))!=0)return false;
        if(!s.word.compare_exchange_strong(w,pack(generation_of(w),Closed|Reserved)))return false;
        s.device->Unacquire();s.device->Release();s.device=nullptr;
        s.word.store(pack(generation_of(w),Closed),std::memory_order_release);   // free; the next open advances the generation
        return true;
    }
};

void DInputProvider::set_admission_hook_for_tests(AdmissionHook hook,void* user){g_hook_user.store(user);g_hook.store(hook);}
void DInputProvider::set_open_hook_for_tests(OpenHook hook,void* user){g_open_hook_user.store(user);g_open_hook.store(hook);}

DInputProvider::DInputProvider(IDirectInput8W* input,HWND window):state_(std::make_shared<State>()){state_->input=input;state_->window=window;}
DInputProvider::~DInputProvider(){if(!shut_)shutdown(0);state_.reset();}
int DInputProvider::misuse()const{return state_->misuse.load();}
int DInputProvider::refused_after_stop()const{return state_->refused.load();}

std::vector<DeviceInfo> DInputProvider::enumerate(){
    const auto st=state_;
    std::vector<DeviceInfo> out;
    if(!st->begin_op())return out;
    if(st->input)st->input->EnumDevices(DI8DEVCLASS_GAMECTRL,collect,&out,DIEDFL_ATTACHEDONLY);
    st->end_op();
    return out;
}

OpenResult DInputProvider::open(const DeviceInfo& listed){
    const auto st=state_;
    OpenResult r;
    GUID instance{};
    if(!guid_parse(listed.dev,instance)){r.why="not an instance GUID: "+listed.dev;return r;}
    if(!st->begin_op()){r.why="the provider is stopped";return r;}
    struct End{State& s;~End(){s.end_op();}} end{*st};
    if(!st->input){r.why="no DirectInput";return r;}
    IDirectInputDevice8W* d=nullptr;
    HRESULT hr=st->input->CreateDevice(instance,&d,nullptr);
    if(FAILED(hr)||!d){r.why="CreateDevice failed";return r;}
    auto fail=[&](const std::string& why){d->Release();r=OpenResult{};r.why=why;return r;};
    if(FAILED(d->SetDataFormat(&c_dfDIJoystick2)))return fail("SetDataFormat failed");
    if(FAILED(d->SetCooperativeLevel(st->window,DISCL_BACKGROUND|DISCL_NONEXCLUSIVE)))return fail("SetCooperativeLevel failed");
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
    r.opened={guid_text(di.guidInstance),guid_text(di.guidProduct),narrow(di.tszProductName),interface_path(d)};
    (void)d->Acquire();   // a refusal shows as NOTACQUIRED at the first read, and takes the reacquisition path
    // Reserve a free slot under a new generation, store the device, then publish.
    for(int i=0;i<MaxSlots;++i){
        auto& s=st->slots[std::size_t(i)];
        std::uint64_t w=s.word.load(std::memory_order_acquire);
        if(low_of(w)!=Closed)continue;   // not free (open, reserved, or closing)
        const auto next=pack(generation_of(w)+1,Closed|Reserved);
        if(!s.word.compare_exchange_strong(w,next,std::memory_order_acq_rel))continue;
        s.device=d;
        const auto generation=generation_of(next);
        if(const auto hook=g_open_hook.load())hook(BeforePublish,g_open_hook_user.load());
        // Publish, then look for a stop (slice 3.1 review, nene 10389). Both the publication and the stop flag are
        // sequentially consistent, and so is shutdown's read of the slot: a stop that came first is seen just below,
        // and a stop that came later sees this slot in shutdown's own pass. So an open never returns a handle that
        // shutdown has not accounted for.
        s.word.store(pack(generation,0));   // published: readers may now be admitted
        if(const auto hook=g_open_hook.load())hook(AfterPublish,g_open_hook_user.load());
        if(st->stopped.load()){
            // Take the slot back, unless shutdown has already closed it (then the device is shutdown's, and counted).
            for(;;){
                std::uint64_t w2=pack(generation,0);
                if(s.word.compare_exchange_strong(w2,pack(generation,Closed|Reserved))){
                    s.device=nullptr;s.word.store(pack(generation,Closed),std::memory_order_release);
                    return fail("the provider stopped during the open");
                }
                if(generation_of(w2)!=generation||(low_of(w2)&(Closed|Reserved))){
                    r=OpenResult{};r.why="the provider stopped during the open (shutdown released its device)";return r;
                }
                std::this_thread::yield();   // a reader backing out of an admission the stop refused
            }
        }
        r.ok=true;r.handle=(Handle(generation)<<8)|Handle(i+1);
        return r;
    }
    return fail("no free device slot");
}

Call DInputProvider::reacquire(Handle h){
    const auto st=state_;
    auto* s=st->enter(h);
    if(!s)return Call::Gone;
    const auto c=classify(s->device->Acquire());
    State::leave(*s);
    return c;
}

void DInputProvider::close(Handle h){
    const auto st=state_;
    std::size_t index{};std::uint32_t generation{};
    if(!State::decode(h,index,generation)){++st->misuse;return;}
    auto& s=st->slots[index];
    std::uint64_t expect=pack(generation,0);            // this generation, open, no reader
    if(!st->close_idle(s,expect))++st->misuse;          // a read in progress, a stale handle, or already closed
}

Call DInputProvider::poll(Handle h){
    const auto st=state_;
    auto* s=st->enter(h);
    if(!s)return Call::Gone;
    const auto c=classify(s->device->Poll());
    State::leave(*s);
    return c;
}

Call DInputProvider::state(Handle h,RawState& out){
    const auto st=state_;   // keeps everything this read touches alive, even if the provider is destroyed meanwhile
    auto* s=st->enter(h);
    if(!s)return Call::Gone;
    DIJOYSTATE2 js;
    std::memset(&js,0,sizeof js);
    for(auto& p:js.rgdwPOV)p=0xFFFFFFFFu;
    const auto c=classify(s->device->GetDeviceState(sizeof js,&js));
    State::leave(*s);
    if(c!=Call::Ok)return c;   // nothing is copied from a failed or pending read
    const LONG* axes=&js.lX;
    out.axes[0]=std::int32_t(axes[0]);out.axes[1]=std::int32_t(axes[1]);out.axes[2]=std::int32_t(axes[2]);
    out.axes[3]=std::int32_t(axes[3]);out.axes[4]=std::int32_t(axes[4]);out.axes[5]=std::int32_t(axes[5]);
    out.axes[6]=std::int32_t(js.rglSlider[0]);out.axes[7]=std::int32_t(js.rglSlider[1]);
    std::memcpy(out.buttons,js.rgbButtons,128);
    for(int i=0;i<4;++i)out.povs[i]=js.rgdwPOV[i];
    return c;
}

std::int64_t DInputProvider::now_us(){
    static const LONGLONG f=[]{LARGE_INTEGER q;QueryPerformanceFrequency(&q);return q.QuadPart;}();
    LARGE_INTEGER c;QueryPerformanceCounter(&c);
    return std::int64_t(c.QuadPart/f*1000000+(c.QuadPart%f)*1000000/f);
}

DInputProvider::ShutdownReport DInputProvider::shutdown(int wait_ms){
    const auto st=state_;
    ShutdownReport rep;
    shut_=true;
    st->stopped.store(true);   // nothing new is admitted from here on
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(wait_ms);
    auto pass=[&](bool final){
        int busy=0;
        for(int i=0;i<MaxSlots;++i){
            auto& s=st->slots[std::size_t(i)];
            for(;;){
                const std::uint64_t w=s.word.load();   // sequentially consistent: pairs with open's publication
                if(low_of(w)&(Closed|Reserved))break;   // free, closing, or an open that will see the stop and back out
                if(st->close_idle(s,w)){++rep.released;break;}
                if((low_of(w)&Readers)==0)continue;      // changed under us: look again
                if(std::chrono::steady_clock::now()>=deadline){
                    ++busy;
                    if(final)rep.detail+="device slot "+std::to_string(i+1)+" is still being read; it is released when that read leaves. ";
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        return busy;
    };
    pass(false);
    while(st->worker_ops.load()>0&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    rep.held=pass(true);   // again, for a slot an in-flight open published just before the stop
    if(!rep.held&&st->worker_ops.load()==0&&st->input){st->input->Release();st->input=nullptr;rep.input_released=true;}
    else if(st->input)rep.detail+="DirectInput is released when the last call leaves; an open still in flight releases its own device.";
    return rep;
}
}
