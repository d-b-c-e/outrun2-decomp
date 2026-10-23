// Input shim slice 2 (windows/docs/INPUT-SHIM.md r3.1 section 17: P22, P23, P25, P26): the OutRun 2006 adapter on fakes.
// The chain is the game's own: a fake pad sample through ps5::DualSenseInput (the Xbox build's pad adapter), then the
// shim core's frame from the scripted provider through outrun::desktop::Adapter, into the NativeInputState the game
// consumes at 453BB0. No devices, no game, no files.
#include "outrun_adapter.hpp"
#include "outrun_actions.hpp"
#include "shim_fake_provider.hpp"
#include "input/dual_sense.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using namespace dbce::shim;
using namespace outrun::desktop;
namespace fk=dbce::shim::fake;
namespace ps5=outrun::ps5;
using outrun::platform::NativeInputState;

namespace {
int checks=0;
void check(bool v,const std::string& what){++checks;if(!v)throw std::runtime_error(what);}

void player_car_guard(){
    check(active_player_car(16,0x7804b0u,2)&&active_player_car(18,0x7804b0u,0x0a),"initialized player car in race and pause");
    check(!active_player_car(13,0x7804b0u,2)&&!active_player_car(16,0,2),"mode and work must match");
    check(!active_player_car(16,0x7804b0u,1)&&!active_player_car(16,0x7804b0u,3),"pending car is unavailable");
}

const std::string W="{d71b8350-61b7-11f1-8001-444553540000}",WP="{0006346e-0000-0000-0000-504944564944}";
const std::string S="{d9b1d890-6f3f-11f1-8002-444553540000}",SP="{0002f3a1-0000-0000-0000-504944564944}";
const std::string Nonce="abcdefgh12345678";
std::string profile(const std::vector<std::string>& lines,const char* transmission="Sequential"){
    std::string s=std::string("[Controls]\nSchema = 1\nProfile = Rig\nRevision = r1\nTransmission = ")+transmission+"\n";
    for(const auto& l:lines)s+=l+"\n";
    return s;
}
const std::string Steer="steer = axis 0 dev="+W+" prod="+WP+" range=0..65535 rest=32768 travel=+1";
const std::string Throttle="throttle = axis 2 dev="+W+" prod="+WP+" range=0..65535 rest=0 travel=+1";
const std::string Brake="brake = axis 5 dev="+W+" prod="+WP+" range=0..65535 rest=0 travel=+1";
std::string wb(const std::string& action,int button){return action+" = button "+std::to_string(button)+" dev="+W+" prod="+WP;}
std::string sb(const std::string& action,int button){return action+" = button "+std::to_string(button)+" dev="+S+" prod="+SP;}

struct Rig {
    fk::Provider p;
    std::unique_ptr<Core> core;
    std::unique_ptr<Adapter> adapter;
    ps5::DualSenseInput pad;
    ps5::PadSample sample;                // the WGI pad as the pad adapter sees it
    GameView view;
    const Frame* frame=nullptr;
    std::size_t w=0,s=0;
    Rig(bool latched,const std::string& prof,bool shifter=false,HPattern::Config gb={}){
        fk::Device d;d.listed={W,WP,"MOZA R12 Base"};d.caps=fk::wheel_caps();w=p.add(d);
        if(shifter){fk::Device x;x.listed={S,SP,"DS-8X Shifter"};x.caps=fk::wheel_caps();s=p.add(x);}
        CoreConfig cfg;cfg.latched=latched;cfg.capability=outrun_capability();
        core=std::make_unique<Core>(p,cfg);
        const auto pr=read_profile(prof);
        core->set_profile(pr);
        adapter=std::make_unique<Adapter>(pr.section.transmission,gb);
        sample.connected=true;
    }
    // One game tick: the pad adapter's native input, then the shim frame applied at the interception point.
    NativeInputState tick(std::int64_t us=16000,const Keyboard* keyboard=nullptr){
        core->service_devices();p.advance(us);
        const auto& f=core->tick();
        frame=&f;
        auto out=pad.update(sample,view.mode);
        adapter->apply(f,view,out,keyboard);
        return out;
    }
    void admit(){core->request_admission(Session{Nonce,2000000000ull},1999999000ull,true,true);}
    void batch(std::uint64_t n,const std::vector<std::string>& commands){
        std::string b="nonce="+Nonce+"\nbatch="+std::to_string(n)+"\n";
        for(const auto& c:commands)b+=c+"\n";
        const auto r=core->offer_batch(b);
        check(r.verdict==BatchVerdict::Accepted,"batch accepted: "+r.why);
    }
    fk::Device& dev(std::size_t i){return p.at(i);}
};

// P22: the sink, as the game consumes it.
void test_sink(){
    Rig r(false,profile({Steer,Throttle,Brake,wb("shiftUp",13),wb("confirm",0),wb("navLeft",7),wb("start",36)}));
    r.view.mode=16;   // racing
    auto& d=r.dev(r.w);
    d.state.axes[0]=49151;   // steer +0.5
    auto out=r.tick();
    check(out.pc_input_layer==false,"the race reads the record, not the PC layer");
    {const double n=(49151.0-32767.5)/32767.5;   // the contract's centred normalization
     const auto word=std::int32_t(std::lround(n*32767.0));
     check(out.pc_device.axes_94[0x0e]==word&&out.steering==word/256,"steer word lround(n * 32767), byte /256: word "+std::to_string(out.pc_device.axes_94[0x0e]));}
    // Pedals max-merged with the pad (an unrelated device): pad trigger 100, wheel throttle 0.2 then 0.8.
    r.sample.r2=100;d.state.axes[2]=13107;
    out=r.tick();
    check(out.accelerator==100&&out.pc_device.axes_94[0x0d]==100,"the pad's larger trigger wins");
    d.state.axes[2]=52428;
    out=r.tick();
    check(out.accelerator==204&&out.pc_device.axes_94[0x0d]==204,"the wheel's larger throttle wins");
    r.sample.r2=0;d.state.axes[2]=0;
    // Race buttons: held bits; the shift edge only on the first frame of the hold.
    d.state.buttons[13]=0x80;
    out=r.tick();
    check((out.pc_device.buttons_04&0x2u)&&out.shift_up,"shiftUp: bit 0x2 and the edge");
    out=r.tick();
    check((out.pc_device.buttons_04&0x2u)&&!out.shift_up,"held: the bit stays, no second edge");
    d.state.buttons[13]=0;
    d.state.buttons[0]=0x80;
    out=r.tick();
    check(!(out.pc_device.buttons_04&0x2u),"confirm sets no race bit (0x2 is shift up while racing)");
    d.state.buttons[0]=0;r.tick();
    // Menus: D-pad bits, menu edges, frontend features.
    r.view.mode=3;
    d.state.buttons[7]=0x80;
    out=r.tick();
    check((out.pc_device.buttons_04&0x100u)&&out.menu_left&&(out.frontend.feature_mask&0x1000u),"navLeft in a menu");
    d.state.buttons[7]=0;d.state.buttons[36]=0x80;
    out=r.tick();
    check(out.start&&out.menu_confirm&&(out.frontend.feature_mask&1u)&&(out.pc_device.buttons_04&1u),"start: edge, confirm, feature 1, bit 0x1");
    check(r.adapter->signals().record_buttons==out.pc_device.buttons_04&&r.adapter->signals().frame_seq>0,"signals carry what was written");
}

// [Audio] AllowHorn: the horn is its own held action (native_race_horn sounds it). It writes no record bit, so it never
// shifts, and shift-down (the record bit behind the PC's X switch) never sounds it.
void test_horn(){
    Rig r(false,profile({Steer,wb("horn",3),wb("shiftDown",12)}));
    r.view.mode=16;auto& d=r.dev(r.w);
    const auto bits=r.tick().pc_device.buttons_04;
    d.state.buttons[3]=0x80;
    auto out=r.tick();
    check(out.horn&&out.pc_device.buttons_04==bits&&!out.shift_down,"horn held: out.horn, no record bit, no shift");
    out=r.tick();
    check(out.horn,"still held: still on (a held action, not an edge)");
    d.state.buttons[3]=0;
    out=r.tick();
    check(!out.horn,"released");
    d.state.buttons[12]=0x80;
    out=r.tick();
    check(!out.horn&&(out.pc_device.buttons_04&0x8u)&&out.shift_down,"shift-down sets its bit and edge, never the horn");
}

// P22: pedals keep working without the steering device (R1 review of 35a8aa3, item 1).
void test_pedals_without_steering(){
    Rig r(false,profile({"steer = axis 0 dev="+S+" prod="+SP+" range=0..65535 rest=32768 travel=+1",Throttle}),false);   // S absent
    r.view.mode=16;
    r.sample.left_x=255;   // the pad's stick, full right
    r.dev(r.w).state.axes[2]=0;r.tick();
    r.dev(r.w).state.axes[2]=65535;
    const auto out=r.tick();
    check(r.frame->action[std::size_t(action_index("steer"))].status==Status::DeviceMissing,"the steering device is missing");
    check(out.pc_input_layer==false&&out.pc_device.axes_94[0x0d]==255,"the throttle still drives the record");
    check(out.pc_device.axes_94[0x0e]==32767,"steering stays the pad's own record word (an unrelated device), not a stale wheel value");
}

// Neutral suppression in a latched process (R2-F3, N3): the pad's gameplay contribution never reaches the consumer.
void test_latched_suppression(){
    Rig r(true,profile({Steer,Throttle,wb("confirm",0)}));
    r.view.mode=16;
    r.admit();
    r.sample.buttons=ps5::Cross|ps5::Right;r.sample.left_x=255;r.sample.r2=255;   // the pad: full throttle, full right
    auto out=r.tick();
    check(out.accelerator==0&&out.steering==0&&out.pc_device.axes_94[0x0d]==0&&out.pc_device.axes_94[0x0e]==0,"pad driving suppressed");
    check(out.pc_device.buttons_04==0&&out.frontend.feature_mask==0&&!out.menu_confirm&&out.pc_input_layer==false,"pad buttons suppressed; the race reads the neutral record");
    check(r.adapter->signals().native_suppressed>0,"the suppressed native input is reported");
    r.batch(1,{"inject raw axis 2 dev="+W+" value=65535 ms=1000"});
    out=r.tick();
    check(out.accelerator==255&&out.pc_device.axes_94[0x0d]==255&&out.steering==0,"only the injected throttle reaches the game");
    // The stop path survives: the pad's exit combination still asks the host to quit.
    r.sample.buttons=ps5::Options|ps5::L1|ps5::R1;
    out=r.pad.update(r.sample,16);
    check(out.exit_requested,"the pad adapter raised exit");
    r.adapter->apply(r.core->tick(),r.view,out);
    check(out.exit_requested&&out.pc_device.buttons_04==0,"exit kept, its gameplay bits suppressed");
    // INERT (latched, no usable profile): everything neutral.
    Rig x(true,"[Wheel]\nx = 1\n");
    x.view.mode=16;x.sample.r2=255;
    out=x.tick();
    check(x.core->run()==Run::Inert&&out.accelerator==0&&out.pc_device.axes_94[0x0d]==0,"inert: the pad is suppressed too");
}

// Release (section 8 item 5): an epoch end or a stop releases the owned contribution; no stale wheel value returns.
void test_release(){
    Rig r(false,profile({Steer,Throttle,wb("shiftUp",13)}));
    r.view.mode=16;
    auto& d=r.dev(r.w);
    d.state.axes[0]=65535;d.state.buttons[13]=0x80;
    r.tick();d.state.buttons[13]=0;r.tick();d.state.buttons[13]=0x80;   // rearm the button
    auto out=r.tick();
    check(out.pc_device.axes_94[0x0e]==32767&&(out.pc_device.buttons_04&0x2u),"wheel steering and shift held");
    d.script.push_back({Call::Ok,Call::Gone});
    out=r.tick();
    check(out.pc_device.axes_94[0x0e]==0&&!(out.pc_device.buttons_04&0x2u)&&!out.shift_up,"epoch end: steering back to the pad's (centred) word, shift released, no edge");
    // The stop frame: ordinary keeps the pad, latched suppresses it.
    NativeInputState pad_only=r.pad.update(r.sample,16);
    r.sample.r2=200;pad_only=r.pad.update(r.sample,16);
    r.adapter->apply_neutral(false,pad_only);
    check(pad_only.accelerator==200,"stop (ordinary): the pad stays");
    r.adapter->apply_neutral(true,pad_only);
    check(pad_only.accelerator==0&&pad_only.pc_device.axes_94[0x0d]==0,"stop (latched): neutral");
}

// P23: the H-pattern state machine on a monotonic clock.
void test_hpattern(){
    HPattern h;
    bool g[6]{};g[2]=true;   // gate 3
    std::int64_t t=0;
    std::uint32_t gear=1;
    check(h.step(g,true,true,gear,t)==1&&h.state()==HPattern::State::Pulse,"gear 1 -> 3: shift up");
    check(h.step(g,true,true,gear,t+=16000)==0&&h.state()==HPattern::State::Release,"one released tick");
    check(h.step(g,true,true,gear,t+=16000)==0&&h.state()==HPattern::State::Wait,"waiting for the consumed gear");
    gear=2;
    check(h.step(g,true,true,gear,t+=16000)==1,"the gear changed: the next step");
    h.step(g,true,true,gear,t+=16000);
    gear=3;
    check(h.step(g,true,true,gear,t+=16000)==0&&h.state()==HPattern::State::AtTarget,"at the target");
    // No response: the deadline, two retries, then stopped (once) until the gate changes.
    HPattern n;t=0;gear=1;
    int pulses=0;
    for(int i=0;i<200;++i)if(n.step(g,true,true,gear,t+=16000)!=0)++pulses;
    check(pulses==3&&n.state()==HPattern::State::Stopped&&n.status().find("not reached (gear 1, target 3)")!=std::string::npos,"one pulse plus two retries, then stopped");
    bool g2[6]{};g2[1]=true;
    check(n.step(g2,true,true,gear,t+=16000)==1,"a new gate restarts");
    // A late response inside the deadline: no retry.
    HPattern l;t=0;gear=1;
    l.step(g,true,true,gear,t);
    for(int i=0;i<12;++i)check(l.step(g,true,true,gear,t+=16000)==0,"no retry before 250 ms");   // 192 ms
    gear=2;
    check(l.step(g,true,true,gear,t+=16000)==1,"responded at 208 ms: the next step, not a retry");
    // Ambiguous gates, unavailable gear, impossible gear.
    HPattern a;bool two[6]{};two[0]=two[3]=true;
    check(a.step(two,true,true,1,0)==0&&a.state()==HPattern::State::Ambiguous,"two gates: ambiguous, no shift");
    HPattern u;
    check(u.step(g,false,true,1,0)==0&&u.state()==HPattern::State::Unavailable,"not racing: unavailable");
    check(u.step(g,true,false,1,16000)==0&&u.state()==HPattern::State::Unavailable,"no car: unavailable");
    HPattern im;bool six[6]{};six[5]=true;t=0;
    for(int i=0;i<200;++i)im.step(six,true,true,5,t+=16000);
    check(im.state()==HPattern::State::Stopped,"gear 6 on a five-speed car: stopped after retries");
    // Slice 2a review 1: a gear word that is not a gear 1-6 never causes a pulse, before the first pulse or mid-sequence.
    for(std::uint32_t bad:{0u,7u,0xffffffffu}){
        HPattern v;
        check(v.step(g,true,true,bad,0)==0&&v.state()==HPattern::State::Unavailable,"gear word "+std::to_string(bad)+": unavailable, no pulse");
    }
    {HPattern v;t=0;
     check(v.step(g,true,true,1,t)==1,"a valid gear: pulse");
     check(v.step(g,true,true,0xffffffffu,t+=16000)==0&&v.state()==HPattern::State::Unavailable,"corrupt during release: no pulse");
     for(int i=0;i<30;++i)check(v.step(g,true,true,0,t+=16000)==0,"missing gear during wait: never a pulse");
     check(v.step(g,true,true,1,t+=16000)==1,"a valid gear again: the sequence resumes");}
    // Slice 2a review 2: the tick after a pulse is released even when the gate or the direction changes.
    {HPattern v;t=0;bool g4[6]{};g4[3]=true;
     check(v.step(g,true,true,1,t)==1,"gate 3 from gear 1: up");
     check(v.step(g4,true,true,1,t+=16000)==0,"gate 4 on the next tick: released first");
     check(v.step(g4,true,true,1,t+=16000)==1,"then the new sequence's pulse");}
    {HPattern v;t=0;bool g1[6]{};g1[0]=true;
     check(v.step(g,true,true,2,t)==1,"gate 3 from gear 2: up");
     check(v.step(g1,true,true,2,t+=16000)==0,"reversal to gate 1 on the next tick: released first");
     check(v.step(g1,true,true,2,t+=16000)==-1,"then down");}
    {HPattern v;t=0;bool none[6]{};
     check(v.step(g,true,true,1,t)==1,"pulse");
     check(v.step(none,true,true,1,t+=16000)==0,"gate released");
     check(v.step(g,true,true,1,t+=16000)==1,"gate again after a released tick: pulse");}
    // Through the adapter: DS-8X gates from the core, pulses as shift bits and edges, reset on a scene change.
    Rig r(false,profile({sb("gear1",0),sb("gear2",1),sb("gear3",2)},"H-pattern"),true);
    r.view.mode=16;r.view.car=true;r.view.gear=1;
    r.dev(r.s).state.buttons[2]=0x80;r.tick();r.dev(r.s).state.buttons[2]=0;r.tick();   // rearm the gate
    r.dev(r.s).state.buttons[2]=0x80;
    auto out=r.tick();
    check((out.pc_device.buttons_04&0x2u)&&out.shift_up,"gate 3 from gear 1: an up pulse with its edge");
    out=r.tick();
    check(!(out.pc_device.buttons_04&0x2u)&&!out.shift_up,"released on the next tick");
    // A gate change right after a pulse through the adapter: the bit drops for a tick, then a new edge.
    {Rig q(false,profile({sb("gear1",0),sb("gear2",1),sb("gear3",2),sb("gear4",3)},"H-pattern"),true);
     q.view.mode=16;q.view.car=true;q.view.gear=1;
     q.dev(q.s).state.buttons[2]=0x80;q.dev(q.s).state.buttons[3]=0x80;q.tick();
     q.dev(q.s).state.buttons[2]=0;q.dev(q.s).state.buttons[3]=0;q.tick();   // rearm both gates
     q.dev(q.s).state.buttons[2]=0x80;
     auto o=q.tick();
     check((o.pc_device.buttons_04&0x2u)&&o.shift_up,"gate 3: pulse");
     q.dev(q.s).state.buttons[2]=0;q.dev(q.s).state.buttons[3]=0x80;
     o=q.tick();
     check(!(o.pc_device.buttons_04&0x2u)&&!o.shift_up,"gate 4 on the next tick: the bit drops");
     o=q.tick();
     check((o.pc_device.buttons_04&0x2u)&&o.shift_up,"then a new pulse with its own edge");}
    // The race's pause (16 -> 18 -> 16; sana 44117): no pulse while paused, its first tick or after, and no retry spent;
    // on the resume a fresh sequence starts after the change's released tick, rather than the old one's 250 ms wait.
    r.view.mode=18;out=r.tick();
    check(!(out.pc_device.buttons_04&0x2u),"no pulse on the pause's own tick");
    for(int i=0;i<40;++i){out=r.tick();check(!(out.pc_device.buttons_04&0x2u)&&!out.shift_up,"no pulse while paused");}   // 640 ms
    check(r.adapter->gearbox().state()!=HPattern::State::Stopped,"a pause spends no retry");
    r.view.mode=16;out=r.tick();
    check(!(out.pc_device.buttons_04&0x2u),"no pulse on the resume's own tick");
    out=r.tick();
    check((out.pc_device.buttons_04&0x2u)&&out.shift_up,"a fresh sequence starts after the resume");
    r.view.mode=3;out=r.tick();out=r.tick();
    check(!(out.pc_device.buttons_04&0x2u)&&r.adapter->gearbox().state()==HPattern::State::Unavailable,"in a menu the gearbox is unavailable");
}

// The scene-aware gearbox entry point is shared by Native DirectInput and the shim adapter.
void test_hpattern_modes(){
    bool gates[6]{};gates[2]=true;
    for(const std::int64_t tick:{16667,8333}){
        HPattern h;std::int64_t now=0;
        check(h.step_mode(gates,16,true,1,now)==0,"entering race: a released tick");
        check(h.step_mode(gates,16,true,1,now+=tick)==1,"race: first up pulse");
        for(int i=0;i<12;++i)check(h.step_mode(gates,16,true,1,now+=tick)==0,"slow gear response: no frame-based retry");
        check(h.step_mode(gates,16,true,2,now+=tick)==1,"consumed gear advanced: next up pulse");
        check(h.step_mode(gates,18,true,2,now+=tick)==0,"pause transition: released");
        for(int i=0;i<240;++i)check(h.step_mode(gates,18,true,2,now+=tick)==0,"pause: no pulse or spent retry");
        check(h.state()!=HPattern::State::Stopped,"long pause retains a fresh retry budget");
        check(h.step_mode(gates,16,true,2,now+=tick)==0,"resume: released first");
        check(h.step_mode(gates,16,true,2,now+=tick)==1,"resume: fresh pulse toward held gate");
        check(h.step_mode(gates,16,true,3,now+=tick)==0,"target reached: release");
        check(h.step_mode(gates,16,true,3,now+=tick)==0,"target reached: no shift");
        check(h.step_mode(gates,3,false,0,now+=tick)==0,"menu transition: released");
        check(h.step_mode(gates,3,false,0,now+=tick)==0,"menu: no shift");
        check(h.step_mode(gates,16,true,1,now+=tick)==0,"new race: released first");
        check(h.step_mode(gates,16,true,1,now+=tick)==1,"new race: no prior target or retry state");

        HPattern stalled;now=0;int pulses=0;bool previous=false;
        for(int i=0;i<240;++i){
            const bool pulse=stalled.step_mode(gates,16,true,1,now+=tick)!=0;
            check(!pulse||!previous,"a pulse always has a released tick");
            previous=pulse;if(pulse)++pulses;
        }
        check(pulses==3&&stalled.state()==HPattern::State::Stopped,"unresponsive car: one pulse and two timed retries");
    }
    HPattern h;h.step_mode(gates,16,true,1,0);
    gates[0]=true;
    check(h.step_mode(gates,16,true,2,16000)==0&&h.state()==HPattern::State::Ambiguous,"two gates never select the higher one");
    gates[0]=false;
    for(std::uint32_t bad:{0u,7u,0xffffffffu})
        check(h.step_mode(gates,16,true,bad,32000)==0,"invalid consumed gear: no shift");
    check(h.step_mode(gates,16,false,1,48000)==0,"missing player car: no shift");
    check(h.step_mode(gates,16,true,1,64000)==1,"valid unambiguous gate recovers");
}

// P25: pre-consumer exclusion of the WGI pad path.
void test_pad_exclusion(){
    const std::vector<PadIdentity> pads{{0x346e,0x0006,"MOZA R12 Base"},{0x045e,0x02ff,"Xbox Controller"}};
    check(select_pad(pads,{WP})==1,"the owned wheel is dropped; the Xbox pad is read");
    check(select_pad({pads[0]},{WP})==-1,"only the owned device: no pad");
    check(select_pad(pads,{})==0,"nothing owned: the first pad");
    // The same identity through both paths reaches the game once: the WGI copy is dropped before the pad adapter.
    Rig r(false,profile({Throttle}));
    r.view.mode=16;
    r.dev(r.w).state.axes[2]=0;r.tick();r.dev(r.w).state.axes[2]=65535;
    ps5::PadSample wheel_as_pad;wheel_as_pad.connected=true;wheel_as_pad.r2=255;   // what the wheel would look like as a pad
    const int pick=select_pad({pads[0]},{WP});
    r.sample=pick<0?ps5::PadSample{}:wheel_as_pad;
    auto out=r.tick();
    check(out.accelerator==255&&out.pc_device.axes_94[0x0d]==255,"one consumer: the provider's throttle");
    check(!out.raw_buttons_held&&r.adapter->signals().native_suppressed==0,"nothing arrived through the pad path");
}

// P26: native-only start and coexistence.
void test_native_only(){
    {Rig r(false,"");   // no profile at all
     r.view.mode=16;r.sample.r2=180;r.sample.left_x=0;
     auto out=r.tick();
     check(out.pc_input_layer&&out.accelerator==180&&out.steering==-128,"no profile: the pad drives the game exactly as before");}
    {Rig r(false,profile({Throttle}));
     r.view.mode=16;r.sample.buttons=ps5::R1;
     r.dev(r.w).state.axes[2]=0;r.tick();r.dev(r.w).state.axes[2]=32768;
     auto out=r.tick();
     check((out.pc_device.buttons_04&0x2u)&&out.pc_device.axes_94[0x0d]==128,"ordinary play: the pad's shift and the wheel's throttle together");}
}
}

// Ordinary play on the shim backend (romi 25050): the desktop keyboard keeps its own path, as with the native wheel path
// (wheel_input.cpp ea34c8b / 45658a0). A padless rig: the pad sample is disconnected, so only the adapter sets the layer.
void test_keyboard_ordinary(){
    Rig r(false,profile({Steer,Throttle,Brake,wb("confirm",0),wb("navLeft",7),wb("shiftUp",13)}));
    r.sample.connected=false;
    auto& d=r.dev(r.w);
    d.state.axes[0]=32768;   // the wheel at rest (centre)
    Keyboard none;Keyboard key;key.any=true;
    r.view.mode=16;
    auto out=r.tick(16000,&none);
    check(out.pc_input_layer&&out.pc_overlay&&!out.pc_overlay_yield,"race, no key: the PC layer runs and the wheel's record drives");
    out=r.tick(16000,&key);
    check(out.pc_input_layer&&out.pc_overlay&&out.pc_overlay_yield,"a key with the pedals and buttons at rest hands the race to the keyboard");
    out=r.tick(16000,&key);
    check(out.pc_overlay_yield,"the keyboard keeps the race while the wheel stays put");
    d.state.axes[2]=13107;   // throttle 0.2: a pedal pressed
    out=r.tick(16000,&key);
    check(!out.pc_overlay_yield&&out.pc_device.axes_94[0x0d]==51,"a pedal takes the race back for the wheel");
    d.state.axes[2]=0;r.tick(16000,&none);
    out=r.tick(16000,&key);
    check(out.pc_overlay_yield,"a key takes it again once the pedal is at rest");
    d.state.axes[0]=32768+800;   // about 2.4% of the half range: less than 3%
    out=r.tick(16000,&none);
    check(out.pc_overlay_yield,"a small wheel movement leaves the race with the keyboard");
    d.state.axes[0]=32768+1300;  // about 4%: more than 3% from the anchor
    out=r.tick(16000,&none);
    check(!out.pc_overlay_yield,"turning the wheel 3% takes the race back");
    d.state.axes[0]=32768;r.tick(16000,&none);
    out=r.tick(16000,&key);check(out.pc_overlay_yield,"keyboard again");
    d.state.buttons[13]=0x80;out=r.tick(16000,&key);
    check(!out.pc_overlay_yield,"a held wheel button takes the race back");
    d.state.buttons[13]=0;r.tick(16000,&none);
    // In the race the menu keys are the PC layer's own, never menu edges.
    Keyboard enter;enter.any=enter.enter=true;
    out=r.tick(16000,&enter);
    check(!out.menu_confirm&&!(out.frontend.feature_mask&4u),"Enter in the race is not a menu confirm");
    r.tick(16000,&none);
    // Menus: the original default menu keys count as confirm, back and navigation, with their own edges.
    r.view.mode=3;
    out=r.tick(16000,&enter);
    check(out.menu_confirm&&(out.frontend.feature_mask&4u)&&(out.pc_device.buttons_04&0x2u),"Enter in a menu: confirm edge, feature 4, held bit 0x2");
    out=r.tick(16000,&enter);
    check(!out.menu_confirm&&(out.pc_device.buttons_04&0x2u),"held Enter: the bit stays, no second edge");
    Keyboard esc;esc.any=esc.esc=true;out=r.tick(16000,&esc);
    check(out.menu_cancel&&(out.frontend.feature_mask&8u),"Esc: cancel edge, feature 8");
    Keyboard up;up.any=up.up=true;out=r.tick(16000,&up);check(out.menu_up&&(out.frontend.feature_mask&0x400u),"Up: menu up");
    Keyboard dn;dn.any=dn.down=true;out=r.tick(16000,&dn);check(out.menu_down&&(out.frontend.feature_mask&0x800u),"Down: menu down");
    Keyboard lf;lf.any=lf.left=true;out=r.tick(16000,&lf);check(out.menu_left&&(out.frontend.feature_mask&0x1000u)&&(out.pc_device.buttons_04&0x100u),"Left: menu left, D-pad bit");
    Keyboard rt;rt.any=rt.right=true;out=r.tick(16000,&rt);check(out.menu_right&&(out.frontend.feature_mask&0x2000u)&&(out.pc_device.buttons_04&0x80u),"Right: menu right, D-pad bit");
    r.tick(16000,&none);
    // The wheel's own menu actions still work with the keyboard present.
    d.state.buttons[7]=0x80;out=r.tick(16000,&none);
    check(out.menu_left&&(out.pc_device.buttons_04&0x100u),"the wheel's navLeft still navigates");
    d.state.buttons[7]=0;
    // A profile without wheel axes: no overlay, the race reads the PC layer (the keyboard) with the layer on.
    Rig b(false,profile({wb("confirm",0)}));b.sample.connected=false;b.view.mode=16;
    out=b.tick(16000,&key);
    check(out.pc_input_layer&&!out.pc_overlay,"no wheel axes: the PC layer drives the race");
}

// An H-pattern lever at rest in a gate is not wheel use (zero 25230): the keyboard can take the race; a gear change
// (the gate's edge, then the gearbox's pulses toward it) returns the race to the wheel.
void test_keyboard_hpattern_rest(){
    Rig r(false,profile({Steer,Throttle,Brake,sb("gear3",2),sb("gear4",3)},"H-pattern"),true);
    r.sample.connected=false;
    auto& d=r.dev(r.w);auto& x=r.dev(r.s);
    d.state.axes[0]=32768;
    r.view.mode=16;r.view.car=true;r.view.gear=3;
    Keyboard none;Keyboard key;key.any=true;
    x.state.buttons[2]=0x80;              // the lever in gate 3, the car already in third: no pulse
    r.tick(16000,&none);r.tick(16000,&none);
    auto out=r.tick(16000,&key);
    check(out.pc_overlay_yield,"a lever resting in gate 3 does not keep the race from the keyboard");
    out=r.tick(16000,&key);
    check(out.pc_overlay_yield,"and the keyboard keeps it while the lever rests");
    x.state.buttons[2]=0;x.state.buttons[3]=0x80;   // into gate 4: a gear change
    out=r.tick(16000,&key);
    check(!out.pc_overlay_yield,"a gear change returns the race to the wheel");
}

// The shim backend without a host (DirectInput or the host refused, romi 25245): shim_input applies an empty frame
// (every action unbound) with the keyboard, so a padless rig keeps the keyboard in the race and the menus.
void test_keyboard_without_host(){
    Adapter a("Sequential");const Frame none{};GameView v;v.mode=16;
    Keyboard key;key.any=true;
    NativeInputState out{};a.apply(none,v,out,&key);
    check(out.pc_input_layer&&!out.pc_overlay,"no host, race: the PC layer runs and its record (the keyboard) drives");
    v.mode=3;Keyboard enter;enter.any=enter.enter=true;
    out=NativeInputState{};a.apply(none,v,out,&enter);
    check(out.pc_input_layer&&out.menu_confirm&&(out.frontend.feature_mask&4u),"no host, menu: Enter confirms");
    out=NativeInputState{};a.apply(none,v,out,&enter);
    check(!out.menu_confirm,"no host: one edge per press");
}

// [Wheel] SteeringSensitivity / SteeringDeadZone in ordinary play, as wheel_input.cpp applies them: the same frame
// through a default adapter and a shaped one. The defaults leave the steering word bit-exact.
void test_steering_shaping(){
    Rig r(false,profile({Steer,Throttle,Brake}));
    r.view.mode=16;
    auto& d=r.dev(r.w);
    Adapter plain("Sequential",{},Steering{1.f,0.f});
    Adapter shaped("Sequential",{},Steering{1.5f,0.1f});
    auto word=[&](Adapter& a){NativeInputState o{};a.apply(*r.frame,r.view,o);return std::int32_t(o.pc_device.axes_94[0x0e]);};
    auto expect=[](std::int32_t w0){double n=w0/32767.0;n=std::fabs(n)<0.1?0.0:(n>0?1.0:-1.0)*(std::fabs(n)-0.1)/0.9;
        return std::int32_t(std::lround(std::clamp(n*1.5,-1.0,1.0)*32767.0));};
    d.state.axes[0]=32768;for(int i=0;i<3;++i)r.tick();   // open and seen at rest: live
    for(const int raw:{49152,34406,3277,32768,60000}){
        d.state.axes[0]=raw;
        const auto out=r.tick();
        const std::int32_t w0=out.pc_device.axes_94[0x0e];
        if(raw==49152)check(w0>16000,"the steer is live (half right)");
        check(word(plain)==w0,"Steering{1,0} is bit-exact with the default adapter (raw "+std::to_string(raw)+")");
        check(std::abs(word(shaped)-expect(w0))<=2,"dead zone 0.1, sensitivity 1.5 as wheel_input (raw "+std::to_string(raw)+")");
    }
    d.state.axes[0]=34406;r.tick();check(word(shaped)==0,"inside the dead zone: centred");
    d.state.axes[0]=3277;r.tick();check(word(shaped)==-32767,"past full lock after the sensitivity: clamped");
}

// A latched frame never takes the keyboard, even if the glue passes one: the test path is unchanged.
void test_keyboard_latched(){
    Rig r(true,profile({Steer,Throttle,wb("confirm",0)}));
    r.view.mode=16;r.admit();
    Keyboard key;key.any=key.enter=true;
    auto out=r.tick(16000,&key);
    check(!out.pc_input_layer&&!out.pc_overlay&&!out.pc_overlay_yield,"latched: the race reads the neutral record, no keyboard overlay");
    r.view.mode=3;out=r.tick(16000,&key);
    check(!out.menu_confirm,"latched: no keyboard menu edges");
}

// One action table (outrun_actions.hpp) for the shim capability and the native controls report (controls_store.cpp
// forwards to outrun_unavailable_reason): every contract action is delivered or has its reason, and the capability
// carries exactly the table's reasons.
void test_action_table(){
    const auto c=outrun_capability();
    int delivered=0;
    for(int i=0;i<ActionCount;++i){
        const std::string name=action_name(i);
        const char* why=outrun_unavailable_reason(name);
        check(why==nullptr||std::string(why)!="unknown action","the table covers "+name);
        const char* cap=c.unsupported[std::size_t(i)];
        check((why==nullptr)==(cap==nullptr)&&(why==nullptr||std::string(why)==cap),"the capability's reason for "+name);
        delivered+=why==nullptr;
    }
    check(delivered==20&&std::size(DeliveredActions)==20&&std::size(UnavailableActions)==11,"20 delivered (horn: AllowHorn), 11 unavailable");
    check(std::string(outrun_unavailable_reason("reverse"))=="OutRun 2006 has no reverse gear","reverse has one reason");
    check(std::string(outrun_unavailable_reason("gear9"))=="unknown action","an action outside the contract is unknown");
}

// A gate (or any shifter button) is read once it was seen released: press and release it outside the race, where it
// sends nothing, then come back to the mode the test was in (its own scene change follows on the next tick).
void rearm(Rig& r,std::size_t device,std::initializer_list<int> buttons){
    const auto mode=r.view.mode;r.view.mode=3;
    for(int b:buttons)r.dev(device).state.buttons[b]=0x80;
    r.tick();
    for(int b:buttons)r.dev(device).state.buttons[b]=0;
    r.tick();r.view.mode=mode;
}
bool up_bit(const NativeInputState& o){return (o.pc_device.buttons_04&0x2u)!=0;}
bool down_bit(const NativeInputState& o){return (o.pc_device.buttons_04&0x8u)!=0;}

// B (sana 44117; C1h run1): the race's pause (mode 18) takes no shift, so the gearbox is not stepped there: no pulse, no
// retry, no deadline. The pause's scene changes reset it; on the resume the gate held then starts from the consumed gear.
void test_hpattern_pause(){
    const auto prof=profile({sb("gear1",0),sb("gear2",1),sb("gear3",2),sb("gear4",3)},"H-pattern");
    {   // A gate's onset during the pause: nothing until the resume, then the first step toward it.
        Rig r(false,prof,true);r.view.car=true;r.view.gear=1;r.view.mode=16;
        rearm(r,r.s,{2});r.tick();
        r.view.mode=18;r.tick();
        r.dev(r.s).state.buttons[2]=0x80;   // gate 3
        for(int i=0;i<40;++i){const auto o=r.tick();check(!up_bit(o)&&!down_bit(o)&&!o.shift_up,"a gate's onset in the pause: no pulse");}
        r.view.mode=16;check(!up_bit(r.tick()),"the resume's own tick: released");
        const auto o=r.tick();
        check(up_bit(o)&&o.shift_up,"then the first step toward gate 3, from gear 1");
    }
    {   // A gate held across the pause: the step it was waiting on is not retried there, and it is not Stopped after.
        Rig r(false,prof,true);r.view.car=true;r.view.gear=1;r.view.mode=16;
        rearm(r,r.s,{3});r.tick();
        r.dev(r.s).state.buttons[3]=0x80;   // gate 4
        check(up_bit(r.tick()),"gate 4 from gear 1: the first pulse");
        r.tick();r.view.gear=2;             // the game took one step
        r.view.mode=18;
        for(int i=0;i<100;++i){const auto o=r.tick();check(!up_bit(o)&&!down_bit(o),"held across the pause: no pulse");}   // 1.6 s
        check(r.adapter->gearbox().state()!=HPattern::State::Stopped,"1.6 s paused: no retry spent, not Stopped");
        r.view.mode=16;r.tick();
        check(up_bit(r.tick()),"the resume continues from gear 2 toward gate 4");
    }
    {   // The gate changed during the pause: the resume follows the gate held then.
        Rig r(false,prof,true);r.view.car=true;r.view.gear=3;r.view.mode=16;
        rearm(r,r.s,{0,2});r.tick();
        r.dev(r.s).state.buttons[2]=0x80;r.tick();   // gate 3 at gear 3: at the target
        r.view.mode=18;r.tick();
        r.dev(r.s).state.buttons[2]=0;r.tick();r.dev(r.s).state.buttons[0]=0x80;   // through neutral to gate 1
        for(int i=0;i<20;++i){const auto o=r.tick();check(!up_bit(o)&&!down_bit(o),"the gate changed in the pause: no pulse");}
        r.view.mode=16;r.tick();
        const auto o=r.tick();
        check(down_bit(o)&&o.shift_down,"the resume steps down toward gate 1, the gate held now");
    }
}

// A (sana 44117; C1h run1): the ordinary shim leaves a pad of a product its profile binds out of the game's own pad path,
// so a button reaches the game only as the profile maps it. The production seam: owned_products and select_pad (what
// shim_input.cpp's gamepad_choice runs before read_pad reads a pad), the pad adapter (src/input/dual_sense.cpp) on the
// sample read_pad leaves, then the adapter. The owned pad is the profile's device seen as a WGI Gamepad, holding the
// C1h gate buttons: Options (gear5), R1 (gear2), Circle (gear4), L1 (gear1).
void test_owned_pad_path(){
    const auto prof=profile({Steer,Throttle,wb("gear1",4),wb("gear2",5),wb("gear3",3),wb("gear4",2),wb("gear5",9)},"H-pattern");
    const auto owned=owned_products(read_profile(prof).section);
    check(owned==std::vector<std::string>{WP},"the profile's one device product");
    const PadIdentity mine{0x346e,0x0006,"the profile's device as a WGI Gamepad"},other{0x045e,0x02ff,"Xbox Controller"};
    const PadIdentity unknown{0,0,"identity unreadable",false};
    check(select_pad({mine},owned)==-1,"only the owned pad: none is read");
    check(select_pad({mine,other},owned)==1&&select_pad({other,mine},owned)==0,"an unrelated pad is still read, wherever it is listed");
    check(select_pad({mine,mine},owned)==-1,"by product: a second pad of the owned product is left out too");
    check(select_pad({unknown,other},owned)==1&&select_pad({unknown},owned)==-1,"an unidentified pad is never read while a product is owned");
    check(select_pad({unknown},{})==0&&select_pad({mine},{})==0,"nothing owned: the first pad, as before (the Xbox console)");
    // One race tick with a gate held on the device (shim) and the pad path's sample: the owned pad read, or left out.
    auto race=[&](int device_button,std::uint32_t pad_buttons,std::uint32_t gear,int pick)->NativeInputState{
        Rig r(false,prof);r.view.car=true;r.view.gear=gear;r.view.mode=16;
        rearm(r,r.w,{device_button});r.tick();
        r.dev(r.w).state.buttons[device_button]=0x80;
        ps5::PadSample s;s.connected=true;s.buttons=pad_buttons;
        r.sample=pick<0?ps5::PadSample{}:s;   // read_pad: the chosen pad's reading, or nothing
        return r.tick();
    };
    const int pick=select_pad({mine},owned);
    check((race(9,ps5::Options,5,0).pc_device.buttons_04&0x1u)!=0,"read twice (C1h run1): the pad path's Options holds the start bit (the pause)");
    check((race(9,ps5::Options,5,pick).pc_device.buttons_04&0x1u)==0,"left out: no start bit from gate 5");
    const auto twice=race(5,ps5::R1,4,0),once=race(5,ps5::R1,4,pick);
    check(up_bit(twice)&&down_bit(twice),"read twice: gate 2 from gear 4 carries the pad path's held shift-up bit beside the down pulse");
    check(!up_bit(once)&&down_bit(once)&&once.shift_down,"left out: gate 2 from gear 4 is the gearbox's down pulse alone");
    check((race(2,ps5::Circle,4,0).pc_device.buttons_04&0x4u)!=0&&(race(2,ps5::Circle,4,pick).pc_device.buttons_04&0x4u)==0,"gate 4 (Circle): no back bit once left out");
    const auto l1=race(4,ps5::L1,1,pick);
    check(!down_bit(l1)&&!up_bit(l1),"gate 1 at gear 1 (L1): no held shift-down bit once left out");
    // An unrelated pad keeps its native path: its Circle is the game's back.
    check((race(9,ps5::Circle,5,select_pad({mine,other},owned)).pc_device.buttons_04&0x4u)!=0,"an unrelated pad's buttons still reach the game");
}

int main(){
    try{player_car_guard();test_sink();test_pedals_without_steering();test_latched_suppression();test_release();test_hpattern();test_hpattern_modes();test_pad_exclusion();test_native_only();
        test_keyboard_ordinary();test_keyboard_hpattern_rest();test_keyboard_without_host();test_keyboard_latched();test_steering_shaping();
        test_action_table();test_hpattern_pause();test_owned_pad_path();test_horn();}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    std::cout<<"outrun adapter: "<<checks<<" checks passed\n";
    return 0;
}
