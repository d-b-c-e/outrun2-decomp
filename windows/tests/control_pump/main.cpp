#include "control_pump.hpp"
#include "shim_fake_provider.hpp"
#include <condition_variable>
#include <functional>
#include <iostream>
using namespace dbce::shim;
namespace c=dbce::shim::control;
namespace {
int checks=0;
void check(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
const std::string W="{11111111-1111-1111-1111-111111111111}",P="{22222222-2222-2222-2222-222222222222}",N="ControlPumpFixture";
std::string batch(int n,const std::string& command=""){
    return "nonce="+N+"\nbatch="+std::to_string(n)+"\n"+(command.empty()?"inject raw button 0 dev="+W+" value=1 ms=50":command)+"\n";
}
struct Io final:c::Io {
    c::Read session{c::ReadState::Bytes,"nonce="+N+"\nexpires=2000\n",{}},input;
    int session_reads{},batch_reads{},appends{},finishes{};bool write_ok=true,throws=false;
    std::uint64_t now=1000,mono=0;
    std::vector<c::Journal> journal;
    std::function<void()> on_session,on_batch,on_append;
    c::Read read_session()override{++session_reads;if(on_session)on_session();return session;}
    c::Read read_batch()override{++batch_reads;if(on_batch)on_batch();if(throws)throw std::runtime_error("read fault");return input;}
    std::uint64_t unix_seconds()override{return now;}
    std::uint64_t monotonic_ms()override{return mono;}
    bool append(const c::Journal& j)override{++appends;if(on_append)on_append();if(write_ok)journal.push_back(j);return write_ok;}
    bool finish()override{++finishes;return true;}
};
struct Rig {
    fake::Provider provider;Core core;Io io;c::Pump pump;
    explicit Rig(bool latched=true):core(provider,config(latched)),pump(core,io,{latched,true,true,N}){
        fake::Device d;d.listed={W,P,"fake wheel"};d.caps=fake::wheel_caps();d.state.buttons[0]=0x80;provider.add(d);
        core.set_profile(read_profile("[Controls]\nSchema=1\nconfirm=button 0 dev="+W+" prod="+P+"\n"));
    }
    static CoreConfig config(bool latched){CoreConfig cfg;cfg.latched=latched;return cfg;}
    const Frame& tick(std::int64_t us=1000){provider.advance(us);core.service_devices();return core.tick();}
    void admit(){pump.pass();check(io.batch_reads==0,"no batch read before admission");check(tick().run==Run::Waiting,"fresh device admitted");}
    float value(){return core.tick().action[std::size_t(action_index("confirm"))].value;}
    void start(){admit();io.input={c::ReadState::Bytes,batch(1),{}};pump.pass();check(tick().run==Run::Running&&value()==1,"batch reaches real resolver");}
};
void basics(){
    {Rig r(false);r.pump.pass();r.tick();r.pump.pass();check(r.io.session_reads==0&&r.io.batch_reads==0,"ordinary play never reads injection files");}
    {Rig r;r.admit();r.io.session={c::ReadState::Error,{},"replacement must be ignored"};r.pump.pass();check(r.io.session_reads==1&&r.core.control_state().run==Run::Waiting,"session read exactly once");
     check(r.io.batch_reads==1,"missing first batch waits");}
    {Rig r;r.start();r.pump.pass();r.pump.pass();check(r.pump.batches_offered()==1&&r.pump.identical_skipped()==2,"unchanged bytes not offered repeatedly");
     r.tick(60000);check(r.value()==0,"identical file cannot extend injection TTL");
     bool hash=false;for(const auto& j:r.io.journal)for(const auto& e:j.events)if(e.kind==EventKind::BatchAccepted&&e.detail.find(hex(sha256(batch(1))))!=std::string::npos)hash=true;
     check(hash,"journal retains full accepted batch hash");
     r.io.input={c::ReadState::Bytes,batch(2,"end"),{}};r.pump.pass();check(r.tick().run==Run::Completed,"end goes through Core ordering");
     int reads=r.io.batch_reads;r.io.input={c::ReadState::Bytes,batch(3),{}};r.pump.pass();check(r.io.batch_reads==reads&&r.value()==0,"terminal never reads later command or rearms");}
    {Rig r;r.start();r.io.input.bytes=batch(1,"inject raw button 0 dev="+W+" value=0 ms=50");r.pump.pass();check(r.tick().run==Run::Failed&&r.value()==0,"same number changed bytes fails neutral");}
}
void failures(){
    for(int mode=0;mode<7;++mode){Rig r;
        if(mode==0)r.io.session={c::ReadState::Missing,{},{}};
        if(mode==1)r.io.session={c::ReadState::Error,{},"denied"};
        if(mode==2)r.io.session.bytes=std::string(513,'x');
        if(mode==3)r.io.session.bytes="nonce=WrongNonce\nexpires=2000\n";
        if(mode==4)r.io.session.bytes="nonce="+N+"\nexpires=999\n";
        if(mode==5)r.io.on_session=[&]{r.io.now=2001;}; // delayed read: current wall time, never pre-read time
        if(mode==6)r.io.session.bytes+="unknown=1\n";
        r.pump.pass();check(r.tick().run==Run::Failed&&r.value()==0,"bad or delayed session fails neutral");check(r.io.batch_reads==0,"refused session cannot read commands");
    }
    for(int mode=0;mode<4;++mode){Rig r;r.start();
        if(mode==0)r.io.input={c::ReadState::Error,{},"sharing error"};
        if(mode==1)r.io.input.bytes=std::string(MaxBatchBytes+1,'x');
        if(mode==2)r.io.throws=true;
        if(mode==3)r.io.write_ok=false;
        r.pump.pass();check(r.tick().run==Run::Failed&&r.value()==0,"read or journal failure releases held input");
        check(r.pump.state()==c::State::Failed&&!r.pump.reason().empty(),"failure remains reported");
        const auto reason=r.pump.reason();r.io.input={c::ReadState::Bytes,batch(2),{}};r.io.write_ok=true;r.io.throws=false;r.pump.pass();
        check(r.pump.reason()==reason&&r.value()==0,"failure cannot recover silently");
    }
}
void busy(){
    const c::Read busy{c::ReadState::Busy,{},"snapshot open busy (Win32 32)"};
    // Transient: a sharing conflict for a while, then the next batch reads and is offered.
    {Rig r;r.start();r.io.input=busy;
     for(int i=0;i<5;++i){r.io.mono+=100;r.pump.pass();}
     check(r.pump.state()==c::State::Polling&&r.tick().run==Run::Running,"a Busy read within the bound does not fail the session");
     check(r.pump.busy_passes()==5&&r.pump.batches_offered()==1,"Busy passes offer nothing");
     r.io.input={c::ReadState::Bytes,batch(2),{}};r.io.mono+=100;r.pump.pass();
     check(r.pump.batches_offered()==2&&r.tick().run==Run::Running&&r.value()==1,"the next batch after a transient Busy is offered and applies");}
    // Prolonged: Busy past the bound fails the session neutral, as an I/O failure.
    {Rig r;r.start();r.io.input=busy;
     r.pump.pass();r.io.mono+=c::Pump::BusyBoundMs;r.pump.pass();
     check(r.pump.state()==c::State::Polling,"exactly the bound is still within it");
     r.io.mono+=1;r.pump.pass();
     check(r.pump.state()==c::State::Failed&&r.pump.reason().find("busy for more than")!=std::string::npos,"Busy past the bound fails, with the reason");
     check(r.tick().run==Run::Failed&&r.value()==0,"the failed session is neutral");}
    // A held batch expires on its original deadline while Busy: Busy never renews it.
    {Rig r;r.start();check(r.value()==1,"the batch holds its button");r.io.input=busy;
     r.io.mono+=10;r.pump.pass();r.tick(60000);   // past the batch's ms=50 hold
     check(r.value()==0,"the held batch expired neutral on its own deadline during Busy");
     check(r.pump.state()==c::State::Polling,"and the session is still polling within the bound");}
    // The bound restarts only after a successful snapshot or a missing file.
    for(int mode=0;mode<2;++mode){Rig r;r.start();r.io.input=busy;
     r.pump.pass();r.io.mono+=1500;r.pump.pass();
     r.io.input=mode==0?c::Read{c::ReadState::Missing,{},{}}:c::Read{c::ReadState::Bytes,batch(1),{}};r.io.mono+=10;r.pump.pass();
     r.io.input=busy;r.pump.pass();r.io.mono+=1500;r.pump.pass();
     check(r.pump.state()==c::State::Polling,mode==0?"a missing file restarts the Busy bound":"a successful snapshot restarts the Busy bound");}
    // An Error stays fatal at once, even right after Busy passes (size, reparse and read errors are never retried).
    {Rig r;r.start();r.io.input=busy;r.pump.pass();r.io.input={c::ReadState::Error,{},"snapshot grew during read"};r.pump.pass();
     check(r.pump.state()==c::State::Failed&&r.tick().run==Run::Failed,"an Error after Busy still fails at once");}
}
void shutdown(){
    {Rig r;r.core.close_control();r.pump.pass();check(r.io.session_reads==0&&r.io.batch_reads==0,"preclosed gate never touches files");
     check(r.core.offer_batch(batch(1)).verdict==BatchVerdict::Refused,"Core independently refuses after close");
     r.core.request_admission({N,2000},1000,true,true);check(r.tick().run==Run::Failed,"late admission cannot reopen closed gate");}
    {Rig r;r.io.on_session=[&]{r.core.close_control();};r.pump.pass();check(r.pump.state()==c::State::Closed&&r.io.batch_reads==0,"session completed after close is discarded");check(r.tick().run==Run::Failed,"closed during session stays neutral");}
    {Rig r;r.start();r.io.input.bytes=batch(2);r.io.on_batch=[&]{r.core.close_control();};r.pump.pass();check(r.pump.batches_offered()==1,"batch completed after close is discarded");check(r.tick().run==Run::Failed&&r.value()==0,"close releases actual injected hold");}
    // Actual concurrent slow I/O: the input thread keeps ticking while the one
    // control worker is blocked; close then release its read. No late offer.
    {Rig r;std::mutex m;std::condition_variable cv;bool inside=false,release=false;
     r.io.on_session=[&]{std::unique_lock<std::mutex> l(m);inside=true;cv.notify_all();cv.wait(l,[&]{return release;});};
     std::thread worker([&]{r.pump.pass();});
     {std::unique_lock<std::mutex> l(m);cv.wait(l,[&]{return inside;});}
     for(int i=0;i<100;++i){check(r.tick().run==Run::Latched,"slow control I/O never holds Core lock");}
     r.core.close_control();
     {std::lock_guard<std::mutex> l(m);release=true;}cv.notify_all();worker.join();
     check(r.io.batch_reads==0&&r.pump.state()==c::State::Closed,"late blocked read cannot admit");}
}
void final_evidence(){
    for(int mode=0;mode<3;++mode){Rig r;r.start();
        if(mode==0)r.io.input={c::ReadState::Error,{},"read failed"};
        if(mode==1)r.io.input.bytes=std::string(MaxBatchBytes+1,'x');
        if(mode==2)r.io.write_ok=false;
        r.pump.pass();r.tick();const int before=r.io.appends;r.core.close_control();
        check(!r.pump.finish(),"fault remains failed after final drain");
        check(r.io.finishes==1,"sink finished after fault");
        check(r.io.appends==before+(mode==2?0:1),"broken journal never retried; healthy sink receives terminal drain");
        bool status=false,event=false;
        for(const auto& j:r.io.journal){for(const auto& s:j.status)if(s.of==StatusChange::Of::Run&&s.status=="failed")status=true;
            for(const auto& e:j.events)if(e.kind==EventKind::RunChanged&&e.detail.find("failed:")!=std::string::npos)event=true;}
        check(mode==2||status,"read failure terminal status journaled");check(mode==2||event,"read failure terminal event journaled");
        check(!r.pump.finish()&&r.io.finishes==1,"final outcome immutable and close exactly once");
    }
    auto text=std::string(510,'a')+"\xe2\x82\xac"+"tail";
    check(bounded_diagnostic(text)==std::string(510,'a')&&strict_utf8(bounded_diagnostic(text)),"diagnostic truncates at UTF8 codepoint");
    check(strict_utf8(bounded_diagnostic("bad\xff")),"invalid diagnostic replaced by valid text");
}
}
int main(){try{basics();failures();busy();shutdown();final_evidence();std::cout<<"PASS "<<checks<<" control-pump checks\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
