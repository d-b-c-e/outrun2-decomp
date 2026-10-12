// Windows desktop build: the Xbox port's Direct3D 11, XAudio2 and gamepad
// layers (xbox/source) as a desktop game. A borderless window over the
// primary monitor (--windowed: 1280x720), and wheels, pedals and shifters
// through DirectInput (wheel_input.hpp). The executable goes in the game
// folder, like the original one; settings in OutRunDesktop.ini beside it.
#include "native_window.hpp"
#include "d3d11_display.hpp"
#include "telemetry_forza.hpp"
#include "ffb_live.hpp"
#include "desktop_panel.hpp"
#include "display_fit.hpp"
#include "renderer.hpp"
#include "d3d11_d3d9.hpp"
#include "platform/pc_car_reflection.hpp"
#include "platform/pc_render_queue.hpp"
#include "platform/race_ghosts.hpp"
#include "platform/pc_pmt_loader.hpp"
#include "platform/pc_scene_renderer.hpp"
#include "platform/pc_screen.hpp"
#include "platform/race_draw_distance.hpp"
#include "platform/race_particles.hpp"
#include <cmath>
#include <fstream>
#include <iterator>
#include <algorithm>
#include "desktop_settings.hpp"
#include "triple_screens.hpp"
#include "triple_rig.hpp"
#include "wheel_input.hpp"
#include "shim_input.hpp"
#include "startup_state.hpp"
#include "test_fence.hpp"
#include <sstream>
#include "desktop_keyboard.hpp"
#include "platform/pc_input_devices.hpp"
#include <windows.h>
#include <shellapi.h>
#include <winrt/base.h>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>
namespace outrun::xbox_runtime {
namespace {
bool closed=false,borderless=true;
outrun::desktop::TripleScreens triple=outrun::desktop::TripleScreens::off;   // [Triple] Screens (set_triple_screens)
// [Triple] Views = Angled asked for (set_triple_projection), or the development switch OR2_TRIPLE_VIEWS=1 (cleared in
// test processes like every OR2_ switch). Both the lods.ini read and the window use this one request.
bool triple_angled_requested=false;
bool triple_latched=false;   // a latched (test) process: no [Triple] keys applied
outrun::desktop::TripleStatus triple_status;                                    // triple_screens_status()
// The [Triple] rig keys (triple_screens.hpp parse_triple_rig), read when Angled is asked for in a normal process; the
// toolkit's fixture rig (32-inch 16:9, eye 660 mm, sides 70 degrees, 8 mm gap) and MatchGameFov = true otherwise.
outrun::desktop::TripleRigSettings triple_rig;
// The three-view corrections (pc_screen.hpp g_pc_triple_corrections): that rig (the owner's 1500R curve approximated
// flat) matched to the projection the game used each time it changes (triple_rig.hpp triple_views). With MatchGameFov =
// false the race camera's projection is already the physical eye's, so the match is that eye.
bool triple_fixture_corrections(const float* projection,float* out){
    outrun::desktop::Mat4 p{};for(std::size_t k=0;k<16;++k)p[k]=double(projection[k]);
    outrun::desktop::TripleViews v{};
    if(!outrun::desktop::triple_views(p,outrun::desktop::triple_rig_from(triple_rig),v))return false;
    for(std::size_t i=0;i<3;++i)for(std::size_t k=0;k<16;++k)out[i*16+k]=float(v.correction[i][k]);
    return true;
}
BOOL CALLBACK add_monitor(HMONITOR h,HDC,LPRECT,LPARAM list){
    MONITORINFO i{};i.cbSize=sizeof i;
    if(GetMonitorInfoW(h,&i))reinterpret_cast<std::vector<outrun::desktop::ScreenRect>*>(list)->push_back(
        {i.rcMonitor.left,i.rcMonitor.top,i.rcMonitor.right,i.rcMonitor.bottom});
    return TRUE;
}
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
    namespace td=outrun::desktop;
    std::string span_note;
    if(borderless){
        MONITORINFO m{};m.cbSize=sizeof m;
        GetMonitorInfoW(MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY),&m);
        RECT r=m.rcMonitor;
        // Separate monitors (STD-015): the window over three equal side-by-side monitors, in physical pixels (the process
        // is per-monitor DPI aware; the left monitor's x is negative). No display mode or layout is ever changed.
        bool separate_span=false;   // Separate monitors found its three-monitor span
        if(triple==td::TripleScreens::separate){
            std::vector<td::ScreenRect> monitors;EnumDisplayMonitors(nullptr,nullptr,add_monitor,reinterpret_cast<LPARAM>(&monitors));
            td::ScreenRect span{};
            if(td::triple_span_from_monitors(monitors,span)){r={span.left,span.top,span.right,span.bottom};span_note="over three monitors";separate_span=true;}
            else span_note=std::string("refused: ")+td::triple_span_refusal(monitors)+" ("+std::to_string(monitors.size())+
                           " found); the primary monitor";
        }
        w.hwnd=CreateWindowW(L"OutRunDesktop",L"OutRun 2006 Coast 2 Coast",WS_POPUP|WS_VISIBLE,r.left,r.top,r.right-r.left,r.bottom-r.top,
                             nullptr,nullptr,instance,nullptr);
        w.width=unsigned(r.right-r.left);w.height=unsigned(r.bottom-r.top);
        if(span_note.empty())span_note="the primary monitor";
        // The planar span (step 2): wider than 16:9 and not Off gives one wide frustum over the whole window.
        outrun::platform::g_pc_wide_aspect=td::planar_aspect(triple,w.width,w.height);
        triple_status={triple,{r.left,r.top,r.right,r.bottom},span_note,outrun::platform::g_pc_wide_aspect};
        // [Triple] Views = Angled (or OR2_TRIPLE_VIEWS=1) turns a planar span into three projected views (TRIPLE-SCREENS.md
        // step 4, experimental): stage culling off while it is on.
        const char* refused=triple_angled_requested?td::triple_projection_refusal(triple_latched,true,triple,separate_span,
                                                                                   outrun::platform::g_pc_wide_aspect):nullptr;
        triple_status.requested=triple_angled_requested?td::TripleProjection::angled:td::TripleProjection::planar;
        triple_status.applied=triple_angled_requested&&!refused?td::TripleProjection::angled:td::TripleProjection::planar;
        triple_status.projection_note=refused?refused:"";
        if(refused)std::printf("triple screens: Angled views not applied: %s\n",refused);
        if(triple_status.applied==td::TripleProjection::angled){
            outrun::platform::g_pc_triple_corrections=triple_fixture_corrections;outrun::platform::render_disable_culling()=true;
            // The side panels look past 90 degrees, where the stage's section lists (the game's own forward visibility,
            // 44DE10) draw nothing behind the car and add objects beside it only a few sections on. Where [Graphics]
            // leaves a key at 0, the views union 64 sections ahead and 64 behind (td::triple_default_union). Tweaks'
            // caution for DrawDistanceBehind applies: some models show their back faces from behind.
            auto& dd=outrun::platform::draw_distance();
            const auto u=td::triple_default_union(dd.increase,dd.behind,std::getenv("OR2_TRIPLE_AHEAD"),std::getenv("OR2_TRIPLE_BEHIND"));
            dd.increase=u.ahead;dd.behind=u.behind;if(dd.active())outrun::platform::render_queues_extended()=true;
            // MatchGameFov = false: the race camera takes the physical eye's vertical FOV (the views' 16:9 screen) in race and
            // goal modes; the front end keeps the game's. True (the default): the eye follows the game's FOV, as before.
            char eye[160];
            // The views take each panel's height from its width and the 16:9 render (triple_views); a saved PanelHeightMm
            // that differs is named as not used, so arbitrary settings are not reported as exact geometry (sana 38280).
            const double used_h=triple_rig.panel_width_mm*9./16.;
            char height[120]="";
            if(std::fabs(triple_rig.panel_height_mm-used_h)>0.5)
                std::snprintf(height,sizeof height," (PanelHeightMm %.1f not used: the height follows the width and the 16:9 render)",
                              triple_rig.panel_height_mm);
            if(triple_rig.match_game_fov)std::snprintf(eye,sizeof eye,"eye matched to the game's FOV");
            else{const double fov=td::triple_physical_fov_y(triple_rig,16./9.);outrun::platform::triple_fov_y()=float(fov);
                std::snprintf(eye,sizeof eye,"eye %.0f mm (MatchGameFov = false: the race camera's vertical FOV %.1f deg)",
                              triple_rig.eye_distance_mm,fov*180./3.14159265358979323846);}
            std::printf("triple screens: %s; window %ux%u at (%ld, %ld), %s; THREE PROJECTED VIEWS ([Triple] Views = Angled, experimental): the rig "
                        "(panels %.1f x %.1f mm%s, sides %.1f deg, gap %.1f mm, %s), one game render replicated into three "
                        "viewports; stage culling off; stage sections drawn +%d ahead and %d behind the car; objects behind the eye "
                        "still culled; the HUD, menus and screen-space particles on the centre only\n",td::triple_screens_name(triple),
                        w.width,w.height,r.left,r.top,span_note.c_str(),triple_rig.panel_width_mm,used_h,height,
                        triple_rig.side_angle_deg,triple_rig.bezel_mm,eye,dd.increase,dd.behind);
        }else if(outrun::platform::g_pc_wide_aspect>0.f)
            std::printf("triple screens: %s; window %ux%u at (%ld, %ld), %s; planar view %.3f:1 over the window (one wide frustum, "
                        "not three views); 2D, menus and movies on the centre 16:9\n",td::triple_screens_name(triple),w.width,w.height,
                        r.left,r.top,span_note.c_str(),double(outrun::platform::g_pc_wide_aspect));
        else std::printf("triple screens: %s; window %ux%u at (%ld, %ld), %s; one 16:9 view, centred\n",td::triple_screens_name(triple),
                         w.width,w.height,r.left,r.top,span_note.c_str());
    }else{
        if(triple!=td::TripleScreens::off)std::printf("triple screens: %s needs the borderless window ([Window] WindowedBorderless, "
                                                      "no --windowed); one 16:9 view\n",td::triple_screens_name(triple));
        RECT r{0,0,1280,720};AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE);
        w.hwnd=CreateWindowW(L"OutRunDesktop",L"OutRun 2006 Coast 2 Coast",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,
                             r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,nullptr);
        w.width=1280;w.height=720;
        triple_status={triple,{},"a 1280x720 window (not borderless)",0.f};
        if(triple_angled_requested){triple_status.requested=outrun::desktop::TripleProjection::angled;
            triple_status.projection_note=outrun::desktop::triple_projection_refusal(triple_latched,false,triple,false,0.f);
            std::printf("triple screens: Angled views not applied: %s\n",triple_status.projection_note.c_str());}
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
void set_triple_screens(outrun::desktop::TripleScreens mode){triple=mode;}
void set_triple_projection(bool angled,bool latched){triple_angled_requested=angled;triple_latched=latched;}
}
const outrun::desktop::TripleStatus& outrun::desktop::triple_screens_status(){return outrun::xbox_runtime::triple_status;}
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
    // Play with Recording and playback (session_request.hpp): an ordinary process takes the one-shot session request
    // first, before anything writes. The owner's settings are only read here (the backend for a record request). An
    // admitted playback runs in its own home under its result folder, seeded from the recording's startup snapshot (saves
    // and options), offline and with the game folder only read, as a latched run's home; its log goes to the result
    // folder; the owner's SaveGame, options.ini and OutRunDesktop.ini are never written (sana 37135/37176).
    namespace session=outrun::desktop::session;
    session::Intake intake;
    if(!plan.latched){
        outrun::desktop::Ini owner;(void)owner.load(folder+"/OutRunDesktop.ini");   // absent: the Native backend
        intake=session::take(session::request_path(),std::filesystem::u8path(folder),outrun::desktop::shim_backend_selected(owner),
                             std::uint64_t(std::time(nullptr)));
    }
    // A refused playback or an unreadable request is never ordinary play, and leaves the owner's files alone (sana 37845):
    // it ends here, before the game folder's runtime.log is opened or a settings file made. Its reason is the status.json
    // in its own result folder, or the .refused-<pid>-<n> request kept beside the request file.
    if(intake.kind==session::Intake::Kind::Exit)return 4;
    const bool playback=intake.kind==session::Intake::Kind::Playback;
    if(playback){
        cleared=fence::clear_development_switches();
        outrun::xbox_runtime::latched_home()=session::utf8(intake.home);
        outrun::xbox_runtime::latched_game_root()=folder;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);   // real pixels for the borderless window
    winrt::init_apartment();   // Windows.Gaming.Input
    const auto log=plan.latched?fence::utf8(fence::log_path(plan,{})):   // the fixture tests log_path
                   playback?session::utf8(intake.request.result/L"runtime.log"):folder+"/runtime.log";
    if(std::freopen(log.c_str(),"w",stdout))std::setvbuf(stdout,nullptr,_IONBF,0);
    if(std::freopen(log.c_str(),"a",stderr))std::setvbuf(stderr,nullptr,_IONBF,0);
    if(plan.latched){
        std::printf("TEST PROCESS: inject.on was present at start. Wheel input is off; saves, options and logs are in %s; the game "
                    "folder %s is only read; %zu OR2_ switch(es) cleared; %zu file(s) seeded (%llu bytes).\n",
                    fence::utf8(plan.run_root).c_str(),folder.c_str(),cleared.size(),plan.seeded.size(),(unsigned long long)plan.seeded_bytes);
    }
    if(playback){
        std::printf("PLAYBACK PROCESS: request %s admitted (recording seal %.16s). Saves and options are in %s, seeded from the "
                    "recording's startup snapshot; the game folder %s is only read; the owner's settings are only read (this rig's "
                    "[Window] and [Triple]); %zu OR2_ switch(es) cleared; offline.\n",intake.request.request_id.c_str(),
                    intake.request.package_seal_sha256.c_str(),session::utf8(intake.home).c_str(),folder.c_str(),cleared.size());
    }
    std::vector<std::string> args;int count=0;
    if(wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&count)){
        for(int i=0;i<count;++i)args.push_back(outrun::xbox_runtime::utf8(argv[i]));LocalFree(argv);}
    // A latched process creates nothing; a playback process reads the recording's settings with this rig's presentation.
    outrun::desktop::Ini settings;
    if(playback){
        std::string owner;
        if(std::ifstream f(folder+"/OutRunDesktop.ini",std::ios::binary);f){std::ostringstream all;all<<f.rdbuf();owner=all.str();}
        settings.load_text(outrun::desktop::startup::playback_settings(intake.recorded_settings,owner),
                           session::utf8(intake.request.startup/L"OutRunDesktop.ini")+" with this rig's [Window] and [Triple]");
        std::printf("settings: %s%s\n",settings.path().c_str(),intake.has_recorded_settings?"":" (the recording had no settings file: defaults)");
    }else settings=outrun::desktop::load_desktop_settings(folder,!plan.latched);
    // The session request's outcome (taken above; a refused playback or an unreadable request already ended the process).
    if(!plan.latched){
        switch(intake.kind){
        case session::Intake::Kind::None:break;
        case session::Intake::Kind::Playback:
            std::printf("session: playback request %s admitted (tape %s)\n",intake.request.request_id.c_str(),session::utf8(intake.request.tape).c_str());
            break;
        case session::Intake::Kind::Record:
            std::printf("session: record request %s admitted\n",intake.request.request_id.c_str());
            outrun::desktop::shim_input_arm_recording(intake.request,intake.exe_sha256);
            break;
        case session::Intake::Kind::Refused:
            std::printf("session: record request refused (%s); ordinary play without a capture\n",intake.why.c_str());break;
        case session::Intake::Kind::Exit:return 4;   // ended above, before anything was written
        }
    }
    bool borderless=settings.flag("Window","WindowedBorderless",true);
    for(const auto& a:args)if(a=="--windowed")borderless=false;
    outrun::xbox_runtime::set_borderless(borderless);
    // [Triple] Screens (STD-022): Off (the default, also when the key is absent), Surround or Separate monitors, read at
    // start (the window opens once). Anything else is logged and Off applies. A latched (test) process keeps Off: a
    // display preference must not reach the harness, as with FrameLatency. windows/docs/TRIPLE-SCREENS.md.
    {   const auto text=settings.text("Triple","Screens");
        const auto mode=outrun::desktop::parse_triple_screens(text);
        if(mode&&!plan.latched)outrun::xbox_runtime::set_triple_screens(*mode);
        else if(mode&&*mode!=outrun::desktop::TripleScreens::off)std::printf("settings: [Triple] Screens = %s not applied in a test process; Off\n",
                                                                       outrun::desktop::triple_screens_name(*mode));
        else if(!mode&&!text.empty())std::printf("settings: [Triple] Screens = %.40s refused (Off, Surround or Separate monitors); Off applies\n",text.c_str());}
    // [Triple] Views (below the selector): Planar (the default, also when the key is absent) or Angled, three projected
    // views (experimental; the window applies it only over a span). Anything else is logged and Planar applies. The
    // development switch OR2_TRIPLE_VIEWS=1 asks for Angled too. One request for both its users (the lods.ini read below
    // and the window); a latched process records it but applies nothing (the status says why).
    {   const auto text=settings.text("Triple","Views");
        const auto projection=outrun::desktop::parse_triple_projection(text);
        if(!projection&&!text.empty())std::printf("settings: [Triple] Views = %.40s refused (Planar or Angled); Planar applies\n",text.c_str());
        const char* dev=std::getenv("OR2_TRIPLE_VIEWS");
        outrun::xbox_runtime::set_triple_projection(projection==outrun::desktop::TripleProjection::angled||(dev&&std::string(dev)=="1"),plan.latched);}
    // [Triple] PanelWidthMm / PanelHeightMm / EyeDistanceMm / SideAngle / BezelMm and MatchGameFov (the portfolio's rig
    // keys, as Wheelkit writes them): read when Angled is asked for in a normal process. A refused value is logged and its
    // default applies; the window logs the rig in use.
    if(outrun::xbox_runtime::triple_angled_requested&&!plan.latched){
        std::vector<std::string> notes;
        outrun::xbox_runtime::triple_rig=outrun::desktop::parse_triple_rig([&](const char* key){return settings.text("Triple",key);},notes);
        for(const auto& n:notes)std::printf("settings: [Triple] %s\n",n.c_str());
    }
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
    // [Graphics] AnisotropicFiltering, the Tweaks key: 0 (absent) or 1 keeps the game's own filtering, 2..16 the anisotropy
    // level of its linearly filtered textures. An integer 0..16; anything else is logged and 0 applies.
    {   const auto text=settings.text("Graphics","AnisotropicFiltering");unsigned level=0u;bool ok=text.empty();
        if(!text.empty()&&text.size()<=2u&&std::all_of(text.begin(),text.end(),[](char c){return c>='0'&&c<='9';})){
            level=unsigned(std::stoul(text));ok=level<=16u;if(!ok)level=0u;}
        if(!ok)std::printf("settings: [Graphics] AnisotropicFiltering = %.32s refused (0..16); 0 applies\n",text.c_str());
        if(level>1u)std::printf("settings: anisotropic filtering %ux\n",level);
        else if(level==1u)std::printf("settings: [Graphics] AnisotropicFiltering = 1 is the game's own filtering (1x)\n");
        outrun::xbox_runtime::set_anisotropic_filtering(level);}
    // [Graphics] ReflectionResolution, the Tweaks key: the car reflection cube's edge. Absent: the game's 128. A power of
    // two from 64 to 4096; anything else is logged and 128 applies.
    {   const auto text=settings.text("Graphics","ReflectionResolution");unsigned edge=128u;
        if(!text.empty()){
            unsigned v=0u;bool ok=text.size()<=4u&&std::all_of(text.begin(),text.end(),[](char c){return c>='0'&&c<='9';});
            if(ok){v=unsigned(std::stoul(text));ok=v>=64u&&v<=4096u&&(v&(v-1u))==0u;}
            if(ok)edge=v;else std::printf("settings: [Graphics] ReflectionResolution = %.32s refused (a power of two, 64..4096); 128 applies\n",text.c_str());
        }
        if(edge!=128u)std::printf("settings: car reflection %ux%u\n",edge,edge);
        outrun::platform::car_reflection_edge()=edge;}
    // [Graphics] TransparencySupersampling, the Tweaks key: alpha-to-coverage for alpha-tested edges under MSAA. Off when
    // absent.
    {   const bool on=settings.flag("Graphics","TransparencySupersampling",false);
        if(on)std::printf("settings: transparency supersampling (alpha-to-coverage under MSAA)\n");
        outrun::xbox_runtime::set_transparency_supersampling(on);}
    // [Graphics] DisableVehicleLODs / DisableStageCulling, the Tweaks keys: other cars always at their full model; every
    // stage node drawn whether in view or not (no pop-in, more draws). Off when absent.
    {   const bool lods=settings.flag("Graphics","DisableVehicleLODs",false),cull=settings.flag("Graphics","DisableStageCulling",false);
        if(lods)std::printf("settings: vehicle LODs disabled (other cars at full detail)\n");
        if(cull)std::printf("settings: stage culling disabled (every node drawn)\n");
        outrun::platform::other_car_lods_disabled()=lods;outrun::platform::render_disable_culling()=cull;}
    // [Graphics] DrawDistanceIncrease / DrawDistanceBehind, the Tweaks keys (sections of track, 0 = the game's own, the
    // default; at most 1024 / 256): more of the stage drawn ahead of / behind the car, less the exclusions of a lods.ini
    // in the game folder (OutRunDesktop.lods.ini, else OutRun2006Tweaks.lods.ini; read only). The render queues grow.
    {   auto& dd=outrun::platform::draw_distance();
        auto sections=[&](const char* key,int max){
            const auto t=settings.text("Graphics",key);if(t.empty())return 0;
            char* end=nullptr;const long v=std::strtol(t.c_str(),&end,10);
            if(end==t.c_str()||*end||v<0||v>max){std::printf("settings: [Graphics] %s = %.32s refused (0..%d); 0 applies\n",key,t.c_str(),max);return 0;}
            return int(v);};
        dd.increase=sections("DrawDistanceIncrease",1024);dd.behind=sections("DrawDistanceBehind",256);
        // Angled views may set sections ahead and behind later (native_window): their exclusions are read here too.
        const bool spike=outrun::xbox_runtime::triple_angled_requested&&!outrun::xbox_runtime::triple_latched;
        const char* from=nullptr;std::size_t excluded=0;
        if(dd.active()||spike)
            for(const char* name:{"OutRunDesktop.lods.ini","OutRun2006Tweaks.lods.ini"}){
                std::ifstream f(folder+"/"+name,std::ios::binary);if(!f)continue;
                const std::string text((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
                excluded=outrun::platform::draw_distance_read_exclusions(text,dd);from=name;break;}
        if(spike&&!dd.active())std::printf("settings: three-view spike: %zu excluded nodes for its sections ahead and behind (%s)\n",excluded,
            from?from:"no lods.ini");
        if(dd.active()){
            outrun::platform::render_queues_extended()=true;
            std::printf("settings: draw distance +%d sections ahead, %d behind; %zu excluded nodes (%s)\n",dd.increase,dd.behind,excluded,
                from?from:"no lods.ini");
        }}
    // [Graphics] CarBaseShadowOpacity, the Tweaks key (1.0 unless set; 0 = the PC's own, no base shadow): the car's flat
    // base shadow of O2SP / C2C Xbox restored (pc_screen.hpp). Like Tweaks, any finite value >= 0 (above 1 strengthens
    // it; nene 29993); a negative or non-finite value is refused.
    {   const float o=settings.number("Graphics","CarBaseShadowOpacity",1.0f);
        if(std::isfinite(o)&&o>=0.f)outrun::platform::g_pc_car_base_shadow_opacity=o;
        else std::printf("settings: [Graphics] CarBaseShadowOpacity refused (a finite value >= 0); 1 applies\n");
        if(outrun::platform::g_pc_car_base_shadow_opacity!=1.f)std::printf("settings: car base shadow opacity %.2f\n",double(outrun::platform::g_pc_car_base_shadow_opacity));}
    // [Bugfixes] FixParticleRendering, the Tweaks key (on unless set false, as there): the half-resolution particle
    // mapping of 418CF0 and the Metropolis fireworks object (pc_screen.hpp).
    outrun::platform::g_pc_fix_particle_rendering=settings.flag("Bugfixes","FixParticleRendering",true);
    if(!outrun::platform::g_pc_fix_particle_rendering)std::printf("settings: particle rendering fix off (the PC's own particle mapping)\n");
    // [Bugfixes] FixIncorrectShading and FixLensFlarePath, the Tweaks keys (on unless set false, as there): double-sided
    // lighting linked as the DOUBLE fragments, and the lens flare offsets read from Media\ (pc_screen.hpp).
    outrun::platform::g_pc_fix_incorrect_shading=settings.flag("Bugfixes","FixIncorrectShading",true);
    if(!outrun::platform::g_pc_fix_incorrect_shading)std::printf("settings: incorrect shading fix off (the TWOSIDE lighting fragments)\n");
    outrun::platform::g_pc_fix_lens_flare_path=settings.flag("Bugfixes","FixLensFlarePath",true);
    if(!outrun::platform::g_pc_fix_lens_flare_path)std::printf("settings: lens flare path fix off (common\\lens_flare_offset.bin only)\n");
    // [Graphics] UseHiDefCharacters, the Tweaks key (on unless set false, as there): the hi-def driver and heroine
    // character sets in gameplay (pc_screen.hpp).
    outrun::platform::g_pc_use_hidef_characters=settings.flag("Graphics","UseHiDefCharacters",true);
    if(!outrun::platform::g_pc_use_hidef_characters)std::printf("settings: hi-def characters off (the standard character sets)\n");
    // [Misc] RestoreJPClarissa, the Tweaks key (off unless set true, as there): Clarissa's Japanese model in O2SP arcade
    // mode and the arcade ending (pc_screen.hpp).
    outrun::platform::g_pc_restore_jp_clarissa=settings.flag("Misc","RestoreJPClarissa",false);
    if(outrun::platform::g_pc_restore_jp_clarissa)std::printf("settings: Clarissa's Japanese model restored (O2SP arcade and ending)\n");
    // [Graphics] UIScalingMode, the Tweaks key (windows/docs/UI-SCALING.md step 1; 2, keep centred, by default here until
    // Tweaks' 1 is built): 0 stretches the 2D over the 16:9 frame, or over a span's centre panel (display_fit.hpp).
    {   std::string note;
        outrun::xbox_runtime::g_ui_scaling_mode=outrun::xbox_runtime::parse_ui_scaling_mode(settings.text("Graphics","UIScalingMode"),note);
        if(!note.empty())std::printf("settings: [Graphics] %s\n",note.c_str());
        if(outrun::xbox_runtime::g_ui_scaling_mode==0u)
            std::printf("settings: UI scaling 0: the 2D stretched over the 16:9 frame (over the centre panel on a span)\n");}
    // [Graphics] ScreenEdgeCullFix, the Tweaks key (on unless set false, as there): course objects culled at the
    // widescreen edge rather than the 4:3 one.
    outrun::platform::g_pc_screen_edge_cull_fix=settings.flag("Graphics","ScreenEdgeCullFix",true);
    if(!outrun::platform::g_pc_screen_edge_cull_fix)std::printf("settings: screen edge cull fix off (objects culled at the 4:3 edge)\n");
    // [Graphics] FixZBufferPrecision, the Tweaks key: the race camera's near plane raised from 0.1 (1.0 in the chase and
    // bumper views) so far geometry stops z-fighting; the lens flare keeps 0.05. Off when absent.
    if(settings.flag("Graphics","FixZBufferPrecision",false)){
        outrun::platform::fix_znear()=true;std::printf("settings: z-buffer precision fix (race camera near plane by view)\n");}
    // [Graphics] SceneTextureReplacement / TextureBaseFolder, the Tweaks keys: stage, car and object textures replaced
    // from <game folder>/<TextureBaseFolder>/load in the Tweaks pack layout. Off when absent; the folder defaults to
    // "textures". A path that leaves the game folder is refused.
    {   const bool on=settings.flag("Graphics","SceneTextureReplacement",false);
        std::string base=settings.text("Graphics","TextureBaseFolder","textures");
        if(base.empty()||base.find("..")!=std::string::npos||base.find(':')!=std::string::npos||base[0]=='\\'||base[0]=='/'){
            if(on)std::printf("settings: [Graphics] TextureBaseFolder = %.64s refused (a folder inside the game folder); \"textures\" applies\n",base.c_str());
            base="textures";}
        outrun::platform::pmt_texture_replacement_configure(on,folder+"/"+base+"/load");}
    // The 35a8aa3 wheel path can write OutRunDesktop.ini (its legacy import). A latched process runs without it until
    // the shim is wired (slice 3); the shim's own test path replaces it.
    if(playback){
        // Playback (shim_input.hpp): no device and no keyboard source; the tape drives every tick. A tape that cannot
        // play never becomes ordinary play.
        if(!outrun::desktop::shim_input_configure_playback(intake,settings)){std::fflush(stdout);return 4;}
        outrun::xbox_runtime::input_filter()=outrun::desktop::shim_input_filter_playback;
    }else if(!plan.latched){
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
    // A playback process is as a latched one here: no telemetry, the software force stream into its home, no device
    // output, no saves; and its panel never opens (the badge only).
    const bool test_like=plan.latched||playback;
    const std::string private_home=plan.latched?fence::utf8(plan.run_root):playback?session::utf8(intake.home):std::string();
    outrun::desktop::telemetry_configure(settings,test_like);
    outrun::desktop::telemetry_wrap_input_filter();
    // Force feedback F2 (ffb_live.hpp): the software force stream (always in a latched run, into its run folder; with
    // OR2_FFB_TRACE in ordinary play); no device output at all.
    outrun::desktop::ffb_configure(settings,test_like,private_home);
    outrun::desktop::ffb_wrap_input_filter();
    // The F6 panel (desktop_panel.hpp, docs/F6-PANEL.md): the toolkit panel drawn at present; a test run saves nothing.
    outrun::desktop::desktop_panel_configure(settings,test_like,private_home,playback);
    outrun::desktop::desktop_panel_wrap_input_filter();   // outermost: a neutral sample while the panel is open
    std::vector<char*> argv;for(auto& a:args)argv.push_back(a.data());argv.push_back(nullptr);
    const int result=or2_xbox_main(int(args.size()),argv.data());
    outrun::desktop::desktop_panel_shutdown();
    outrun::desktop::telemetry_shutdown();
    outrun::desktop::ffb_shutdown();
    if(const auto& t=outrun::platform::pmt_texture_replacement_stats();t.files){
        std::printf("texture replacement: %u replaced of %u looked up (%u kept stock: unreadable, unusable or not created)\n",
            t.replaced,t.looked,t.failed);
        // Banks the pack covers, one line each; the others named once (a stage variant the pack lacks shows up here).
        std::string others;unsigned uncovered=0;
        for(const auto& [bank,b]:outrun::platform::pmt_texture_replacement_banks()){
            if(b.folder){std::printf("texture replacement: %s %u of %u replaced (%u kept stock)\n",bank.c_str(),b.replaced,b.looked,b.failed);continue;}
            ++uncovered;if(others.size()<400)others+=(others.empty()?"":", ")+bank;
        }
        if(uncovered)std::printf("texture replacement: %u banks without a pack folder: %s%s\n",uncovered,others.c_str(),others.size()>=400?", ...":"");
    }
    if(const auto& smoke=outrun::platform::tire_smoke_emit_stats();smoke.loops){
        // The record names the car and tire: 934448 + tire*5C (player), 9345B8 + (car-1)*170 + tire*5C (rivals).
        const auto r=smoke.longest_record;const bool player=r<0x9345b8u;
        const unsigned car=player?0u:(r-0x9345b8u)/0x170u+1u,tire=player?(r-0x934448u)/0x5cu:((r-0x9345b8u)%0x170u)/0x5cu;
        std::printf("particles: tire smoke emit loops %llu, longest %u iterations (%s, car %u tire %u, travel %.1f, frame %u), "
            "%llu ended early on a full source (%llu iterations)\n",static_cast<unsigned long long>(smoke.loops),smoke.longest,
            smoke.longest_function==0x41cb30u?"41CB30 player":"41CED0 rivals",car,tire,double(smoke.longest_moved),smoke.longest_frame,
            static_cast<unsigned long long>(smoke.full_tails),static_cast<unsigned long long>(smoke.skipped));}
    if(const auto dropped=outrun::platform::render_queue_dropped())
        std::printf("render queues: %llu draws dropped (queue full)\n",static_cast<unsigned long long>(dropped));
    outrun::desktop::wheel_input_shutdown();
    if(plan.latched)outrun::desktop::shim_input_shutdown();   // close, final tick, host stop; unresolved parks (P21)
    else if(playback)outrun::desktop::shim_input_shutdown_playback();   // the replay capture's stop and save
    else outrun::desktop::shim_input_shutdown_ordinary();     // the ordinary shim backend's final tick and stop; never parks
    return result;
}
