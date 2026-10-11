#pragma once
// Versioned observation only: no device, file, game adapter, or command authority.
#include "observer_transport.hpp"
#include "shim.hpp"
#include <cmath>
#include <cstring>

namespace dbce::shim::observe {
constexpr std::uint32_t DomainMagic=0x4442534f, DomainVersion=2;
constexpr std::size_t MaxSamples=8, MaxSignals=16;
struct Identity {
    std::uint32_t process{};
    std::uint32_t profile_devices{}; // exact immutable profile device-table size
    std::uint64_t process_start{}; // Windows process creation FILETIME
    std::array<std::uint8_t,16> nonce{};
    Sha256 executable{},build{},profile{};
    bool operator==(const Identity& x) const noexcept {
        return process==x.process && profile_devices==x.profile_devices && process_start==x.process_start && nonce==x.nonce &&
            executable==x.executable && build==x.build && profile==x.profile;
    }
};
// Signal ids/units live in the game adapter's versioned schema, whose SHA is build.
// The source frame and signed offset must agree; a previous-tick consumer is -1.
struct Signal { std::uint32_t id{}; std::uint64_t frame{}; std::int64_t us{}; std::int32_t offset{}; float value{}; };
struct RingEvent {
    std::uint64_t seq{}; std::int64_t us{}; EventKind kind{};
    std::int32_t device{-1}; std::uint32_t epoch{}; std::uint64_t batch{};
};
struct Document {
    Identity identity;
    std::uint64_t frame{},event_seq{},down{},up{};
    std::int64_t tick_us{},boundary_us{};
    Run run{};
    bool latched{},suppress_native{},fences_verified{},force_absent{};
    std::uint64_t status_dropped{},events_dropped{};
    std::uint32_t sample_count{},signal_count{},event_count{};
    std::array<ActionValue,ActionCount> actions{};
    std::array<DeviceSample,MaxSamples> samples{};
    std::array<Signal,MaxSignals> signals{};
    std::array<RingEvent,EventRingSize> events{}; // ascending, newest <= event_seq
};

namespace domain_detail {
template<class A> inline bool any(const A& a) { for(auto v:a) if(v) return true; return false; }
inline bool identity_ok(const Identity& i) { return i.process && i.profile_devices<=ActionCount && i.process_start && any(i.nonce) && any(i.executable) && any(i.build) && any(i.profile); }
inline bool valid(const Document& d) {
    if(!identity_ok(d.identity) || !d.frame || d.tick_us<0 || d.boundary_us<d.tick_us ||
       unsigned(d.run)>unsigned(Run::Inert) || d.sample_count>MaxSamples || d.signal_count>MaxSignals || d.event_count>EventRingSize ||
       (d.down>>ActionCount) || (d.up>>ActionCount)) return false;
    for(std::size_t i=0;i<d.sample_count;++i) {
        const auto& s=d.samples[i];
        if(s.device>=d.identity.profile_devices || !s.epoch || !s.seq || s.read_start_us<d.tick_us || s.read_end_us<s.read_start_us || s.read_end_us>d.boundary_us ||
           unsigned(s.poll)>unsigned(Call::Pending) || unsigned(s.state)>unsigned(Call::Pending) || s.injected.povs>15 ||
           (s.valid && (s.poll!=Call::Ok || s.state!=Call::Ok)) || (s.stale && !s.valid) ||
           (s.injected.any() && (!s.valid || s.stale || !s.injected.batch || !any(s.injected.hash)))) return false;
        for(std::size_t j=0;j<i;++j) if(d.samples[j].device==s.device) return false;
    }
    for(int i=0;i<ActionCount;++i) {
        const auto& a=d.actions[i];
        if(!std::isfinite(a.value) || a.value<(i==0?-1.f:0.f) || a.value>1.f || unsigned(a.status)>unsigned(Status::TestNeutral) ||
           a.sample < -1 || a.sample>=int(d.sample_count)) return false;
        if(a.injected && (a.sample<0 || !d.samples[std::size_t(a.sample)].injected.any())) return false;
        if(a.status==Status::Live && (a.sample<0 || !d.samples[std::size_t(a.sample)].valid || d.samples[std::size_t(a.sample)].stale)) return false;
    }
    for(std::size_t i=0;i<d.signal_count;++i) {
        const auto& s=d.signals[i];
        if(!s.id || !s.frame || s.frame>d.frame || s.offset>0 || std::uint64_t(-std::int64_t(s.offset))!=d.frame-s.frame ||
           s.us<0 || s.us>d.boundary_us || !std::isfinite(s.value)) return false;
        for(std::size_t j=0;j<i;++j) if(d.signals[j].id==s.id) return false;
    }
    const std::uint64_t expected=d.event_seq<EventRingSize?d.event_seq:EventRingSize;
    if(d.event_count!=expected) return false;
    for(std::size_t i=0;i<d.event_count;++i) {
        const auto& e=d.events[i];
        if(e.seq!=d.event_seq-d.event_count+i+1 || e.us<0 || e.us>d.boundary_us || unsigned(e.kind)>unsigned(EventKind::ReadPending) ||
           e.device< -1 || (e.device>=0 && unsigned(e.device)>=d.identity.profile_devices)) return false;
    }
    return true;
}
struct Encode {
    Payload& p; std::size_t at{}; bool ok=true;
    void word(std::uint32_t& v) { if(at<p.size()) p[at++]=v; else ok=false; }
};
struct Decode {
    const Payload& p; std::size_t at{}; bool ok=true;
    void word(std::uint32_t& v) { if(at<p.size()) v=p[at++]; else ok=false; }
};
template<class C> void value(C& c,std::uint32_t& v) { c.word(v); }
template<class C> void value(C& c,std::int32_t& v) { std::uint32_t u;std::memcpy(&u,&v,4);c.word(u);std::memcpy(&v,&u,4); }
template<class C> void value(C& c,std::uint64_t& v) { std::uint32_t lo=std::uint32_t(v),hi=std::uint32_t(v>>32);c.word(lo);c.word(hi);v=lo|(std::uint64_t(hi)<<32); }
template<class C> void value(C& c,std::int64_t& v) { std::uint64_t u;std::memcpy(&u,&v,8);value(c,u);std::memcpy(&v,&u,8); }
template<class C> void value(C& c,float& v) { std::uint32_t u;std::memcpy(&u,&v,4);c.word(u);std::memcpy(&v,&u,4); }
template<class C> void value(C& c,bool& v) { std::uint32_t u=v; c.word(u);if(u>1)c.ok=false;v=u!=0; }
template<class C,class E> void enumeration(C& c,E& v) { std::uint32_t u=unsigned(v);c.word(u);if(u>255)c.ok=false;v=E(u); }
template<class C,std::size_t N> void bytes(C& c,std::uint8_t (&a)[N]) {
    static_assert(N%4==0);for(std::size_t i=0;i<N;i+=4) {std::uint32_t u=0;for(int b=0;b<4;++b)u|=std::uint32_t(a[i+b])<<(b*8);c.word(u);for(int b=0;b<4;++b)a[i+b]=std::uint8_t(u>>(b*8));}
}
template<class C,std::size_t N> void bytes(C& c,std::array<std::uint8_t,N>& a) {
    static_assert(N%4==0);for(std::size_t i=0;i<N;i+=4) {std::uint32_t u=0;for(int b=0;b<4;++b)u|=std::uint32_t(a[i+b])<<(b*8);c.word(u);for(int b=0;b<4;++b)a[i+b]=std::uint8_t(u>>(b*8));}
}
template<class C> void raw(C& c,RawState& r) {for(auto& a:r.axes)value(c,a);bytes(c,r.buttons);for(auto& p:r.povs)value(c,p);}
template<class C> void fields(C& c,Document& d) {
    auto& id=d.identity;value(c,id.process);value(c,id.profile_devices);value(c,id.process_start);bytes(c,id.nonce);bytes(c,id.executable);bytes(c,id.build);bytes(c,id.profile);
    value(c,d.frame);value(c,d.tick_us);value(c,d.boundary_us);value(c,d.event_seq);value(c,d.down);value(c,d.up);enumeration(c,d.run);
    value(c,d.latched);value(c,d.suppress_native);value(c,d.fences_verified);value(c,d.force_absent);value(c,d.status_dropped);value(c,d.events_dropped);
    value(c,d.sample_count);value(c,d.signal_count);value(c,d.event_count);
    if(d.sample_count>MaxSamples || d.signal_count>MaxSignals || d.event_count>EventRingSize) {c.ok=false;return;}
    for(auto& a:d.actions) {value(c,a.value);enumeration(c,a.status);std::int32_t sample=a.sample;value(c,sample);if(sample < -1 || sample>=int(MaxSamples))c.ok=false;a.sample=std::int16_t(sample);value(c,a.injected);}
    for(std::size_t i=0;i<d.sample_count;++i) {
        auto& s=d.samples[i];value(c,s.device);value(c,s.epoch);value(c,s.seq);value(c,s.read_start_us);value(c,s.read_end_us);enumeration(c,s.poll);enumeration(c,s.state);
        value(c,s.valid);value(c,s.stale);raw(c,s.physical);raw(c,s.delivered);value(c,s.injected.batch);bytes(c,s.injected.hash);
        std::uint32_t axes=s.injected.axes,povs=s.injected.povs;value(c,axes);value(c,povs);if(axes>255 || povs>15)c.ok=false;s.injected.axes=std::uint8_t(axes);s.injected.povs=std::uint8_t(povs);
        for(int k=0;k<4;++k) {std::uint32_t b=0;for(int bit=0;bit<32;++bit)if(s.injected.buttons[k*32+bit])b|=1u<<bit;value(c,b);for(int bit=0;bit<32;++bit)s.injected.buttons[k*32+bit]=(b&(1u<<bit))!=0;}
    }
    for(std::size_t i=0;i<d.signal_count;++i) {auto& s=d.signals[i];value(c,s.id);value(c,s.frame);value(c,s.us);value(c,s.offset);value(c,s.value);}
    for(std::size_t i=0;i<d.event_count;++i) {auto& e=d.events[i];value(c,e.seq);value(c,e.us);enumeration(c,e.kind);value(c,e.device);value(c,e.epoch);value(c,e.batch);}
}
}
// Transactional in-memory codec. Unused words are zero and checked, never old tail data.
inline bool encode(const Document& source,Payload& out) {
    if(!domain_detail::valid(source))return false;
    Document copy=source;Payload next{};next[0]=DomainMagic;next[1]=DomainVersion;
    domain_detail::Encode c{next,3};domain_detail::fields(c,copy);if(!c.ok)return false;
    next[2]=std::uint32_t(c.at);out=next;return true;
}
inline bool decode(const Payload& in,Document& out) {
    if(in[0]!=DomainMagic || in[1]!=DomainVersion || in[2]>in.size() || in[2]<3)return false;
    Document next;domain_detail::Decode c{in,3};domain_detail::fields(c,next);
    if(!c.ok || c.at!=in[2] || !domain_detail::valid(next))return false;
    for(std::size_t i=c.at;i<in.size();++i)if(in[i])return false;
    out=next;return true;
}
// Adapt the actual Core frame and ring on the input thread. Never silently drop
// devices, events or signals to fit a bound; a false result is an observer fault.
inline bool compose(const Identity& identity,const Frame& f,const std::array<Event,EventRingSize>& ring,
                    const Signal* signals,std::size_t count,bool fences,bool force_absent,
                    std::uint64_t status_dropped,std::uint64_t events_dropped,Document& out) {
    if(f.samples.size()>MaxSamples || count>MaxSignals || (count && !signals))return false;
    Document d;d.identity=identity;d.frame=f.seq;d.tick_us=f.tick_us;d.boundary_us=f.boundary_us;d.event_seq=f.event_seq;d.down=f.down;d.up=f.up;
    d.run=f.run;d.latched=f.latched;d.suppress_native=f.suppress_native;d.fences_verified=fences;d.force_absent=force_absent;d.actions=f.action;
    d.status_dropped=status_dropped;d.events_dropped=events_dropped;
    d.sample_count=std::uint32_t(f.samples.size());d.signal_count=std::uint32_t(count);
    for(std::size_t i=0;i<f.samples.size();++i)d.samples[i]=f.samples[i];
    for(std::size_t i=0;i<count;++i)d.signals[i]=signals[i];
    d.event_count=std::uint32_t(f.event_seq<EventRingSize?f.event_seq:EventRingSize);
    for(std::size_t i=0;i<d.event_count;++i) {
        const auto wanted=f.event_seq-d.event_count+i+1;
        const Event* found=nullptr;for(const auto& e:ring)if(e.seq==wanted){if(found)return false;found=&e;}
        if(!found)return false;d.events[i]={found->seq,found->us,found->kind,found->device,found->epoch,found->batch};
    }
    if(!domain_detail::valid(d))return false;out=d;return true;
}
enum class Acceptance { Accepted,Malformed,WrongIdentity,Stale,Repeated,Reversed };
struct ReadResult { Acceptance state; std::uint64_t events_lost{}; };
class Cursor {
    Identity expected_;std::uint64_t frame_{},event_{};
public:
    explicit Cursor(Identity expected):expected_(expected){}
    // now_us MUST share DInputProvider::now_us()'s system-wide QPC origin and
    // integer microsecond conversion. Never use a per-process Stopwatch origin.
    ReadResult accept(const Payload& p,std::int64_t now_us,std::int64_t freshness_us,Document& out) {
        Document next;if(!decode(p,next))return {Acceptance::Malformed};
        if(!(next.identity==expected_))return {Acceptance::WrongIdentity};
        if(freshness_us<0 || now_us<next.boundary_us || now_us-next.boundary_us>freshness_us)return {Acceptance::Stale};
        if(next.frame==frame_)return {Acceptance::Repeated};
        if(next.frame<frame_ || next.event_seq<event_)return {Acceptance::Reversed};
        const auto first=next.event_seq-next.event_count+1;
        const auto gap=first>event_ && first-event_>1?first-event_-1:0;
        frame_=next.frame;event_=next.event_seq;out=next;return {Acceptance::Accepted,gap};
    }
};
}
