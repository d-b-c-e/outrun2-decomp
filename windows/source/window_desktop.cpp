// Windows desktop build: the Xbox port's Direct3D 11, XAudio2 and gamepad
// layers (xbox/source) as a desktop game. A borderless window over the
// primary monitor (--windowed: 1280x720), and wheels, pedals and shifters
// through DirectInput (wheel_input.hpp). The executable goes in the game
// folder, like the original one; settings in OutRunDesktop.ini beside it.
#include "native_window.hpp"
#include "d3d11_display.hpp"
#include "telemetry_forza.hpp"
#include "ffb_live.hpp"
#include "desktop_settings.hpp"
#include "wheel_input.hpp"
#include "shim_input.hpp"
#include "test_fence.hpp"
#include "desktop_keyboard.hpp"
#include "platform/pc_input_devices.hpp"
#include <windows.h>
#include <shellapi.h>
#include <winrt/base.h>
#include <cstdio>
#include <string>
#include <vector>
namespace outrun::xbox_runtime {
namespace {
bool closed=false,borderless=true;
LRESULT CALLBACK window_proc(HWND hwnd,UINT message,WPARAM w,LPARAM l){
    if(message==WM_CLOSE){closed=true;return 0;}
    // The keyboard as DirectInput reports it (DIK = scan code, 0x80 for the extended keys); every key up when the
    // window loses the focus. Alt+F4 and the other system keys still reach DefWindowProc.
    if(message==WM_KEYDOWN||message==WM_SYSKEYDOWN||message==WM_KEYUP||message==WM_SYSKEYUP){
        const unsigned scan=(unsigned(l)>>16)&0xffu;
        if(scan)outrun::desktop::desktop_keys()[scan|((unsigned(l)>>24)&1u?0x80u:0u)]=message==WM_KEYDOWN||message==WM_SYSKEYDOWN?0x80u:0u;
    }
    if(message==WM_KILLFOCUS)outrun::desktop::desktop_keys().fill(0);
    if(message==WM_SETCURSOR&&borderless&&LOWORD(l)==HTCLIENT){SetCursor(nullptr);return TRUE;}
    return DefWindowProcW(hwnd,message,w,l);
}
std::string utf8(const std::wstring& w){
    if(w.empty())return {};const int n=WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);
    std::string s(std::size_t(n),'\0');WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),s.data(),n,nullptr,nullptr);return s;
}
}
NativeWindow& native_window(){
    static NativeWindow w;
    if(w.hwnd)return w;
    const HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW c{};c.lpfnWndProc=window_proc;c.hInstance=instance;c.lpszClassName=L"OutRunDesktop";c.hCursor=LoadCursor(nullptr,IDC_ARROW);
    c.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));
    RegisterClassW(&c);
    if(borderless){
        MONITORINFO m{};m.cbSize=sizeof m;
        GetMonitorInfoW(MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY),&m);
        const RECT r=m.rcMonitor;
        w.hwnd=CreateWindowW(L"OutRunDesktop",L"OutRun 2006 Coast 2 Coast",WS_POPUP|WS_VISIBLE,r.left,r.top,r.right-r.left,r.bottom-r.top,
                             nullptr,nullptr,instance,nullptr);
        w.width=unsigned(r.right-r.left);w.height=unsigned(r.bottom-r.top);
    }else{
        RECT r{0,0,1280,720};AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE);
        w.hwnd=CreateWindowW(L"OutRunDesktop",L"OutRun 2006 Coast 2 Coast",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,
                             r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,nullptr);
        w.width=1280;w.height=720;
    }
    SetForegroundWindow(static_cast<HWND>(w.hwnd));
    return w;
}
bool pump_window_events(){
    MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}
    return !closed;
}
std::string launch_folder(){
    std::wstring path(MAX_PATH,L'\0');
    const DWORD n=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));path.resize(n);
    const auto slash=path.find_last_of(L"\\/");return utf8(slash==std::wstring::npos?L".":path.substr(0,slash));
}
std::string local_state_folder(){return {};}
unsigned select_refresh_rate(unsigned){
    // A desktop window presents at the monitor's rate; the mode is never changed.
    MONITORINFOEXW info{};info.cbSize=sizeof info;
    if(!GetMonitorInfoW(MonitorFromWindow(static_cast<HWND>(native_window().hwnd),MONITOR_DEFAULTTOPRIMARY),&info))return 60;
    DEVMODEW mode{};mode.dmSize=sizeof mode;
    return EnumDisplaySettingsW(info.szDevice,ENUM_CURRENT_SETTINGS,&mode)&&mode.dmDisplayFrequency>1?unsigned(mode.dmDisplayFrequency):60u;
}
void set_borderless(bool on){borderless=on;}
}
// A crash writes its address and the return addresses on the stack (as
// offsets in the executable, for llvm-symbolizer with the PDB) to runtime.log.
static LONG WINAPI crash_report(EXCEPTION_POINTERS* e){
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::fprintf(stderr,"crash: exception %08lx at exe+0x%llx\n",e->ExceptionRecord->ExceptionCode,
        (unsigned long long)(reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress)-base));
    if(e->ExceptionRecord->NumberParameters>=2)std::fprintf(stderr,"  %s address %016llx\n",e->ExceptionRecord->ExceptionInformation[0]?"write":"read",
        (unsigned long long)e->ExceptionRecord->ExceptionInformation[1]);
    CONTEXT c=*e->ContextRecord;
    for(int depth=0;depth<24;++depth){
        DWORD64 image=0;auto* f=RtlLookupFunctionEntry(c.Rip,&image,nullptr);
        if(!f){c.Rip=*reinterpret_cast<DWORD64*>(c.Rsp);c.Rsp+=8;}
        else{void* data=nullptr;DWORD64 frame=0;RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,c.Rip,f,&c,&data,&frame,nullptr);}
        if(!c.Rip)break;
        std::fprintf(stderr,"  #%d %s+0x%llx\n",depth,image==base?"exe":"other",(unsigned long long)(c.Rip-(image?image:0)));
    }
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    SetUnhandledExceptionFilter(crash_report);
    // The test fence decides first, before anything writes (INPUT-SHIM.md section 15): a test-requested process gets
    // its own run folder as home (saves, options, logs), the game folder only read, no OR2_ switches; or it refuses.
    namespace fence=outrun::desktop::fence;
    const auto folder=outrun::xbox_runtime::launch_folder();
    const auto dbce=fence::dbce_folder();
    fence::Plan plan;
    std::vector<std::string> cleared;
    if(fence::latch_requested(dbce)){
        std::wstring game(folder.size(),L'\0');
        game.resize(std::size_t(MultiByteToWideChar(CP_UTF8,0,folder.data(),int(folder.size()),game.data(),int(game.size()))));
        plan=fence::prepare_run(dbce,game,GetCurrentProcessId());
        if(!plan.refusal.empty())return fence::write_refusal(plan,GetCurrentProcessId())?3:4;   // 4: refused, not recorded (a linked folder)
        cleared=fence::clear_development_switches();
        outrun::xbox_runtime::latched_home()=fence::utf8(plan.run_root);
        outrun::xbox_runtime::latched_game_root()=folder;   // application_home refuses any other data root
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);   // real pixels for the borderless window
    winrt::init_apartment();   // Windows.Gaming.Input
    const auto log=plan.latched?fence::utf8(fence::log_path(plan,{})):folder+"/runtime.log";   // the fixture tests log_path
    if(std::freopen(log.c_str(),"w",stdout))std::setvbuf(stdout,nullptr,_IONBF,0);
    if(std::freopen(log.c_str(),"a",stderr))std::setvbuf(stderr,nullptr,_IONBF,0);
    if(plan.latched){
        std::printf("TEST PROCESS: inject.on was present at start. Wheel input is off; saves, options and logs are in %s; the game "
                    "folder %s is only read; %zu OR2_ switch(es) cleared; %zu file(s) seeded (%llu bytes).\n",
                    fence::utf8(plan.run_root).c_str(),folder.c_str(),cleared.size(),plan.seeded.size(),(unsigned long long)plan.seeded_bytes);
    }
    std::vector<std::string> args;int count=0;
    if(wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&count)){
        for(int i=0;i<count;++i)args.push_back(outrun::xbox_runtime::utf8(argv[i]));LocalFree(argv);}
    const auto settings=outrun::desktop::load_desktop_settings(folder,!plan.latched);   // a latched process creates nothing
    bool borderless=settings.flag("Window","WindowedBorderless",true);
    for(const auto& a:args)if(a=="--windowed")borderless=false;
    outrun::xbox_runtime::set_borderless(borderless);
    // [Window] FrameLatency: frames queued ahead of the display. 2 by default (also when the key is absent): about 33 ms
    // less from Present to scan-out than the plain chain, with one frame of slack. 1 is the lowest latency, 0 the plain
    // chain as before. Exactly one digit 0..3; anything else is logged and 2 applies.
    {   const auto text=settings.text("Window","FrameLatency");unsigned frames=2u;
        if(text.size()==1u&&text[0]>='0'&&text[0]<='3')frames=unsigned(text[0]-'0');
        else if(!text.empty())std::printf("settings: [Window] FrameLatency = %.32s refused (0, 1, 2 or 3); 2 applies\n",text.c_str());
        // A latched (test) process keeps the plain chain: its tapes were recorded on it, and a display preference must not
        // reach the harness (nene 26924). OR2_FRAME_LATENCY still overrides for tests.
        if(plan.latched)frames=0u;
        outrun::xbox_runtime::set_frame_latency(frames);}
    // The 35a8aa3 wheel path can write OutRunDesktop.ini (its legacy import). A latched process runs without it until
    // the shim is wired (slice 3); the shim's own test path replaces it.
    if(!plan.latched){
        // Ordinary play: the game's own keyboard (the reconstructed PC keyboard device and its key tables), on either backend.
        outrun::platform::native_pc_keyboard_source()=[](std::uint8_t keys[256]){
            const auto& k=outrun::desktop::desktop_keys();std::copy(k.begin(),k.end(),keys);};
        if(outrun::desktop::shim_backend_selected(settings)){
            // [Input] Backend = Shim: the profile's devices through the shared shim (shim_input.hpp). wheel_input is never
            // configured, so no DirectInput device is opened twice; the pad and the keyboard keep their native paths.
            outrun::desktop::shim_input_configure_ordinary(settings,folder);
            outrun::xbox_runtime::input_filter()=outrun::desktop::shim_input_filter_ordinary;
        }else{
            outrun::desktop::wheel_input_configure(settings);
            outrun::xbox_runtime::input_filter()=outrun::desktop::wheel_input_filter;
        }
    }else{
        // The fenced test process: the input shim (Core, the control worker over inject.txt, the OutRun adapter).
        outrun::desktop::shim_input_configure(plan,settings);
        outrun::xbox_runtime::input_filter()=outrun::desktop::shim_input_filter;
    }
    // Forza telemetry and the mod's shared-memory block, one frame per tick after the input filter (telemetry_forza.hpp).
    // Off in a latched process; OR2_TELEMETRY=0 turns it off for unattended runs.
    outrun::desktop::telemetry_configure(settings,plan.latched);
    outrun::desktop::telemetry_wrap_input_filter();
    // Force feedback F2 (ffb_live.hpp): the software force stream (always in a latched run, into its run folder; with
    // OR2_FFB_TRACE in ordinary play); no device output at all.
    outrun::desktop::ffb_configure(settings,plan.latched,plan.latched?fence::utf8(plan.run_root):std::string());
    outrun::desktop::ffb_wrap_input_filter();
    std::vector<char*> argv;for(auto& a:args)argv.push_back(a.data());argv.push_back(nullptr);
    const int result=or2_xbox_main(int(args.size()),argv.data());
    outrun::desktop::telemetry_shutdown();
    outrun::desktop::ffb_shutdown();
    outrun::desktop::wheel_input_shutdown();
    if(plan.latched)outrun::desktop::shim_input_shutdown();   // close, final tick, host stop; unresolved parks (P21)
    else outrun::desktop::shim_input_shutdown_ordinary();     // the ordinary shim backend's final tick and stop; never parks
    return result;
}
