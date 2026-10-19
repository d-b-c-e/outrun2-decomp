// The WheelFfb bridge provider (shim/wheelffb_bridge.hpp) through the real Core and DInputProvider over fake COM, with a
// fake WheelFfb read-slot API: one owner of the wheel's handles at a time (the provider's own handle, or WheelFfb's slot
// on its force handle), the asynchronous hand-over as new epochs, the identity and ranges read back from the shared
// handle, loss, release, shutdown, WheelFfb called only while it holds the wheel, and no phase change that waits for a
// worker sitting in a driver call (romi 32201/32222), down to the host's bounded stop. No DirectInput, DLL or device.
#include "wheelffb_bridge.hpp"
#include "ffb_device.hpp"
#include "provider_host.hpp"
#include "shim_fake_com.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using namespace dbce::shim;
namespace h=dbce::shim::hosting;

namespace {
int checks=0;
void check(bool v,const std::string& what){++checks;if(!v)throw std::runtime_error(what);}
const GUID WI=fakecom::make_guid(0xd71b8350u,0x61b7),WPg=fakecom::make_guid(0x0006346eu,0x0000);
const GUID SI=fakecom::make_guid(0xd9b1d890u,0x6f3f),SPg=fakecom::make_guid(0x0002f3a1u,0x0000);
std::string W(){return guid_text(WI);}std::string WP(){return guid_text(WPg);}
std::string S(){return guid_text(SI);}std::string SP(){return guid_text(SPg);}
int A(const char* id){return action_index(id);}
long long ms_since(std::chrono::steady_clock::time_point t){
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-t).count();}
// The game thread's calls on a thread of their own, waited for at most `ms`; then the worker is let out of the "driver"
// (`release`) and both are joined. True when the calls had returned within the bound (a regression fails, not hangs).
template<class F> bool game_calls(int ms,F f,HANDLE release,std::thread& worker){
    HANDLE done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::thread game([&]{f();SetEvent(done);});
    const bool returned=WaitForSingleObject(done,DWORD(ms))==WAIT_OBJECT_0;
    SetEvent(release);
    worker.join();game.join();
    CloseHandle(done);
    return returned;
}

// The fake library: it lists the wheel, holds it (`held`, as after the delivery's open) or not, and has read slots that
// share its force handle. Any call while it does not hold the wheel is counted: the bridge must never make one.
// GetReadSlotCaps can be held inside the "driver" (block_caps) until the test lets it go.
struct Library {
    bool held{};
    int slots{};                     // open read slots
    int read_ok=1;                   // ReadDeviceStateWithPov's result
    int axes[8]{49152,0,0,0,0,32768,0,0};
    ReadSlotCapsV1 caps{};
    int caps_ok=1;
    bool block_caps{};
    HANDLE caps_entered{},caps_release{};
    std::vector<std::string> log;
    int calls_unheld{};
};
Library L;
std::mutex Lm;   // the worker and the test thread both call in
void note(const char* s){std::lock_guard<std::mutex> l(Lm);L.log.push_back(s);if(!L.held)++L.calls_unheld;}
long count(const char* s){std::lock_guard<std::mutex> l(Lm);return long(std::count(L.log.begin(),L.log.end(),std::string(s)));}
std::size_t calls(){std::lock_guard<std::mutex> l(Lm);return L.log.size();}
void set_held(bool v){std::lock_guard<std::mutex> l(Lm);L.held=v;}
int __cdecl EnumAll(){note("EnumerateAllDevices");return 2;}
int __cdecl AnyGuid(int i,void* g){note("GetAnyDeviceGuid");*static_cast<GUID*>(g)=i==0?SI:WI;return 1;}   // the wheel is second
int __cdecl OpenRead(int i){note("OpenReadDevice");return i==1?L.slots++:-1;}
int __cdecl ReadPov(int slot,int* axes,int na,unsigned char* buttons,int nb,int* pov,int np){
    note("ReadDeviceStateWithPov");
    if(slot<0||slot>=L.slots||na!=8||nb!=128||np!=4||L.read_ok!=1)return 0;
    std::memcpy(axes,L.axes,sizeof L.axes);std::memset(buttons,0,128);buttons[3]=1;for(int p=0;p<4;++p)pov[p]=-1;pov[0]=9000;
    return 1;
}
void __cdecl CloseReads(){note("CloseReadDevices");L.slots=0;}
int __cdecl SlotCaps(int slot,ReadSlotCapsV1* c){
    note("GetReadSlotCaps");
    if(L.block_caps){SetEvent(L.caps_entered);WaitForSingleObject(L.caps_release,INFINITE);}
    if(!c||c->cbSize!=sizeof(ReadSlotCapsV1)||slot<0||slot>=L.slots||L.caps_ok!=1)return 0;
    *c=L.caps;c->cbSize=sizeof *c;c->version=1;return 1;
}
WheelFfbApi api(){
    WheelFfbApi a{};
    a.EnumerateAllDevices=EnumAll;a.GetAnyDeviceGuid=AnyGuid;a.OpenReadDevice=OpenRead;a.ReadDeviceStateWithPov=ReadPov;
    a.CloseReadDevices=CloseReads;a.GetReadSlotCaps=SlotCaps;
    return a;
}
// What WheelFfb reads back from its force handle: the wheel as the fake COM device describes it (X, Z, Rz; 12 buttons).
ReadSlotCapsV1 wheel_caps(){
    ReadSlotCapsV1 c{};c.cbSize=sizeof c;
    std::memcpy(c.instanceGuid,&WI,16);std::memcpy(c.productGuid,&WPg,16);
    for(int a:{0,2,5}){c.axisPresent[a]=1;c.rangeOk[a]=1;c.rangeMin[a]=0;c.rangeMax[a]=65535;}
    for(int b=0;b<12;++b)c.buttonPresent[b]=1;
    c.povPresent[0]=1;
    std::strcpy(c.interfacePath,"\\\\?\\hid#vid_346e&pid_0006#shared");
    return c;
}
fakecom::DeviceScript wheel(){fakecom::DeviceScript s;s.instance=WI;s.product=WPg;s.name=L"MOZA R12 Base";s.interface_path=L"\\\\?\\hid#vid_346e&pid_0006#own";
    s.state.lX=16384;s.state.lZ=0;s.state.lRz=32768;return s;}
fakecom::DeviceScript shifter(){fakecom::DeviceScript s;s.instance=SI;s.product=SPg;s.name=L"DS-8X Shifter";s.axis[0]=s.axis[2]=s.axis[5]=false;s.buttons=11;s.povs=0;return s;}
std::string profile(){
    return "[Controls]\nSchema = 1\nProfile = Rig\nRevision = r1\nTransmission = Sequential\n"
           "steer = axis 0 dev="+W()+" prod="+WP()+" range=0..65535 rest=32768 travel=+1\n"
           "shiftUp = button 0 dev="+S()+" prod="+SP()+"\n";
}
void reset_library(){std::lock_guard<std::mutex> l(Lm);L=Library{};L.caps=wheel_caps();}

struct Rig {
    fakecom::Input* in;
    std::shared_ptr<DInputProvider> inner;
    WheelFfbApi a=api();
    std::shared_ptr<WheelFfbBridge> bridge;
    std::unique_ptr<Core> core;
    const Frame* f{};
    Rig(){
        reset_library();
        in=new fakecom::Input;in->add(wheel());in->add(shifter());
        inner=std::make_shared<DInputProvider>(in,nullptr);
        bridge=std::make_shared<WheelFfbBridge>(inner,&a,W());
        // A step fixture: an absent device is looked for on every pass, as test_shim's step Rig does since the 250 ms
        // missing-device wait (24dc0f8). With the default wait the wheel is read through the slot one interval later.
        CoreConfig cfg;cfg.missing_rescan_us=0;core=std::make_unique<Core>(*bridge,cfg);
        core->set_profile(read_profile(profile()));
    }
    ~Rig(){core.reset();bridge->shutdown();inner->shutdown(200);}
    // The provider worker's pass, then the input thread's tick (one thread here: the order is the point).
    const Frame& step(){core->service_devices();f=&core->tick();return *f;}
    const ActionValue& steer()const{return f->action[std::size_t(A("steer"))];}
    std::uint32_t epoch(const char* id)const{
        const auto& v=f->action[std::size_t(A(id))];
        return v.sample>=0?f->samples[std::size_t(v.sample)].epoch:0;
    }
    bool live(const char* id)const{return f->action[std::size_t(A(id))].status==Status::Live;}
    // The delivery's side of a successful open: ask until the provider's handle is closed, then the library holds it.
    void promote(){
        bool now=bridge->begin_promotion();
        for(int i=0;i<6&&!now;++i){step();now=bridge->begin_promotion();}
        check(now,"the promotion goes on once the worker closed the provider's handle");
        set_held(true);
        bridge->end_promotion(true);
    }
};

void test_hand_over(){
    Rig r;
    r.step();
    check(r.live("steer")&&r.epoch("steer")==1&&r.live("shiftUp")&&r.epoch("shiftUp")==1,"Inner: both devices live on the provider's own handles");
    check(r.in->counters.creates==2&&calls()==0,"Inner: two CreateDevice calls, nothing asked of WheelFfb");
    check(std::fabs(r.steer().value-(16384.0f-32768.0f)/32767.0f)<1e-3f,"Inner: the provider's own read of the wheel");
    // The delivery asks: the provider's wheel handle is retired at once, but not closed by the game thread.
    const int released=r.in->counters.releases.load();
    check(!r.bridge->begin_promotion()&&r.bridge->phase()==WheelFfbBridge::Phase::Draining,"promotion asked: not yet (the handle is retired, not closed)");
    check(r.in->counters.releases.load()==released,"the game thread closes nothing");
    r.step();
    check(!r.live("steer")&&r.live("shiftUp")&&r.epoch("shiftUp")==1,"the retired handle reads Gone: the wheel's epoch ends; the shifter untouched");
    check(!r.bridge->begin_promotion(),"still not yet: Core has only now handed the handle back");
    r.step();
    check(r.in->counters.releases.load()==released+1,"the worker closed the provider's wheel handle");
    check(r.bridge->begin_promotion()&&r.bridge->phase()==WheelFfbBridge::Phase::Promoting,"now the promotion goes on");
    r.step();r.step();
    check(!r.live("steer")&&r.bridge->counts().refused>=1&&r.in->counters.creates==2&&calls()==0,
          "promoting: wheel opens refused; no second CreateDevice; no WheelFfb call");
    check(r.bridge->counts().stale_reads>=1,"the stale handle's reads were Gone");
    set_held(true);
    r.bridge->end_promotion(true);
    r.step();
    check(r.live("steer")&&r.epoch("steer")==2,"Shared: the wheel live again as epoch 2, read through WheelFfb's slot");
    check(std::fabs(r.steer().value-(49152.0f-32768.0f)/32767.0f)<1e-3f,"Shared: the value is WheelFfb's read");
    check(r.in->counters.creates==2,"Shared: still no CreateDevice of the wheel by the provider");
    check(count("OpenReadDevice")==1&&count("GetReadSlotCaps")==1&&L.slots==1,"Shared: the slot prepared once");
    const auto reports=r.core->device_reports();
    check(reports[0].opened&&reports[0].identity.interface_path=="\\\\?\\hid#vid_346e&pid_0006#shared","Shared: the identity read back from the shared handle");
    check(reports[0].caps.axis[0]&&!reports[0].caps.axis[1]&&reports[0].caps.range_ok[0]&&reports[0].caps.max[0]==65535&&reports[0].caps.button.count()==12,
          "Shared: objects and ranges as GetReadSlotCaps read them");
    for(int i=0;i<5;++i)r.step();
    check(r.live("steer")&&r.epoch("steer")==2&&r.live("shiftUp")&&r.epoch("shiftUp")==1,"Shared: steady");
    // A failed read ends the epoch; Core reopens the same slot as a new epoch, with no library call and no provider handle.
    L.read_ok=0;r.step();
    check(!r.live("steer"),"Shared: a failed read is not live");
    L.read_ok=1;r.step();r.step();
    check(r.live("steer")&&r.epoch("steer")==3&&r.in->counters.creates==2,"Shared: reopened as epoch 3; still no provider handle");
    check(count("OpenReadDevice")==1&&count("GetReadSlotCaps")==1&&count("CloseReadDevices")==0,"Shared: the reopen reused the slot");
    // The release: the slot closed (the game thread) before the library frees the wheel; the provider's own handle after.
    r.bridge->begin_release();
    check(count("CloseReadDevices")==1&&L.slots==0,"release: the shared slot closed first");
    set_held(false);
    r.bridge->end_release();
    check(r.bridge->library_quiet(),"after the release the library is quiet");
    const auto n=calls();
    r.step();r.step();r.step();
    check(r.live("steer")&&r.epoch("steer")>=4&&r.in->counters.creates==3,"after the release: the wheel on a provider handle again, a new epoch");
    check(calls()==n&&L.calls_unheld==0,"after the release no call into WheelFfb; and none was ever made while it did not hold the wheel");
}

void test_identity_checked(){
    // The shared handle reads back another product: Core does not adopt it.
    {Rig r;r.step();L.caps.productGuid[0]^=1;r.promote();
     r.step();r.step();
     check(!r.live("steer")&&!r.core->device_reports()[0].opened,"Shared: another product read back is not adopted");
     check(count("GetReadSlotCaps")==1,"Shared: the rejected slot is not prepared again");}
    // An axis whose range is not qualified on the shared handle: the steer is not applied from it.
    {Rig r;r.step();L.caps.rangeOk[0]=0;r.promote();
     r.step();r.step();
     check(!r.live("steer")&&r.steer().status==Status::RangeUnqualified,"Shared: an unqualified range is reported, not guessed");}
    // GetReadSlotCaps refused: no slot is kept, and none is prepared again in this phase.
    {Rig r;r.step();L.caps_ok=0;r.promote();
     r.step();r.step();r.step();
     check(!r.live("steer")&&L.slots==0&&count("GetReadSlotCaps")==1,"Shared: no read-back, no slot kept, not retried");}
}

void test_failed_open_and_shutdown(){
    // The library's open failed: the wheel goes back to the provider (a new epoch), WheelFfb never asked.
    {Rig r;r.step();check(!r.bridge->begin_promotion(),"asked");r.step();r.step();check(r.bridge->begin_promotion(),"goes on");
     r.bridge->end_promotion(false);r.step();r.step();
     check(r.live("steer")&&r.epoch("steer")==2&&r.in->counters.creates==3&&calls()==0,"a failed open: back on a provider handle");}
    // A cancelled hand-over (the delivery left the race while it waited): the provider again.
    {Rig r;r.step();check(!r.bridge->begin_promotion(),"asked");r.step();r.bridge->end_promotion(false);r.step();r.step();
     check(r.bridge->phase()==WheelFfbBridge::Phase::Inner&&r.live("steer")&&calls()==0,"a cancelled hand-over: back on a provider handle");}
    // Shutdown in the Shared phase: the slot closed, nothing opened through WheelFfb again.
    {Rig r;r.step();r.promote();r.step();
     check(r.live("steer")&&L.slots==1,"Shared before the shutdown");
     r.bridge->shutdown();
     check(L.slots==0&&r.bridge->phase()==WheelFfbBridge::Phase::Stopped&&r.bridge->library_quiet(),"shutdown: the shared slot closed, quiet");
     const auto n=calls();
     r.step();r.step();
     check(!r.live("steer")&&calls()==n,"after the shutdown no call into WheelFfb");}
    // A library without GetReadSlotCaps (older than 0.11.0) gives no bridge at all.
    {WheelFfbApi a=api();a.GetReadSlotCaps=nullptr;
     check(!WheelFfbBridge::usable(a),"no GetReadSlotCaps: not usable");
     auto* in=new fakecom::Input;auto inner=std::make_shared<DInputProvider>(in,nullptr);
     bool threw=false;try{WheelFfbBridge b(inner,&a,W());}catch(const std::invalid_argument&){threw=true;}
     check(threw,"the bridge refuses an unusable library");
     inner->shutdown(200);}
}

// romi 32201: a worker inside the driver (the provider's open of the wheel: its Acquire never returns until released)
// must not hold up the game thread: the promotion says "not yet", the release and the shutdown return at once.
void test_worker_in_driver(){
    Rig r;r.step();
    auto& sc=r.in->scripts[0];
    sc.acquire_armed=std::make_shared<std::atomic<bool>>(false);sc.acquire_delay_ms=-1;
    sc.acquire_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);sc.acquire_release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    sc.reads.push_back({DIERR_UNPLUGGED,DIERR_UNPLUGGED});
    r.f=&r.core->tick();                                   // the read fails: Core hands the handle back
    check(!r.live("steer"),"the wheel lost");
    sc.acquire_armed->store(true);
    std::thread worker([&]{r.core->service_devices();});  // closes it, reopens: the new handle's Acquire stays inside
    check(WaitForSingleObject(sc.acquire_entered,5000)==WAIT_OBJECT_0,"the worker is inside the driver's Acquire");
    // The game thread's calls run on their own thread with a bound, so a regression fails here instead of hanging.
    bool now=true;
    const bool returned=game_calls(1000,[&]{now=r.bridge->begin_promotion();r.bridge->begin_release();r.bridge->shutdown();},
                                   sc.acquire_release,worker);
    check(returned&&!now,"the worker in the driver: promotion not yet, release and shutdown return at once");
    check(r.bridge->library_quiet()&&calls()==0,"no library call on that path: quiet");
    check(r.bridge->counts().late_closes==1&&r.bridge->counts().inner_opens==1,"the handle that came back late was closed by the worker, never used");
    CloseHandle(sc.acquire_entered);CloseHandle(sc.acquire_release);
}

// romi 32222: the worker preparing the shared slot sits inside GetReadSlotCaps: the release and the shutdown still
// return at once, the library is not quiet (it must stay loaded) until the worker is out, and the worker closes the slot
// it prepared once the phase has moved on.
void test_worker_in_library(){
    for(int mode=0;mode<2;++mode){
        Rig r;r.step();r.promote();
        L.caps_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);L.caps_release=CreateEventW(nullptr,TRUE,FALSE,nullptr);L.block_caps=true;
        r.f=&r.core->tick();
        std::thread worker([&]{r.core->service_devices();});   // prepares the slot: GetReadSlotCaps stays inside
        check(WaitForSingleObject(L.caps_entered,5000)==WAIT_OBJECT_0,"the worker is inside GetReadSlotCaps");
        const auto closes=count("CloseReadDevices");
        bool quiet=true;long closed=-1;
        const bool returned=game_calls(1000,[&]{
            if(mode==0)r.bridge->begin_release();else r.bridge->shutdown();
            quiet=r.bridge->library_quiet();closed=count("CloseReadDevices");},L.caps_release,worker);
        check(returned,std::string(mode?"shutdown":"release")+" returns at once with the worker inside WheelFfb");
        check(!quiet&&closed==closes,"not quiet while the worker is inside: the library stays loaded; the game thread closed nothing");
        check(r.bridge->library_quiet(),"the worker is out: quiet");
        check(r.bridge->counts().worker_slot_closes==1&&L.slots==0&&count("CloseReadDevices")==closes+1,"the worker closed the slot it prepared after the phase moved on");
        L.block_caps=false;
        if(mode==0){
            set_held(false);
            r.bridge->end_release();r.step();r.step();
            check(r.live("steer")&&r.in->counters.creates==3,"after the release: the provider's handle again");
        }
        CloseHandle(L.caps_entered);CloseHandle(L.caps_release);
    }
}

// romi 32320 (1): a close of the provider's wheel handle still inside the driver (its Unacquire held) keeps the
// promotion waiting, so the library's exclusive open never overlaps a provider handle: the ordinary close (the handle
// Core handed back), and the close of a late handle (an open that returned after the phase changed).
void arm_unacquire(fakecom::DeviceScript& sc){
    sc.unacquire_delay_ms=-1;
    sc.unacquire_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);sc.unacquire_release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    sc.unacquire_armed=std::make_shared<std::atomic<bool>>(true);
}
void test_close_in_driver(){
    {Rig r;r.step();
     auto& sc=r.in->scripts[0];
     sc.reads.push_back({DIERR_UNPLUGGED,DIERR_UNPLUGGED});
     r.f=&r.core->tick();                                  // the read fails: Core hands the handle back
     arm_unacquire(sc);
     std::thread worker([&]{r.core->service_devices();});  // closes it: the Unacquire stays inside
     check(WaitForSingleObject(sc.unacquire_entered,5000)==WAIT_OBJECT_0,"the worker is inside the close's Unacquire");
     bool now=true;
     const bool returned=game_calls(1000,[&]{now=r.bridge->begin_promotion();},sc.unacquire_release,worker);
     check(returned&&!now,"an ordinary close still in the driver: the promotion waits (no overlapping exclusive open)");
     check(r.bridge->begin_promotion(),"the close returned: the promotion goes on");
     CloseHandle(sc.unacquire_entered);CloseHandle(sc.unacquire_release);}
    {Rig r;r.step();
     auto& sc=r.in->scripts[0];
     sc.acquire_armed=std::make_shared<std::atomic<bool>>(false);sc.acquire_delay_ms=-1;
     sc.acquire_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);sc.acquire_release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
     sc.reads.push_back({DIERR_UNPLUGGED,DIERR_UNPLUGGED});
     r.f=&r.core->tick();
     sc.acquire_armed->store(true);
     std::thread worker([&]{r.core->service_devices();});  // reopens: the Acquire stays inside
     check(WaitForSingleObject(sc.acquire_entered,5000)==WAIT_OBJECT_0,"the worker is inside the reopen's Acquire");
     check(!r.bridge->begin_promotion(),"an open in flight: not yet");
     arm_unacquire(sc);
     SetEvent(sc.acquire_release);                         // the open returns after the phase changed: its close stays inside
     check(WaitForSingleObject(sc.unacquire_entered,5000)==WAIT_OBJECT_0,"the worker is inside the late handle's Unacquire");
     bool now=true;
     const bool returned=game_calls(1000,[&]{now=r.bridge->begin_promotion();},sc.unacquire_release,worker);
     check(returned&&!now,"a late handle's close still in the driver: the promotion waits");
     check(r.bridge->begin_promotion()&&r.bridge->counts().late_closes==1,"closed: the promotion goes on");
     CloseHandle(sc.acquire_entered);CloseHandle(sc.acquire_release);CloseHandle(sc.unacquire_entered);CloseHandle(sc.unacquire_release);}
}

// romi 32320 (2): the real delivery with the real bridge, its worker inside GetReadSlotCaps (holding the library, as
// WheelFfb's api mutex would): the delivery's sample and its full shutdown return at once with no library call (the
// stop is retained, the library stays loaded); once the worker is out the next shutdown completes the stop.
namespace dev=outrun::desktop::ffbdev;
int __cdecl FVersion(){note("GetWheelFfbVersion");return 1100;}
void __cdecl FHold(int){note("SetHoldTimeoutMs");}
void __cdecl FPref(const char*){note("SetPreferredDevice");}
void __cdecl FPrefIndex(int){note("SetPreferredDeviceIndex");}
void __cdecl FPrefGuid(const void*){note("SetPreferredDeviceGuid");}
void __cdecl FStrict(int){note("SetStrictDeviceSelection");}
int __cdecl FEnum(){note("EnumerateDevices");return 1;}
int __cdecl FGuid(int i,void* g){note("GetDeviceGuid");if(i!=0)return 0;*static_cast<GUID*>(g)=WI;return 1;}
int __cdecl FInit(int){note("InitDirectInput");return 1;}
int __cdecl FGuards(){note("InstallExitGuards");return 1;}
int __cdecl FForces(int,int){note("SetDeviceForcesXY");return 1;}
int __cdecl FLastHr(){return 0;}
void __cdecl FZero(){note("ZeroForces");}
void __cdecl FPanic(){note("PanicStop");}
void __cdecl FFree(){note("FreeDirectInput");}
struct BridgeOwner final:dev::Owner {
    WheelFfbBridge* b{};
    bool begin_open()override{return b->begin_promotion();}
    void end_open(bool held)override{if(held)set_held(true);b->end_promotion(held);}
    void begin_release()override{b->begin_release();}
    void end_release()override{set_held(false);b->end_release();}
    bool library_quiet()const override{return b->library_quiet();}
    bool enter_library()override{return b->enter_library();}
    void leave_library()override{b->leave_library();}
};
void test_delivery_with_worker_in_library(){
    Rig r;
    r.a.GetWheelFfbVersion=FVersion;r.a.SetHoldTimeoutMs=FHold;r.a.SetPreferredDevice=FPref;r.a.SetPreferredDeviceIndex=FPrefIndex;
    r.a.SetPreferredDeviceGuid=FPrefGuid;r.a.SetStrictDeviceSelection=FStrict;r.a.EnumerateDevices=FEnum;r.a.GetDeviceGuid=FGuid;
    r.a.InitDirectInput=FInit;r.a.InstallExitGuards=FGuards;r.a.SetDeviceForcesXY=FForces;r.a.GetLastHResult=FLastHr;
    r.a.ZeroForces=FZero;r.a.PanicStop=FPanic;r.a.FreeDirectInput=FFree;
    dev::Config cfg;cfg.output=true;cfg.shim=true;cfg.steer_dev=W();
    dev::Delivery d;check(d.start(r.a,cfg),"the delivery starts with the shim exports");
    BridgeOwner o;o.b=r.bridge.get();d.set_owner(&o);
    std::uint64_t ms=0;
    // The force direction is per product (dev::wheel_direction); the fake rig's product GUID is synthetic, so the sample
    // names the measured MOZA R12 Base product the delivery requires before any hand-over.
    const auto sample=[&]{dev::Sample s;s.race=s.front=true;s.hwnd=reinterpret_cast<void*>(std::intptr_t(0x1234));s.steer_dev=W();
        s.steer_prod="{0006346e-0000-0000-0000-504944564944}";
        s.steer_live=r.live("steer");s.steer_epoch=r.epoch("steer");s.ms=ms;ms+=16;return s;};
    for(int i=0;i<10&&d.phase()!=dev::Phase::Open;++i){r.step();d.frame(sample(),2000);}
    check(d.phase()==dev::Phase::Open&&r.bridge->phase()==WheelFfbBridge::Phase::Shared,"the delivery opened through the hand-over");
    L.caps_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);L.caps_release=CreateEventW(nullptr,TRUE,FALSE,nullptr);L.block_caps=true;
    r.f=&r.core->tick();
    std::thread worker([&]{r.core->service_devices();});   // prepares the slot: GetReadSlotCaps stays inside
    check(WaitForSingleObject(L.caps_entered,5000)==WAIT_OBJECT_0,"the worker is inside GetReadSlotCaps");
    const auto before=calls();
    std::size_t during=0;bool retained=false,quiet=true;
    const bool returned=game_calls(1000,[&]{
        d.frame(sample(),2000);d.shutdown();
        during=calls();retained=d.retained();quiet=r.bridge->library_quiet();},L.caps_release,worker);
    check(returned,"the delivery's sample and its full shutdown return at once with the worker inside the library");
    check(during==before&&retained&&!quiet&&d.phase()==dev::Phase::Stopped,"no library call, the stop retained, the library not quiet");
    check(r.bridge->phase()==WheelFfbBridge::Phase::Releasing,"the bridge stopped reading through the library but did not take the wheel back");
    check(r.bridge->counts().worker_slot_closes==1&&L.slots==0,"the worker closed the slot it prepared");
    L.block_caps=false;
    d.shutdown();
    check(!d.retained()&&count("PanicStop")==1&&count("FreeDirectInput")==1&&r.bridge->phase()==WheelFfbBridge::Phase::Inner,
          "the worker out: the next shutdown completes the stop and hands the wheel back");
    check(r.bridge->library_quiet(),"and the library is quiet: it may be unloaded");
    CloseHandle(L.caps_entered);CloseHandle(L.caps_release);
}

// The same through the host (romi 32201: shim_input_shutdown_ordinary's bridge.shutdown before host.stop): a worker in
// the driver, the bridge's shutdown and the host's stop return within the host's own budgets.
struct NoCapture {
    void finish(){}
    struct Drained{h::RecOutcome outcome;bool joined;};
    Drained drain(h::Clock::time_point){return {h::RecOutcome::Complete,true};}
};
void test_host_stop_bounded(){
    reset_library();
    auto* in=new fakecom::Input;in->add(wheel());in->add(shifter());
    WheelFfbApi a=api();
    std::shared_ptr<WheelFfbBridge> bridge;
    wchar_t tmp[MAX_PATH];GetTempPathW(MAX_PATH,tmp);
    const std::wstring folder=std::wstring(tmp)+L"or2_bridge_host_"+std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(folder.c_str(),nullptr);
    auto host=std::make_unique<h::ProviderHost<NoCapture>>(folder,"bridge-test",in,profile(),false,std::make_unique<NoCapture>(),
        Capability{},nullptr,100'000,[&](const std::shared_ptr<DInputProvider>& inner)->std::shared_ptr<Provider>{
            bridge=std::make_shared<WheelFfbBridge>(inner,&a,W());return bridge;});
    check(bridge!=nullptr,"the host built the bridge as its front provider");
    const auto live=[&]{return host->tick().action[std::size_t(A("steer"))].status==Status::Live;};
    bool up=false;
    for(int i=0;i<400&&!up;++i){Sleep(5);up=live();}
    check(up,"the host's worker opened the wheel through the bridge");
    auto& sc=in->scripts[0];
    sc.acquire_delay_ms=-1;
    sc.acquire_entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);sc.acquire_release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    sc.acquire_armed=std::make_shared<std::atomic<bool>>(true);
    sc.reads.push_back({DIERR_UNPLUGGED,DIERR_UNPLUGGED});
    bool entered=false;
    for(int i=0;i<400&&!entered;++i){(void)host->tick();entered=WaitForSingleObject(sc.acquire_entered,5)==WAIT_OBJECT_0;}
    check(entered,"the host's worker is inside the driver reopening the wheel");
    const auto t0=std::chrono::steady_clock::now();
    (void)host->tick();
    bridge->shutdown();
    const auto rep=host->stop();
    const auto took=ms_since(t0);
    check(took<3000,"bridge shutdown + host stop are bounded with the worker in the driver ("+std::to_string(took)+" ms)");
    check(!rep.resolved,"and the stop is reported unresolved (the worker is still inside), not hidden");
    // Released: the provider's shutdown makes that open fail and release its own device (or, had it published first,
    // the bridge closes the late handle); either way the wheel is never adopted again.
    const int releases=in->counters.releases.load();
    SetEvent(sc.acquire_release);
    for(int i=0;i<400&&bridge->counts().late_closes==0&&in->counters.releases.load()==releases;++i)Sleep(5);
    check(bridge->counts().late_closes==1||in->counters.releases.load()>releases,"released: the late open's device was released");
    check(bridge->counts().inner_opens==1,"and never adopted (the only provider open of the wheel is the first)");
    host.reset();
    DeleteFileW((folder+L"\\host-shutdown.json").c_str());RemoveDirectoryW(folder.c_str());
}
}

int main(){
    try{test_hand_over();test_identity_checked();test_failed_open_and_shutdown();test_worker_in_driver();test_worker_in_library();
        test_close_in_driver();test_delivery_with_worker_in_library();test_host_stop_bounded();}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    std::cout<<"wheelffb bridge: "<<checks<<" checks passed\n";
    return 0;
}
