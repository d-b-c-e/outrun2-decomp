// F4b's load gates with the real ffb_live.cpp (romi 30155: muted, no DLL). A fake WheelFfb.dll beside this exe
// (tests/fake_wheelffb.cpp, its own output folder) counts its attaches and logs every call it receives into the process
// environment. Under the default, Output = none, DBCE_FFB_MUTE=1, a latched process or no steer device the module must
// never attach; with Output = wheel it attaches, receives only GetWheelFfbVersion until a race sample, and is unloaded at
// ffb_shutdown. Stubs stand in for the game window, the input filter slot and the wheel input layer only.
#include "ffb_live.hpp"
#include "native_window.hpp"
#include "platform/runtime_input.hpp"
#include <windows.h>
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

    _putenv_s("DBCE_FFB_MUTE","");
    DeleteFileA((folder()+"\\settings.ini").c_str());DeleteFileA((folder()+"\\ffb-trace.csv").c_str());RemoveDirectoryA(folder().c_str());
    std::printf("ffb wiring: %d checks passed\n",checks);
    return 0;
}
