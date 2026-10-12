// The WheelFfb bridge provider (wheelffb_bridge.hpp).
#include "wheelffb_bridge.hpp"
#include <cstring>
#include <stdexcept>
namespace dbce::shim {
namespace {
std::string guid_bytes(const unsigned char b[16]){GUID g;std::memcpy(&g,b,sizeof g);return guid_text(g);}
OpenResult refusal(const char* why){OpenResult r;r.why=why;return r;}
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

// The worker.
OpenResult WheelFfbBridge::open(const DeviceInfo& listed){
    if(listed.dev!=instance_)return inner_->open(listed);
    bool prepare=false;
    {
        std::lock_guard<std::mutex> l(m_);
        if(phase_==Phase::Shared){
            if(slot_>=0){
                OpenResult r=shared_open_;   // prepared once in this phase; no library call here
                r.ok=true;
                r.handle=Tag|(Handle(++generation_)<<8)|Handle(slot_+1);
                shared_.store(r.handle);
                ++counts_.shared_opens;
                return r;
            }
            if(prepared_){++counts_.refused;return refusal("WheelFfb gave no read slot for the wheel");}
            prepared_=prepare=true;
        }else if(phase_==Phase::Inner){
            ++inner_busy_;   // until this open's handle is adopted, or closed if it comes back too late
        }else{
            ++counts_.refused;
            return refusal(phase_==Phase::Stopped?"the WheelFfb bridge is stopped":"the force output is taking over or handing back the wheel");
        }
    }
    if(prepare)return prepare_shared(listed);
    auto r=inner_->open(listed);   // may sit in the driver: never under the lock
    bool keep=false;
    {
        std::lock_guard<std::mutex> l(m_);
        keep=r.ok&&phase_==Phase::Inner;
        if(keep){inner_wheel_=r.handle;++counts_.inner_opens;}
        else if(r.ok)++counts_.late_closes;
        if(!r.ok||keep)--inner_busy_;
    }
    if(r.ok&&!keep){
        inner_->close(r.handle);   // the phase changed during the open: this handle is never used
        {std::lock_guard<std::mutex> l(m_);--inner_busy_;}   // only now is no provider handle of the wheel left
        return refusal("the force output took the wheel during the open");
    }
    return r;
}

// The worker, once per Shared phase, outside the lock: the read slot on the library's force handle and what that
// handle has. Counted in before the phase is checked, so library_quiet() never misses a call about to start.
OpenResult WheelFfbBridge::prepare_shared(const DeviceInfo& listed){
    library_calls_.fetch_add(1);
    struct Out{std::atomic<int>& n;~Out(){n.fetch_sub(1);}} out{library_calls_};
    std::lock_guard<std::mutex> gate(library_gate_);   // the delivery's library calls skip while this is held
    {std::lock_guard<std::mutex> l(m_);if(phase_!=Phase::Shared)return refusal("the force output is handing back the wheel");}
    int slot=-1;OpenResult o;
    const int n=api_->EnumerateAllDevices();
    int index=-1;
    for(int i=0;i<n&&index<0;++i){GUID g{};if(api_->GetAnyDeviceGuid(i,&g)&&IsEqualGUID(g,guid_))index=i;}
    if(index>=0)slot=api_->OpenReadDevice(index);
    if(slot>=0xff){api_->CloseReadDevices();slot=-1;}
    if(slot>=0){
        ReadSlotCapsV1 c{};c.cbSize=sizeof c;
        if(api_->GetReadSlotCaps(slot,&c)==1&&c.version==1){
            for(std::size_t a=0;a<8;++a){
                o.caps.axis[a]=c.axisPresent[a]!=0;
                o.caps.range_ok[a]=o.caps.axis[a]&&c.rangeOk[a]!=0;
                if(o.caps.range_ok[a]){o.caps.min[a]=c.rangeMin[a];o.caps.max[a]=c.rangeMax[a];}
            }
            for(std::size_t b=0;b<128;++b)if(c.buttonPresent[b])o.caps.button.set(b);
            for(std::size_t p=0;p<4;++p)o.caps.pov[p]=c.povPresent[p]!=0;
            const auto end=static_cast<const char*>(std::memchr(c.interfacePath,0,sizeof c.interfacePath));
            o.opened={guid_bytes(c.instanceGuid),guid_bytes(c.productGuid),listed.name,
                      end?std::string(c.interfacePath,std::size_t(end-c.interfacePath)):std::string()};
        }else{api_->CloseReadDevices();slot=-1;}
    }
    if(slot<0)return refusal("WheelFfb gave no read slot for the wheel");
    {
        std::lock_guard<std::mutex> l(m_);
        if(phase_==Phase::Shared){
            slot_=slot;shared_open_=o;
            o.ok=true;
            o.handle=Tag|(Handle(++generation_)<<8)|Handle(slot+1);
            shared_.store(o.handle);
            ++counts_.shared_opens;
            return o;
        }
        ++counts_.worker_slot_closes;
    }
    api_->CloseReadDevices();   // the phase moved on while this was prepared: the slot is closed here, still counted in
    return refusal("the force output is handing back the wheel");
}

Call WheelFfbBridge::reacquire(Handle h){
    if(h&Tag)return Call::Gone;                          // never the shared force handle: its owner reacquires it
    if(h==retired_.load())return Call::Gone;
    return inner_->reacquire(h);
}

// The worker: Core hands back what it no longer reads.
void WheelFfbBridge::close(Handle h){
    if(h&Tag){
        Handle current=h;
        shared_.compare_exchange_strong(current,0);      // forgotten; the slot stays this phase's
        return;
    }
    if(h==retired_.load()){
        inner_->close(h);                                // the provider's wheel handle a promotion retired
        retired_.store(0);                               // only now may the promotion go on
        return;
    }
    bool wheel=false;
    {
        std::lock_guard<std::mutex> l(m_);
        if(h==inner_wheel_){inner_wheel_=0;++inner_busy_;wheel=true;}
    }
    inner_->close(h);
    if(wheel){std::lock_guard<std::mutex> l(m_);--inner_busy_;}   // the provider's wheel handle is gone only now
}

// The input (game) thread.
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
    // A failed read of the force handle ends the epoch (Gone): the shim never reacquires it; Core reopens the slot.
    if(api_->ReadDeviceStateWithPov(int(h&0xff)-1,axes,8,buttons,128,pov,4)!=1)return Call::Gone;
    RawState s;
    for(int a=0;a<8;++a)s.axes[a]=axes[a];
    for(int b=0;b<128;++b)s.buttons[b]=buttons[b]?0x80:0;
    for(int p=0;p<4;++p)s.povs[p]=pov[p]<0?0xFFFFFFFFu:std::uint32_t(pov[p]);
    out=s;
    return Call::Ok;
}

std::int64_t WheelFfbBridge::now_us(){return inner_->now_us();}

bool WheelFfbBridge::begin_promotion(){
    std::lock_guard<std::mutex> l(m_);
    if(phase_==Phase::Inner){
        phase_=Phase::Draining;
        // Nothing reads this handle between ticks; Core's next read of it is Gone, and the worker closes it.
        if(inner_wheel_){retired_.store(inner_wheel_);inner_wheel_=0;}
    }
    if(phase_!=Phase::Draining||inner_busy_||retired_.load())return false;
    phase_=Phase::Promoting;
    return true;
}

void WheelFfbBridge::end_promotion(bool held){
    std::lock_guard<std::mutex> l(m_);
    if(phase_!=Phase::Draining&&phase_!=Phase::Promoting)return;
    phase_=held&&phase_==Phase::Promoting?Phase::Shared:Phase::Inner;   // cancelled or failed: the provider again
    prepared_=false;slot_=-1;
}

void WheelFfbBridge::begin_release(){
    int slot=-1;
    {
        std::lock_guard<std::mutex> l(m_);
        if(phase_!=Phase::Shared)return;
        phase_=Phase::Releasing;
        shared_.store(0);
        slot=slot_;slot_=-1;
    }
    if(slot>=0)api_->CloseReadDevices();   // a prepared slot: no worker is inside the library for it
}

void WheelFfbBridge::end_release(){
    std::lock_guard<std::mutex> l(m_);
    if(phase_==Phase::Releasing){phase_=Phase::Inner;prepared_=false;}
}

void WheelFfbBridge::shutdown(){
    int slot=-1;
    {
        std::lock_guard<std::mutex> l(m_);
        phase_=Phase::Stopped;
        shared_.store(0);
        slot=slot_;slot_=-1;
    }
    if(slot>=0)api_->CloseReadDevices();
}

bool WheelFfbBridge::enter_library(){return library_gate_.try_lock();}
void WheelFfbBridge::leave_library(){library_gate_.unlock();}

bool WheelFfbBridge::library_quiet()const{
    {std::lock_guard<std::mutex> l(m_);if(phase_==Phase::Shared)return false;}
    return library_calls_.load()==0;
}

WheelFfbBridge::Phase WheelFfbBridge::phase()const{std::lock_guard<std::mutex> l(m_);return phase_;}

WheelFfbBridge::Counts WheelFfbBridge::counts()const{
    std::lock_guard<std::mutex> l(m_);
    auto c=counts_;c.stale_reads=stale_reads_.load();return c;
}
}
