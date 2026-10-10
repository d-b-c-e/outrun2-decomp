// Windows desktop build: the Xbox port's Direct3D 11, XAudio2 and gamepad
// layers (xbox/source) as a desktop game. A borderless window over the
// primary monitor (--windowed: 1280x720), and wheels, pedals and shifters
// through DirectInput (wheel_input.hpp). The executable goes in the game
// folder, like the original one; settings in OutRunDesktop.ini beside it.
#include "native_window.hpp"
#include "desktop_settings.hpp"
#include "wheel_input.hpp"
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
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);   // real pixels for the borderless window
    winrt::init_apartment();   // Windows.Gaming.Input
    const auto folder=outrun::xbox_runtime::launch_folder();
    const auto log=folder+"/runtime.log";
    if(std::freopen(log.c_str(),"w",stdout))std::setvbuf(stdout,nullptr,_IONBF,0);
    if(std::freopen(log.c_str(),"a",stderr))std::setvbuf(stderr,nullptr,_IONBF,0);
    std::vector<std::string> args;int count=0;
    if(wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&count)){
        for(int i=0;i<count;++i)args.push_back(outrun::xbox_runtime::utf8(argv[i]));LocalFree(argv);}
    const auto settings=outrun::desktop::load_desktop_settings(folder);
    bool borderless=settings.flag("Window","WindowedBorderless",true);
    for(const auto& a:args)if(a=="--windowed")borderless=false;
    outrun::xbox_runtime::set_borderless(borderless);
    outrun::desktop::wheel_input_configure(settings);
    outrun::xbox_runtime::input_filter()=outrun::desktop::wheel_input_filter;
    std::vector<char*> argv;for(auto& a:args)argv.push_back(a.data());argv.push_back(nullptr);
    const int result=or2_xbox_main(int(args.size()),argv.data());
    outrun::desktop::wheel_input_shutdown();
    return result;
}
