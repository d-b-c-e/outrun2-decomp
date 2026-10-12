// The WheelFfb bridge provider (shim/wheelffb_bridge.hpp) through the real Core and DInputProvider over fake COM, with a
// fake WheelFfb read-slot API: one owner of the wheel's handles at a time (the provider's own handle, or WheelFfb's slot
// on its force handle), the hand-over as new epochs, the identity and ranges read back from the shared handle, loss,
// release, shutdown, and WheelFfb called only while it holds the wheel. No DirectInput, no DLL, no device.
#include "wheelffb_bridge.hpp"
#include "shim_fake_com.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using namespace dbce::shim;

namespace {
int checks=0;
void check(bool v,const std::string& what){++checks;if(!v)throw std::runtime_error(what);}
const GUID WI=fakecom::make_guid(0xd71b8350u,0x61b7),WPg=fakecom::make_guid(0x0006346eu,0x0000);
const GUID SI=fakecom::make_guid(0xd9b1d890u,0x6f3f),SPg=fakecom::make_guid(0x0002f3a1u,0x0000);
std::string W(){return guid_text(WI);}std::string WP(){return guid_text(WPg);}
std::string S(){return guid_text(SI);}std::string SP(){return guid_text(SPg);}
int A(const char* id){return action_index(id);}

// The fake library: it lists the wheel, holds it (`held`, as after the delivery's open) or not, and has read slots that
// share its force handle. Any call while it does not hold the wheel is counted: the bridge must never make one.
struct Library {
    bool held{};
    int slots{};                     // open read slots
    int read_ok=1;                   // ReadDeviceStateWithPov's result
    int axes[8]{49152,0,0,0,0,32768,0,0};
    ReadSlotCapsV1 caps{};
    int caps_ok=1;
    std::vector<std::string> log;
    int calls_unheld{};
};
Library L;
void note(const char* s){L.log.push_back(s);if(!L.held)++L.calls_unheld;}
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

struct Rig {
    fakecom::Input* in;
    std::shared_ptr<DInputProvider> inner;
    WheelFfbApi a=api();
    std::shared_ptr<WheelFfbBridge> bridge;
    std::unique_ptr<Core> core;
    const Frame* f{};
    Rig(){
        L=Library{};L.caps=wheel_caps();
        in=new fakecom::Input;in->add(wheel());in->add(shifter());
        inner=std::make_shared<DInputProvider>(in,nullptr);
        bridge=std::make_shared<WheelFfbBridge>(inner,&a,W());
        CoreConfig cfg;core=std::make_unique<Core>(*bridge,cfg);
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
};

void test_hand_over(){
    Rig r;
    r.step();
    check(r.live("steer")&&r.epoch("steer")==1&&r.live("shiftUp")&&r.epoch("shiftUp")==1,"Inner: both devices live on the provider's own handles");
    check(r.in->counters.creates==2&&L.log.empty(),"Inner: two CreateDevice calls, nothing asked of WheelFfb");
    check(std::fabs(r.steer().value-(16384.0f-32768.0f)/32767.0f)<1e-3f,"Inner: the provider's own read of the wheel");
    // The delivery's open: the provider's wheel handle is closed before the library opens the wheel.
    const int released=r.in->counters.releases.load();
    r.bridge->begin_promotion();
    check(r.in->counters.releases.load()==released+1&&r.bridge->phase()==WheelFfbBridge::Phase::Promoting,"promotion: the provider's wheel handle is released at once");
    r.step();
    check(!r.live("steer")&&r.live("shiftUp")&&r.epoch("shiftUp")==1,"promotion: the wheel's epoch ends (its stale handle reads Gone); the shifter is untouched");
    r.step();r.step();
    check(!r.live("steer")&&r.bridge->counts().shared_refused>=1&&r.in->counters.creates==2&&L.log.empty(),
          "promotion: wheel opens refused until the library holds it; no second CreateDevice; no WheelFfb call");
    check(r.bridge->counts().stale_reads>=1,"the stale handle's reads were Gone");
    L.held=true;r.bridge->end_promotion(true);
    r.step();
    check(r.live("steer")&&r.epoch("steer")==2,"Shared: the wheel live again as epoch 2, read through WheelFfb's slot");
    check(std::fabs(r.steer().value-(49152.0f-32768.0f)/32767.0f)<1e-3f,"Shared: the value is WheelFfb's read");
    check(r.in->counters.creates==2,"Shared: still no CreateDevice of the wheel by the provider");
    const auto reports=r.core->device_reports();
    check(reports[0].opened&&reports[0].identity.interface_path=="\\\\?\\hid#vid_346e&pid_0006#shared","Shared: the identity read back from the shared handle");
    check(reports[0].caps.axis[0]&&!reports[0].caps.axis[1]&&reports[0].caps.range_ok[0]&&reports[0].caps.max[0]==65535&&reports[0].caps.button.count()==12,
          "Shared: objects and ranges as GetReadSlotCaps read them");
    for(int i=0;i<5;++i)r.step();
    check(r.live("steer")&&r.epoch("steer")==2&&r.live("shiftUp")&&r.epoch("shiftUp")==1,"Shared: steady");
    // A failed read of the force handle ends the epoch; Core reopens the slot (a new epoch), never a provider handle.
    L.read_ok=0;r.step();
    check(!r.live("steer"),"Shared: a failed read is not live");
    L.read_ok=1;r.step();r.step();
    check(r.live("steer")&&r.epoch("steer")==3&&r.in->counters.creates==2,"Shared: the slot reopened as epoch 3; still no provider handle");
    // The release: the slot closed before the library frees the wheel; the provider's own handle afterwards.
    const auto closes=std::count(L.log.begin(),L.log.end(),std::string("CloseReadDevices"));
    r.bridge->begin_release();
    check(std::count(L.log.begin(),L.log.end(),std::string("CloseReadDevices"))==closes+1,"release: the shared slot closed first");
    L.held=false;r.bridge->end_release();
    const auto calls=L.log.size();
    r.step();r.step();r.step();
    check(r.live("steer")&&r.epoch("steer")>=4&&r.in->counters.creates==3,"after the release: the wheel on a provider handle again, a new epoch");
    check(L.log.size()==calls&&L.calls_unheld==0,"after the release no call into WheelFfb; and none was ever made while it did not hold the wheel");
}

void test_identity_checked(){
    // The shared handle reads back another product: Core does not adopt it.
    {Rig r;r.step();r.bridge->begin_promotion();L.caps.productGuid[0]^=1;L.held=true;r.bridge->end_promotion(true);
     r.step();r.step();
     check(!r.live("steer")&&!r.core->device_reports()[0].opened,"Shared: another product read back is not adopted");}
    // An axis whose range is not qualified on the shared handle: the steer is not applied from it.
    {Rig r;r.step();r.bridge->begin_promotion();L.caps.rangeOk[0]=0;L.held=true;r.bridge->end_promotion(true);
     r.step();r.step();
     check(!r.live("steer")&&r.steer().status==Status::RangeUnqualified,"Shared: an unqualified range is reported, not guessed");}
    // GetReadSlotCaps refused: no slot is kept.
    {Rig r;r.step();r.bridge->begin_promotion();L.caps_ok=0;L.held=true;r.bridge->end_promotion(true);
     r.step();r.step();
     check(!r.live("steer")&&L.slots==0,"Shared: no read-back, no slot kept");}
}

void test_failed_open_and_shutdown(){
    // The library's open failed: the wheel goes back to the provider (a new epoch), WheelFfb never asked.
    {Rig r;r.step();r.bridge->begin_promotion();r.step();r.bridge->end_promotion(false);r.step();r.step();
     check(r.live("steer")&&r.epoch("steer")==2&&r.in->counters.creates==3&&L.log.empty(),"a failed open: back on a provider handle");}
    // Shutdown in the Shared phase: the slot closed, nothing opened through WheelFfb again.
    {Rig r;r.step();r.bridge->begin_promotion();L.held=true;r.bridge->end_promotion(true);r.step();r.step();
     check(r.live("steer")&&L.slots==1,"Shared before the shutdown");
     r.bridge->shutdown();
     check(L.slots==0&&r.bridge->phase()==WheelFfbBridge::Phase::Stopped,"shutdown: the shared slot closed");
     const auto calls=L.log.size();
     r.step();r.step();
     check(!r.live("steer")&&L.log.size()==calls,"after the shutdown no call into WheelFfb");}
    // A library without GetReadSlotCaps (older than 0.11.0) gives no bridge at all.
    {WheelFfbApi a=api();a.GetReadSlotCaps=nullptr;
     check(!WheelFfbBridge::usable(a),"no GetReadSlotCaps: not usable");
     auto* in=new fakecom::Input;auto inner=std::make_shared<DInputProvider>(in,nullptr);
     bool threw=false;try{WheelFfbBridge b(inner,&a,W());}catch(const std::invalid_argument&){threw=true;}
     check(threw,"the bridge refuses an unusable library");
     inner->shutdown(200);}
}
}

int main(){
    try{test_hand_over();test_identity_checked();test_failed_open_and_shutdown();}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    std::cout<<"wheelffb bridge: "<<checks<<" checks passed\n";
    return 0;
}
