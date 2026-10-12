// The WheelFfb bridge provider (wheelffb_bridge.hpp).
#include "wheelffb_bridge.hpp"
#include <cstring>
#include <stdexcept>
namespace dbce::shim {
namespace {
std::string guid_bytes(const unsigned char b[16]){GUID g;std::memcpy(&g,b,sizeof g);return guid_text(g);}
}

WheelFfbBridge::WheelFfbBridge(std::shared_ptr<DInputProvider> inner,const WheelFfbApi* api,const std::string& instance)
    :inner_(std::move(inner)),api_(api),instance_(instance){
    if(!inner_||!api_||!usable(*api_)||!guid_parse(instance_,guid_))
        throw std::invalid_argument("WheelFfbBridge needs the DirectInput provider, a WheelFfb with read slot caps and an instance GUID");
}

bool WheelFfbBridge::usable(const WheelFfbApi& a){
    return a.EnumerateAllDevices&&a.GetAnyDeviceGuid&&a.OpenReadDevice&&a.ReadDeviceStateWithPov&&a.CloseReadDevices&&a.GetReadSlotCaps;
}

std::vector<DeviceInfo> WheelFfbBridge::enumerate(){return inner_->enumerate();}   // a listing opens no handle

OpenResult WheelFfbBridge::open(const DeviceInfo& listed){
    if(listed.dev!=instance_)return inner_->open(listed);
    std::lock_guard<std::mutex> l(m_);
    if(phase_==Phase::Inner){
        auto r=inner_->open(listed);
        if(r.ok){inner_wheel_=r.handle;++counts_.inner_opens;}
        return r;
    }
    if(phase_==Phase::Shared)return open_shared(listed);
    ++counts_.shared_refused;
    OpenResult r;
    r.why=phase_==Phase::Stopped?"the WheelFfb bridge is stopped":"the force output is taking over or handing back the wheel";
    return r;
}

// Under m_, in the Shared phase: the wheel's read slot on WheelFfb's force handle, and what that handle has.
OpenResult WheelFfbBridge::open_shared(const DeviceInfo& listed){
    OpenResult r;
    if(shared_.load())close_shared_locked();   // never two slots: Core reopens only what it handed back
    const int n=api_->EnumerateAllDevices();
    int index=-1;
    for(int i=0;i<n&&index<0;++i){GUID g{};if(api_->GetAnyDeviceGuid(i,&g)&&IsEqualGUID(g,guid_))index=i;}
    if(index<0){r.why="WheelFfb does not list the wheel";return r;}
    const int slot=api_->OpenReadDevice(index);
    if(slot<0||slot>=0xff){if(slot>=0)api_->CloseReadDevices();r.why="WheelFfb refused the wheel's read slot";return r;}
    ReadSlotCapsV1 c{};c.cbSize=sizeof c;
    if(api_->GetReadSlotCaps(slot,&c)!=1||c.version!=1){
        api_->CloseReadDevices();r.why="WheelFfb could not read back the shared handle";return r;}
    for(std::size_t a=0;a<8;++a){
        r.caps.axis[a]=c.axisPresent[a]!=0;
        r.caps.range_ok[a]=r.caps.axis[a]&&c.rangeOk[a]!=0;
        if(r.caps.range_ok[a]){r.caps.min[a]=c.rangeMin[a];r.caps.max[a]=c.rangeMax[a];}
    }
    for(std::size_t b=0;b<128;++b)if(c.buttonPresent[b])r.caps.button.set(b);
    for(std::size_t p=0;p<4;++p)r.caps.pov[p]=c.povPresent[p]!=0;
    const auto path_end=static_cast<const char*>(std::memchr(c.interfacePath,0,sizeof c.interfacePath));
    r.opened={guid_bytes(c.instanceGuid),guid_bytes(c.productGuid),listed.name,
              path_end?std::string(c.interfacePath,std::size_t(path_end-c.interfacePath)):std::string()};
    r.ok=true;
    r.handle=Tag|(Handle(++generation_)<<8)|Handle(slot+1);
    shared_.store(r.handle);
    ++counts_.shared_opens;
    return r;
}

void WheelFfbBridge::close_shared_locked(){
    shared_.store(0);
    api_->CloseReadDevices();
}

Call WheelFfbBridge::reacquire(Handle h){
    if(h&Tag)return Call::Gone;                          // never the shared force handle: its owner reacquires it
    if(h==retired_.load())return Call::Gone;
    return inner_->reacquire(h);
}

void WheelFfbBridge::close(Handle h){
    std::lock_guard<std::mutex> l(m_);
    if(h&Tag){if(h==shared_.load())close_shared_locked();return;}   // a stale one was closed by the hand-over
    if(h==retired_.load()){retired_.store(0);return;}
    if(h==inner_wheel_)inner_wheel_=0;
    inner_->close(h);
}

Call WheelFfbBridge::poll(Handle h){
    if(h&Tag){if(h==shared_.load())return Call::Ok;++stale_reads_;return Call::Gone;}   // the slot's read polls itself
    if(h==retired_.load()){++stale_reads_;return Call::Gone;}
    return inner_->poll(h);
}

Call WheelFfbBridge::state(Handle h,RawState& out){
    if(!(h&Tag)){
        if(h==retired_.load()){++stale_reads_;return Call::Gone;}
        return inner_->state(h,out);
    }
    if(h!=shared_.load()){++stale_reads_;return Call::Gone;}
    int axes[8];unsigned char buttons[128];int pov[4];
    // A failed read of the force handle ends the epoch (Gone): the shim never reacquires it, and Core reopens the slot.
    if(api_->ReadDeviceStateWithPov(int(h&0xff)-1,axes,8,buttons,128,pov,4)!=1)return Call::Gone;
    RawState s;
    for(int a=0;a<8;++a)s.axes[a]=axes[a];
    for(int b=0;b<128;++b)s.buttons[b]=buttons[b]?0x80:0;
    for(int p=0;p<4;++p)s.povs[p]=pov[p]<0?0xFFFFFFFFu:std::uint32_t(pov[p]);
    out=s;
    return Call::Ok;
}

std::int64_t WheelFfbBridge::now_us(){return inner_->now_us();}

void WheelFfbBridge::begin_promotion(){
    std::lock_guard<std::mutex> l(m_);
    if(phase_!=Phase::Inner)return;
    phase_=Phase::Promoting;
    // The input thread is between ticks: nothing reads this handle now. Core's next read of it is Gone.
    if(inner_wheel_){retired_.store(inner_wheel_);inner_->close(inner_wheel_);inner_wheel_=0;}
}

void WheelFfbBridge::end_promotion(bool held){
    std::lock_guard<std::mutex> l(m_);
    if(phase_==Phase::Promoting)phase_=held?Phase::Shared:Phase::Inner;
}

void WheelFfbBridge::begin_release(){
    std::lock_guard<std::mutex> l(m_);
    if(phase_!=Phase::Shared)return;
    phase_=Phase::Releasing;
    if(shared_.load())close_shared_locked();
}

void WheelFfbBridge::end_release(){
    std::lock_guard<std::mutex> l(m_);
    if(phase_==Phase::Releasing)phase_=Phase::Inner;
}

void WheelFfbBridge::shutdown(){
    std::lock_guard<std::mutex> l(m_);
    if(shared_.load())close_shared_locked();
    phase_=Phase::Stopped;
}

WheelFfbBridge::Phase WheelFfbBridge::phase()const{std::lock_guard<std::mutex> l(m_);return phase_;}

WheelFfbBridge::Counts WheelFfbBridge::counts()const{
    std::lock_guard<std::mutex> l(m_);
    auto c=counts_;c.stale_reads=stale_reads_.load();return c;
}
}
