// Input shim slice 3 (windows/docs/INPUT-SHIM.md section 17, P28): the real DirectInput provider through the core, over
// fake COM devices. Covers identity before and after the open, objects by offset, read-back ranges, Poll and
// GetDeviceState classified separately, epochs with reacquisition and rescans, no close during a read, stale handles,
// the worker never blocking the tick, shutdown with a read in progress, and no force-feedback call at all.
#include "dinput_provider.hpp"
#include "shim_fake_com.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
using namespace dbce::shim;

namespace {
int checks=0;
void check(bool v,const std::string& what){++checks;if(!v)throw std::runtime_error(what);}
const GUID WI=fakecom::make_guid(0xd71b8350u,0x61b7),WPg=fakecom::make_guid(0x0006346eu,0x0000);
const GUID SI=fakecom::make_guid(0xd9b1d890u,0x6f3f),SPg=fakecom::make_guid(0x0002f3a1u,0x0000);
std::string W(){return guid_text(WI);}std::string WP(){return guid_text(WPg);}
std::string S(){return guid_text(SI);}std::string SP(){return guid_text(SPg);}
fakecom::DeviceScript wheel(){fakecom::DeviceScript s;s.instance=WI;s.product=WPg;s.name=L"MOZA R12 Base";return s;}
fakecom::DeviceScript shifter(){fakecom::DeviceScript s;s.instance=SI;s.product=SPg;s.name=L"DS-8X Shifter";s.axis[0]=s.axis[2]=s.axis[5]=false;s.buttons=11;s.povs=0;return s;}
std::string profile(const std::vector<std::string>& lines){
    std::string p="[Controls]\nSchema = 1\nProfile = Rig\nRevision = r1\nTransmission = Sequential\n";
    for(const auto& l:lines)p+=l+"\n";
    return p;
}
int A(const char* id){return action_index(id);}

void test_classify_and_guids(){
    check(DInputProvider::classify(DI_OK)==Call::Ok&&DInputProvider::classify(DI_NOEFFECT)==Call::Ok&&DInputProvider::classify(DI_POLLEDDEVICE)==Call::Ok,"success codes are a valid call");
    check(DInputProvider::classify(DIERR_INPUTLOST)==Call::AcquisitionLost&&DInputProvider::classify(DIERR_NOTACQUIRED)==Call::AcquisitionLost,"acquisition lost");
    check(DInputProvider::classify(DIERR_UNPLUGGED)==Call::Gone&&DInputProvider::classify(E_FAIL)==Call::Gone&&DInputProvider::classify(DIERR_INVALIDPARAM)==Call::Gone,"gone or unknown");
    GUID g{};
    check(guid_parse(W(),g)&&IsEqualGUID(g,WI),"guid round trip");
    std::string upper=W();for(auto& c:upper)c=char(std::toupper(static_cast<unsigned char>(c)));
    check(!guid_parse(upper,g)&&!guid_parse("{1234}",g),"only the canonical lower-case form parses");
}

void test_open_caps(){
    auto* in=new fakecom::Input;in->add(wheel());
    {DInputProvider p(in,nullptr);
     const auto list=p.enumerate();
     check(list.size()==1&&list[0].dev==W()&&list[0].prod==WP()&&list[0].name=="MOZA R12 Base","listing by instance and product text");
     const auto o=p.open(list[0]);
     check(o.ok&&o.opened.dev==W()&&o.opened.prod==WP(),"opened, identity read back from the handle");
     check(o.caps.axis[0]&&!o.caps.axis[1]&&o.caps.axis[2]&&o.caps.axis[5]&&!o.caps.axis[6],"axes by offset: X, Z, Rz only");
     check(o.caps.range_ok[0]&&o.caps.min[0]==0&&o.caps.max[0]==65535,"the requested range, read back");
     check(o.caps.button.count()==12&&o.caps.button.test(11)&&!o.caps.button.test(12)&&o.caps.pov[0]&&!o.caps.pov[1],"buttons and hats by offset");
     p.close(o.handle);}
    {auto* in2=new fakecom::Input;auto s=wheel();s.accept_range=false;s.native_min[0]=-32768;s.native_max[0]=32767;s.range_unreadable[5]=true;s.native_min[2]=s.native_max[2]=0;
     in2->add(s);
     DInputProvider p(in2,nullptr);
     const auto o=p.open(p.enumerate()[0]);
     check(o.ok&&o.caps.range_ok[0]&&o.caps.min[0]==-32768&&o.caps.max[0]==32767,"a refused request: the native range is what is read back");
     check(!o.caps.range_ok[5],"an unreadable range is not qualified");
     check(!o.caps.range_ok[2],"an empty range is not qualified");
     p.shutdown(0);
     check(in2->counters.force_calls==0,"no force-feedback call");
     delete in2;}
    check(in->counters.force_calls==0&&in->counters.releases==1&&in->counters.after_release==0,"released once, never used after");
    delete in;
    // An open that fails after CreateDevice releases the new handle: a refused data format, an unreadable identity.
    for(int which=0;which<2;++which){
        auto* f=new fakecom::Input;auto s=wheel();
        (which==0?s.fail_data_format:s.fail_device_info)=true;
        f->add(s);
        {DInputProvider p(f,nullptr);
         const auto o=p.open(p.enumerate()[0]);
         check(!o.ok&&f->made.size()==1&&f->made[0]->released(),which==0?"a refused data format: refused, handle released":"an unreadable identity: refused, handle released");
         p.shutdown(0);}
        delete f;
    }
}

struct Rig {
    fakecom::Input* in;
    std::unique_ptr<DInputProvider> p;
    std::unique_ptr<Core> core;
    explicit Rig(const std::string& prof,bool latched=false){
        in=new fakecom::Input;
        p=std::make_unique<DInputProvider>(in,nullptr);
        CoreConfig cfg;cfg.latched=latched;
        cfg.fresh_us=10'000'000;   // real QPC time: a slow test machine must not make samples stale
        core=std::make_unique<Core>(*p,cfg);
        core->set_profile(read_profile(prof));
    }
    ~Rig(){core.reset();p->shutdown(100);p.reset();delete in;}
    const Frame& step(){core->service_devices();return core->tick();}
};

// P1-P5 through the real provider: identity, objects, reads, epochs, reacquisition, rescans.
void test_core_end_to_end(){
    Rig r(profile({"steer = axis 0 dev="+W()+" prod="+WP()+" range=0..65535 rest=32768 travel=+1",
                   "throttle = axis 2 dev="+W()+" prod="+WP()+" range=0..65535 rest=0 travel=+1",
                   "brake = axis 1 dev="+W()+" prod="+WP()+" range=0..65535 rest=0 travel=+1",
                   "confirm = button 3 dev="+W()+" prod="+WP()}));
    auto& s=r.in->scripts[r.in->add(wheel())];
    s.state.lX=65535;s.state.rgbButtons[3]=0x80;
    s.reads.push_back({DI_NOEFFECT,DI_OK});   // a device that does not need polling
    auto f=r.step();
    check(f.action[std::size_t(A("steer"))].status==Status::Live&&f.action[std::size_t(A("steer"))].value>0.99f,"steer live through the real provider (Poll DI_NOEFFECT is a success)");
    check(f.action[std::size_t(A("brake"))].status==Status::ObjectMissing,"brake on Y: object missing");
    check(f.action[std::size_t(A("confirm"))].status==Status::Rearming,"held at the epoch start: masked");
    s.state.rgbButtons[3]=0;r.step();s.state.rgbButtons[3]=0x80;
    f=r.step();
    check(f.action[std::size_t(A("confirm"))].value==1,"pressed after release");
    // GetDeviceState loses acquisition and writes into the buffer anyway: nothing from it is used.
    s.fill_on_failure=true;s.state.lX=0;
    s.reads.push_back({DI_OK,DIERR_INPUTLOST});
    f=r.step();
    check(!f.samples[0].valid&&f.samples[0].state==Call::AcquisitionLost&&f.samples[0].physical.axes[0]==0,"a failed state read is not admitted");
    check(f.action[std::size_t(A("confirm"))].value==0&&((f.up>>A("confirm"))&1),"neutral with one up edge");
    s.fill_on_failure=false;s.state.lX=65535;
    f=r.step();   // the worker reacquires the same handle: a new epoch
    check(f.samples[0].epoch==2&&r.in->counters.creates==1,"reacquired: epoch 2 on the same device object");
    check(f.action[std::size_t(A("confirm"))].status==Status::Rearming,"rearm after reacquisition");
    // A failed Poll: GetDeviceState is not called; gone: closed, then reopened from a new listing.
    const int reads=r.in->counters.state_reads.load();
    s.reads.push_back({DIERR_UNPLUGGED,DI_OK});
    f=r.step();
    check(r.in->counters.state_reads==reads&&f.samples[0].state==Call::NotCalled,"a failed Poll skips GetDeviceState");
    f=r.step();
    check(r.in->made[0]->released()&&r.in->counters.creates==2&&f.samples[0].epoch==3,"gone: released, then a new device object as epoch 3");
    // Acquisition lost and reacquisition refused three times: closed and rescanned.
    s.reads.push_back({DI_OK,DIERR_NOTACQUIRED});
    s.acquires={DIERR_OTHERAPPHASPRIO,DIERR_OTHERAPPHASPRIO,DIERR_OTHERAPPHASPRIO};
    r.step();
    f=r.step();
    check(r.in->made[1]->released()&&r.in->counters.creates==3&&f.samples[0].epoch==4,"refused reacquisition: closed and reopened as epoch 4");
    check(r.p->misuse()==0&&r.in->counters.after_release==0&&r.in->counters.release_during_read==0,"no misuse");
}

// P1: an identity change between the listing and the open is refused, and the handle released.
void test_identity_change(){
    Rig r(profile({"confirm = button 0 dev="+W()+" prod="+WP()}));
    auto i=r.in->add(wheel());
    r.in->scripts[i].opened_product=SPg;
    const auto& f=r.step();
    check(f.action[std::size_t(A("confirm"))].status==Status::ProductMismatch,"identity changed during the open");
    check(r.in->made.size()==1&&r.in->made[0]->released(),"and the opened handle was released");
}

// Two devices through the real provider: each binding from its own device; one device failing leaves the other live.
void test_two_devices(){
    Rig r(profile({"confirm = button 0 dev="+W()+" prod="+WP(),"shiftUp = button 4 dev="+S()+" prod="+SP()}));
    const auto wi=r.in->add(wheel()),si=r.in->add(shifter());   // both first: adding may move the scripts
    auto& w=r.in->scripts[wi];
    auto& sh=r.in->scripts[si];
    r.step();
    w.state.rgbButtons[0]=0x80;sh.state.rgbButtons[4]=0x80;
    auto f=r.step();
    check(f.action[std::size_t(A("confirm"))].value==1&&f.action[std::size_t(A("shiftUp"))].value==1&&f.samples.size()==2,"wheel and shifter, each from its own device");
    sh.reads.push_back({DIERR_UNPLUGGED,DI_OK});
    f=r.step();
    check(f.action[std::size_t(A("shiftUp"))].status==Status::ReadFailed&&f.action[std::size_t(A("confirm"))].value==1,"the shifter failing leaves the wheel live (ordinary play)");
}

// No close during a read; stale handles.
void test_close_and_stale_handles(){
    auto* in=new fakecom::Input;
    auto i=in->add(wheel());in->scripts[i].state_delay_ms=200;
    DInputProvider p(in,nullptr);
    auto o=p.open(p.enumerate()[0]);
    // A failed state read copies nothing out, even when the device wrote into its buffer.
    {in->scripts[i].state_delay_ms=0;in->scripts[i].fill_on_failure=true;in->scripts[i].state.lX=12345;
     in->scripts[i].reads.push_back({DI_OK,DIERR_INPUTLOST});
     RawState st;st.axes[0]=-1;
     check(p.poll(o.handle)==Call::Ok&&p.state(o.handle,st)==Call::AcquisitionLost&&st.axes[0]==-1,"nothing copied from a failed read");
     check(p.poll(o.handle)==Call::Ok&&p.state(o.handle,st)==Call::Ok&&st.axes[0]==12345,"a whole read copies the state");
     in->scripts[i].fill_on_failure=false;in->scripts[i].state_delay_ms=200;}
    std::thread reader([&]{RawState st;(void)p.state(o.handle,st);});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    p.close(o.handle);   // refused: the read is in progress
    check(p.misuse()==1&&!in->made[0]->released(),"no close while the handle is being read");
    reader.join();
    p.close(o.handle);
    check(in->made[0]->released()&&in->counters.release_during_read==0,"closed after the read finished");
    in->scripts[i].state_delay_ms=0;
    const auto again=p.open(p.enumerate()[0]);
    check(again.ok&&again.handle!=o.handle&&(again.handle&0xffu)==(o.handle&0xffu),"the slot is reused with a new generation");
    const int before=in->counters.polls.load();
    check(p.poll(o.handle)==Call::Gone&&p.misuse()==2&&in->counters.polls==before,"a stale handle never reaches the new device");
    check(p.poll(again.handle)==Call::Ok,"the new handle works");
    p.shutdown(0);
    delete in;
}

// Shutdown: nothing is released under a reader; what a read still holds is released when that read leaves.
void test_shutdown(){
    auto* in=new fakecom::Input;
    auto a=in->add(wheel());in->add(shifter());
    in->scripts[a].state_delay_ms=300;
    {DInputProvider p(in,nullptr);
     const auto list=p.enumerate();
     auto o1=p.open(list[0]),o2=p.open(list[1]);
     std::thread reader([&]{RawState st;(void)p.state(o1.handle,st);});
     std::this_thread::sleep_for(std::chrono::milliseconds(50));
     const auto rep=p.shutdown(50);
     check(rep.released==1&&rep.held==1&&!rep.input_released&&rep.detail.find("still being read")!=std::string::npos,"shutdown keeps the device being read, reports it");
     check(in->made[1]->released()&&!in->made[0]->released()&&in->counters.release_during_read==0,"only the idle device was released");
     reader.join();
     (void)o2;}
    check(in->made[0]->released()&&in->refs.load()==0&&in->counters.release_during_read==0,"after the read left and the provider went: the held device and DirectInput released, never during the read");
    auto* in2=new fakecom::Input;in2->add(wheel());
    {DInputProvider p(in2,nullptr);(void)p.open(p.enumerate()[0]);
     const auto rep=p.shutdown(50);
     check(rep.released==1&&rep.held==0&&rep.input_released&&in2->refs.load()==0,"an idle shutdown releases every device and DirectInput");}
    check(in->counters.force_calls==0&&in2->counters.force_calls==0,"no force-feedback call");
    delete in2;delete in;
}

// Slice 3 review, finding 2: the provider destroyed (and its memory fenced) while a read is inside the driver call.
// The read keeps the shared state alive; its device is released when it leaves.
void test_destroyed_during_a_read(){
    auto* in=new fakecom::Input;
    auto a=in->add(wheel());in->scripts[a].state_delay_ms=300;
    SYSTEM_INFO si;GetSystemInfo(&si);
    void* mem=VirtualAlloc(nullptr,si.dwPageSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    check(mem&&sizeof(DInputProvider)<=si.dwPageSize,"a page for the provider");
    auto* p=new(mem) DInputProvider(in,nullptr);
    const auto o=p->open(p->enumerate()[0]);
    std::atomic<int> result{-1};
    std::thread reader([&]{RawState st;result=int(p->state(o.handle,st));});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    (void)p->shutdown(10);
    p->~DInputProvider();
    DWORD old=0;
    check(VirtualProtect(mem,si.dwPageSize,PAGE_NOACCESS,&old)!=0,"the provider's page is fenced");
    reader.join();   // resumes after the driver call: touches only the shared state
    check(result==int(Call::Ok),"the read finished normally after the provider was gone");
    check(in->made[0]->released()&&in->refs.load()==0&&in->counters.release_during_read==0,"its device and DirectInput were released as it left");
    VirtualFree(mem,0,MEM_RELEASE);
    delete in;
}

// Slice 3 review, finding 3: after shutdown nothing new is admitted, even once a timed-out read has left.
void test_stop_is_terminal(){
    auto* in=new fakecom::Input;
    auto a=in->add(wheel());in->add(shifter());
    in->scripts[a].state_delay_ms=150;
    {DInputProvider p(in,nullptr);
     const auto list=p.enumerate();
     const auto o=p.open(list[0]);
     std::thread reader([&]{RawState st;(void)p.state(o.handle,st);});
     std::this_thread::sleep_for(std::chrono::milliseconds(30));
     (void)p.shutdown(0);
     reader.join();
     const auto again=p.open(list[1]);
     check(!again.ok,"no open after shutdown");
     check(p.poll(o.handle)==Call::Gone&&p.enumerate().empty(),"no read and no listing after shutdown");
     check(p.refused_after_stop()>=3,"refusals counted ("+std::to_string(p.refused_after_stop())+")");}
    delete in;
}

// Finding 3: shutdown racing an open that is inside CreateDevice: DirectInput is kept for it; the open sees the stop,
// releases its new device and fails.
void test_shutdown_races_an_open(){
    auto* in=new fakecom::Input;in->add(wheel());
    in->create_delay_ms=200;
    {DInputProvider p(in,nullptr);
     const auto list=p.enumerate();
     OpenResult r;
     std::thread opener([&]{r=p.open(list[0]);});
     std::this_thread::sleep_for(std::chrono::milliseconds(50));
     const auto rep=p.shutdown(20);
     check(!rep.input_released&&rep.detail.find("last call leaves")!=std::string::npos,"DirectInput kept while an open is in flight");
     opener.join();
     check(!r.ok&&in->made.size()==1&&in->made[0]->released(),"the open saw the stop and released its device");}
    check(in->refs.load()==0,"DirectInput released when the provider went");
    delete in;
}

// Finding 1: a stale handle paused between reading the slot and the compare-exchange, while the slot is closed and
// reopened for another device, is never admitted to the new device.
namespace aba {
std::atomic<bool> armed{false},paused{false},resume{false};
void hook(void*){
    bool expected=true;
    if(armed.compare_exchange_strong(expected,false)){paused=true;while(!resume.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
}
void test_stale_handle_admission_race(){
    auto* in=new fakecom::Input;in->add(wheel());in->add(shifter());
    {DInputProvider p(in,nullptr);
     const auto list=p.enumerate();
     const auto old=p.open(list[0]);
     DInputProvider::set_admission_hook_for_tests(aba::hook,nullptr);
     aba::armed=true;aba::paused=false;aba::resume=false;
     std::atomic<int> result{-1};
     std::thread reader([&]{result=int(p.poll(old.handle));});
     while(!aba::paused.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
     p.close(old.handle);                                   // no reader admitted yet: the close succeeds
     const auto fresh=p.open(list[1]);                      // the same slot, the next generation
     check(fresh.ok&&(fresh.handle&0xffu)==(old.handle&0xffu)&&fresh.handle!=old.handle,"the slot reused for another device");
     const int polls=in->counters.polls.load();
     aba::resume=true;
     reader.join();
     DInputProvider::set_admission_hook_for_tests(nullptr,nullptr);
     check(result==int(Call::Gone)&&in->counters.polls==polls,"the paused stale handle was refused, the new device untouched");
     check(p.poll(fresh.handle)==Call::Ok,"the new handle works");}
    delete in;
}

// Finding 4: E_PENDING keeps the device open and is retried, within a bound.
void test_pending_reads(){
    {// The first read of every new device object is pending: one device object, not one per tick.
     Rig r(profile({"steer = axis 0 dev="+W()+" prod="+WP()+" range=0..65535 rest=32768 travel=+1"}));
     auto& s=r.in->scripts[r.in->add(wheel())];
     s.pending_first_read=true;s.state.lX=65535;
     int valid=0;
     for(int i=0;i<10;++i){const auto& f=r.step();if(!f.samples.empty()&&f.samples[0].valid)++valid;}
     check(r.in->counters.creates==1&&r.in->counters.releases==0&&valid==9,"pending: one device object, retried ("+std::to_string(valid)+" valid of 10)");}
    {// A held button across a pending frame never returns as a new press.
     Rig r(profile({"confirm = button 3 dev="+W()+" prod="+WP()}));
     auto& s=r.in->scripts[r.in->add(wheel())];
     r.step();s.state.rgbButtons[3]=0x80;
     auto f=r.step();
     check(f.action[std::size_t(A("confirm"))].value==1,"pressed");
     s.reads.push_back({DI_OK,E_PENDING});
     f=r.step();
     check(f.action[std::size_t(A("confirm"))].status==Status::ReadPending&&f.action[std::size_t(A("confirm"))].value==0&&f.samples[0].state==Call::Pending,"a pending frame: neutral, read-pending");
     f=r.step();
     check(f.action[std::size_t(A("confirm"))].status==Status::Rearming&&f.down==0,"the held button is masked: no new press");
     s.state.rgbButtons[3]=0;r.step();s.state.rgbButtons[3]=0x80;
     f=r.step();
     check(((f.down>>A("confirm"))&1)&&r.in->counters.creates==1,"a real press after release, same device object");}
    {// Pending for longer than the limit ends the epoch (reacquisition of the same handle).
     auto* in=new fakecom::Input;
     auto p=std::make_unique<DInputProvider>(in,nullptr);
     CoreConfig cfg;cfg.fresh_us=10'000'000;cfg.pending_limit_us=50'000;
     auto core=std::make_unique<Core>(*p,cfg);
     core->set_profile(read_profile(profile({"confirm = button 0 dev="+W()+" prod="+WP()})));
     auto& s=in->scripts[in->add(wheel())];
     core->service_devices();core->tick();
     for(int i=0;i<4;++i)s.reads.push_back({DI_OK,E_PENDING});
     core->service_devices();(void)core->tick();
     std::this_thread::sleep_for(std::chrono::milliseconds(80));
     core->service_devices();(void)core->tick();          // pending past the limit: the epoch ends
     core->service_devices();const auto& f=core->tick();
     check(f.samples[0].epoch==2&&in->counters.creates==1,"pending past the limit: epoch 2 by reacquisition, same device object");
     core.reset();p->shutdown(100);p.reset();delete in;}
    {// In a test run a pending read of a workload device fails the run (strict freshness).
     Rig r(profile({"confirm = button 0 dev="+W()+" prod="+WP()}),true);
     auto& s=r.in->scripts[r.in->add(wheel())];
     r.core->request_admission(Session{"abcdefgh12345678",2000000000ull},1999999000ull,true,true);
     r.step();
     check(r.core->run()==Run::Waiting,"admitted");
     s.reads.push_back({DI_OK,E_PENDING});
     r.step();
     check(r.core->run()==Run::Failed,"a pending read fails a test run");}
}

// P6 with the real provider: slow listing and creation on the worker never block the tick.
void test_worker_never_blocks_tick(){
    Rig r(profile({"confirm = button 0 dev="+W()+" prod="+WP()}));
    r.in->add(wheel());
    r.in->enumerate_delay_ms=300;r.in->create_delay_ms=200;
    std::atomic<bool> stop{false};
    std::thread worker([&]{while(!stop){r.core->service_devices();std::this_thread::sleep_for(std::chrono::milliseconds(5));}});
    std::int64_t worst=0;bool live=false;
    for(int i=0;i<300&&!live;++i){
        const auto t0=std::chrono::steady_clock::now();
        const auto& f=r.core->tick();
        worst=std::max<std::int64_t>(worst,std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-t0).count());
        live=f.action[std::size_t(A("confirm"))].status==Status::Live;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    stop=true;worker.join();
    check(live,"opened through the worker");
    check(worst<20000,"no tick waited for the listing or the open (worst "+std::to_string(worst)+" us)");
}
}

int main(){
    try{test_classify_and_guids();test_open_caps();test_core_end_to_end();test_identity_change();test_two_devices();test_close_and_stale_handles();test_shutdown();
        test_destroyed_during_a_read();test_stop_is_terminal();test_shutdown_races_an_open();test_stale_handle_admission_race();test_pending_reads();
        test_worker_never_blocks_tick();}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    std::cout<<"dinput provider: "<<checks<<" checks passed\n";
    return 0;
}
