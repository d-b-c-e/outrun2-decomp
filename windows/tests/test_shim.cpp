// Input shim slice 1 (windows/docs/INPUT-SHIM.md r3.1 section 17, P1-P8 and P12-P18 plus the r3 corrections): the pure
// core against the scripted provider. No DirectInput, no files, no game.
#include "shim.hpp"
#include "shim_fake_provider.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using namespace dbce::shim;
namespace fk=dbce::shim::fake;

namespace {
int checks=0;
void check(bool v,const std::string& what){++checks;if(!v)throw std::runtime_error(what);}

const std::string W="{d71b8350-61b7-11f1-8001-444553540000}",WP="{0006346e-0000-0000-0000-504944564944}";
const std::string S="{d9b1d890-6f3f-11f1-8002-444553540000}",SP="{0002f3a1-0000-0000-0000-504944564944}";
const std::string Nonce="abcdefgh12345678";
std::string wheel(const std::string& rest){return rest+" dev="+W+" prod="+WP;}
std::string shifter(const std::string& rest){return rest+" dev="+S+" prod="+SP;}
std::string axis(int i,int restv){return "axis "+std::to_string(i)+" dev="+W+" prod="+WP+" range=0..65535 rest="+std::to_string(restv)+" travel=+1";}
int A(const char* id){return action_index(id);}

Capability capability(){Capability c;c.unsupported[std::size_t(A("horn"))]="no horn in this game";return c;}

std::string profile(const std::vector<std::string>& lines){
    std::string s="[Controls]\nSchema = 1\nProfile = Test\nRevision = r1\nTransmission = Sequential\n";
    for(const auto& l:lines)s+=l+"\n";
    return s;
}
std::string batch(std::uint64_t n,const std::vector<std::string>& commands){
    std::string s="nonce="+Nonce+"\nbatch="+std::to_string(n)+"\n";
    for(const auto& c:commands)s+=c+"\n";
    return s;
}

struct Rig {
    fk::Provider p;
    std::unique_ptr<Core> core;
    std::size_t w=0,s=0;
    explicit Rig(bool latched,const std::string& profile_text,bool with_shifter=false,CoreConfig cfg={}){
        fk::Device d;d.listed={W,WP,"Wheel"};d.caps=fk::wheel_caps();
        w=p.add(d);
        if(with_shifter){fk::Device x;x.listed={S,SP,"Shifter"};x.caps=fk::wheel_caps();s=p.add(x);}
        cfg.latched=latched;cfg.capability=capability();
        core=std::make_unique<Core>(p,cfg);
        core->set_profile(read_profile(profile_text));
    }
    const Frame& step(std::int64_t us=10000){core->service_devices();p.advance(us);return core->tick();}
    void admit(){core->request_admission(Session{Nonce,2000000000ull},1999999000ull,true,true);}
    fk::Device& dev(std::size_t i){return p.at(i);}
};
const ActionValue& act(const Frame& f,const char* id){return f.action[std::size_t(A(id))];}
bool has_event(Core& c,EventKind k,const std::string& part=""){
    for(const auto& e:c.drain_events())if(e.kind==k&&e.detail.find(part)!=std::string::npos)return true;
    return false;
}

void test_sha256(){
    check(hex(sha256(""))=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","sha256 empty");
    check(hex(sha256("abc"))=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","sha256 abc");
    check(hex(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))=="248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1","sha256 two blocks");
}

void test_profile(){
    auto ok=read_profile("\xEF\xBB\xBF; comment\r\n[Other]\r\nx = 1\r\n[Controls]\r\nSchema = 1\r\n"+wheel("confirm = button 0")+"\r\n");
    check(ok.ok&&ok.section.error.empty()&&ok.section.bound.size()==1,"BOM, CRLF, other sections: "+ok.error);
    auto dup=read_profile(profile({wheel("confirm = button 0"),wheel("confirm = button 1")}));
    check(!dup.ok&&dup.error.find("duplicate key \"confirm\"")!=std::string::npos&&dup.error.find("lines 6 and 7")!=std::string::npos,"duplicate key named with both lines: "+dup.error);
    auto two=read_profile("[Controls]\nSchema = 1\n[controls]\nSchema = 1\n");
    check(!two.ok&&two.error.find("second [Controls] at line 3")!=std::string::npos,"second [Controls]: "+two.error);
    check(!read_profile("[Controls]\nSchema = 1\nProfile = \xC0\x80\n").ok,"overlong UTF-8 refused");
    check(!read_profile("[Controls]\nSchema = 1\nProfile = \xED\xA0\x80\n").ok,"surrogate refused");
    check(!read_profile(std::string(MaxProfileBytes+1,'a')).ok,"oversize refused");
    check(!read_profile("[Wheel]\nx = 1\n").ok,"no [Controls]");
    check(!read_profile("[Controls]\nSchema = 1\njust words\n").ok,"a line that is not key = value");
    auto two_schema=read_profile("[Controls]\nSchema = 2\n");
    check(two_schema.ok&&!two_schema.section.error.empty(),"Schema 2: structurally fine, nothing applies");
}

void test_session(){
    Session s;std::string why;
    check(parse_session("nonce="+Nonce+"\nexpires=2000\n",1000,s,why)&&s.nonce==Nonce&&s.expires_unix==2000,"valid session: "+why);
    check(!parse_session("nonce="+Nonce+"\nexpires=2000\nowner=harness\n",1000,s,why)&&why.find("inject.receipt")!=std::string::npos,"owner= refused (receipt is separate)");
    check(!parse_session("nonce="+Nonce+"\nnonce="+Nonce+"\nexpires=2000\n",1000,s,why),"duplicate nonce refused");
    check(!parse_session("nonce="+Nonce+"\n",1000,s,why),"missing expires refused");
    check(!parse_session("nonce="+Nonce+"\nexpires=900\n",1000,s,why),"expired refused");
    check(!parse_session("nonce="+Nonce+"\nexpires=5000\n",1000,s,why),"more than an hour refused");
    check(!parse_session("nonce=abc-defgh\nexpires=2000\n",1000,s,why),"nonce characters");
    check(!parse_session(std::string(513,'a'),1000,s,why),"oversize refused");
}

// P1: identity in the listing and after the open.
void test_identity(){
    {Rig r(false,profile({wheel("confirm = button 0")}));
     r.dev(r.w).listed.prod=SP;   // the instance is listed with another product
     const auto& f=r.step();
     check(act(f,"confirm").status==Status::ProductMismatch,"listed product mismatch is a status");
     check(r.dev(r.w).opens==0,"a mismatched instance is never opened");}
    {Rig r(false,profile({wheel("confirm = button 0")}));
     r.dev(r.w).opened={W,SP,"Other"};   // the identity changes between listing and open
     const auto& f=r.step();
     check(act(f,"confirm").status==Status::ProductMismatch,"identity changed during the open");
     check(r.dev(r.w).closes==1,"the wrong handle is closed");}
}

// P2, P3: objects by offset, non-contiguous axes, ranges read back.
void test_objects_and_ranges(){
    Rig r(false,profile({"brake = "+axis(1,0),"throttle = "+axis(2,0),"clutch = "+axis(5,0),
                         "steer = axis 0 dev="+W+" prod="+WP+" range=0..65535 rest=32768 travel=+1"}));
    r.dev(r.w).caps.range_ok[5]=false;
    r.dev(r.w).caps.min[0]=-32768;r.dev(r.w).caps.max[0]=32767;   // the device keeps a signed range
    r.dev(r.w).state.axes[0]=32767;
    const auto& f=r.step();
    check(act(f,"brake").status==Status::ObjectMissing,"axis 1 (Y) is not on a device with X, Z, Rz");
    check(act(f,"throttle").status==Status::Live,"axis 2 (Z) applies");
    check(act(f,"clutch").status==Status::RangeUnqualified,"an unreadable range is unqualified");
    check(std::fabs(act(f,"steer").value-1.0f)<1e-4f,"rescaled from the read-back range -32768..32767");
    r.dev(r.w).state.axes[0]=-32768;
    check(std::fabs(r.step().action[std::size_t(A("steer"))].value+1.0f)<1e-4f,"full left from the read-back range");
    Rig e(false,profile({"throttle = "+axis(2,0)}));
    e.dev(e.w).caps.min[2]=e.dev(e.w).caps.max[2]=0;   // an empty range
    check(act(e.step(),"throttle").status==Status::RangeUnqualified,"an empty range is unqualified");
}

// P4, P5: read classification, epoch end, reacquisition, rearm per action.
void test_reads_and_rearm(){
    Rig r(false,profile({wheel("confirm = button 3"),"throttle = "+axis(2,0),"brake = "+axis(5,0),
                         "steer = axis 0 dev="+W+" prod="+WP+" range=0..65535 rest=32768 travel=+1",wheel("handbrake = button 7")}));
    auto& d=r.dev(r.w);
    d.state.buttons[3]=0x80;d.state.axes[2]=52428;d.state.axes[5]=45875;d.state.axes[0]=49151;d.state.buttons[7]=0x80;
    auto f=r.step();   // first epoch: everything held
    check(act(f,"confirm").status==Status::Rearming&&act(f,"confirm").value==0,"a held button is masked at the epoch start");
    check(act(f,"throttle").status==Status::Rearming&&act(f,"throttle").value==0,"a held throttle waits for rest");
    check(act(f,"handbrake").status==Status::Rearming,"a held handbrake waits for rest");
    check(act(f,"brake").status==Status::Live&&std::fabs(act(f,"brake").value-0.7f)<1e-3f,"a held brake goes live on the fresh read");
    check(act(f,"steer").status==Status::Live&&act(f,"steer").value>0.4f,"steering goes live on the fresh read");
    check(f.down==0,"no press manufactured at the epoch start");
    d.state.buttons[3]=0;d.state.axes[2]=0;d.state.buttons[7]=0;
    f=r.step();
    check(act(f,"confirm").status==Status::Live&&act(f,"throttle").status==Status::Live&&act(f,"handbrake").status==Status::Live,"released: live");
    d.state.buttons[3]=0x80;d.state.axes[2]=52428;
    f=r.step();
    check(act(f,"confirm").value==1&&(f.down>>A("confirm")&1),"a real press after rearm");
    check(std::fabs(act(f,"throttle").value-0.8f)<1e-3f,"throttle live");
    // Poll succeeds, the state read loses acquisition: no sample, the epoch ends, outputs neutral, one up edge.
    d.script.push_back({Call::Ok,Call::AcquisitionLost});
    const int reads=d.state_reads;
    f=r.step();
    check(d.state_reads==reads+1,"the state read is a separate call after Poll");
    check(act(f,"confirm").status==Status::ReadFailed&&act(f,"confirm").value==0&&(f.up>>A("confirm")&1),"epoch end: neutral with one up edge");
    check(act(f,"throttle").value==0,"the held throttle is neutral in the same frame");
    check(!f.samples.empty()&&!f.samples[0].valid&&f.samples[0].poll==Call::Ok&&f.samples[0].state==Call::AcquisitionLost,"both results recorded");
    check(has_event(*r.core,EventKind::EpochEnd,"acquisition lost"),"epoch-end event");
    // Reacquisition of the same handle: a new epoch with rearm (the button and throttle are still held).
    f=r.step();
    check(d.reacquires==1&&f.samples[0].epoch==2,"reacquired as epoch 2");
    check(act(f,"confirm").status==Status::Rearming&&act(f,"throttle").status==Status::Rearming,"rearm after reacquisition");
    check(f.down==0,"no press manufactured by the reconnect");
    // A failed Poll: the state read is not called.
    d.script.push_back({Call::Gone,Call::Ok});
    const int reads2=d.state_reads;
    f=r.step();
    check(d.state_reads==reads2&&f.samples[0].state==Call::NotCalled,"a failed Poll skips the state read");
    // Gone: closed, then reopened through a new listing as epoch 3.
    f=r.step();
    check(d.closes==1&&d.opens==2&&f.samples[0].epoch==3,"gone: closed and reopened as a new epoch");
    // One invalid read never rearms: release, but the read fails, so the next epoch still waits for rest.
    d.state.axes[2]=0;d.script.push_back({Call::Ok,Call::Gone});
    r.step();
    d.state.axes[2]=52428;
    f=r.step();
    check(act(f,"throttle").status==Status::Rearming,"an invalid read gave no rearm credit");
    check(r.p.misuse==0,"no read of a closed handle, no close during a read");
}

// P6: the worker never blocks the tick.
void test_worker_never_blocks_tick(){
    Rig r(false,profile({wheel("confirm = button 0")}));
    r.p.real_clock=true;r.p.enumerate_delay_ms=300;r.p.open_delay_ms=200;
    std::atomic<bool> stop{false};
    std::thread worker([&]{while(!stop){r.core->service_devices();std::this_thread::sleep_for(std::chrono::milliseconds(5));}});
    std::int64_t worst=0;bool live=false;
    for(int i=0;i<200&&!live;++i){
        const auto t0=std::chrono::steady_clock::now();
        const auto& f=r.core->tick();
        const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-t0).count();
        worst=std::max<std::int64_t>(worst,us);
        live=act(f,"confirm").status==Status::Live;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    stop=true;worker.join();
    check(live,"the device opened through the worker");
    check(worst<20000,"no tick waited for enumeration or open (worst "+std::to_string(worst)+" us)");
    check(r.p.misuse==0,"no misuse");
}

// P7: statuses and the start line after the device checks.
void test_start_line(){
    Rig r(false,profile({wheel("confirm = button 0"),"throttle = "+axis(1,0),"brake = button 2 dev="+S+" prod="+SP,
                         wheel("horn = button 4"),"nitro = button 1 dev="+W+" prod="+WP,"select =",
                         "back = button 5 dev="+W+" prod="+SP}));
    r.step();
    const auto rep=r.core->start_report();
    check(rep.line=="controls: profile \"Test\" revision r1 via shim-ini-controls-1: applied 1 (confirm); unsupported 1 (horn); "
                    "invalid 2 (nitro,back); unavailable 2 (throttle,brake)","start line: "+rep.line);
    bool missing=false,object=false;
    for(const auto& n:rep.notes){if(n.find("brake device-missing")!=std::string::npos)missing=true;if(n.find("throttle object-missing: no axis 1")!=std::string::npos)object=true;}
    check(missing&&object,"one note per unavailable action, with the reason");
}

// P8: status changes coalesce per key and are bounded.
void test_status_queue(){
    CoreConfig cfg;cfg.status_queue=4;
    Rig r(false,profile({wheel("confirm = button 0"),wheel("back = button 1"),wheel("start = button 2"),wheel("camera = button 3"),wheel("navUp = button 4")}),false,cfg);
    r.step();
    check(r.core->status_dropped()>0,"distinct keys beyond the bound are counted as dropped");
    const auto before=r.core->status_coalesced();
    r.dev(r.w).script.push_back({Call::Ok,Call::Gone});r.step();
    check(r.core->status_coalesced()>before,"a key already queued is coalesced");
    check(r.core->drain_status().size()<=4,"the queue never exceeds its bound");
}

// P12: the latch, INERT and admission refusal.
void test_latch(){
    {Rig r(true,"[Wheel]\nx = 1\n");
     const auto& f=r.step();
     check(r.core->run()==Run::Inert&&f.run==Run::Inert,"latched with no usable profile: inert");
     bool neutral=true;for(const auto& a:f.action)if(a.value!=0)neutral=false;
     check(neutral&&f.suppress_native,"inert: every action neutral, native suppressed");
     r.admit();r.step();
     check(r.core->run()==Run::Inert,"inert is final");}
    {Rig r(true,profile({wheel("confirm = button 0")}));
     r.core->refuse_admission("inject.on expired");
     r.step();
     check(r.core->run()==Run::Failed,"refused admission fails the run");}
    {CoreConfig cfg;cfg.admission_us=50000;
     Rig r(true,profile({wheel("confirm = button 0")}),false,cfg);
     r.dev(r.w).state.buttons[0]=0x80;
     auto f=r.step();
     check(r.core->run()==Run::Latched&&act(f,"confirm").status==Status::TestNeutral&&act(f,"confirm").value==0,"latched: physical input never reaches an action");
     for(int i=0;i<10;++i)r.step();
     check(r.core->run()==Run::Failed,"no admission request in time fails the run");}
    {Rig r(false,profile({wheel("confirm = button 0")}));
     r.step();
     check(r.core->run()==Run::None,"not latched: no test run");}
}

// P13: admission requirements.
void test_admission(){
    {Rig r(true,profile({wheel("confirm = button 0")}));
     r.core->request_admission(Session{Nonce,2000000000ull},1999999000ull,false,true);r.step();
     check(r.core->run()==Run::Failed&&r.core->run_reason().find("fence")!=std::string::npos,"an unverified fence refuses admission");}
    {Rig r(true,profile({wheel("confirm = button 0")}));
     r.core->request_admission(Session{Nonce,2000000000ull},1999999000ull,true,false);r.step();
     check(r.core->run()==Run::Failed&&r.core->run_reason().find("force")!=std::string::npos,"a force path refuses admission");}
    {CoreConfig cfg;cfg.admission_us=50000;
     Rig r(true,profile({wheel("confirm = button 0"),"back = button 1 dev="+S+" prod="+SP}),false,cfg);   // the shifter is absent
     r.admit();
     for(int i=0;i<10;++i)r.step();
     check(r.core->run()==Run::Failed&&r.core->run_reason().find("devices not ready")!=std::string::npos,"a missing workload device refuses admission");}
    {Rig r(true,profile({wheel("confirm = button 0")}));
     r.admit();r.step();
     check(r.core->run()==Run::Waiting,"admitted: waiting for the first batch");}
}

// P14: batch identity, order of checks, whole-batch validation, the first-batch wait.
void test_batches(){
    auto admitted=[](CoreConfig cfg={}){
        auto r=std::make_unique<Rig>(true,profile({wheel("confirm = button 0"),"steer = axis 0 dev="+W+" prod="+WP+" range=0..65535 rest=32768 travel=+1"}),false,cfg);
        r->admit();r->step();return r;};
    {auto r=admitted();
     const auto b1=batch(1,{"inject raw axis 0 dev="+W+" value=65535 ms=500"});
     check(r->core->offer_batch(b1).verdict==BatchVerdict::Accepted,"batch 1 accepted");
     r->step(1000);   // activated: deadline = now + 500 ms
     r->p.advance(300000);
     const auto dup=r->core->offer_batch(b1);
     check(dup.verdict==BatchVerdict::Duplicate,"the same n and bytes: a duplicate");
     r->step(150000);
     check(act(r->core->tick(),"steer").injected,"still running at 450 ms");
     r->p.advance(60000);
     check(!act(r->core->tick(),"steer").injected,"ended at 500 ms: the duplicate did not extend it");
     check(r->core->run()==Run::Running,"a duplicate is not a failure");
     const auto same_n=r->core->offer_batch(batch(1,{"inject raw axis 0 dev="+W+" value=0 ms=500"}));
     check(same_n.verdict==BatchVerdict::Refused,"the same n with other bytes is refused");
     r->step();
     check(r->core->run()==Run::Failed,"and fails the run");}
    {auto r=admitted();
     r->core->offer_batch(batch(2,{"inject raw button 0 dev="+W+" value=1 ms=500"}));r->step();
     check(r->core->offer_batch(batch(1,{"inject raw button 0 dev="+W+" value=1 ms=500"})).verdict==BatchVerdict::Refused,"a lower n is refused");
     r->step();
     check(r->core->run()==Run::Failed,"a regression fails the run");}
    {auto r=admitted();
     std::vector<std::string> many(33,"inject raw button 0 dev="+W+" value=1 ms=500");
     check(r->core->offer_batch(batch(1,many)).verdict==BatchVerdict::Refused,"33 commands refused");
     r->step();check(r->core->run()==Run::Failed,"and the run fails");}
    {auto r=admitted();
     const auto bad=batch(1,{"inject raw button 0 dev="+W+" value=1 ms=500","inject raw button 1 dev="+W+" value=1 ms=500",
                             "inject raw button 2 dev="+W+" value=1 ms=500","inject raw button 3 dev="+W+" value=1 ms=500","inject raw wobble 4"});
     check(r->core->offer_batch(bad).verdict==BatchVerdict::Refused,"a bad 5th line refuses the whole batch");
     const auto& f=r->step();
     check(r->core->run()==Run::Failed&&!f.samples[0].injected.any(),"nothing from the refused batch was delivered");}
    {CoreConfig cfg;cfg.wait_first_us=100000;
     auto r=admitted(cfg);
     for(int i=0;i<12;++i)r->step();
     check(r->core->run()==Run::Failed&&r->core->run_reason().find("first-batch")!=std::string::npos,"no first batch in time fails the run");}
}

// P15: the frame boundary over the admitted workload.
void test_frame_boundary(){
    const auto prof=profile({"steer = axis 0 dev="+W+" prod="+WP+" range=0..65535 rest=32768 travel=+1",shifter("shiftUp = button 1")});
    {Rig r(true,prof,true);
     r.admit();r.step();
     r.core->offer_batch(batch(1,{"inject raw axis 0 dev="+W+" value=65535 ms=1000"}));
     auto f=r.step();
     check(act(f,"steer").injected&&act(f,"steer").value>0.99f,"steer injected");
     r.dev(r.s).script.push_back({Call::Ok,Call::Gone});   // the shifter fails in the next tick
     f=r.step();
     check(r.core->run()==Run::Failed,"a second-device failure fails the run");
     check(!act(f,"steer").injected&&act(f,"steer").value==0,"and the first device's injected steer never reached the sink in that frame");}
    {Rig r(true,prof,true);
     r.admit();r.step();
     r.core->offer_batch(batch(1,{"inject raw axis 0 dev="+W+" value=65535 ms=1000"}));r.step();
     r.dev(r.s).script.push_back({Call::AcquisitionLost,Call::Ok});   // a device the run never injected into
     r.step();
     check(r.core->run()==Run::Failed,"epoch loss of a not-yet-injected workload device fails the run");}
}

// P16 and correction 4: RUNNING is neutral outside the injection set; physical values never rearm or release.
void test_running_neutral(){
    Rig r(true,profile({"steer = axis 0 dev="+W+" prod="+WP+" range=0..65535 rest=32768 travel=+1","throttle = "+axis(2,0),wheel("confirm = button 3")}));
    auto& d=r.dev(r.w);
    d.state.axes[2]=60000;d.state.buttons[3]=0x80;   // physically held from startup
    r.admit();
    auto f=r.step();
    check(act(f,"throttle").status==Status::TestNeutral&&act(f,"throttle").value==0&&f.down==0,"physical holds at startup reach nothing");
    r.core->offer_batch(batch(1,{"inject raw axis 0 dev="+W+" value=65535 ms=100","inject raw button 3 dev="+W+" value=1 ms=200"}));
    f=r.step();
    check(act(f,"steer").injected&&act(f,"confirm").injected&&(f.down>>A("confirm")&1),"injected press: one down edge");
    check(act(f,"throttle").status==Status::TestNeutral&&act(f,"throttle").value==0,"a held physical pedal outside the set stays neutral");
    check(f.samples[0].physical.axes[2]==60000,"its raw value is still observed");
    f=r.step(100000);
    check(!act(f,"steer").injected&&act(f,"steer").status==Status::TestNeutral,"per-object expiry returns to test-neutral");
    check(has_event(*r.core,EventKind::ObjectExpired,"axis 0"),"object-expired event");
    f=r.step(100000);
    check(act(f,"confirm").status==Status::TestNeutral&&(f.up>>A("confirm")&1),"the injected press ends with an up edge although the button is physically held");
    d.state.buttons[3]=0;f=r.step();d.state.buttons[3]=0x80;f=r.step();
    check(f.down==0,"physical presses during a run give no edge");
    // The converse: injected held, physically released, then an epoch loss.
    r.core->offer_batch(batch(2,{"inject raw button 3 dev="+W+" value=1 ms=5000"}));
    d.state.buttons[3]=0;
    f=r.step();
    check(act(f,"confirm").value==1,"injected hold with the button physically up");
    d.script.push_back({Call::AcquisitionLost,Call::Ok});
    f=r.step();
    check(r.core->run()==Run::Failed&&act(f,"confirm").value==0&&(f.up>>A("confirm")&1),"epoch loss: failed, the injected hold released with one up edge");
    r.core->offer_batch(batch(3,{"inject raw button 3 dev="+W+" value=1 ms=500"}));
    f=r.step();f=r.step();
    check(r.core->run()==Run::Failed&&act(f,"confirm").value==0,"a new file never rearms a failed run");
}

// P17: COMPLETED and EXPIRED are terminal and neutral.
void test_terminal(){
    {Rig r(true,profile({wheel("confirm = button 0")}));
     r.admit();r.step();
     r.core->offer_batch(batch(1,{"inject raw button 0 dev="+W+" value=1 ms=5000"}));r.step();
     r.core->offer_batch(batch(2,{"end"}));
     auto f=r.step();
     check(r.core->run()==Run::Completed&&act(f,"confirm").value==0,"end: completed and neutral");
     r.dev(r.w).state.buttons[0]=0x80;f=r.step();
     check(act(f,"confirm").value==0,"physical input never returns after completion");
     check(r.core->offer_batch(batch(3,{"inject raw button 0 dev="+W+" value=1 ms=500"})).verdict==BatchVerdict::Refused&&r.core->run()==Run::Completed,"later batches are refused; still completed");}
    {Rig r(true,profile({wheel("confirm = button 0")}));
     r.core->request_admission(Session{Nonce,1000000002ull},1000000000ull,true,true);   // two seconds
     r.step();
     r.core->offer_batch(batch(1,{"inject raw button 0 dev="+W+" value=1 ms=15000"}));r.step();
     r.step(2000000);
     check(r.core->run()==Run::Expired,"the session expiry ends the run");}
}

// P18: provenance from action to sample to batch.
void test_provenance(){
    Rig r(true,profile({"steer = axis 0 dev="+W+" prod="+WP+" range=0..65535 rest=32768 travel=+1"}));
    r.admit();r.step();
    const auto text=batch(1,{"inject raw axis 0 dev="+W+" value=65535 ms=100"});
    const auto accepted=r.core->offer_batch(text);
    auto f=r.step();
    const auto& a=act(f,"steer");
    check(a.injected&&a.sample>=0,"the action names its sample");
    const auto& s=f.samples[std::size_t(a.sample)];
    check(s.device==0&&s.epoch==1&&s.seq>0,"the sample names its device, epoch and sequence");
    check(s.injected.batch==1&&std::equal(s.injected.hash,s.injected.hash+8,accepted.generation.hash.begin()),"the mark names the batch and its hash");
    check((s.injected.axes&1u)&&!s.injected.buttons.any(),"only the delivered object is marked");
    f=r.step(200000);
    check(!f.samples[0].injected.any(),"a later physical read carries no mark");
}

// Correction 3: no accepted generation disappears.
void test_generations(){
    Rig r(true,profile({wheel("confirm = button 0"),"steer = axis 0 dev="+W+" prod="+WP+" range=0..65535 rest=32768 travel=+1"}));
    r.admit();r.step();
    r.core->drain_events();
    r.core->offer_batch(batch(1,{"inject raw button 0 dev="+W+" value=1 ms=50"}));   // a short press
    r.core->offer_batch(batch(2,{"inject raw axis 0 dev="+W+" value=65535 ms=500"}));
    auto f=r.step(1000);
    check(act(f,"confirm").value==1&&(f.down>>A("confirm")&1),"batch 1's short press was activated, not overwritten");
    f=r.step(1000);
    check(act(f,"steer").injected&&act(f,"confirm").value==0,"batch 2 activated on the next tick, replacing batch 1");
    int activated=0;for(const auto& e:r.core->drain_events())if(e.kind==EventKind::BatchActivated)++activated;
    check(activated==2,"each accepted batch has an activation event");
    r.core->offer_batch(batch(3,{"inject raw button 0 dev="+W+" value=1 ms=50"}));
    r.core->offer_batch(batch(4,{"inject raw button 0 dev="+W+" value=0 ms=50"}));
    r.core->offer_batch(batch(4,{"inject raw button 0 dev="+W+" value=1 ms=60"}));   // conflicting: fails the run
    r.step();
    int superseded=0;for(const auto& e:r.core->drain_events())if(e.kind==EventKind::BatchSuperseded)++superseded;
    check(r.core->run()==Run::Failed&&superseded==2,"the run failed with batches 3 and 4 queued: both superseded, with events");
}
}

int main(){
    try{
        test_sha256();test_profile();test_session();test_identity();test_objects_and_ranges();test_reads_and_rearm();
        test_worker_never_blocks_tick();test_start_line();test_status_queue();test_latch();test_admission();test_batches();
        test_frame_boundary();test_running_neutral();test_terminal();test_provenance();test_generations();
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    std::cout<<"shim slice 1: "<<checks<<" checks passed\n";
    return 0;
}
