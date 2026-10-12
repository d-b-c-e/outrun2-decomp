// F4b's load gates with the real ffb_live.cpp (romi 30155: muted, no DLL). A fake WheelFfb.dll beside this exe
// (tests/fake_wheelffb.cpp, its own output folder) counts its attaches and logs every call it receives into the process
// environment. Under the default, Output = none, DBCE_FFB_MUTE=1, a latched process or no steer device the module must
// never attach; with Output = wheel it attaches, receives only GetWheelFfbVersion until a race sample, and is unloaded at
// ffb_shutdown. Stubs stand in for the game window, the input filter slot and the wheel input layer only.
#include "ffb_live.hpp"
#include "native_window.hpp"
#include "platform/runtime_input.hpp"
#include "platform/native_runtime.hpp"
#include <windows.h>
#include <cstring>
#include <memory>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
namespace outrun::xbox_runtime {
InputFilter& input_filter(){static InputFilter f=nullptr;return f;}
NativeWindow& native_window(){static NativeWindow w;return w;}
}
namespace outrun::desktop {
std::string g_steer;   // what the wheel input layer reports as the connected steering device
std::string wheel_input_steer_device(){return g_steer;}
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
    _putenv_s("DBCE_FFB_MUTE",mute?mute:"");
    _putenv_s("OR2_FFB_TRACE","");
    SetEnvironmentVariableW(L"FAKE_WHEELFFB_CALLS",nullptr);
    const int before=attaches();
    outrun::desktop::ffb_configure(ini(body),latched,latched?folder():std::string());
    outrun::desktop::ffb_wrap_input_filter();
    if(auto f=outrun::xbox_runtime::input_filter()){outrun::platform::NativeInputState in{};f(in,nullptr);}   // a menu sample
    outrun::desktop::ffb_shutdown();
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

    _putenv_s("DBCE_FFB_MUTE","");
    DeleteFileA((folder()+"\\settings.ini").c_str());DeleteFileA((folder()+"\\ffb-trace.csv").c_str());RemoveDirectoryA(folder().c_str());
    std::printf("ffb wiring: %d checks passed\n",checks);
    return 0;
}
