// The F4 delivery gates (source/ffb_device) against a fake WheelFfb library: what reaches the library, in which order,
// and that nothing reaches it when a gate is closed. No device, no DLL.
#define DIRECTINPUT_VERSION 0x0800
#include "ffb_device.hpp"
#include <dinput.h>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>
using namespace outrun::desktop::ffbdev;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"ffb delivery line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
const std::string Steer="{d71b8350-61b7-11f1-8001-444553540000}";      // the MOZA R12 Base instance of the rig logs
const std::string Other="{d9b1d890-6f3f-11f1-8002-444553540000}";
struct Fake {
    std::vector<std::string> log;
    int version=1000,init=1;
    std::deque<int> forces;          // results of SetDeviceForcesXY in order (1 when empty)
    long hr=0;                       // what GetLastHResult returns
    std::vector<std::string> devices{Steer};
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
void __cdecl Hold(int ms){note("SetHoldTimeoutMs "+std::to_string(ms));}
void __cdecl Preferred(const char* n){note(std::string("SetPreferredDevice ")+(n?n:"null"));}
void __cdecl PreferredIndex(int i){note("SetPreferredDeviceIndex "+std::to_string(i));}
void __cdecl PreferredGuid(const void* g){note("SetPreferredDeviceGuid "+text(*static_cast<const GUID*>(g)));}
void __cdecl Strict(int on){note("SetStrictDeviceSelection "+std::to_string(on));}
int __cdecl Enumerate(){note("EnumerateDevices");return int(F.devices.size());}
int __cdecl DeviceGuid(int i,void* g){
    if(i<0||i>=int(F.devices.size()))return 0;
    *static_cast<GUID*>(g)=guid_of(F.devices[std::size_t(i)]);return 1;
}
int __cdecl Init(int hwnd){note("InitDirectInput "+std::to_string(hwnd));return F.init;}
int __cdecl Guards(){note("InstallExitGuards");return 1;}
int __cdecl Forces(int x,int y){
    note("SetDeviceForcesXY "+std::to_string(x)+" "+std::to_string(y));
    if(F.forces.empty())return 1;
    const int r=F.forces.front();F.forces.pop_front();return r;
}
int __cdecl LastHr(){return int(F.hr);}
void __cdecl Zero(){note("ZeroForces");}
void __cdecl Panic(){note("PanicStop");}
void __cdecl Free(){note("FreeDirectInput");}
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
// The log from entry `from` on.
std::vector<std::string> since(std::size_t from){return {F.log.begin()+std::ptrdiff_t(from),F.log.end()};}
bool device_calls(const std::vector<std::string>& v){
    for(const auto& s:v)if(s.rfind("InitDirectInput",0)==0||s.rfind("SetDeviceForcesXY",0)==0||s.rfind("ZeroForces",0)==0||
        s.rfind("PanicStop",0)==0||s.rfind("FreeDirectInput",0)==0||s.rfind("InstallExitGuards",0)==0)return true;
    return false;
}
using Lines=std::vector<std::string>;
const Lines OpenSequence{"SetPreferredDevice null","SetPreferredDeviceIndex -1","SetPreferredDeviceGuid "+Steer,
    "SetStrictDeviceSelection 1","EnumerateDevices","InitDirectInput 4660","InstallExitGuards","SetDeviceForcesXY 0 0"};
// A started delivery opened on the first race sample; the log is cleared after the open.
Delivery opened(){
    reset();Delivery d;CHECK(d.start(api(),on()));d.frame(race(0),0);
    CHECK(d.phase()==Phase::Open);F.log.clear();
    return d;
}
}
int main(){
    // GUID text: braces, dashes, hex; nothing else.
    {GUID g{};CHECK(parse_guid(Steer,g)&&text(g)==Steer);
     CHECK(parse_guid("{D71B8350-61B7-11F1-8001-444553540000}",g)&&text(g)==Steer);
     for(const char* bad:{"","d71b8350-61b7-11f1-8001-444553540000","{d71b8350-61b7-11f1-8001-44455354000}","{d71b8350-61b7-11f1-8001-4445535400000}",
                          "{d71b8350x61b7-11f1-8001-444553540000}","{g71b8350-61b7-11f1-8001-444553540000}"})CHECK(!parse_guid(bad,g));}

    // load: off by default; mute, latched and an unbound steer refuse before anything is loaded.
    {Config c;CHECK(!load_refusal(c).empty());
     c=on();CHECK(load_refusal(c).empty());
     c=on();c.muted=true;CHECK(load_refusal(c)=="DBCE_FFB_MUTE=1");
     c=on();c.latched=true;CHECK(load_refusal(c)=="a latched (test) process");
     c=on();c.steer_dev.clear();CHECK(!load_refusal(c).empty());
     c=on();c.steer_dev="MOZA R12 Base";CHECK(!load_refusal(c).empty());
     // A start under a refusal calls nothing, and frames stay silent.
     reset();Delivery d;c=on();c.muted=true;CHECK(!d.start(api(),c)&&d.phase()==Phase::Off&&F.log.empty());
     d.frame(race(0),5000);d.shutdown();CHECK(F.log.empty());}

    // start: every export used, a 0.9.0 or newer library, then the hold watchdog; no device.
    {reset();Delivery d;CHECK(d.start(api(),on())&&d.phase()==Phase::Waiting);
     CHECK((F.log==Lines{"GetWheelFfbVersion","SetHoldTimeoutMs 250"}));
     reset();WheelFfbApi a=api();a.ZeroForces=nullptr;CHECK(!d.start(a,on())&&d.phase()==Phase::Released&&d.why()=="WheelFfb.dll has no ZeroForces");
     CHECK(F.log.empty());
     reset();F.version=899;CHECK(!d.start(api(),on())&&d.phase()==Phase::Released&&!device_calls(F.log));
     d.frame(race(0),5000);CHECK(!device_calls(F.log));}

    // open: only in the race, in front, with the input layer reading the configured instance; then the exact order.
    {reset();Delivery d;CHECK(d.start(api(),on()));const auto base=F.log.size();
     d.frame(menu(0),0);d.frame(race(1,false),3000);d.frame(race(2,true,""),3000);d.frame(race(3,true,Other),3000);
     CHECK(d.phase()==Phase::Waiting&&since(base).empty());
     d.frame(race(4),3000);
     Lines want=OpenSequence;want.push_back("SetDeviceForcesXY 3000 0");
     CHECK(since(base)==want&&d.phase()==Phase::Open);
     CHECK(d.counts().sent==1&&d.counts().zeroes==1);}

    // open refusals: not a force-feedback device, a refused open, a refused starting zero: released, never a command.
    {reset();F.devices={Other};Delivery d;CHECK(d.start(api(),on()));d.frame(race(0),3000);
     CHECK(d.phase()==Phase::Released&&!device_calls(F.log));
     d.frame(race(1),3000);CHECK(!device_calls(F.log));}
    {reset();F.init=0;F.hr=DIERR_NOTEXCLUSIVEACQUIRED;Delivery d;CHECK(d.start(api(),on()));d.frame(race(0),3000);
     CHECK(d.phase()==Phase::Released);
     CHECK(F.log.back()=="FreeDirectInput"&&F.log[F.log.size()-2]=="ZeroForces");   // a refused open is freed
     for(const auto& s:F.log)CHECK(s.rfind("SetDeviceForcesXY",0)!=0);
     const auto n=F.log.size();d.frame(race(1),3000);d.shutdown();CHECK(F.log.size()==n);}   // released: final
    {reset();F.forces={0};F.hr=DIERR_INPUTLOST;Delivery d;CHECK(d.start(api(),on()));d.frame(race(0),3000);
     CHECK(d.phase()==Phase::Released);   // even a transient refusal of the starting zero is a failed open
     int nonzero=0;for(const auto& s:F.log)if(s.rfind("SetDeviceForcesXY",0)==0&&s!="SetDeviceForcesXY 0 0")++nonzero;
     CHECK(nonzero==0&&F.log.back()=="FreeDirectInput");}

    // force: every race sample sends the command (the held value too: the hold watchdog needs the refresh), clamped.
    {Delivery d=opened();
     d.frame(race(10),2500);d.frame(race(20),2500);d.frame(race(30),20000);d.frame(race(40),-20000);
     CHECK((F.log==Lines{"SetDeviceForcesXY 2500 0","SetDeviceForcesXY 2500 0","SetDeviceForcesXY 10000 0","SetDeviceForcesXY -10000 0"}));}

    // out of the race (pause, menus): one zero, then nothing; back in, an accepted zero before the command.
    {Delivery d=opened();
     d.frame(race(10),2500);d.frame(menu(20),2500);d.frame(menu(30),2500);d.frame(race(40),2600);
     CHECK((F.log==Lines{"SetDeviceForcesXY 2500 0","ZeroForces","SetDeviceForcesXY 0 0","SetDeviceForcesXY 2600 0"}));
     // already silent: no ZeroForces, but still the accepted zero on return
     F.log.clear();d.frame(race(50),0);d.frame(menu(60),0);d.frame(race(70),100);
     CHECK((F.log==Lines{"SetDeviceForcesXY 0 0","SetDeviceForcesXY 0 0","SetDeviceForcesXY 100 0"}));}

    // out of front: the same; nothing at all while behind another window.
    {Delivery d=opened();
     d.frame(race(10),2500);d.frame(race(20,false),2500);d.frame(race(30,false),2500);d.frame(race(40),2500);
     CHECK((F.log==Lines{"SetDeviceForcesXY 2500 0","ZeroForces","SetDeviceForcesXY 0 0","SetDeviceForcesXY 2500 0"}));}

    // a transient refusal: zero, then accepted zeros only; the recovery clock counts only time in the race and in front.
    {Delivery d=opened();
     F.forces={0};F.hr=DIERR_NOTACQUIRED;
     d.frame(race(10),2500);CHECK(d.phase()==Phase::Recovering&&d.counts().transient==1);
     CHECK((F.log==Lines{"SetDeviceForcesXY 2500 0","ZeroForces"}));
     F.log.clear();F.forces={0,0};
     d.frame(race(20),2500);d.frame(race(1500),2500);       // two refused zeros, 1.48 s of the 2 s
     d.frame(race(1600,false),2500);d.frame(race(9000,false),2500);   // out of front: the clock stops and restarts
     d.frame(race(9010),2500);                               // accepted zero, then this sample's command
     CHECK((F.log==Lines{"SetDeviceForcesXY 0 0","ZeroForces","SetDeviceForcesXY 0 0","ZeroForces",
                         "SetDeviceForcesXY 0 0","SetDeviceForcesXY 2500 0"}));
     CHECK(d.phase()==Phase::Open&&d.counts().recoveries==1);}
    {Delivery d=opened();
     F.forces={0,0,0,0};F.hr=DIERR_NOTDOWNLOADED;
     d.frame(race(10),2500);d.frame(race(20),2500);d.frame(race(1000),2500);d.frame(race(2019),2500);
     CHECK(d.phase()==Phase::Recovering);
     F.log.clear();d.frame(race(2020),2500);                 // 2 s of refused zeros: released without another try
     CHECK(d.phase()==Phase::Released&&(F.log==Lines{"ZeroForces","FreeDirectInput"}));
     F.log.clear();d.frame(race(2030),2500);d.shutdown();CHECK(F.log.empty());}

    // any other refusal releases at once.
    {Delivery d=opened();
     F.forces={0};F.hr=DIERR_INCOMPLETEEFFECT;
     d.frame(race(10),2500);
     CHECK(d.phase()==Phase::Released&&(F.log==Lines{"SetDeviceForcesXY 2500 0","ZeroForces","FreeDirectInput"}));
     F.log.clear();d.frame(race(20),2500);CHECK(F.log.empty());}

    // one device epoch: the input layer losing or changing the steering device releases the open device.
    {Delivery d=opened();
     d.frame(race(10,true,""),2500);
     CHECK(d.phase()==Phase::Released&&(F.log==Lines{"ZeroForces","FreeDirectInput"}));}
    {Delivery d=opened();
     d.frame(menu(10),0);F.log.clear();
     Sample s=menu(20);s.steer_dev=Other;d.frame(s,0);       // also outside the race
     CHECK(d.phase()==Phase::Released&&(F.log==Lines{"ZeroForces","FreeDirectInput"}));}

    // shutdown: PanicStop then FreeDirectInput once for an opened device; nothing for one never opened.
    {Delivery d=opened();d.frame(race(10),2500);F.log.clear();
     d.shutdown();CHECK((F.log==Lines{"PanicStop","FreeDirectInput"})&&d.phase()==Phase::Stopped);
     F.log.clear();d.shutdown();d.frame(race(20),2500);CHECK(F.log.empty());}
    {reset();Delivery d;CHECK(d.start(api(),on()));F.log.clear();d.frame(menu(0),0);d.shutdown();
     CHECK(F.log.empty()&&d.phase()==Phase::Stopped);}

    std::printf("ffb delivery: %u checks\n",checks);
    return 0;
}
