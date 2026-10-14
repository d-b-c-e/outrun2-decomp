// F4b's load gates with the real ffb_live.cpp (romi 30155: muted, no DLL). A fake WheelFfb.dll beside this exe
// (tests/fake_wheelffb.cpp, its own output folder) counts its attaches and logs every call it receives into the process
// environment. Under the default, Output = none, DBCE_FFB_MUTE=1, a latched process, the shim input backend or no steer
// device the module must never attach; with Output = wheel it attaches, receives only GetWheelFfbVersion until a race
// sample, and is unloaded at ffb_shutdown. Stubs stand in for the game window, the input filter slot and the two input
// layers' steering devices only.
#include "ffb_live.hpp"
#include "ffb_shadow_live.hpp"
#include "native_window.hpp"
#include "shim_input.hpp"
#include "platform/runtime_input.hpp"
#include "platform/native_runtime.hpp"
#include "platform/car_producer_observer.hpp"
#include <windows.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <limits>
#include <memory>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
namespace outrun::xbox_runtime {
InputFilter& input_filter(){static InputFilter f=nullptr;return f;}
NativeWindow& native_window(){static NativeWindow w;return w;}
outrun::platform::NativeCarProducerObserver& car_producer_observer(){
    static outrun::platform::NativeCarProducerObserver o;return o;
}
}
namespace outrun::desktop {
std::string g_steer;   // what the wheel input layer reports as the connected steering device
std::string wheel_input_steer_device(){return g_steer;}
ShimSteer g_shim;      // the shim backend's steering wheel, as the test sets it
ShimSteer shim_input_steer(){return g_shim;}
bool g_panel_open=false;
bool desktop_panel_open(){return g_panel_open;}   // the F6 panel's open state, as the test sets it
}
namespace {
int checks=0;
void check(bool v,const char* what){++checks;if(!v){std::fprintf(stderr,"ffb wiring: FAIL: %s\n",what);std::exit(1);}}
std::wstring env(const wchar_t* name){
    wchar_t b[8192]{};const DWORD n=GetEnvironmentVariableW(name,b,8192);
    return n&&n<8192?std::wstring(b,n):std::wstring();
}
int attaches(){return _wtoi(env(L"FAKE_WHEELFFB_ATTACH").c_str());}
const std::string Wheel="{d71b8350-61b7-11f1-8001-444553540000}";
const std::string Controls="[Controls]\r\nSchema = 1\r\nProfile = Rig\r\nRevision = abc\r\n"
    "steer = axis 0 dev="+Wheel+" prod={0006346e-0000-0000-0000-504944564944} range=0..65535 rest=33265 travel=-1\r\n";
std::string folder(){
    char dir[MAX_PATH];GetTempPathA(MAX_PATH,dir);
    return std::string(dir)+"or2_ffb_wiring_"+std::to_string(GetCurrentProcessId());
}
outrun::desktop::Ini ini(const std::string& body){
    const std::string path=folder()+"\\settings.ini";
    {std::ofstream f(path,std::ios::binary);f<<body;}
    outrun::desktop::Ini i;i.load(path);return i;
}
// One configure + a non-race sample + shutdown; the attaches it caused and the calls the fake received.
struct Result{int attached;std::wstring calls;bool loaded_after;};
// The game reads its switches with getenv (the CRT's copy, set at process start); _putenv_s updates that copy and the
// process environment both.
Result run(const std::string& body,bool latched,const char* mute){
    outrun::xbox_runtime::input_filter()=nullptr;
    _putenv_s("DBCE_FFB_MUTE",mute?mute:"");
    _putenv_s("OR2_FFB_TRACE","");
    SetEnvironmentVariableW(L"FAKE_WHEELFFB_CALLS",nullptr);
    const int before=attaches();
    outrun::desktop::ffb_configure(ini(body),latched,latched?folder():std::string());
    outrun::desktop::ffb_wrap_input_filter();
    outrun::desktop::ffb_shadow_wrap_input_filter();
    if(auto f=outrun::xbox_runtime::input_filter()){outrun::platform::NativeInputState in{};f(in,nullptr);}   // a menu sample
    outrun::desktop::ffb_shutdown();
    outrun::xbox_runtime::input_filter()=nullptr;
    return {attaches()-before,env(L"FAKE_WHEELFFB_CALLS"),GetModuleHandleW(L"WheelFfb.dll")!=nullptr};
}
}
int main(){
    CreateDirectoryA(folder().c_str(),nullptr);
    outrun::desktop::g_steer=Wheel;
    // The fake is really beside this exe (else a "never attached" would prove nothing).
    {wchar_t exe[MAX_PATH];const DWORD n=GetModuleFileNameW(nullptr,exe,MAX_PATH);std::wstring p(exe,n);
     p=p.substr(0,p.find_last_of(L"\\/")+1)+L"WheelFfb.dll";
     check(GetFileAttributesW(p.c_str())!=INVALID_FILE_ATTRIBUTES,"the fake WheelFfb.dll is beside the test");}
    check(GetModuleHandleW(L"WheelFfb.dll")==nullptr,"not loaded at start");
    // The F6 next-start setting asks only whether the add-on is there: a file check that never loads it.
    check(outrun::desktop::ffb_library_present(),"the panel sees the force add-on beside the game");
    check(GetModuleHandleW(L"WheelFfb.dll")==nullptr,"asking whether the add-on is present does not load it");

    auto r=run(Controls,false,nullptr);
    check(r.attached==0&&r.calls.empty()&&!r.loaded_after,"no [FFB] Output key: WheelFfb.dll never attached");
    r=run(Controls+"[FFB]\r\nOutput = none\r\n",false,nullptr);
    check(r.attached==0&&r.calls.empty()&&!r.loaded_after,"Output = none: never attached");
    r=run(Controls+"[FFB]\r\nOutput = wheel\r\n",false,"1");
    check(r.attached==0&&r.calls.empty()&&!r.loaded_after,"Output = wheel under DBCE_FFB_MUTE=1: never attached");
    r=run(Controls+"[FFB]\r\nOutput = wheel\r\n",true,nullptr);
    check(r.attached==0&&r.calls.empty()&&!r.loaded_after,"Output = wheel in a latched process: never attached");
    r=run("[FFB]\r\nOutput = wheel\r\n",false,nullptr);
    check(r.attached==0&&r.calls.empty()&&!r.loaded_after,"Output = wheel without a steer device: never attached");
    r=run(Controls+"[FFB]\r\nOutput = wheel\r\nFFBSpringStrength = 99\r\n",false,nullptr);
    check(r.attached==0&&r.calls.empty()&&!r.loaded_after,"refused [FFB] settings: never attached");
    r=run(Controls+"[FFB]\r\nModel = outrun-producer@2\r\nOutput = wheel\r\n",false,nullptr);
    check(r.attached==1&&r.calls==L"GetWheelFfbVersion;GetWheelFfbVersion;"&&!r.loaded_after,
          "producer selection starts gated delivery but never opens a device outside the race");
    r=run(Controls+"[FFB]\r\nModel = outrun-producer@2\r\nOutput = none\r\n",false,nullptr);
    check(r.attached==0&&r.calls.empty()&&!r.loaded_after,"selected software model leaves native library unloaded by default");
    r=run(Controls+"[FFB]\r\nModel = outrun-producer@2\r\nOutput = wheel\r\n",false,"1");
    check(r.attached==0&&r.calls.empty()&&!r.loaded_after,"selected output obeys the process-wide mute before DLL load");
    r=run(Controls+"[FFB]\r\nModel = unrecognized\r\nOutput = wheel\r\n",false,nullptr);
    check(r.attached==0&&r.calls.empty()&&!r.loaded_after,"unknown model refuses before loading the DLL");
    r=run(Controls+"[FFB]\r\nModel = outrun-producer@2\r\nOutput = wheel\r\n[FFBProducer2]\r\nSteeringStrengthPercent = 101\r\n",false,nullptr);
    check(r.attached==0&&r.calls.empty()&&!r.loaded_after,"invalid producer gains refuse before loading the DLL");
    // One native observer feeds the selected producer and its optional CSV. The fake DLL records device calls;
    // no physical device is opened. A skipped callback must zero and re-prime before any new force.
    {
        outrun::xbox_runtime::input_filter()=nullptr;
        _putenv_s("DBCE_FFB_MUTE","");
        SetEnvironmentVariableW(L"FAKE_WHEELFFB_CALLS",nullptr);
        SetEnvironmentVariableW(L"FAKE_WHEELFFB_ACCEPT",L"{d71b8350-61b7-11f1-8001-444553540000}");
        outrun::desktop::ffb_front_check()=[](void*){return true;};
        outrun::xbox_runtime::native_window().hwnd=reinterpret_cast<void*>(std::intptr_t(0x1234));
        outrun::desktop::ffb_configure(ini(Controls+"[FFB]\r\nModel = outrun-producer@2\r\nOutput = wheel\r\nFFBGlobalStrength = 0\r\n"
            "[FFBProducer2]\r\nSteeringStrengthPercent = 25\r\nShiftStrengthPercent = 75\r\n"),false,{});
        check(outrun::desktop::ffb_output_running()&&outrun::xbox_runtime::car_producer_observer().observe,
              "selected model starts delivery and registers the car observer");
        const auto trace_name=folder()+"\\selected.csv";
        const std::wstring trace_path(trace_name.begin(),trace_name.end());
        check(outrun::desktop::ffb_shadow_configure(trace_path,true),"selected trace attaches to the same observer");
        outrun::desktop::ffb_wrap_input_filter();
        outrun::desktop::ffb_shadow_wrap_input_filter();
        auto rt=std::make_unique<outrun::platform::NativeRuntimeContext>();
        rt->mode_state.current=16;
        auto& state=rt->event_function36.car_select;
        state.race_inits=1;state.producer_active=true;
        auto& car=state.car_799d18;
        const auto put=[&](std::size_t at,const auto& v){std::memcpy(car.data()+at,&v,sizeof v);};
        put(0x202,std::int16_t(16384));put(0x1f8,72.0f);put(0x208,std::uint32_t(2));
        const auto forces=[]{std::vector<long> v;const auto c=env(L"FAKE_WHEELFFB_CALLS");const std::wstring k=L"SetDeviceForcesXY ";
            for(std::size_t at=c.find(k);at!=std::wstring::npos;at=c.find(k,at+1))v.push_back(std::wcstol(c.c_str()+at+k.size(),nullptr,10));return v;};
        std::uint64_t attempt=0;
        const auto tick=[&](bool callback=true){
            ++rt->frame_state.frame_counter_95af0c;
            outrun::platform::NativeInputState in{};outrun::xbox_runtime::input_filter()(in,rt.get());
            if(callback){
                outrun::platform::NativeCarProducerObservation o{};
                o.car=car.data();o.car_bytes=car.size();o.mode=16;o.car_generation=1;o.continuity=state.producer_continuity;
                o.attempt=++attempt;o.frame_counter=rt->frame_state.frame_counter_95af0c;
                o.car_active=o.producer_fresh=o.direct=true;
                state.producer_attempt=attempt;
                outrun::platform::notify_car_producer(outrun::xbox_runtime::car_producer_observer(),o);
            }
        };
        const auto gated_tick=[&](const char* what){
            const auto before=forces();
            tick(); // Direct, fresh producer callback still arrives after this gated input tick.
            const auto after=forces();
            bool all_zero=after.size()>=before.size()&&!after.empty()&&after.back()==0;
            for(std::size_t i=before.size();i<after.size();++i)all_zero=all_zero&&after[i]==0;
            check(all_zero,what);
        };
        tick();tick();tick();
        auto sent=forces();check(!sent.empty()&&sent.back()!=0,"primed producer reaches fake native with bounded force");
        outrun::desktop::ffb::producer2::Stream expected({25,75});
        outrun::desktop::ffb::producer2::Frame request{};
        request.mode=16;request.race_epoch=1;request.car_generation=1;request.producer_fresh=true;
        request.car=car.data();request.car_bytes=car.size();request.dt_seconds=1.0f/60.0f;
        request.source_tick=1;expected.step(request);request.source_tick=2;
        const auto row=expected.step(request);
        const auto direct=std::lround(std::clamp(row.shaped_force,-1.0f,1.0f)*10000.0f);
        check(sent.back()==direct,"selected request converts once to DirectInput units without legacy global gain");
        tick(false);tick(false);
        sent=forces();check(sent.back()==0,"missing callback sends zero without waiting for another valid car");
        tick();tick();
        sent=forces();check(sent.back()==0,"resumed callback primes at zero");
        tick();sent=forces();check(sent.back()!=0,"fresh consecutive callback resumes force after prime");
        state.producer_continuity=1;
        tick();sent=forces();check(sent.back()==0,"recovery gate zeros before another car callback");
        tick();sent=forces();check(sent.back()==0,"recovery callback neutralizes the next delivery");
        tick();sent=forces();check(sent.back()!=0,"force resumes after recovery prime");
        outrun::desktop::g_panel_open=true;
        for(int i=0;i<3;++i)gated_tick("every F6-open tick stays zero with producer callbacks present");
        outrun::desktop::g_panel_open=false;
        tick();sent=forces();check(sent.back()==0,"F6 close first tick stays zero");
        tick();sent=forces();check(sent.back()==0,"F6 close primes before force");
        tick();sent=forces();check(sent.back()!=0,"force resumes only after F6 prime");
        outrun::desktop::ffb_front_check()=[](void*){return false;};
        for(int i=0;i<3;++i)gated_tick("every unfocused tick stays zero with producer callbacks present");
        outrun::desktop::ffb_front_check()=[](void*){return true;};
        tick();sent=forces();check(sent.back()==0,"focus return first tick stays zero");
        tick();sent=forces();check(sent.back()==0,"focus return primes before force");
        tick();sent=forces();check(sent.back()!=0,"focus return resumes after prime");
        rt->mode_state.current=13;tick(false);sent=forces();check(sent.back()==0,"menu transition zeros selected force");
        rt->mode_state.current=16;tick();tick();sent=forces();check(sent.back()==0,"menu return primes before force");
        tick();sent=forces();check(sent.back()!=0,"menu return resumes after prime");
        rt->event_state.pause_depth=1;tick(false);sent=forces();check(sent.back()==0,"pause zeros selected force");
        rt->event_state.pause_depth=0;tick();tick();sent=forces();check(sent.back()==0,"unpause primes before force");
        tick();sent=forces();check(sent.back()!=0,"unpause resumes after prime");
        put(0x1f8,std::numeric_limits<float>::quiet_NaN());tick();tick(false);
        sent=forces();check(sent.back()==0,"invalid producer speed zeros held force");
        put(0x1f8,72.0f);tick();tick();sent=forces();check(sent.back()==0,"valid input after rejection primes first");
        tick();sent=forces();check(sent.back()!=0,"valid input resumes only after prime");
        outrun::desktop::ffb_stop_output();
        check(!outrun::desktop::ffb_output_running()&&
              env(L"FAKE_WHEELFFB_CALLS").find(L"PanicStop;FreeDirectInput;")!=std::wstring::npos,
              "Stop FFB quiesces selected delivery through the existing lifecycle");
        outrun::desktop::ffb_shadow_shutdown();outrun::desktop::ffb_shutdown();
        check(!GetModuleHandleW(L"WheelFfb.dll")&&
              !outrun::xbox_runtime::car_producer_observer().observe,"shutdown unloads and unregisters observer");
        std::ifstream trace(trace_name,std::ios::binary);std::string header;std::getline(trace,header);
        check(header.find("model=outrun-producer@2,output=selected-request")!=std::string::npos&&
              header.find("steering_percent=25,shift_percent=75")!=std::string::npos,
              "retained trace pins selected model and both gains");
        trace.close();DeleteFileA(trace_name.c_str());
        outrun::xbox_runtime::input_filter()=nullptr;
        SetEnvironmentVariableW(L"FAKE_WHEELFFB_ACCEPT",nullptr);
    }
    // The shim backend (INPUT-SHIM.md section 6): the library loads and its delivery waits, but it never opens the wheel
    // until the shim hands it over (ffb_set_shim_owner, from the shim's WheelFfb bridge): race ticks with a force, the
    // steering live and the fake accepting this very wheel reach no device call (no unattended exclusive open).
    {
        SetEnvironmentVariableW(L"FAKE_WHEELFFB_ACCEPT",L"{d71b8350-61b7-11f1-8001-444553540000}");
        _putenv_s("DBCE_FFB_MUTE","");_putenv_s("OR2_FFB_TRACE","");
        SetEnvironmentVariableW(L"FAKE_WHEELFFB_CALLS",nullptr);
        outrun::desktop::ffb_front_check()=[](void*){return true;};
        outrun::xbox_runtime::native_window().hwnd=reinterpret_cast<void*>(std::intptr_t(0x1234));
        const int before=attaches();
        const std::string prod="{0006346e-0000-0000-0000-504944564944}";
        outrun::desktop::g_shim={Wheel,prod,"live",true,true,2};
        outrun::desktop::ffb_configure(ini(Controls+"[Input]\r\nBackend = Shim\r\n[FFB]\r\nOutput = wheel\r\n"),false,std::string());
        outrun::desktop::ffb_wrap_input_filter();
        check(attaches()==before+1&&outrun::desktop::ffb_output_running(),"Backend = Shim, Output = wheel: the library loaded, the output started");
        check(outrun::desktop::ffb_shim_wheel_api()!=nullptr,"the shim's bridge is offered the library while the output waits");
        auto rt=std::make_unique<outrun::platform::NativeRuntimeContext>();
        rt->mode_state.current=16;
        auto& car=rt->event_function36.car_select.car_799d18;
        const auto putf=[&](std::size_t at,float v){std::memcpy(car.data()+at,&v,4);};
        putf(0x1c4,40.f);putf(0x1d0,0.4f);putf(0x1d4,0.02f);putf(0x264,6.f);putf(0x268,6.f);
        const auto filter=outrun::xbox_runtime::input_filter();
        for(int i=0;i<60;++i){outrun::platform::NativeInputState in{};filter(in,rt.get());}
        check(env(L"FAKE_WHEELFFB_CALLS")==L"GetWheelFfbVersion;GetWheelFfbVersion;","no owner: no device call in 60 race ticks (only the version at the start)");
        check(outrun::desktop::ffb_status_text().rfind("on: waits",0)==0,"status: on, waiting");
        outrun::desktop::ffb_shutdown();
        check(!GetModuleHandleW(L"WheelFfb.dll")&&outrun::desktop::ffb_shim_wheel_api()==nullptr,"unloaded at shutdown; no library offered");
        // Off with the shim backend: the reason, and the steering wheel as the shim reads it.
        outrun::desktop::ffb_configure(ini("[Input]\r\nBackend = Shim\r\n[FFB]\r\nOutput = wheel\r\n"),false,std::string());
        auto status=outrun::desktop::ffb_status_text();
        check(status.find("steer names no device")!=std::string::npos&&status.find("live, epoch 2")!=std::string::npos,"status: off with the reason and the shim's live wheel");
        outrun::desktop::g_shim={Wheel,prod,"read-failed",false,false,2};
        status=outrun::desktop::ffb_status_text();
        check(status.find("read-failed")!=std::string::npos,"status: the wheel's shim status when not open");
        outrun::desktop::ffb_shutdown();
        outrun::desktop::ffb_configure(ini(Controls+"[FFB]\r\nOutput = none\r\n"),false,std::string());
        check(outrun::desktop::ffb_status_text()=="off","Output = none: the plain off, no shim reason");
        outrun::desktop::ffb_shutdown();
        SetEnvironmentVariableW(L"FAKE_WHEELFFB_ACCEPT",nullptr);
        outrun::desktop::g_shim={};
    }
    // Positive control: the same exe and fake do attach when every gate allows it; only the version is asked, nothing
    // reaches a device outside the race, and the module is gone after shutdown.
    r=run(Controls+"[FFB]\r\nOutput = wheel\r\n",false,nullptr);
    check(r.attached==1,"Output = wheel, unmuted, ordinary: WheelFfb.dll attached once");
    check(r.calls==L"GetWheelFfbVersion;GetWheelFfbVersion;","only the version asked (start, start line); no device call outside the race");
    check(!r.loaded_after,"unloaded at ffb_shutdown");

    // romi 31299: the wheel output on (the fake accepts the steering device: a fake sink, no force), race ticks through
    // the real filter with a car state that makes a force, then the F6 panel opens: that same tick sends the zero, and
    // nothing more goes out while it is open; closed again, an accepted zero before the force resumes.
    {
        _putenv_s("DBCE_FFB_MUTE","");_putenv_s("OR2_FFB_TRACE","");
        SetEnvironmentVariableW(L"FAKE_WHEELFFB_CALLS",nullptr);
        SetEnvironmentVariableW(L"FAKE_WHEELFFB_ACCEPT",L"{d71b8350-61b7-11f1-8001-444553540000}");
        outrun::desktop::ffb_front_check()=[](void*){return true;};   // "in front" without taking the focus
        outrun::xbox_runtime::native_window().hwnd=reinterpret_cast<void*>(std::intptr_t(0x1234));
        outrun::desktop::ffb_configure(ini(Controls+"[FFB]\r\nOutput = wheel\r\n"),false,std::string());
        outrun::desktop::ffb_wrap_input_filter();
        auto rt=std::make_unique<outrun::platform::NativeRuntimeContext>();
        rt->mode_state.current=16;
        auto& car=rt->event_function36.car_select.car_799d18;
        const auto putf=[&](std::size_t at,float v){std::memcpy(car.data()+at,&v,4);};
        // Speed, steering, its rate and the lateral load (CarSignals: 1C4 / 1D0 / 1D4 / 264 / 268), in a long corner.
        putf(0x1c4,40.f);putf(0x1d0,0.4f);putf(0x1d4,0.02f);putf(0x264,6.f);putf(0x268,6.f);
        const auto filter=outrun::xbox_runtime::input_filter();
        check(filter!=nullptr,"the FFB filter is installed");
        const auto tick=[&]{outrun::platform::NativeInputState in{};filter(in,rt.get());};
        // The force commands the fake received so far, in order.
        const auto forces=[]{std::vector<long> v;const auto c=env(L"FAKE_WHEELFFB_CALLS");const std::wstring k=L"SetDeviceForcesXY ";
            for(std::size_t at=c.find(k);at!=std::wstring::npos;at=c.find(k,at+1))v.push_back(std::wcstol(c.c_str()+at+k.size(),nullptr,10));return v;};
        for(int i=0;i<90;++i)tick();
        auto sent=forces();
        bool force=false;for(const auto x:sent)if(x!=0)force=true;
        check(outrun::desktop::ffb_output_running()&&force,"the wheel output opened and a nonzero force went out");
        check(sent.back()!=0,"the last command before the panel opens is a force");
        outrun::desktop::g_panel_open=true;
        const auto before=sent.size();
        tick();
        sent=forces();
        check(sent.size()==before+1&&sent.back()==0,"opening the panel: that very tick sends exactly one command, a zero");
        for(int i=0;i<30;++i)tick();
        check(forces().size()==before+1,"while the panel is open nothing more is sent");
        outrun::desktop::g_panel_open=false;
        tick();
        sent=forces();
        check(sent.size()==before+3&&sent[before+1]==0&&sent[before+2]!=0,"closed again: an accepted zero, then the force");
        outrun::desktop::ffb_shutdown();
        check(GetModuleHandleW(L"WheelFfb.dll")==nullptr,"unloaded after shutdown");
        const auto calls=env(L"FAKE_WHEELFFB_CALLS");
        check(calls.find(L"PanicStop;FreeDirectInput;")!=std::wstring::npos,"shutdown: PanicStop then FreeDirectInput");
        SetEnvironmentVariableW(L"FAKE_WHEELFFB_ACCEPT",nullptr);
    }

    // The shim's owner may still have a call inside the library (a worker preparing the read slot, romi 32222): the
    // library stays loaded after the shutdown, never freed under a call; once quiet it is unloaded as before.
    {
        struct Owner final:outrun::desktop::ffbdev::Owner {
            bool quiet=false;
            bool begin_open()override{return false;}
            void end_open(bool)override{}
            void begin_release()override{}
            void end_release()override{}
            bool library_quiet()const override{return quiet;}
            bool enter_library()override{return true;}
            void leave_library()override{}
        };
        for(const bool quiet:{false,true}){
            Owner o;o.quiet=quiet;
            outrun::desktop::ffb_configure(ini(Controls+"[Input]\r\nBackend = Shim\r\n[FFB]\r\nOutput = wheel\r\n"),false,std::string());
            outrun::desktop::ffb_set_shim_owner(&o);
            outrun::desktop::ffb_shutdown();
            const bool loaded=GetModuleHandleW(L"WheelFfb.dll")!=nullptr;
            check(quiet?!loaded:loaded,quiet?"the owner quiet: unloaded at shutdown":"the owner not quiet: WheelFfb.dll stays loaded");
            if(loaded)FreeLibrary(GetModuleHandleW(L"WheelFfb.dll"));   // the test's own clean-up
        }
    }
    // Stop FFB (F8) while the shim's worker is inside the library (romi 32320/32343): the stop is retained, nothing is
    // asked of the library, the status says stopping and the library stays loaded; the exit completes the stop
    // (PanicStop, FreeDirectInput) and unloads it.
    {
        struct Owner final:outrun::desktop::ffbdev::Owner {
            bool busy=false;
            bool begin_open()override{return true;}
            void end_open(bool)override{}
            void begin_release()override{}
            void end_release()override{}
            bool library_quiet()const override{return !busy;}
            bool enter_library()override{return !busy;}
            void leave_library()override{}
        };
        SetEnvironmentVariableW(L"FAKE_WHEELFFB_ACCEPT",L"{d71b8350-61b7-11f1-8001-444553540000}");
        SetEnvironmentVariableW(L"FAKE_WHEELFFB_CALLS",nullptr);
        const std::string prod="{0006346e-0000-0000-0000-504944564944}";
        outrun::desktop::g_shim={Wheel,prod,"live",true,true,1};
        outrun::desktop::ffb_configure(ini(Controls+"[Input]\r\nBackend = Shim\r\n[FFB]\r\nOutput = wheel\r\n"),false,std::string());
        outrun::desktop::ffb_wrap_input_filter();
        Owner o;outrun::desktop::ffb_set_shim_owner(&o);
        auto rt=std::make_unique<outrun::platform::NativeRuntimeContext>();
        rt->mode_state.current=16;
        auto& car=rt->event_function36.car_select.car_799d18;
        const auto putf=[&](std::size_t at,float v){std::memcpy(car.data()+at,&v,4);};
        putf(0x1c4,40.f);putf(0x1d0,0.4f);putf(0x1d4,0.02f);putf(0x264,6.f);putf(0x268,6.f);
        const auto filter=outrun::xbox_runtime::input_filter();
        const auto tick=[&]{outrun::platform::NativeInputState in{};filter(in,rt.get());};
        tick();                                                // the open, through the owner's hand-over
        outrun::desktop::g_shim.epoch=2;                       // the steering live again through the force handle
        for(int i=0;i<60;++i)tick();
        check(env(L"FAKE_WHEELFFB_CALLS").find(L"InitDirectInput")!=std::wstring::npos,"the output opened with the owner");
        o.busy=true;
        const auto before=env(L"FAKE_WHEELFFB_CALLS");
        outrun::desktop::ffb_stop_output();
        check(env(L"FAKE_WHEELFFB_CALLS")==before,"F8 with the worker inside: no library call");
        check(outrun::desktop::ffb_status_text().rfind("stopping",0)==0,"the status says stopping, not stopped");
        check(GetModuleHandleW(L"WheelFfb.dll")!=nullptr,"the library stays loaded");
        o.busy=false;
        outrun::desktop::ffb_shutdown();
        check(env(L"FAKE_WHEELFFB_CALLS").find(L"PanicStop;FreeDirectInput;",before.size())!=std::wstring::npos,"the exit completes the stop");
        check(GetModuleHandleW(L"WheelFfb.dll")==nullptr,"and unloads the library");
        SetEnvironmentVariableW(L"FAKE_WHEELFFB_ACCEPT",nullptr);
        outrun::desktop::g_shim={};
    }

    _putenv_s("DBCE_FFB_MUTE","");
    DeleteFileA((folder()+"\\settings.ini").c_str());DeleteFileA((folder()+"\\ffb-trace.csv").c_str());RemoveDirectoryA(folder().c_str());
    std::printf("ffb wiring: %d checks passed\n",checks);
    return 0;
}
