// The F4 delivery gates (source/ffb_device) against a fake WheelFfb library: what reaches the library, in which order,
// and that nothing reaches it when a gate is closed. The fake keeps what a real library would hold: the magnitude the
// wheel last accepted (a refused command or zero leaves it), the hold watchdog's worker, the exit guards' window
// subclass and its DirectInput state, so every case can check that silence is real and that the end state is safe to
// unload. No device, no DLL.
#define DIRECTINPUT_VERSION 0x0800
#include "ffb_device.hpp"
#include <dinput.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <deque>
#include <string>
#include <vector>
using namespace outrun::desktop::ffbdev;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"ffb delivery line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
const std::string Steer="{d71b8350-61b7-11f1-8001-444553540000}";      // the MOZA R12 Base instance of the rig logs
const std::string SteerUpper="{D71B8350-61B7-11F1-8001-444553540000}";
const std::string Other="{d9b1d890-6f3f-11f1-8002-444553540000}";
struct Fake {
    std::vector<std::string> log;
    int version=1000,init=1,guards=1;
    std::deque<int> forces;          // results of SetDeviceForcesXY in order (1 when empty)
    long hr=0;                       // what GetLastHResult returns
    std::vector<std::string> devices{Steer};
    int wheel=0;                     // the magnitude the wheel holds: only an accepted command changes it
    bool di{},worker{},subclass{};   // library state: DirectInput, the hold watchdog's thread, the window subclass
};
Fake F;
GUID guid_of(const std::string& s){GUID g{};parse_guid(s,g);return g;}
std::string text(const GUID& g){
    char t[40];std::snprintf(t,sizeof t,"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",g.Data1,g.Data2,g.Data3,
        g.Data4[0],g.Data4[1],g.Data4[2],g.Data4[3],g.Data4[4],g.Data4[5],g.Data4[6],g.Data4[7]);
    return t;
}
void note(const std::string& s){F.log.push_back(s);}
int __cdecl Version(){note("GetWheelFfbVersion");return F.version;}
void __cdecl Hold(int ms){note("SetHoldTimeoutMs "+std::to_string(ms));if(ms>0)F.worker=true;}
void __cdecl Preferred(const char* n){note(std::string("SetPreferredDevice ")+(n?n:"null"));}
void __cdecl PreferredIndex(int i){note("SetPreferredDeviceIndex "+std::to_string(i));}
void __cdecl PreferredGuid(const void* g){note("SetPreferredDeviceGuid "+text(*static_cast<const GUID*>(g)));}
void __cdecl Strict(int on){note("SetStrictDeviceSelection "+std::to_string(on));}
int __cdecl Enumerate(){note("EnumerateDevices");F.di=true;return int(F.devices.size());}
int __cdecl DeviceGuid(int i,void* g){
    if(i<0||i>=int(F.devices.size()))return 0;
    *static_cast<GUID*>(g)=guid_of(F.devices[std::size_t(i)]);return 1;
}
int __cdecl Init(int hwnd){note("InitDirectInput "+std::to_string(hwnd));F.di=true;return F.init;}
int __cdecl Guards(){note("InstallExitGuards");if(F.guards)F.subclass=true;return F.guards;}
int __cdecl Forces(int x,int y){
    note("SetDeviceForcesXY "+std::to_string(x)+" "+std::to_string(y));
    int r=1;if(!F.forces.empty()){r=F.forces.front();F.forces.pop_front();}
    if(r==1)F.wheel=x;               // refused: the wheel keeps what it held
    return r;
}
int __cdecl LastHr(){return int(F.hr);}
void __cdecl Zero(){note("ZeroForces");}
void __cdecl Panic(){note("PanicStop");F.wheel=0;}
void __cdecl Free(){note("FreeDirectInput");F.wheel=0;F.di=F.worker=F.subclass=false;}
WheelFfbApi api(){
    WheelFfbApi a{};
    a.GetWheelFfbVersion=Version;a.SetHoldTimeoutMs=Hold;a.SetPreferredDevice=Preferred;a.SetPreferredDeviceIndex=PreferredIndex;
    a.SetPreferredDeviceGuid=PreferredGuid;a.SetStrictDeviceSelection=Strict;a.EnumerateDevices=Enumerate;a.GetDeviceGuid=DeviceGuid;
    a.InitDirectInput=Init;a.InstallExitGuards=Guards;a.SetDeviceForcesXY=Forces;a.GetLastHResult=LastHr;a.ZeroForces=Zero;
    a.PanicStop=Panic;a.FreeDirectInput=Free;
    return a;
}
Config on(){Config c;c.output=true;c.steer_dev=Steer;return c;}
Sample race(std::uint64_t ms,bool front=true,const std::string& dev=Steer){Sample s;s.race=true;s.front=front;s.hwnd=reinterpret_cast<void*>(0x1234);s.steer_dev=dev;s.ms=ms;return s;}
Sample menu(std::uint64_t ms){Sample s=race(ms);s.race=false;return s;}
void reset(){F=Fake{};}
using Lines=std::vector<std::string>;
std::vector<std::string> since(std::size_t from){return {F.log.begin()+std::ptrdiff_t(from),F.log.end()};}
bool device_calls(const Lines& v){
    for(const auto& s:v)for(const char* k:{"InitDirectInput","SetDeviceForcesXY","ZeroForces","PanicStop","FreeDirectInput","InstallExitGuards","SetHoldTimeoutMs"})
        if(s.rfind(k,0)==0)return true;
    return false;
}
// What the gate believes must hold on the fake wheel; and the library holds nothing once it stopped or released.
void invariant(const Delivery& d){
    if(d.silent())CHECK(F.wheel==0);
    if((d.phase()==Phase::Released||d.phase()==Phase::Stopped)&&!d.retained())CHECK(!F.di&&!F.worker&&!F.subclass&&F.wheel==0);
    for(const auto& s:F.log)CHECK(s!="ZeroForces");     // void ZeroForces is never relied on
}
void step(Delivery& d,const Sample& s,std::int32_t c){d.frame(s,c);invariant(d);}
const Lines OpenSequence{"SetPreferredDevice null","SetPreferredDeviceIndex -1","SetPreferredDeviceGuid "+Steer,
    "SetStrictDeviceSelection 1","EnumerateDevices","InitDirectInput 4660","InstallExitGuards","SetHoldTimeoutMs 250",
    "SetDeviceForcesXY 0 0"};
// A started delivery opened on the first race sample (at 0 ms, command 0); the log is cleared after the open.
Delivery opened(){
    reset();Delivery d;CHECK(d.start(api(),on()));step(d,race(0),0);
    CHECK(d.phase()==Phase::Open&&d.silent()&&F.worker&&F.subclass);F.log.clear();
    return d;
}
void finish(Delivery& d){d.shutdown();invariant(d);CHECK(!F.di&&!F.worker&&!F.subclass&&F.wheel==0);}

// The shim backend's hand-over (ffbdev::Owner): its calls go into the same log, so every order is checked against the
// library's. The read-slot exports are present (Delivery::start requires them with Config::shim) and never called.
struct FakeOwner final:Owner {
    std::deque<bool> answers;        // begin_open's answers in order (true when empty)
    bool begin_open()override{
        bool a=true;if(!answers.empty()){a=answers.front();answers.pop_front();}
        note(std::string("Owner begin_open ")+(a?"1":"0"));return a;
    }
    bool library_quiet()const override{return true;}
    bool busy=false;                 // the input's worker inside the library: the gate cannot be entered
    bool entered=false;
    bool enter_library()override{if(busy)return false;CHECK(!entered);entered=true;return true;}
    void leave_library()override{CHECK(entered);entered=false;}
    void end_open(bool held)override{note(std::string("Owner end_open ")+(held?"1":"0"));}
    void begin_release()override{note("Owner begin_release");}
    void end_release()override{note("Owner end_release");}
};
int __cdecl NotCalled0(){note("read slot export called");return 0;}
int __cdecl NotCalledGuid(int,void*){note("read slot export called");return 0;}
int __cdecl NotCalledOpen(int){note("read slot export called");return -1;}
int __cdecl NotCalledRead(int,int*,int,unsigned char*,int,int*,int){note("read slot export called");return 0;}
void __cdecl NotCalledClose(){note("read slot export called");}
int __cdecl NotCalledCaps(int,ReadSlotCapsV1*){note("read slot export called");return 0;}
WheelFfbApi shim_api(){
    WheelFfbApi a=api();
    a.EnumerateAllDevices=NotCalled0;a.GetAnyDeviceGuid=NotCalledGuid;a.OpenReadDevice=NotCalledOpen;
    a.ReadDeviceStateWithPov=NotCalledRead;a.CloseReadDevices=NotCalledClose;a.GetReadSlotCaps=NotCalledCaps;
    return a;
}
Config shim_on(){Config c=on();c.shim=true;return c;}
Sample live(std::uint64_t ms,std::uint32_t epoch,bool is_live=true){Sample s=race(ms);s.steer_live=is_live;s.steer_epoch=epoch;return s;}
// Opened with the owner at 0 ms in epoch 1 (the input's own handle), then the steer live in epoch 2 (through the library).
Delivery owned(FakeOwner& o){
    reset();Delivery d;CHECK(d.start(shim_api(),shim_on()));d.set_owner(&o);
    step(d,live(0,1),0);CHECK(d.phase()==Phase::Open);
    step(d,live(5,2),0);CHECK(d.phase()==Phase::Open);
    F.log.clear();return d;
}
}
int main(){
    // GUID text: braces, dashes, hex; nothing else.
    {GUID g{};CHECK(parse_guid(Steer,g)&&text(g)==Steer);
     CHECK(parse_guid(SteerUpper,g)&&text(g)==Steer);
     for(const char* bad:{"","d71b8350-61b7-11f1-8001-444553540000","{d71b8350-61b7-11f1-8001-44455354000}","{d71b8350-61b7-11f1-8001-4445535400000}",
                          "{d71b8350x61b7-11f1-8001-444553540000}","{g71b8350-61b7-11f1-8001-444553540000}"})CHECK(!parse_guid(bad,g));}

    // load: off by default; mute, latched and an unbound steer refuse before anything is loaded.
    {Config c;CHECK(!load_refusal(c).empty());
     c=on();CHECK(load_refusal(c).empty());
     c=on();c.muted=true;CHECK(load_refusal(c)=="DBCE_FFB_MUTE=1");
     c=on();c.latched=true;CHECK(load_refusal(c)=="a latched (test) process");
     c=on();c.steer_dev.clear();CHECK(!load_refusal(c).empty());
     c=on();c.steer_dev="MOZA R12 Base";CHECK(!load_refusal(c).empty());
     reset();Delivery d;c=on();c.muted=true;CHECK(!d.start(api(),c)&&d.phase()==Phase::Off&&F.log.empty());
     step(d,race(0),5000);d.shutdown();CHECK(F.log.empty());}

    // start: every export used and a 0.9.0 or newer library; no library state at all (no watchdog before an open).
    {reset();Delivery d;CHECK(d.start(api(),on())&&d.phase()==Phase::Waiting);
     CHECK((F.log==Lines{"GetWheelFfbVersion"})&&!F.worker&&!F.di);
     CHECK(!d.start(api(),on())&&d.why()=="started twice"&&d.phase()==Phase::Waiting);   // a second start is refused
     finish(d);CHECK((F.log==Lines{"GetWheelFfbVersion"}));                                 // never opened: nothing owed
     reset();WheelFfbApi a=api();a.FreeDirectInput=nullptr;Delivery e;
     CHECK(!e.start(a,on())&&e.phase()==Phase::Released&&e.why()=="WheelFfb.dll has no FreeDirectInput"&&F.log.empty());
     reset();F.version=899;Delivery v;CHECK(!v.start(api(),on())&&v.phase()==Phase::Released&&!device_calls(F.log));
     step(v,race(0),5000);CHECK(!device_calls(F.log));}

    // open: only in the race, in front, with the input layer reading the configured instance (any case); exact order.
    {reset();Delivery d;CHECK(d.start(api(),on()));const auto base=F.log.size();
     step(d,menu(0),0);step(d,race(1,false),3000);step(d,race(2,true,""),3000);step(d,race(3,true,Other),3000);step(d,race(4,true,"steer"),3000);
     CHECK(d.phase()==Phase::Waiting&&since(base).empty());
     step(d,race(5,true,SteerUpper),3000);
     Lines want=OpenSequence;want.push_back("SetDeviceForcesXY 3000 0");
     CHECK(since(base)==want&&d.phase()==Phase::Open&&F.wheel==3000);
     CHECK(d.counts().sent==1&&d.counts().zeroes==1);
     finish(d);CHECK((since(base+want.size())==Lines{"PanicStop","FreeDirectInput"}));}

    // open refusals: not a force-feedback device, a refused open, no exit guards, a refused starting zero: released,
    // never a command, the library emptied.
    {reset();F.devices={Other};Delivery d;CHECK(d.start(api(),on()));step(d,race(0),3000);
     CHECK(d.phase()==Phase::Released&&F.log.back()=="FreeDirectInput");
     for(const auto& s:F.log)CHECK(s.rfind("InitDirectInput",0)!=0&&s.rfind("PanicStop",0)!=0);
     const auto n=F.log.size();step(d,race(1),3000);finish(d);CHECK(F.log.size()==n);}
    {reset();F.init=0;F.hr=DIERR_NOTEXCLUSIVEACQUIRED;Delivery d;CHECK(d.start(api(),on()));step(d,race(0),3000);
     CHECK(d.phase()==Phase::Released&&F.log.back()=="FreeDirectInput");
     for(const auto& s:F.log)CHECK(s.rfind("SetDeviceForcesXY",0)!=0&&s.rfind("InstallExitGuards",0)!=0);
     const auto n=F.log.size();step(d,race(1),3000);finish(d);CHECK(F.log.size()==n);}
    {reset();F.guards=0;Delivery d;CHECK(d.start(api(),on()));step(d,race(0),3000);
     CHECK(d.phase()==Phase::Released&&(since(F.log.size()-3)==Lines{"InstallExitGuards","PanicStop","FreeDirectInput"}));
     for(const auto& s:F.log)CHECK(s.rfind("SetDeviceForcesXY",0)!=0&&s.rfind("SetHoldTimeoutMs",0)!=0);}
    {reset();F.forces={0};F.hr=DIERR_INPUTLOST;Delivery d;CHECK(d.start(api(),on()));step(d,race(0),3000);
     CHECK(d.phase()==Phase::Released);   // even a transient refusal of the starting zero is a failed open
     CHECK((since(F.log.size()-3)==Lines{"SetDeviceForcesXY 0 0","PanicStop","FreeDirectInput"}));}

    // force: a change goes out at once; the held value again every 100 ms (the hold watchdog's refresh); clamped.
    {Delivery d=opened();
     step(d,race(10),2500);step(d,race(20),2500);step(d,race(109),2500);step(d,race(110),2500);step(d,race(120),2600);
     step(d,race(130),20000);step(d,race(140),-20000);
     CHECK((F.log==Lines{"SetDeviceForcesXY 2500 0","SetDeviceForcesXY 2500 0","SetDeviceForcesXY 2600 0",
                         "SetDeviceForcesXY 10000 0","SetDeviceForcesXY -10000 0"}));
     CHECK(d.counts().keepalive==1&&F.wheel==-10000&&!d.silent());
     finish(d);}

    // out of the race (pause, menus): an accepted zero, then nothing; back in, an accepted zero before the command.
    {Delivery d=opened();
     step(d,race(10),2500);step(d,menu(20),2500);step(d,menu(30),2500);step(d,race(40),2600);
     CHECK((F.log==Lines{"SetDeviceForcesXY 2500 0","SetDeviceForcesXY 0 0","SetDeviceForcesXY 0 0","SetDeviceForcesXY 2600 0"}));
     // already silent: no zero on the way out, still one on the way back
     F.log.clear();step(d,race(150),0);step(d,menu(160),0);step(d,menu(170),0);step(d,race(180),100);
     CHECK((F.log==Lines{"SetDeviceForcesXY 0 0","SetDeviceForcesXY 0 0","SetDeviceForcesXY 100 0"}));
     finish(d);}

    // out of front: the same; a refused zero (the handle lost with the focus) is retried every sample, and the device
    // is released when none is accepted within 2 s; the wheel never counts as silent before that.
    {Delivery d=opened();
     step(d,race(10),2500);F.forces={0,0};F.hr=DIERR_NOTACQUIRED;
     step(d,race(20,false),2500);CHECK(!d.silent()&&F.wheel==2500);
     step(d,race(30,false),2500);CHECK(!d.silent()&&F.wheel==2500);
     step(d,race(40,false),2500);CHECK(d.silent()&&F.wheel==0&&d.counts().recoveries==1);   // accepted at last
     step(d,race(50,false),2500);step(d,race(60),2500);
     CHECK((F.log==Lines{"SetDeviceForcesXY 2500 0","SetDeviceForcesXY 0 0","SetDeviceForcesXY 0 0","SetDeviceForcesXY 0 0",
                         "SetDeviceForcesXY 0 0","SetDeviceForcesXY 2500 0"}));
     finish(d);}
    {Delivery d=opened();
     step(d,race(10),2500);F.forces={0,0,0,0,0,0,0,0};F.hr=DIERR_NOTEXCLUSIVEACQUIRED;
     for(std::uint64_t t=20;t<2020;t+=500)step(d,race(t,false),2500);
     CHECK(d.phase()==Phase::Open&&!d.silent()&&F.wheel==2500);
     step(d,race(2020,false),2500);                                          // 2 s after the first refused zero
     CHECK(d.phase()==Phase::Released&&(since(F.log.size()-2)==Lines{"PanicStop","FreeDirectInput"})&&F.wheel==0);
     const auto n=F.log.size();step(d,race(2030),2500);finish(d);CHECK(F.log.size()==n);}

    // a transient refusal of a command in the race: the wheel holds the old force, so an accepted zero comes first.
    {Delivery d=opened();
     step(d,race(10),2500);F.forces={0,0};F.hr=DIERR_NOTDOWNLOADED;
     step(d,race(20),3000);CHECK(F.wheel==2500&&!d.silent()&&d.counts().transient==1);
     step(d,race(30),3000);CHECK(F.wheel==2500);                              // the zero refused too
     step(d,race(40),3000);
     CHECK((F.log==Lines{"SetDeviceForcesXY 2500 0","SetDeviceForcesXY 3000 0","SetDeviceForcesXY 0 0","SetDeviceForcesXY 0 0",
                         "SetDeviceForcesXY 3000 0"}));
     CHECK(F.wheel==3000&&d.counts().recoveries==1);
     finish(d);}

    // any other refusal releases at once.
    {Delivery d=opened();
     step(d,race(10),2500);F.forces={0};F.hr=DIERR_INCOMPLETEEFFECT;
     step(d,race(20),3000);
     CHECK(d.phase()==Phase::Released&&(since(F.log.size()-2)==Lines{"PanicStop","FreeDirectInput"}));
     const auto n=F.log.size();step(d,race(30),2500);CHECK(F.log.size()==n);}

    // a held force older than the hold since its last accepted command (a stall): the library's watchdog may have
    // stopped the wheel, so an accepted zero comes before the held value is sent again; also when no single sampling
    // gap exceeds the hold (romi 29717: 0 / 90 / 290 ms).
    {Delivery d=opened();
     step(d,race(10),2500);step(d,race(100),2500);step(d,race(300),2500);
     CHECK((F.log==Lines{"SetDeviceForcesXY 2500 0","SetDeviceForcesXY 0 0","SetDeviceForcesXY 2500 0"}));
     F.log.clear();step(d,race(360),0);step(d,race(700),0);step(d,race(710),100);   // a held zero never expires
     CHECK((F.log==Lines{"SetDeviceForcesXY 0 0","SetDeviceForcesXY 100 0"}));
     finish(d);}
    {Delivery d=opened();
     step(d,race(10),2500);step(d,race(261),2500);step(d,race(262),2500);step(d,race(370),2500);
     CHECK((F.log==Lines{"SetDeviceForcesXY 2500 0","SetDeviceForcesXY 0 0","SetDeviceForcesXY 2500 0","SetDeviceForcesXY 2500 0"}));
     finish(d);}

    // one device epoch: the input layer losing or changing the steering device releases the open device.
    {Delivery d=opened();
     step(d,race(10),2500);step(d,race(20,true,""),2500);
     CHECK(d.phase()==Phase::Released&&(since(F.log.size()-2)==Lines{"PanicStop","FreeDirectInput"}));}
    {Delivery d=opened();
     step(d,menu(10),0);step(d,menu(20),0);F.log.clear();
     Sample s=menu(30);s.steer_dev=Other;step(d,s,0);                           // also outside the race
     CHECK(d.phase()==Phase::Released&&(F.log==Lines{"PanicStop","FreeDirectInput"}));}
    {Delivery d=opened();step(d,race(10,true,SteerUpper),2500);CHECK(d.phase()==Phase::Open);finish(d);}   // same GUID

    // shutdown: PanicStop then FreeDirectInput once for an opened device; nothing again afterwards.
    {Delivery d=opened();step(d,race(10),2500);F.log.clear();
     d.shutdown();invariant(d);CHECK((F.log==Lines{"PanicStop","FreeDirectInput"})&&d.phase()==Phase::Stopped);
     F.log.clear();d.shutdown();step(d,race(20),2500);CHECK(F.log.empty());
     CHECK(!d.start(api(),on()));}                                                // and no restart

    // With the shim backend: the read-slot exports (0.11.0) are required at the start; without the owner nothing opens.
    {reset();Delivery d;CHECK(!d.start(api(),shim_on())&&d.phase()==Phase::Released&&d.why().find("EnumerateAllDevices")!=std::string::npos);
     CHECK(F.log.empty());
     reset();WheelFfbApi a=shim_api();a.GetReadSlotCaps=nullptr;Delivery e;
     CHECK(!e.start(a,shim_on())&&e.why().find("GetReadSlotCaps")!=std::string::npos&&e.why().find("0.11.0")!=std::string::npos);
     reset();Delivery n;CHECK(n.start(shim_api(),shim_on()));const auto base=F.log.size();
     for(std::uint64_t ms=0;ms<3000;ms+=10)step(n,live(ms,1),4000);
     CHECK(n.phase()==Phase::Waiting&&!device_calls(since(base))&&n.why().find("no owner")!=std::string::npos);
     finish(n);}

    // The owner's hand-over around the open: its handle closed before InitDirectInput, handed back after the accepted
    // zero; the open waits for live steering; then zeros only until the steer is live in a new epoch.
    {reset();FakeOwner o;Delivery d;CHECK(d.start(shim_api(),shim_on()));d.set_owner(&o);const auto base=F.log.size();
     step(d,live(0,1,false),3000);step(d,race(1),3000);                          // not live (and a sample without liveness)
     CHECK(d.phase()==Phase::Waiting&&since(base).empty()&&d.why().find("not live")!=std::string::npos);
     step(d,live(2,1),3000);
     Lines want=OpenSequence;
     want.insert(want.begin(),"Owner begin_open 1");                             // before the library is touched at all
     want.push_back("Owner end_open 1");
     CHECK(since(base)==want&&d.phase()==Phase::Open&&F.wheel==0);               // the open's tick sends no force
     const auto opened=F.log.size();
     for(std::uint64_t ms=10;ms<500;ms+=10)step(d,live(ms,1),3000);              // still the old epoch: nothing
     step(d,live(500,2,false),3000);                                             // the new epoch, not live yet: nothing
     CHECK(since(opened).empty()&&F.wheel==0&&d.silent());
     step(d,live(510,2),3000);
     CHECK((since(opened)==Lines{"SetDeviceForcesXY 0 0","SetDeviceForcesXY 3000 0"})&&F.wheel==3000);   // an accepted zero, then the force
     for(const auto& s:F.log)CHECK(s!="read slot export called");
     finish(d);}

    // Steering not live in the shared epoch: an accepted zero, no release; live again: the force resumes.
    {FakeOwner o;Delivery d=owned(o);
     step(d,live(10,2),2500);CHECK(F.wheel==2500);
     F.log.clear();step(d,live(20,2,false),2500);
     CHECK((F.log==Lines{"SetDeviceForcesXY 0 0"})&&d.phase()==Phase::Open&&F.wheel==0);
     step(d,live(30,2,false),2500);CHECK(F.log.size()==1);                      // held zero: no refresh
     step(d,live(40,2),2500);CHECK(F.log.back()=="SetDeviceForcesXY 2500 0"&&F.wheel==2500);
     finish(d);}

    // A later epoch change releases: PanicStop, the owner stops reading through the library, FreeDirectInput, the owner
    // takes the wheel back. The same order at shutdown.
    {FakeOwner o;Delivery d=owned(o);
     step(d,live(10,2),2500);F.log.clear();
     step(d,live(20,3),2500);
     CHECK(d.phase()==Phase::Released&&(F.log==Lines{"PanicStop","Owner begin_release","FreeDirectInput","Owner end_release"}));
     F.log.clear();step(d,live(30,3),2500);finish(d);CHECK(F.log.empty());}
    {FakeOwner o;Delivery d=owned(o);
     step(d,live(10,2),2500);F.log.clear();
     d.shutdown();invariant(d);
     CHECK(d.phase()==Phase::Stopped&&(F.log==Lines{"PanicStop","Owner begin_release","FreeDirectInput","Owner end_release"}));}

    // No new epoch within 2 s of the open: released (the owner never got the wheel back through the library).
    {reset();FakeOwner o;Delivery d;CHECK(d.start(shim_api(),shim_on()));d.set_owner(&o);
     step(d,live(0,1),0);CHECK(d.phase()==Phase::Open);F.log.clear();
     for(std::uint64_t ms=10;ms<2000;ms+=10)step(d,live(ms,1),3000);
     CHECK(d.phase()==Phase::Open&&F.log.empty());
     step(d,live(2000,1),3000);
     CHECK(d.phase()==Phase::Released&&d.why().find("did not resume")!=std::string::npos);
     CHECK((F.log==Lines{"PanicStop","Owner begin_release","FreeDirectInput","Owner end_release"}));}

    // A failed open with the owner: the library frees the wheel before the owner takes it back.
    {reset();F.init=0;FakeOwner o;Delivery d;CHECK(d.start(shim_api(),shim_on()));d.set_owner(&o);
     const auto base=F.log.size();step(d,live(0,1),3000);
     const auto got=since(base);
     const auto at=[&](const char* s){return std::find(got.begin(),got.end(),std::string(s))-got.begin();};
     CHECK(d.phase()==Phase::Released&&at("Owner begin_open 1")<at("InitDirectInput 4660")&&at("InitDirectInput 4660")<at("FreeDirectInput")&&
           at("FreeDirectInput")<at("Owner end_open 0")&&at("Owner end_open 0")==std::ptrdiff_t(got.size())-1);
     for(const auto& s:got)CHECK(s!="PanicStop"&&s!="Owner begin_release");}
    {reset();F.forces={0};F.hr=long(DIERR_INPUTLOST);FakeOwner o;Delivery d;CHECK(d.start(shim_api(),shim_on()));d.set_owner(&o);
     const auto base=F.log.size();step(d,live(0,1),3000);                          // the starting zero refused
     const auto got=since(base);
     CHECK(d.phase()==Phase::Released&&got.back()=="Owner end_open 0"&&got[got.size()-2]=="FreeDirectInput");}

    // The hand-over never waits (romi 32201): while the owner answers "not yet" nothing touches the library and the
    // owner is asked again with each race sample in front, even though the steering is not live during it (the
    // hand-over itself ends the epoch); then the open goes on.
    {reset();FakeOwner o;o.answers={false,false,false};Delivery d;CHECK(d.start(shim_api(),shim_on()));d.set_owner(&o);
     const auto base=F.log.size();
     step(d,live(0,1),3000);step(d,live(10,1,false),3000);step(d,live(20,1,false),3000);
     CHECK((since(base)==Lines{"Owner begin_open 0","Owner begin_open 0","Owner begin_open 0"})&&d.phase()==Phase::Waiting);
     CHECK(d.why().find("handing the wheel over")!=std::string::npos);
     step(d,live(30,1,false),3000);
     CHECK(d.phase()==Phase::Open&&since(base)[3]=="Owner begin_open 1"&&F.log.back()=="Owner end_open 1");
     step(d,live(40,2),3000);CHECK(F.wheel==3000);                              // E0 was 1: epoch 2 is the shared one
     finish(d);}
    // Leaving the race (or the front) during the hand-over cancels it; the next hand-over needs live steering again.
    {reset();FakeOwner o;o.answers={false};Delivery d;CHECK(d.start(shim_api(),shim_on()));d.set_owner(&o);
     const auto base=F.log.size();
     step(d,live(0,1),3000);step(d,menu(10),0);
     CHECK((since(base)==Lines{"Owner begin_open 0","Owner end_open 0"})&&d.phase()==Phase::Waiting);
     step(d,menu(20),0);step(d,live(30,1,false),3000);
     CHECK(since(base).size()==2&&d.why().find("not live")!=std::string::npos);
     o.answers={false};step(d,live(40,1),3000);step(d,race(50,false),3000);       // out of front: cancelled too
     CHECK(F.log.back()=="Owner end_open 0"&&d.phase()==Phase::Waiting&&!device_calls(since(base)));
     finish(d);CHECK(F.log.back()=="Owner end_open 0");}                        // no further hand-over to cancel
    // Shutdown during a hand-over ends it.
    {reset();FakeOwner o;o.answers={false};Delivery d;CHECK(d.start(shim_api(),shim_on()));d.set_owner(&o);
     step(d,live(0,1),3000);const auto n=F.log.size();d.shutdown();
     CHECK((since(n)==Lines{"Owner end_open 0"})&&d.phase()==Phase::Stopped);}

    // The library gate (romi 32320): a sample that cannot enter it makes no library call at all; a release that cannot
    // enter it is retained (no PanicStop or FreeDirectInput, the owner stops reading through the library but does not
    // take the wheel back) and completes at the next shutdown, which does not wait either.
    {FakeOwner o;Delivery d=owned(o);
     step(d,live(10,2),2500);CHECK(F.wheel==2500);F.log.clear();
     o.busy=true;
     step(d,live(400,2),2500);                                                  // the hold would be due: still no call
     CHECK(F.log.empty()&&d.phase()==Phase::Open);
     o.busy=false;step(d,live(420,2),2500);
     CHECK((F.log==Lines{"SetDeviceForcesXY 0 0","SetDeviceForcesXY 2500 0"}));   // the expired hold restarts through a zero
     F.log.clear();o.busy=true;
     d.shutdown();
     CHECK(d.retained()&&d.phase()==Phase::Stopped&&(F.log==Lines{"Owner begin_release"}));
     CHECK(d.why().find("hold watchdog")!=std::string::npos);
     F.log.clear();d.shutdown();CHECK(d.retained()&&F.log.empty());           // still busy: again nothing, no wait
     o.busy=false;d.shutdown();invariant(d);
     CHECK(!d.retained()&&(F.log==Lines{"PanicStop","Owner begin_release","FreeDirectInput","Owner end_release"}));
     CHECK(!o.entered);}
    // A release inside a sample (an epoch change) holds the gate already: it completes in that sample.
    {FakeOwner o;Delivery d=owned(o);step(d,live(10,2),2500);F.log.clear();
     step(d,live(20,3),2500);
     CHECK(d.phase()==Phase::Released&&!d.retained()&&(F.log==Lines{"PanicStop","Owner begin_release","FreeDirectInput","Owner end_release"}));
     CHECK(!o.entered);}

    // set_owner only before the open; native (no owner) never sees an owner call.
    {FakeOwner o,late;Delivery d=owned(o);d.set_owner(&late);
     step(d,live(10,3),2500);CHECK(d.phase()==Phase::Released);}                 // the first owner's epoch rule still applies
    {Delivery d=opened();step(d,race(10),2500);step(d,race(20),2500);finish(d);
     for(const auto& s:F.log)CHECK(s.rfind("Owner",0)!=0);}

    std::printf("ffb delivery: %u checks\n",checks);
    return 0;
}
