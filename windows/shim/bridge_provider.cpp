#include "bridge_provider.hpp"
#include <set>
#include <algorithm>

namespace dbce::shim {
namespace {
bool same_caps(const ObjectCaps& a,const ObjectCaps& b){return a.axis==b.axis && a.range_ok==b.range_ok && a.min==b.min && a.max==b.max && a.button==b.button && a.pov==b.pov;}
bool same_device(const BridgeDevice& a,const BridgeDevice& b){return a.identity.dev==b.identity.dev && a.identity.prod==b.identity.prod && a.identity.interface_path==b.identity.interface_path && a.generation==b.generation && same_caps(a.caps,b.caps);}
bool valid_guid(const std::string& s){
    if(s.size()!=38 || s.front()!='{' || s.back()!='}')return false;
    bool nonzero=false;
    for(std::size_t i=1;i<37;++i){if(i==9||i==14||i==19||i==24){if(s[i]!='-')return false;continue;}
        if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;
        nonzero|=s[i]!='0';}
    return nonzero;
}
}
BridgeProvider::Entry* BridgeProvider::entry(std::uint64_t generation){for(auto& e:entries_)if(e.device.generation==generation)return &e;return nullptr;}
const BridgeSample* BridgeProvider::sample(Handle h){const auto lease=leases_.find(h);if(lease==leases_.end())return nullptr;const auto found=frame_.find(lease->second);return found==frame_.end()?nullptr:&found->second;}
bool BridgeProvider::devices(const std::vector<BridgeDevice>& values,std::string& why){
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopped_ || values.size()>Limit){why="bridge stopped or device limit exceeded";return false;}
    std::set<std::string> identities;std::set<std::uint64_t> generations;std::vector<Entry> next;
    for(const auto& d:values){
        if(!d.generation || !valid_guid(d.identity.dev) || !valid_guid(d.identity.prod) || !identities.insert(d.identity.dev).second || !generations.insert(d.generation).second){why="invalid or duplicate bridge identity/generation";return false;}
        for(std::size_t i=0;i<8;++i)if(d.caps.range_ok[i] && (!d.caps.axis[i] || d.caps.min[i]>=d.caps.max[i])){why="unqualified bridge range";return false;}
        auto* prior=entry(d.generation);
        if(prior){if(!same_device(prior->device,d)){why="bridge metadata changed within a generation";return false;}next.push_back(*prior);}
        else{
            if(d.generation<=greatest_generation_){why="retired bridge generation";return false;}
            next.push_back({d,0,0,false});
        }
    }
    for(const auto& e:next)greatest_generation_=std::max(greatest_generation_,e.device.generation);
    entries_=std::move(next);frame_.clear();why.clear();return true;
}
bool BridgeProvider::samples(const std::vector<BridgeSample>& values,std::string& why){
    std::lock_guard<std::mutex> lock(mutex_);frame_.clear();
    if(stopped_ || values.size()>Limit){why="bridge stopped or sample limit exceeded";return false;}
    std::map<std::uint64_t,BridgeSample> next;const auto now=clock_();
    for(const auto& s:values){
        auto* e=entry(s.source.generation);
        if(!e || e->failed || !s.source.sequence || s.source.sequence<=e->last_sequence || s.source.start_us<e->last_end ||
            s.source.start_us<0 || s.source.end_us<s.source.start_us || s.source.end_us>now ||
            int(s.poll)>int(Call::Pending) || int(s.state)>int(Call::Pending) || (s.poll!=Call::Ok && s.state!=Call::NotCalled) ||
            !next.emplace(s.source.generation,s).second){why="invalid, failed, repeated or mistimed bridge sample";return false;}
    }
    for(const auto& pair:next){auto* e=entry(pair.first);const auto& s=pair.second;
        e->last_sequence=s.source.sequence;e->last_end=s.source.end_us;
        e->failed=(s.poll!=Call::Ok && s.poll!=Call::Pending)||(s.poll==Call::Ok && s.state!=Call::Ok && s.state!=Call::Pending);
    }
    frame_=std::move(next);why.clear();return true;
}
void BridgeProvider::finish_frame(){std::lock_guard<std::mutex> lock(mutex_);frame_.clear();}
std::vector<DeviceInfo> BridgeProvider::enumerate(){std::lock_guard<std::mutex> lock(mutex_);std::vector<DeviceInfo> out;if(!stopped_)for(const auto& e:entries_)if(!e.failed)out.push_back(e.device.identity);return out;}
OpenResult BridgeProvider::open(const DeviceInfo& listed){
    std::lock_guard<std::mutex> lock(mutex_);OpenResult r;
    if(stopped_ || leases_.size()>=Limit*2){r.why="bridge stopped or lease limit";return r;}
    for(const auto& e:entries_)if(!e.failed && e.device.identity.dev==listed.dev && e.device.identity.prod==listed.prod){
        r.ok=true;r.opened=e.device.identity;r.caps=e.device.caps;r.handle=++next_;leases_[r.handle]=e.device.generation;return r;}
    r.why="bridge generation is no longer available";return r;
}
Call BridgeProvider::reacquire(Handle){return Call::Gone;} // owner, not this provider, recovers the handle
void BridgeProvider::close(Handle h){std::lock_guard<std::mutex> lock(mutex_);leases_.erase(h);}
Call BridgeProvider::poll(Handle h){
    std::lock_guard<std::mutex> lock(mutex_);if(stopped_)return Call::Gone;
    const auto lease=leases_.find(h);if(lease==leases_.end() || !entry(lease->second))return Call::Gone;
    const auto* s=sample(h);return s?s->poll:Call::Pending;
}
Call BridgeProvider::state(Handle h,RawState& out){std::lock_guard<std::mutex> lock(mutex_);if(stopped_)return Call::Gone;const auto* s=sample(h);if(!s)return Call::Pending;if(s->poll==Call::Ok && s->state==Call::Ok)out=s->raw;return s->state;}
bool BridgeProvider::source_read(Handle h,SourceRead& out){std::lock_guard<std::mutex> lock(mutex_);const auto* s=sample(h);if(!s)return false;out=s->source;return true;}
ProviderShutdownReport BridgeProvider::shutdown(int){std::lock_guard<std::mutex> lock(mutex_);ProviderShutdownReport r;r.released=int(leases_.size());r.input_released=true;r.detail="copied-input leases released; external device owner untouched";stopped_=true;leases_.clear();entries_.clear();frame_.clear();return r;}
}
