#include "observer_domain.hpp"
#include "shim_fake_provider.hpp"
#include <cstdio>
#include <stdexcept>
#include <limits>
using namespace dbce::shim;
namespace o=dbce::shim::observe;
static int checks;
void check(bool b,const char* why) {++checks;if(!b)throw std::runtime_error(why);}
o::Identity identity() {o::Identity id;id.process=73;id.process_start=(1ull<<60)+7;id.nonce[2]=23;id.executable[3]=91;id.build[4]=73;id.profile[5]=41;return id;}
o::Document example() {
    o::Document d;d.identity=identity();d.frame=81;d.tick_us=1000000;d.boundary_us=1000400;
    d.run=Run::Running;d.latched=true;d.suppress_native=true;d.fences_verified=true;d.force_absent=true;
    d.sample_count=1;auto& s=d.samples[0];s.device=0;s.epoch=7;s.seq=(1ull<<61)+17;s.read_start_us=1000001;s.read_end_us=1000300;
    s.poll=Call::Ok;s.state=Call::Ok;s.valid=true;s.physical.axes[0]=INT32_MIN;s.delivered.axes[0]=INT32_MAX;
    for(int i=0;i<128;++i)s.physical.buttons[i]=std::uint8_t(i);s.delivered.buttons[127]=0x80;
    s.injected.batch=(1ull<<60)+9;s.injected.axes=1;s.injected.hash[0]=71;s.injected.buttons[127]=true;
    d.actions[0]={-1.f,Status::Live,0,true};d.actions[5]={1.f,Status::Live,0,false};d.down=1u<<5;
    d.signal_count=2;d.signals[0]={1,81,1000400,0,-32767};d.signals[1]={2,80,999000,-1,-192};
    return d;
}
void unit() {
    auto d=example();o::Payload p{};check(o::encode(d,p),"encode typed snapshot");o::Document decoded;
    check(o::decode(p,decoded),"decode typed snapshot");o::Payload q{};check(o::encode(decoded,q) && p==q,"exact canonical roundtrip");
    check(decoded.identity==d.identity && decoded.samples[0].seq==d.samples[0].seq && decoded.samples[0].injected.batch==d.samples[0].injected.batch,"large identifiers exact");
    check(decoded.samples[0].physical.axes[0]==INT32_MIN && decoded.samples[0].delivered.axes[0]==INT32_MAX && decoded.samples[0].physical.buttons[127]==127 && decoded.samples[0].injected.buttons[127],"raw range and bit127 exact");
    auto refuse=[&](o::Payload bad,const char* why){o::Document out=decoded;check(!o::decode(bad,out) && out.frame==decoded.frame,why);};
    q=p;q[1]++;refuse(q,"unknown version refused");q=p;q[2]--;refuse(q,"short declared length refused");q=p;q.back()=1;refuse(q,"nonzero tail refused");
    auto invalid=[&](o::Document bad,const char* why){o::Payload old=p;check(!o::encode(bad,old) && old==p,why);};
    auto bad=d;bad.sample_count=9;invalid(bad,"oversize sample table refused");bad=d;bad.signal_count=17;invalid(bad,"oversize signals refused");
    bad=d;bad.actions[0].value=std::numeric_limits<float>::quiet_NaN();invalid(bad,"NaN action refused");
    bad=d;bad.actions[0].sample=1;invalid(bad,"missing source sample refused");bad=d;bad.samples[0].stale=true;invalid(bad,"live stale source refused");
    bad=d;bad.samples[0].poll=Call::Pending;invalid(bad,"valid pending source refused");bad=d;bad.samples[0].injected.batch=0;invalid(bad,"injection without batch refused");
    bad=d;bad.samples[1]=bad.samples[0];bad.sample_count=2;invalid(bad,"duplicate device refused");
    bad=d;bad.signals[1].offset=0;invalid(bad,"mismatched signal source frame refused");bad=d;bad.signals[1].us=d.boundary_us+1;invalid(bad,"future signal refused");
    bad=d;bad.signals[1].id=1;invalid(bad,"duplicate signal id refused");
    o::Cursor c(identity());o::Document out;
    check(c.accept(p,1000500,1000,out).state==o::Acceptance::Accepted,"matching fresh join accepted");
    check(c.accept(p,1000500,1000,out).state==o::Acceptance::Repeated,"repeat is not new");
    d.frame++;d.signals[0].frame++;d.signals[1].frame++;check(o::encode(d,q),"next frame encoded");
    check(c.accept(q,1002000,1000,out).state==o::Acceptance::Stale && out.frame==81,"stale not displayed as new");
    check(c.accept(q,999999,1000,out).state==o::Acceptance::Stale,"future observation refused");
    check(c.accept(q,1000500,1000,out).state==o::Acceptance::Accepted,"stale refusal did not consume new frame");
    check(c.accept(p,1000500,1000,out).state==o::Acceptance::Reversed,"frame reversal refused");
    for(int k=0;k<6;++k) {auto id=identity();switch(k){case 0:id.process++;break;case 1:id.process_start++;break;case 2:id.nonce[2]++;break;case 3:id.executable[0]++;break;case 4:id.build[0]++;break;case 5:id.profile[0]++;break;}
        o::Cursor wrong(id);check(wrong.accept(p,1000500,1000,out).state==o::Acceptance::WrongIdentity,"wrong run/process/build/profile identity refused");}
    // An overrun is surfaced, not silently accepted as a complete event history.
    d=example();d.event_seq=70;d.event_count=64;
    for(std::size_t i=0;i<64;++i)d.events[i]={i+7,1000000,EventKind::BatchAccepted,0,7,i+7};
    check(o::encode(d,q),"70-event ring encoded");o::Cursor ring(identity());auto r=ring.accept(q,1000500,1000,out);
    check(r.state==o::Acceptance::Accepted && r.events_lost==6 && out.events[0].seq==7 && out.events[63].seq==70,"first reader detects six overwritten events");
    d.frame++;d.signals[0].frame++;d.signals[1].frame++;check(o::encode(d,q),"unchanged ring encoded");
    check(ring.accept(q,1000500,1000,out).events_lost==0,"same ring is not a new gap");
    bad=d;bad.events[7].seq++;invalid(bad,"noncontiguous ring refused");
}
void actual_core() {
    // Real Core resolver and real event ring; provider is fake, no hardware.
    fake::Provider provider;fake::Device dev;
    dev.listed={"{11111111-1111-1111-1111-111111111111}","{22222222-2222-2222-2222-222222222222}","fake wheel"};
    dev.caps.axis[0]=true;dev.caps.range_ok[0]=true;dev.caps.min[0]=0;dev.caps.max[0]=65535;dev.state.axes[0]=32768;
    provider.add(dev);CoreConfig cfg;Core core(provider,cfg);
    const auto profile=read_profile("[Controls]\nSchema = 1\nProfile = Observer fixture\nRevision = r1\nTransmission = Sequential\nsteer = axis 0 dev={11111111-1111-1111-1111-111111111111} prod={22222222-2222-2222-2222-222222222222} range=0..65535 rest=32768 travel=+1\n");
    check(profile.ok,"fixture profile parsed");core.set_profile(profile);core.service_devices();
    bool live=false;o::Cursor cursor(identity());
    for(int i=0;i<150;++i) {
        if(i%2)provider.at(0).script.push_back({Call::Pending,Call::NotCalled});
        provider.advance(1000);const auto& f=core.tick();core.service_devices();o::Document d;
        check(o::compose(identity(),f,core.ring(),nullptr,0,true,true,d),"actual Core frame and ring composed");
        o::Payload p;check(o::encode(d,p),"actual Core encoded");o::Document out;
        check(cursor.accept(p,provider.now_us(),100000,out).state==o::Acceptance::Accepted,"actual Core joined");
        live|=out.actions[0].status==Status::Live;
    }
    check(live,"actual Core wheel resolves live");
    const auto& last=core.tick();check(last.event_seq>64,"actual Core overwrote its event ring");
    o::Document d;check(o::compose(identity(),last,core.ring(),nullptr,0,true,true,d),"actual overwritten ring composed");
    o::Payload p;check(o::encode(d,p),"actual overwritten ring encoded");o::Cursor late(identity());o::Document out;
    const auto result=late.accept(p,provider.now_us(),100000,out);
    check(result.state==o::Acceptance::Accepted && result.events_lost==last.event_seq-64,"late reader detects actual Core ring gap");
}
int main() {try {unit();actual_core();std::printf("PASS %d observer domain checks; no game/device/force\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
