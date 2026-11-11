// Windows desktop build: the Xbox port's Direct3D 11, XAudio2 and gamepad
// layers (xbox/source) as a desktop game. A borderless window over the
// primary monitor (--windowed: 1280x720), and wheels, pedals, shifters, the F6
// panel and telemetry through the Wheelkit runtime (wheelkit_runtime.hpp). The
// executable goes in the game folder, like the original one (or anywhere, with
// --data=<game folder>); settings in OutRunDesktop.ini beside it.
#include "native_window.hpp"
#include "d3d11_display.hpp"
#include "enhancements/fast_load.hpp"
#include "enhancements/general_presentation.hpp"
#include "gamepad_vibration_live.hpp"
#include "login_protection.hpp"
#include "wheelkit_runtime.hpp"
#include "camera_controls.hpp"
#include "hud_toggle.hpp"
#include "level_select_input.hpp"
#include "cd_switcher.hpp"
#include "display_fit.hpp"
#include "renderer.hpp"
#include "d3d11_d3d9.hpp"
#include "platform/pc_car_reflection.hpp"
#include "platform/pc_render_queue.hpp"
#include "platform/race_ghosts.hpp"
#include "platform/pc_pmt_loader.hpp"
#include "platform/pc_scene_renderer.hpp"
#include "platform/pc_screen.hpp"
#include "platform/pc_network.hpp"
#include "platform/race_draw_distance.hpp"
#include "platform/race_particles.hpp"
#include "platform/race_input.hpp"
#include "platform/music_stream.hpp"
#include <cmath>
#include <fstream>
#include <iterator>
#include <algorithm>
#include "desktop_settings.hpp"
#include "triple_screens.hpp"
#include "startup_monitor.hpp"
#include "startup_diagnostics.hpp"
#include "triple_rig.hpp"
#include "curve_scene_profile.hpp"
#include <memory>
#include <sstream>
#include "desktop_keyboard.hpp"
#include "platform/pc_input_devices.hpp"
#include <windows.h>
#include <shellapi.h>
#include <winrt/base.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>
namespace outrun::xbox_runtime {
namespace {
bool closed=false,borderless=true,hide_mouse_cursor=true;
outrun::desktop::TripleScreens triple=outrun::desktop::TripleScreens::off;   // [Triple] Screens (set_triple_screens)
// [Triple] Views = Angled asked for (set_triple_projection), or the development switch OR2_TRIPLE_VIEWS=1 (cleared in
// test processes like every OR2_ switch). Both the lods.ini read and the window use this one request.
bool triple_angled_requested=false;
bool triple_latched=false;   // a latched (test) process: no [Triple] keys applied
outrun::desktop::TripleStatus triple_status;                                    // triple_screens_status()
// The [Triple] rig keys (triple_screens.hpp parse_triple_rig), read when Angled is asked for in a normal process; the
// toolkit's fixture rig (32-inch 16:9, eye 660 mm, sides 70 degrees, 8 mm gap) and MatchGameFov = true otherwise.
outrun::desktop::TripleRigSettings triple_rig;
std::unique_ptr<const outrun::desktop::CurveSceneProfile> curve_profile;
bool curve_mesh_view(unsigned width,unsigned height,outrun::platform::PcCurveMeshView& out){return curve_profile&&outrun::desktop::curve_scene_mesh_view(*curve_profile,width,height,out);}

// The three-view corrections (pc_screen.hpp g_pc_triple_corrections): that rig (the owner's 1500R curve approximated
// flat) matched to the projection the game used each time it changes (triple_rig.hpp triple_views). With MatchGameFov =
// false the race camera's projection is already the physical eye's, so the match is that eye.
bool triple_fixture_corrections(const float* projection,float* out){
    if(curve_profile&&outrun::platform::pc_curve_active()&&outrun::platform::pc_comparison_curve()){
        if(outrun::desktop::curve_scene_corrections(*curve_profile,projection,out))return true;
        return false; // only an actual World draw may latch flat; frontend/culling projections can differ
    }
    outrun::desktop::Mat4 p{};for(std::size_t k=0;k<16;++k)p[k]=double(projection[k]);
    outrun::desktop::TripleViews v{};
    if(!outrun::desktop::triple_views(p,outrun::desktop::triple_rig_from(triple_rig),v))return false;
    for(std::size_t i=0;i<3;++i)for(std::size_t k=0;k<16;++k)out[i*16+k]=float(v.correction[i][k]);
    return true;
}
bool sun_fixture_views(const float* projection,outrun::platform::PcSunViews& out){
    if(!curve_profile||!outrun::platform::g_pc_triple_corrections)return false;
    outrun::desktop::TripleViews views;
    if(outrun::platform::pc_curve_active()&&outrun::platform::pc_comparison_curve()){
        if(!outrun::desktop::curve_scene_views(*curve_profile,projection,views))return false;
    }else{
        outrun::desktop::Mat4 p{};for(unsigned k=0;k<16;++k)p[k]=projection[k];
        if(!outrun::desktop::triple_views(p,outrun::desktop::triple_rig_from(triple_rig),views))return false;
    }
    outrun::platform::PcSunViews candidate;
    for(unsigned i=0;i<3;++i)for(unsigned k=0;k<16;++k){
        candidate.rotation[i][k]=float(views.rotation[i][k]);candidate.projection[i][k]=float(views.projection[i][k]);
        if(!std::isfinite(candidate.rotation[i][k])||!std::isfinite(candidate.projection[i][k]))return false;
    }
    out=candidate;return true;
}
struct MonitorScan { std::vector<outrun::desktop::ScreenRect> rects; bool failed{}; };
BOOL CALLBACK add_monitor(HMONITOR h,HDC,LPRECT,LPARAM state){
    auto& scan=*reinterpret_cast<MonitorScan*>(state);
    MONITORINFO i{};i.cbSize=sizeof i;
    if(!GetMonitorInfoW(h,&i)){scan.failed=true;return FALSE;}
    scan.rects.push_back({i.rcMonitor.left,i.rcMonitor.top,i.rcMonitor.right,i.rcMonitor.bottom,
                          (i.dwFlags&MONITORINFOF_PRIMARY)!=0});
    return TRUE;
}
LRESULT CALLBACK window_proc(HWND hwnd,UINT message,WPARAM w,LPARAM l){
    if(message==WM_CLOSE){outrun::desktop::gamepad_vibration_stop();closed=true;return 0;}
    // The keyboard as DirectInput reports it (DIK = scan code, 0x80 for the extended keys); every key up when the
    // window loses the focus. Alt+F4 and the other system keys still reach DefWindowProc.
    if(message==WM_KEYDOWN||message==WM_SYSKEYDOWN||message==WM_KEYUP||message==WM_SYSKEYUP){
        const unsigned scan=(unsigned(l)>>16)&0xffu;
        if(scan)outrun::desktop::desktop_keys()[scan|((unsigned(l)>>24)&1u?0x80u:0u)]=message==WM_KEYDOWN||message==WM_SYSKEYDOWN?0x80u:0u;
    }
    if(message==WM_KILLFOCUS){outrun::desktop::desktop_keys_clear();outrun::desktop::gamepad_vibration_stop();}
    if(message==WM_SETCURSOR&&borderless&&hide_mouse_cursor&&LOWORD(l)==HTCLIENT){SetCursor(nullptr);return TRUE;}
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
    const auto& monitor_state=td::startup_monitor_state();
    const auto* selected_monitor=monitor_state.selected();
    std::string span_note;
    if(borderless){
        MONITORINFO m{};m.cbSize=sizeof m;
        GetMonitorInfoW(MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY),&m);
        RECT r=m.rcMonitor;
        std::string monitor_note="the primary monitor";
        if(selected_monitor){
            const auto& chosen=selected_monitor->bounds;r={chosen.left,chosen.top,chosen.right,chosen.bottom};
            monitor_note="the selected gameplay display";
        }
        // Separate monitors (STD-015): the primary-centered racing row in physical pixels (the process is per-monitor
        // DPI aware; the left monitor's x is negative). A dashboard outside the row is ignored. No mode/layout changes.
        bool separate_span=false;   // Separate monitors found its three-monitor span
        if(triple==td::TripleScreens::separate){
            MonitorScan scan{};
            const bool enumerated=selected_monitor||EnumDisplayMonitors(nullptr,nullptr,add_monitor,reinterpret_cast<LPARAM>(&scan))!=FALSE;
            td::ScreenRect span{};std::string reason;
            const bool found=selected_monitor?
                td::startup_monitor_span(monitor_state.monitors,*monitor_state.selection.index,span,&reason):
                enumerated&&!scan.failed&&td::triple_span_from_monitors(scan.rects,span,&reason);
            if(found){
                r={span.left,span.top,span.right,span.bottom};
                span_note=selected_monitor?"over three monitors centered on the selected gameplay display":"over three monitors";separate_span=true;
            }else{
                if(!enumerated||scan.failed)reason="Windows did not report all monitors";
                span_note="refused: "+reason+" ("+std::to_string(selected_monitor?monitor_state.monitors.size():scan.rects.size())+
                    " found); "+monitor_note;
            }
        }
        w.hwnd=CreateWindowW(L"OutRunDesktop",L"OutRun 2006 Coast 2 Coast",WS_POPUP|WS_VISIBLE,r.left,r.top,r.right-r.left,r.bottom-r.top,
                             nullptr,nullptr,instance,nullptr);
        w.width=unsigned(r.right-r.left);w.height=unsigned(r.bottom-r.top);
        if(span_note.empty())span_note=monitor_note;
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
            outrun::platform::g_pc_triple_corrections=triple_fixture_corrections;
            if(curve_profile){
                outrun::platform::g_pc_curve_mesh_provider=curve_mesh_view;
                outrun::platform::g_pc_sun_views_provider=sun_fixture_views;
            }
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
            // An absent one is not named (triple_height_unused; nene 40406).
            const double used_h=triple_rig.panel_width_mm*9./16.;
            char height[120]="";
            if(td::triple_height_unused(triple_rig))
                std::snprintf(height,sizeof height," (PanelHeightMm %.1f not used: the height follows the width and the 16:9 render)",
                              triple_rig.panel_height_mm);
            if(triple_rig.match_game_fov)std::snprintf(eye,sizeof eye,"eye matched to the game's FOV");
            else{const double fov=td::triple_physical_fov_y(triple_rig,16./9.);outrun::platform::triple_fov_y()=float(fov);
                std::snprintf(eye,sizeof eye,"eye %.0f mm (MatchGameFov = false: the race camera's vertical FOV %.1f deg)",
                              triple_rig.eye_distance_mm,fov*180./3.14159265358979323846);}
            std::printf("triple screens: %s; window %ux%u at (%ld, %ld), %s; THREE PROJECTED VIEWS ([Triple] Views = Angled, experimental): the rig "
                        "(panels %.1f x %.1f mm%s, sides %.1f deg, gap %.1f mm, %s), one game render replicated into three "
                        "viewports; ordinary nodes culled against all panel frusta (unless DisableStageCulling); stage sections "
                        "drawn +%d ahead and %d behind the car; the HUD, menus and screen-space particles on the center only\n",td::triple_screens_name(triple),
                        w.width,w.height,r.left,r.top,span_note.c_str(),triple_rig.panel_width_mm,used_h,height,
                        triple_rig.side_angle_deg,triple_rig.bezel_mm,eye,dd.increase,dd.behind);
        }else if(outrun::platform::g_pc_wide_aspect>0.f)
            std::printf("triple screens: %s; window %ux%u at (%ld, %ld), %s; planar view %.3f:1 over the window (one wide frustum, "
                        "not three views); 2D, menus and movies on the center 16:9\n",td::triple_screens_name(triple),w.width,w.height,
                        r.left,r.top,span_note.c_str(),double(outrun::platform::g_pc_wide_aspect));
        else std::printf("triple screens: %s; window %ux%u at (%ld, %ld), %s; one 16:9 view, centered\n",td::triple_screens_name(triple),
                         w.width,w.height,r.left,r.top,span_note.c_str());
    }else{
        if(triple!=td::TripleScreens::off)std::printf("triple screens: %s needs the borderless window ([Window] WindowedBorderless, "
                                                      "no --windowed); one 16:9 view\n",td::triple_screens_name(triple));
        RECT r{0,0,1280,720};AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE);
        int x=CW_USEDEFAULT,y=CW_USEDEFAULT;
        w.width=1280;w.height=720;
        if(selected_monitor){
            const long frame_width=r.right-r.left-1280,frame_height=r.bottom-r.top-720;
            const auto& work=selected_monitor->work;
            if(work.right-work.left>frame_width&&work.bottom-work.top>frame_height){
                const auto placed=td::center_startup_window(work,r.right-r.left,r.bottom-r.top);
                x=int(placed.left);y=int(placed.top);r={0,0,placed.right-placed.left,placed.bottom-placed.top};
                w.width=unsigned(r.right-frame_width);w.height=unsigned(r.bottom-frame_height);
            }
        }
        w.hwnd=CreateWindowW(L"OutRunDesktop",L"OutRun 2006 Coast 2 Coast",WS_OVERLAPPEDWINDOW|WS_VISIBLE,x,y,
                             r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,nullptr);
        triple_status={triple,{},selected_monitor?"a bordered window on the selected gameplay display":"a 1280x720 window (not borderless)",0.f};
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
void set_hide_mouse_cursor(bool on){hide_mouse_cursor=on;}
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
    // Unattended tests use the Wheelkit runtime's switches (ffb-fence.on, inject.on in its log folder) and the launch
    // check's backups; this process has no test or playback mode of its own.
    const auto folder=outrun::xbox_runtime::launch_folder();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);   // real pixels for the borderless window
    winrt::init_apartment();   // Windows.Gaming.Input
    const auto log=folder+"/runtime.log";
    if(std::freopen(log.c_str(),"w",stdout))std::setvbuf(stdout,nullptr,_IONBF,0);
    if(std::freopen(log.c_str(),"a",stderr))std::setvbuf(stderr,nullptr,_IONBF,0);
    std::vector<std::string> args;int count=0;
    if(wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&count)){
        for(int i=0;i<count;++i)args.push_back(outrun::xbox_runtime::utf8(argv[i]));LocalFree(argv);}
    const outrun::desktop::Ini settings=outrun::desktop::load_desktop_settings(folder,true);
    std::printf("%s\n",outrun::desktop::startup_diagnostics().c_str());
    bool borderless=settings.flag("Window","WindowedBorderless",true);
    for(const auto& a:args)if(a=="--windowed")borderless=false;
    outrun::xbox_runtime::set_borderless(borderless);
    outrun::xbox_runtime::set_hide_mouse_cursor(settings.flag("Window","WindowedHideMouseCursor",true));
    outrun::desktop::configure_startup_monitor(settings.text("Window","MonitorDevicePath"),true);
    // [Triple] Screens (STD-022): Off (the default, also when the key is absent), Surround or Separate monitors, read at
    // start (the window opens once). Anything else is logged and Off applies. windows/docs/TRIPLE-SCREENS.md.
    {   const auto text=settings.text("Triple","Screens");
        const auto mode=outrun::desktop::parse_triple_screens(text);
        if(mode)outrun::xbox_runtime::set_triple_screens(*mode);
        else if(!text.empty())std::printf("settings: [Triple] Screens = %.40s refused (Off, Surround or Separate monitors); Off applies\n",text.c_str());}
    // [Triple] Views (below the selector): Planar (the default, also when the key is absent) or Angled, three projected
    // views (experimental; the window applies it only over a span). Anything else is logged and Planar applies. The
    // development switch OR2_TRIPLE_VIEWS=1 asks for Angled too. One request for both its users (the lods.ini read below
    // and the window); a latched process records it but applies nothing (the status says why).
    {   const auto text=settings.text("Triple","Views");
        const auto projection=outrun::desktop::parse_triple_projection(text);
        if(!projection&&!text.empty())std::printf("settings: [Triple] Views = %.40s refused (Planar or Angled); Planar applies\n",text.c_str());
        const char* dev=std::getenv("OR2_TRIPLE_VIEWS");
        outrun::xbox_runtime::set_triple_projection(projection==outrun::desktop::TripleProjection::angled||(dev&&std::string(dev)=="1"),false);}
    // [Triple] PanelWidthMm / PanelHeightMm / EyeDistanceMm / SideAngle / BezelMm and MatchGameFov (the portfolio's rig
    // keys, as Wheelkit writes them): read when Angled is asked for. A refused value is logged and its default applies;
    // the window logs the rig in use.
    if(outrun::xbox_runtime::triple_angled_requested){
        std::vector<std::string> notes;
        outrun::xbox_runtime::triple_rig=outrun::desktop::parse_triple_rig([&](const char* key){return settings.text("Triple",key);},notes);
        for(const auto& n:notes)std::printf("settings: [Triple] %s\n",n.c_str());
        const char* opt=std::getenv("OR2_CURVE_COMPOSITOR");
        if(opt&&std::string(opt)=="1"){
            const auto raw=settings.text("Triple","CurveRadiusMm");char* end=nullptr;
            const double radius=raw.empty()?0.:std::strtod(raw.c_str(),&end);
            auto rig=outrun::desktop::triple_rig_from(outrun::xbox_runtime::triple_rig);
            // This first prototype preserves the admitted equal16:9 native layout.
            rig.panel_height_mm=rig.panel_width_mm*9./16.;
            auto profile=std::make_unique<outrun::desktop::CurveSceneProfile>();
            if((raw.empty()||(end!=raw.c_str()&&*end=='\0'))&&outrun::desktop::curve_scene_profile(rig,radius,*profile)){
                outrun::platform::g_pc_curve_radius_mm=float(radius);
                outrun::xbox_runtime::curve_profile=std::move(profile);
                std::printf("curve prototype: requested radius %.9g mm; equal16:9 source, resources pending; developer only\n",radius);
            }else std::printf("curve prototype: refused CurveRadiusMm/geometry; legacy Angled retained\n");
        }

    }
    // [Window] FrameLatency: frames queued ahead of the display. 2 by default (also when the key is absent): about 33 ms
    // less from Present to scan-out than the plain chain, with one frame of slack. 1 is the lowest latency, 0 the plain
    // chain as before. Exactly one digit 0..3; anything else is logged and 2 applies.
    {   const auto text=settings.text("Window","FrameLatency");unsigned frames=2u;
        if(text.size()==1u&&text[0]>='0'&&text[0]<='3')frames=unsigned(text[0]-'0');
        else if(!text.empty())std::printf("settings: [Window] FrameLatency = %.32s refused (0, 1, 2 or 3); 2 applies\n",text.c_str());
        outrun::xbox_runtime::set_frame_latency(frames);}
    // [Window] AutoDetectResolution, the Tweaks key (on unless set false, as there): while options.ini holds no resolution,
    // the render resolution takes the display's height instead of the fixed 1920x1440 default (xbox/source/main.cpp). The
    // height is the selected monitor's, or the primary's by default (a span has the same height), or the bordered client.
    // No display mode is changed.
    {   const bool on=settings.flag("Window","AutoDetectResolution",true);
        unsigned height=0u;
        if(on){
            const auto* monitor=outrun::desktop::startup_monitor_state().selected();
            if(monitor)height=outrun::desktop::selected_monitor_render_height(borderless);
            else if(!borderless)height=720u;
            else{MONITORINFO m{};m.cbSize=sizeof m;
                 if(GetMonitorInfoW(MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY),&m))height=unsigned(m.rcMonitor.bottom-m.rcMonitor.top);}
        }
        outrun::xbox_runtime::g_auto_detect_height=height;
        if(!on)std::printf("settings: resolution auto-detect off (1920x1440 while Settings has no resolution)\n");}
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
    // Experimental loading-only Tweaks mode 2; no INI writes or mode-3 streaming.
    {
        const bool requested=settings.flag("Performance","FramerateUnlockExperimental",false);
        if(!outrun::enhancements::general_presentation().configure(requested,
            settings.text("Performance","FramerateLimit","0"),true))
            std::printf("settings: experimental FramerateLimit refused (0 or 60..1000); ordinary 60/120 policy applies\n");
        if(!outrun::enhancements::presentation_controls().configure(
            settings.text("Performance","FramerateLimitMode","0"),settings.text("Performance","VSync",""),true))
            std::printf("settings: invalid presentation control; FramerateLimitMode needs 0/1, VSync empty/0/1/2; invalid key uses its default\n");
    }
    {   const auto value=settings.text("Performance","FramerateFastLoad","0");
        auto& fast_load=outrun::enhancements::fast_load();
        if(!fast_load.configure(value))
            std::printf("settings: FramerateFastLoad = %.32s refused (only 0, 1 or 2 supported); off\n",value.c_str());
        if(fast_load.enabled())
            std::printf("settings: experimental FramerateFastLoad %u (loading frames only, %s; normal pacing resumes at START stage 65; runtime unqualified)\n",
                fast_load.setting(),fast_load.setting()==1u?"VSync kept":"VSync off");}
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
    // [Bugfixes] FixFullPedalChecks, the Tweaks key (on unless set false, as there): 453720 / 453750 read accelerator and
    // brake volumes of 250..254 as 255 for the game's full-press checks (race_input.hpp).
    outrun::platform::g_pc_fix_full_pedal_checks=settings.flag("Bugfixes","FixFullPedalChecks",true);
    if(!outrun::platform::g_pc_fix_full_pedal_checks)std::printf("settings: full pedal check fix off (the PC's own 255-only full-press checks)\n");
    // [Misc] FineSteering (not a Tweaks key; off unless set true): the car's steering word at the analogue steering's full
    // resolution instead of 255 steps (race_input.hpp). It changes the handling, so its value is logged either way.
    {   const bool requested=settings.flag("Misc","FineSteering",false);
        outrun::platform::g_pc_fine_steering=requested;
        std::printf("settings: steering %s\n",outrun::platform::g_pc_fine_steering?
            "FINE ([Misc] FineSteering = true: the analogue steering unstepped, not stock handling)":"in the PC's 255 steps (stock)");}
    // [Bugfixes] FixIncorrectShading and FixLensFlarePath, the Tweaks keys (on unless set false, as there): double-sided
    // lighting linked as the DOUBLE fragments, and the lens flare offsets read from Media\ (pc_screen.hpp).
    outrun::platform::g_pc_fix_incorrect_shading=settings.flag("Bugfixes","FixIncorrectShading",true);
    if(!outrun::platform::g_pc_fix_incorrect_shading)std::printf("settings: incorrect shading fix off (the TWOSIDE lighting fragments)\n");
    outrun::platform::g_pc_fix_lens_flare_path=settings.flag("Bugfixes","FixLensFlarePath",true);
    if(!outrun::platform::g_pc_fix_lens_flare_path)std::printf("settings: lens flare path fix off (common\\lens_flare_offset.bin only)\n");
    // [Bugfixes] FixPegasusClopping, the Tweaks key (on unless set false, as there): the Pegasus off callback 44BCC0 also
    // stops the hoof clop loop 0x8D (pc_screen.hpp).
    outrun::platform::g_pc_fix_pegasus_clopping=settings.flag("Bugfixes","FixPegasusClopping",true);
    if(!outrun::platform::g_pc_fix_pegasus_clopping)std::printf("settings: Pegasus clopping fix off (the PC's own single stop)\n");
    // [Bugfixes] FixRightSideBunkiAnimations (on unless set false, as in Tweaks): omit the right-side branch model's
    // culling-flag store at 44F6DC. Off preserves the PC's original store (pc_screen.hpp, race_area.cpp).
    outrun::platform::g_pc_fix_right_side_bunki_animations=settings.flag("Bugfixes","FixRightSideBunkiAnimations",true);
    if(!outrun::platform::g_pc_fix_right_side_bunki_animations)std::printf("settings: right-side bunki animation fix off (the PC's own model flag)\n");
    // This distinct default-off key preserves native event conditions and C2C selection. Read once from
    // the effective startup INI (including recorded Misc on cold-start playback); do not reset its PRNG here.
    outrun::platform::g_pc_random_highway_animation_rows=settings.flag("Misc","RandomHighwayAnimationRows",false);
    if(outrun::platform::g_pc_random_highway_animation_rows)
        std::printf("settings: highway animation table rows on (v%u, fixed initial state %08X; native conditions and C2C retained)\n",
            outrun::platform::PcHighwayAnimationRows::Version,unsigned(outrun::platform::PcHighwayAnimationRows::InitialState));
    // Tweaks' distinct mode replacement also changes event conditions and the
    // selector's C2C path. Read the recorded startup Misc key without resetting
    // the separate process stream. It takes precedence over the row-only key.
    outrun::platform::g_pc_random_highway_anim_sets=settings.flag("Misc","RandomHighwayAnimSets",false);
    if(outrun::platform::g_pc_random_highway_anim_sets)
        std::printf("settings: highway animation sets on (v%u, fixed initial state %08X; local modes 0..3, takes precedence over table rows)\n",
            outrun::platform::PcHighwayAnimSets::Version,unsigned(outrun::platform::PcHighwayAnimSets::InitialState));
    // [Bugfixes] HideOnlineSigninText, the Tweaks key (off unless set true, as in its INI): no sign-in status box or text
    // and no SIGN-IN help-bar button on the menus (pc_screen.hpp).
    outrun::platform::g_pc_hide_online_signin_text=settings.flag("Bugfixes","HideOnlineSigninText",false);
    if(outrun::platform::g_pc_hide_online_signin_text)std::printf("settings: online sign-in text hidden\n");
    // [Graphics] UseHiDefCharacters, the Tweaks key (on unless set false, as there): the hi-def driver and heroine
    // character sets in gameplay (pc_screen.hpp).
    outrun::platform::g_pc_use_hidef_characters=settings.flag("Graphics","UseHiDefCharacters",true);
    if(!outrun::platform::g_pc_use_hidef_characters)std::printf("settings: hi-def characters off (the standard character sets)\n");
    // [Misc] RestoreJPClarissa, the Tweaks key (off unless set true, as there): Clarissa's Japanese model in O2SP arcade
    // mode and the arcade ending (pc_screen.hpp).
    outrun::platform::g_pc_restore_jp_clarissa=settings.flag("Misc","RestoreJPClarissa",false);
    if(outrun::platform::g_pc_restore_jp_clarissa)std::printf("settings: Clarissa's Japanese model restored (O2SP arcade and ending)\n");
    // [Misc] DisableCountdownTimer, the Tweaks key (off unless set true, as there): the race clock stands still (pc_screen.hpp).
    // It changes the game's rules, so its value is logged either way (recordings and score accounting read runtime.log
    // and the startup INI; sana 38796). Online ranking upload is not ported.
    outrun::platform::g_pc_disable_countdown_timer=settings.flag("Misc","DisableCountdownTimer",false);
    std::printf("settings: countdown timer %s\n",outrun::platform::g_pc_disable_countdown_timer?
        "DISABLED ([Misc] DisableCountdownTimer = true: the race clock stands still; not stock rules)":"on (stock rules)");
    // [Misc] ShowOutRunMilesOnMenu, the Tweaks key (on unless set false, as there): the menu's "Not Signed In" status
    // also shows the license's OutRun Miles (pc_screen.hpp).
    outrun::platform::g_pc_show_outrun_miles_on_menu=settings.flag("Misc","ShowOutRunMilesOnMenu",true);
    if(!outrun::platform::g_pc_show_outrun_miles_on_menu)std::printf("settings: OutRun Miles on the menu off\n");
    // [Misc] AllowCharacterSelection, the Tweaks key (off unless set true, as there): the license editor's menu reaches
    // its character item from the name (Down) and the nationality (Up) (pc_screen.hpp).
    outrun::platform::g_pc_allow_character_selection=settings.flag("Misc","AllowCharacterSelection",false);
    if(outrun::platform::g_pc_allow_character_selection)std::printf("settings: the license editor's character selection on\n");
    // [Misc] EnableHollyCourse2, the Tweaks key (3 by default, as Tweaks' shipped INI): Holly's MIX 2 course set as the
    // racer menu's group 7; 0 off, 1 unlocked with Holly 1, 2 once Holly 1 reaches A, 3 as 2 and shown only then
    // (pc_screen.hpp). It needs the fourth course lane; without it the group stays locked.
    {   const int v=settings.integer("Misc","EnableHollyCourse2",3);
        if(v<0||v>3)std::printf("settings: EnableHollyCourse2 = %d refused (0..3); 3 applies\n",v);
        outrun::platform::g_pc_enable_holly_course2=v<0||v>3?3:v;
        if(outrun::platform::g_pc_enable_holly_course2!=3)
            std::printf("settings: Holly's MIX 2 course (EnableHollyCourse2) %d\n",outrun::platform::g_pc_enable_holly_course2);}
    // [Audio] AllowHorn, the Tweaks key (on unless set false, as there): the controls contract's "horn" action sounds the
    // horn in the race (pc_screen.hpp; race_sound_runtime.cpp native_race_horn).
    outrun::platform::g_pc_allow_horn=settings.flag("Audio","AllowHorn",true);
    // [Audio] AllowWAV / AllowFLAC, the Tweaks keys (on unless set false, as there): a same-stem .wav/.flac in Sound
    // replaces the Ogg Vorbis track (FLAC > WAV > Ogg). Read once, no INI writes or per-frame filesystem work.
    const outrun::platform::MusicReplacementPolicy music_policy{
        settings.flag("Audio","AllowWAV",true),settings.flag("Audio","AllowFLAC",true)};
    outrun::platform::set_music_replacement_policy(music_policy);
    const bool flac_loop_points=settings.flag("Audio","AllowFLACLoopPoints",false);
    outrun::platform::set_music_flac_loop_points_enabled(flac_loop_points);
    if(music_policy.allow_wav||music_policy.allow_flac)
        std::printf("settings: experimental music replacements WAV=%d FLAC=%d custom FLAC loop points=%d (Tweaks exclusive LENGTH-1; loop requests only)\n",
                    int(music_policy.allow_wav),int(music_policy.allow_flac),int(flac_loop_points));
    if(!outrun::platform::g_pc_allow_horn)std::printf("settings: the horn off ([Audio] AllowHorn = false)\n");
    // [Misc] DefaultManualTransmission, the Tweaks key (off unless set true, as there; Tweaks reads it from [Controls],
    // which here is the controls contract): the transmission menu starts on Manual (pc_screen.hpp).
    outrun::platform::g_pc_default_manual_transmission=settings.flag("Misc","DefaultManualTransmission",false);
    if(outrun::platform::g_pc_default_manual_transmission)std::printf("settings: the transmission menu starts on Manual\n");
    outrun::platform::g_pc_enable_level_select=settings.flag("Misc","EnableLevelSelect",false);
    if(outrun::platform::g_pc_enable_level_select)std::printf("settings: initial frontend opens the level selection menu\n");
    // [Graphics] UIScalingMode, the Tweaks key (windows/docs/UI-SCALING.md; 2, keep centred, by default here until Tweaks'
    // 1 is checked in game): 0 stretches the 2D over the 16:9 frame, or over a span's centre panel (display_fit.hpp); 1
    // spaces the HUD to the edges (partial: the note says which parts).
    {   std::string note;
        outrun::xbox_runtime::g_ui_scaling_mode=outrun::xbox_runtime::parse_ui_scaling_mode(settings.text("Graphics","UIScalingMode"),note);
        if(!note.empty())std::printf("settings: [Graphics] %s\n",note.c_str());
        if(outrun::xbox_runtime::g_ui_scaling_mode==0u)
            std::printf("settings: UI scaling 0: the 2D stretched over the 16:9 frame (over the center panel on a span)\n");}
    // [Graphics] UILetterboxing, the Tweaks key (1 by default, as there; clamped to 0..2 as Tweaks reads it): black bars
    // outside the centered 4:3, 1 outside the race, 2 always, 0 none; only with UIScalingMode 1 or 2 (pc_screen.hpp).
    {   const int v=settings.integer("Graphics","UILetterboxing",1);
        outrun::platform::g_pc_ui_letterboxing=v<0?0:v>2?2:v;
        if(outrun::platform::g_pc_ui_letterboxing==0)std::printf("settings: UI letterboxing off\n");
        else if(outrun::xbox_runtime::g_ui_scaling_mode==0u)std::printf("settings: UI letterboxing inactive with UI scaling 0 (as Tweaks)\n");
        else if(outrun::platform::g_pc_ui_letterboxing==2)std::printf("settings: UI letterboxing always on (in the race too)\n");}
    // [Graphics] ScreenEdgeCullFix, the Tweaks key (on unless set false, as there): course objects culled at the
    // widescreen edge rather than the 4:3 one.
    outrun::platform::g_pc_screen_edge_cull_fix=settings.flag("Graphics","ScreenEdgeCullFix",true);
    if(!outrun::platform::g_pc_screen_edge_cull_fix)std::printf("settings: screen edge cull fix off (objects culled at the 4:3 edge)\n");
    // [Graphics] FixZBufferPrecision, the Tweaks key: the race camera's near plane raised from 0.1 (1.0 in the chase and
    // bumper views) so far geometry stops z-fighting; the lens flare keeps 0.05. Off when absent.
    if(settings.flag("Graphics","FixZBufferPrecision",false)){
        outrun::platform::fix_znear()=true;std::printf("settings: z-buffer precision fix (race camera near plane by view)\n");}
    // [Graphics] SceneTextureReplacement / UITextureReplacement / TextureBaseFolder, the Tweaks keys: stage, car and
    // object textures, and the menu and HUD sprite textures (the pack's spr_*_xst folders), replaced from
    // <game folder>/<TextureBaseFolder>/load in the Tweaks pack layout. Each off when absent; the folder defaults to
    // "textures". A path that leaves the game folder is refused. TextureMatchAnyFolder (not a Tweaks key, off when
    // absent): a texture with no file under Tweaks' names takes an unambiguous same-name file from another pack folder.
    {   const bool on=settings.flag("Graphics","SceneTextureReplacement",false),ui=settings.flag("Graphics","UITextureReplacement",false);
        const bool any=settings.flag("Graphics","TextureMatchAnyFolder",false);
        const bool extract_requested=settings.flag("Graphics","SceneTextureExtract",false);
        const bool ui_extract_requested=settings.flag("Graphics","UITextureExtract",false);
        const bool cache_requested=settings.flag("Graphics","EnableTextureCache",false);
        const bool extraction_allowed=true;
        const bool extract=extract_requested,ui_extract=ui_extract_requested;
        std::string base=settings.text("Graphics","TextureBaseFolder","textures");
        if(base.empty()||base.find("..")!=std::string::npos||base.find(':')!=std::string::npos||base[0]=='\\'||base[0]=='/'){
            if(on||ui||extract||ui_extract)std::printf("settings: [Graphics] TextureBaseFolder = %.64s refused (a folder inside the game folder); \"textures\" applies\n",base.c_str());
            base="textures";}
        outrun::platform::texture_replacement_configure(on,ui,folder+"/"+base+"/load",any,extract,ui_extract,cache_requested&&extraction_allowed);
        if(cache_requested&&extraction_allowed&&(on||ui))std::printf("settings: replacement DDS cache on (128 MiB, next ordinary start; no hot reload)\n");
        if(any&&(on||ui))std::printf("settings: [Graphics] TextureMatchAnyFolder: same-name files from other pack folders when unambiguous\n");}
    // The game's own keyboard (the reconstructed PC keyboard device and its key tables).
    outrun::platform::native_pc_keyboard_source()=[](std::uint8_t keys[256]){
        const auto& k=outrun::desktop::desktop_keys();std::copy(k.begin(),k.end(),keys);};
    // The Wheelkit runtime (wheelkit_runtime.hpp): the wheel, pedals and shifter from the Wheelkit profile, the F6 panel
    // and telemetry; its filter is the first, so every later one sees the wheel's sample (or a neutral one under F6).
    outrun::desktop::wheelkit_configure(settings);
    outrun::desktop::wheelkit_wrap_input_filter();
    const auto server=settings.text("Misc","DemonwareServerOverride");
    if(!outrun::platform::native_network_set_server_override(server))
        std::printf("network: invalid server override refused; Windows transport remains off\n");
    else if(!server.empty())std::printf("network: opt-in IPv4 DNS/UDP server override configured; online play unqualified\n");
    outrun::desktop::login_protection_configure(settings,true);
    outrun::desktop::level_select_input_configure(outrun::platform::g_pc_enable_level_select,false);
    outrun::desktop::level_select_wrap_input_filter();
    outrun::desktop::gamepad_vibration_configure(settings,true);
    outrun::desktop::gamepad_vibration_wrap_input_filter();
    // [Misc] HudToggleKey (hud_toggle.hpp): the key still reaches the game.
    outrun::desktop::hud_toggle_configure(settings,false,false);
    outrun::desktop::hud_toggle_wrap_input_filter();
    outrun::desktop::cd_switcher_configure(settings,false,false);
    outrun::desktop::cd_switcher_wrap_input_filter();
    outrun::desktop::camera_controls_configure(settings,false,
        float(outrun::desktop::triple_physical_fov_y(outrun::xbox_runtime::triple_rig,16./9.)));
    outrun::desktop::camera_controls_wrap_input_filter();
    std::vector<char*> argv;for(auto& a:args)argv.push_back(a.data());argv.push_back(nullptr);
    struct PadOutputCleanup {~PadOutputCleanup(){outrun::desktop::gamepad_vibration_shutdown();}} pad_output_cleanup;
    const int result=or2_xbox_main(int(args.size()),argv.data());
    outrun::desktop::gamepad_vibration_shutdown();
    outrun::desktop::wheelkit_shutdown();
    // Texture replacement summaries, scene then UI: totals, the banks the pack covers one line each, the others named once
    // (a stage variant the pack lacks shows up here).
    for(const bool ui:{false,true}){
        const auto& t=ui?outrun::platform::ui_texture_replacement_stats():outrun::platform::pmt_texture_replacement_stats();
        if(!t.files)continue;
        const char* kind=ui?"UI texture replacement":"texture replacement";
        std::printf("%s: %u replaced of %u looked up (%u kept stock: unreadable, unusable or not created)\n",kind,t.replaced,t.looked,t.failed);
        if(t.other||t.ambiguous)std::printf("%s: %u of those from other pack folders (TextureMatchAnyFolder), %u kept stock as ambiguous\n",
            kind,t.other,t.ambiguous);
        std::string others;unsigned uncovered=0;
        for(const auto& [bank,b]:ui?outrun::platform::ui_texture_replacement_banks():outrun::platform::pmt_texture_replacement_banks()){
            if(b.folder||b.other){std::printf("%s: %s %u of %u replaced (%u from other folders, %u ambiguous, %u kept stock, %.2f s in the pack)\n",
                kind,bank.c_str(),b.replaced,b.looked,b.other,b.ambiguous,b.failed,b.seconds);continue;}
            ++uncovered;if(others.size()<400)others+=(others.empty()?"":", ")+bank;
        }
        if(uncovered)std::printf("%s: %u banks without a pack folder: %s%s\n",kind,uncovered,others.c_str(),others.size()>=400?", ...":"");
    }
    for(bool ui:{false,true}){
        const auto& t=ui?outrun::platform::ui_texture_extraction_stats():outrun::platform::pmt_texture_extraction_stats();
        if(t.written||t.existing||t.failed||t.unsupported)
            std::printf("%s texture extraction: %u written, %u existing files kept, %u failed, %u unsupported%s%s\n",
                ui?"UI":"scene",t.written,t.existing,t.failed,t.unsupported,t.error.empty()?"":"; ",t.error.c_str());
    }
    {const auto cache=outrun::platform::texture_file_cache_stats();
     if(cache.reads||cache.hits||cache.failed)
         std::printf("replacement DDS cache: %llu reads, %llu hits, %llu failed, %llu evictions; %zu bytes/%zu files retained\n",
             static_cast<unsigned long long>(cache.reads),static_cast<unsigned long long>(cache.hits),
             static_cast<unsigned long long>(cache.failed),static_cast<unsigned long long>(cache.evicted),cache.bytes,cache.entries);}
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
    return result;
}
