// The F6 panel (desktop_panel.hpp): the toolkit panel's frame sequence on our own D3D11 device, as the toolkit's
// overlay runs it (dbce_overlay_d3d11.cpp render11 / drawFrame), called from the renderer's present instead of a hook.
#include "desktop_panel.hpp"
#include "desktop_keyboard.hpp"
#include "desktop_settings.hpp"
#include "ffb_live.hpp"
#include "native_window.hpp"
#include "telemetry_forza.hpp"
#include "shim_input.hpp"
#include "triple_screens.hpp"
#include "wheel_input.hpp"
#include "platform/runtime_input.hpp"
#include <cmath>
#include "dbce_panel.h"
#include "dbce_panel_input.h"
#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <cstring>
#include <string>
namespace outrun::desktop {
namespace {
struct Panel {
    bool configured{},latched{},input{},backend{},was_open{};
    xbox_runtime::InputFilter inner{};
    ID3D11Device* device{};
    std::string ini,dev,name;
    LARGE_INTEGER freq{},last{};
};
Panel p;
// The values the pages show, read once from the settings at start; each change saves its one key at once (write_key)
// and applies at the next game start (the owner's choice is never half-applied to a running game).
struct Values {
    int triple{},latency{2};
    bool telemetry{true};int layout{};
    int global{100},spring{},damper{},weight{},grip{},transfer{},wall{},gear{},road{},slip{},idle{};bool invert{};
    int sensitivity{100},dead_zone{};
    bool output_next{};   // [FFB] Output = wheel in the file (the wheel output at the next start)
};
Values v;
int pct(float f){return int(std::lround(double(f)*100.0));}
std::string fraction(int percent){char t[16];std::snprintf(t,sizeof t,"%.2f",percent/100.0);return t;}
// The toolkit's own lines already start with "panel:"; ours get the prefix here.
void log_line(const char* line){std::printf(std::strncmp(line,"panel:",6)==0?"%s\n":"panel: %s\n",line);std::fflush(stdout);}
// A failed save's note, shown again at the next drawn frame: the toolkit's own Stop FFB path notes success after the
// callback returns, which would hide a failure.
std::string pending_failure,stop_error;
// One key into OutRunDesktop.ini (the file Wheelkit reads); never in a test run. False: not saved (the caller puts
// its control back to the saved value, so the page shows only what the next start will use).
bool save(const char* section,const char* key,const std::string& value){
    if(p.latched){pending_failure="a test run saves nothing";dbce::panel::NoteSaved(false,pending_failure.c_str());return false;}
    std::string error;
    const bool ok=write_key(p.ini,section,key,value,error);
    if(ok)pending_failure.clear();else pending_failure="not saved: "+error;
    dbce::panel::NoteSaved(ok,ok?"applies at the next game start":pending_failure.c_str());
    std::printf("panel: [%s] %s = %s %s\n",section,key,value.c_str(),ok?"saved":pending_failure.c_str());std::fflush(stdout);
    return ok;
}
// A control's change saved, or put back to the value it had (the saved one).
template<class T,class S> void keep(T* value,T before,S saved){if(!saved())*value=before;}
bool percent_key(const char* label,int* value,int max,int def,const char* key,const char* help=nullptr){
    const int before=*value;
    if(!dbce::panel::Percent(label,value,0,max,def,help))return false;
    keep(value,before,[&]{return save("FFB",key,fraction(*value));});return true;
}
void page_controls(bool){
    // The backend this process uses: [Input] Backend = Shim runs the shared input shim and never configures wheel_input.
    const auto shim=shim_input_status_text();
    dbce::panel::Status("Input",shim.empty()?"Native (the build's own wheel input)":"Shim (the shared input shim, [Input] Backend = Shim)");
    dbce::panel::Status("Controls",(shim.empty()?wheel_input_status_text():shim).c_str());
    dbce::panel::Text("Bindings are set in Wheelkit ([Controls] of the settings file).");
    {const int before=v.sensitivity;
     if(dbce::panel::Slider("Steering sensitivity",&v.sensitivity,10,300,100,"%d%%"))
         keep(&v.sensitivity,before,[&]{return save("Wheel","SteeringSensitivity",fraction(v.sensitivity));});}
    {const int before=v.dead_zone;
     if(dbce::panel::Percent("Steering dead zone",&v.dead_zone,0,90,0))
         keep(&v.dead_zone,before,[&]{return save("Wheel","SteeringDeadZone",fraction(v.dead_zone));});}
}
void page_ffb(bool advanced){
    const auto status=ffb_status_text();
    dbce::panel::Status("Wheel output",status.c_str(),ffb_output_running()?dbce::panel::Tone::Good:dbce::panel::Tone::Normal);
    dbce::panel::Status("At the next start",v.output_next?"on":"off",v.output_next&&!ffb_output_running()?dbce::panel::Tone::Warning:dbce::panel::Tone::Normal);
    if(!stop_error.empty())dbce::panel::Status("Stop FFB",("the output stopped, but turning it off for the next start failed: "+stop_error).c_str(),
                                                 dbce::panel::Tone::Error);
    dbce::panel::Text("Turning the wheel output on is not offered in this panel yet. Stop FFB and F8 turn it off now and at "
                      "the next start.");
    const ffb::LegacySettings d;   // the mod's compiled defaults, for each slider's Default
    percent_key("Strength",&v.global,100,pct(d.global_strength),"FFBGlobalStrength");
    if(!advanced)return;
    percent_key("Centering",&v.spring,200,pct(d.spring),"FFBSpringStrength");
    percent_key("Damping",&v.damper,200,pct(d.damper),"FFBDamperStrength");
    percent_key("Cornering load",&v.weight,200,pct(d.steering_weight),"FFBSteeringWeight");
    percent_key("Grip loss",&v.grip,100,pct(d.grip_loss),"FFBGripLoss");
    percent_key("Weight transfer",&v.transfer,200,pct(d.weight_transfer),"FFBWeightTransfer");
    percent_key("Wall impact",&v.wall,200,pct(d.wall_impact),"FFBWallImpact");
    percent_key("Gear shift",&v.gear,200,pct(d.gear_shift),"FFBGearShift");
    percent_key("Road texture",&v.road,200,pct(d.road_texture),"FFBRoadTexture");
    percent_key("Tire slip",&v.slip,200,pct(d.tire_slip),"FFBTireSlip");
    percent_key("Engine idle",&v.idle,100,pct(d.engine_idle),"FFBEngineIdle");
    {const bool before=v.invert;
     if(dbce::panel::OffOn("Invert force",&v.invert))keep(&v.invert,before,[&]{return save("FFB","FFBInvertForce",v.invert?"true":"false");});}
}
void page_cameras(bool advanced){
    static const char* const modes[]{"Off","Surround","Separate monitors"};
    {const int before=v.triple;
     if(dbce::panel::Choice("Triple screens",modes,3,&v.triple,"Off: one 16:9 view. Surround: one wide view in a Surround desktop's "
         "wide window. Separate monitors: one wide view in one window over three equal side-by-side monitors."))
         keep(&v.triple,before,[&]{return save("Triple","Screens",triple_screens_name(TripleScreens(v.triple)));});}
    const auto& st=triple_screens_status();
    char now[256];
    if(st.aspect>0.f)std::snprintf(now,sizeof now,"%s, %s, one wide view %.2f:1 (planar, not three views)",triple_screens_name(st.mode),st.where.c_str(),double(st.aspect));
    else std::snprintf(now,sizeof now,"%s, %s, one 16:9 view",triple_screens_name(st.mode),st.where.c_str());
    dbce::panel::Status("Now",now);
    // romi 31510: the span is one planar view; never imply three angled views before they exist.
    dbce::panel::Text("Surround and Separate monitors both draw one wide view across the screens; separate angled side views "
                      "are not built yet.");
    if(v.triple!=int(st.mode))dbce::panel::Text("The choice applies at the next game start.");
    if(!advanced)return;
    static const char* const latency[]{"0 (plain)","1","2 (default)","3"};
    const int before=v.latency;
    if(dbce::panel::Choice("Frame latency",latency,4,&v.latency,"Frames the swap chain may queue; fewer is less input lag, more is smoother."))
        keep(&v.latency,before,[&]{return save("Window","FrameLatency",std::to_string(v.latency));});
}
void page_telemetry(bool advanced){
    {const bool before=v.telemetry;
     if(dbce::panel::OffOn("Telemetry",&v.telemetry,"Forza Data Out to 127.0.0.1 and the OutRun 2006 mod's shared memory (SimHub, dashboards)."))
         keep(&v.telemetry,before,[&]{return save("Telemetry","Enabled",v.telemetry?"true":"false");});}
    if(advanced){
        static const char* const layouts[]{"Forza Horizon (324 bytes)","Forza Motorsport 7 (311 bytes)"};
        const int before=v.layout;
        if(dbce::panel::Choice("Packet layout",layouts,2,&v.layout))
            keep(&v.layout,before,[&]{return save("Telemetry","Layout",v.layout?"FM7":"Horizon");});
    }
    dbce::panel::Text("Changes apply at the next game start.");
}
void draw_page(dbce::panel::Page page,bool advanced){
    if(!pending_failure.empty()){dbce::panel::NoteSaved(false,pending_failure.c_str());pending_failure.clear();}   // after the toolkit's own note
    switch(page){
    case dbce::panel::PAGE_CONTROLS:page_controls(advanced);break;
    case dbce::panel::PAGE_FFB:page_ffb(advanced);break;
    case dbce::panel::PAGE_CAMERAS:page_cameras(advanced);break;
    case dbce::panel::PAGE_TELEMETRY:page_telemetry(advanced);break;
    case dbce::panel::PAGE_HELP:
        dbce::panel::Status("Settings file",p.latched?"none in a test run (nothing is saved)":p.ini.c_str());
        dbce::panel::Text("Controls are bound in Wheelkit.");
        break;
    default:break;
    }
}
// Stop FFB (the header command and F8, STD-004): the wheel released for this session and [FFB] Output = none saved; the
// next-start state changes only when the save succeeded, else the FFB page says what failed.
void stop_ffb(){
    ffb_stop_output();
    if(p.latched)return;
    if(save("FFB","Output","none")){v.output_next=false;stop_error.clear();}
    else stop_error=pending_failure;
}
bool triples_on(){const auto& st=triple_screens_status();return st.mode!=TripleScreens::off&&st.aspect>0.f;}
void release_backend(){
    if(p.backend){ImGui_ImplDX11_Shutdown();p.backend=false;}
    p.device=nullptr;
}
// The present overlay: every presented frame. Closed: the frame-rate note and the dev-channel poll only.
void overlay(void* device,void* context,void* view,unsigned width,unsigned height){
    dbce::panel::NoteFrame();
    dbce::panel::PollDev();
    if(!p.input){
        if(const HWND hwnd=static_cast<HWND>(xbox_runtime::native_window().hwnd)){
            p.input=dbce::panel::AttachInput(hwnd);
            log_line(p.input?"F6 input attached to the game window":"F6 input hook FAILED; the dev channel still opens it");
        }
    }
    if(!dbce::panel::IsOpen()||!device||!context||!view||!width||!height)return;
    auto* dev=static_cast<ID3D11Device*>(device);auto* ctx=static_cast<ID3D11DeviceContext*>(context);
    if(dev!=p.device){   // the first open frame (the device lives for the process)
        release_backend();
        if(!ImGui::GetCurrentContext()){
            ImGui::CreateContext();
            ImGuiIO& io=ImGui::GetIO();
            io.IniFilename=nullptr;io.LogFilename=nullptr;   // ImGui writes no files; the panel keeps [Panel] itself
            io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NoMouseCursorChange;
            io.MouseDrawCursor=true;   // the game hides the system cursor; the panel draws its own while open
        }
        if(!p.freq.QuadPart)QueryPerformanceFrequency(&p.freq);   // also when a context already existed (romi 30736)
        p.backend=ImGui_ImplDX11_Init(dev,ctx);
        if(!p.backend){log_line("ImGui's DX11 backend could not start; the panel is not drawn");return;}
        p.device=dev;
    }
    const int w=int(width),h=int(height);
    if(dbce::panel::Prepare(w,h)){dbce::panel::BuildFonts();ImGui_ImplDX11_InvalidateDeviceObjects();}
    ImGui_ImplDX11_NewFrame();
    ImGuiIO& io=ImGui::GetIO();
    io.DisplaySize=ImVec2(float(w),float(h));
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    io.DeltaTime=p.last.QuadPart?float(double(now.QuadPart-p.last.QuadPart)/double(p.freq.QuadPart)):1.0f/60.0f;
    if(io.DeltaTime<=0.0f||io.DeltaTime>1.0f)io.DeltaTime=1.0f/60.0f;
    p.last=now;
    dbce::panel::DrainInput(w,h);
    ImGui::NewFrame();
    dbce::panel::Draw(w,h);
    ImGui::Render();
    // On the back buffer, then the renderer's targets back (the DX11 backend restores the rest of its state itself).
    ID3D11RenderTargetView* saved[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};ID3D11DepthStencilView* depth=nullptr;
    ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,saved,&depth);
    auto* target=static_cast<ID3D11RenderTargetView*>(view);
    ctx->OMSetRenderTargets(1,&target,nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,saved,depth);
    for(auto* v:saved)if(v)v->Release();
    if(depth)depth->Release();
}
}

void desktop_panel_configure(const Ini& settings,bool latched,const std::string& run_folder){
    p=Panel{};
    p.latched=latched;
    p.ini=settings.path();
    const auto slash=p.ini.find_last_of("\\/");
    const std::string folder=latched?run_folder:(slash==std::string::npos?std::string("."):p.ini.substr(0,slash));
    if(!folder.empty())p.dev=folder+"\\OutRunDesktop.panel-dev.txt";   // none for a latched run without its folder
    p.name="OutRun 2006 desktop (decomp)";
    // The pages' values as the settings file holds them now (the same readers the game uses at start).
    v=Values{};
    v.triple=int(parse_triple_screens(settings.text("Triple","Screens")).value_or(TripleScreens::off));
    {const auto t=settings.text("Window","FrameLatency");if(t.size()==1&&t[0]>='0'&&t[0]<='3')v.latency=t[0]-'0';}
    {std::string notes;const auto t=telemetry_settings(settings,nullptr,nullptr);v.telemetry=t.enabled;v.layout=t.layout==dbce::forza::FORZA_FM7_DASH_311?1:0;}
    {std::string notes,source;const auto s=ffb_settings(settings,notes,source);
     v.global=pct(s.global_strength);v.spring=pct(s.spring);v.damper=pct(s.damper);v.weight=pct(s.steering_weight);v.grip=pct(s.grip_loss);
     v.transfer=pct(s.weight_transfer);v.wall=pct(s.wall_impact);v.gear=pct(s.gear_shift);v.road=pct(s.road_texture);v.slip=pct(s.tire_slip);
     v.idle=pct(s.engine_idle);v.invert=s.invert;}
    {const auto w=wheel_steering(settings);v.sensitivity=pct(w.sensitivity);v.dead_zone=pct(w.dead_zone);}
    {std::string notes;v.output_next=ffb_output_config(settings,false,nullptr,notes).output;}
    dbce::panel::Config c;
    c.modName=p.name.c_str();
    c.iniPath=latched?nullptr:p.ini.c_str();   // a test process saves nothing
    for(const auto page:{dbce::panel::PAGE_CONTROLS,dbce::panel::PAGE_FFB,dbce::panel::PAGE_CAMERAS,dbce::panel::PAGE_TELEMETRY})c.pages[page]=true;
    c.drawPage=draw_page;
    c.ffbOn=ffb_output_running;c.stopFfb=stop_ffb;   // Stop FFB / F8 (STD-004)
    c.log=log_line;
    c.devFile=p.dev.empty()?nullptr:p.dev.c_str();
    c.triplesOn=triples_on;
    dbce::panel::Configure(c);
    xbox_runtime::present_overlay()=overlay;
    p.configured=true;
    std::printf("panel: F6 settings panel ready (Controls, FFB, Cameras, Telemetry, Help; %s; dev channel %s)\n",
        latched?"a test run: nothing saved":"saves to the settings file",p.dev.empty()?"off":p.dev.c_str());
    std::fflush(stdout);
}

bool desktop_panel_open(){return p.configured&&dbce::panel::IsOpen();}

void desktop_panel_neutral_input(platform::NativeInputState& out){
    const bool connected=out.connected,layer=out.pc_input_layer;
    out=platform::NativeInputState{};
    out.connected=connected;out.pc_input_layer=layer;
}

namespace {
void input_filter(platform::NativeInputState& out,const platform::NativeRuntimeContext* rt){
    if(p.inner)p.inner(out,rt);
    const bool open=desktop_panel_open();
    if(open!=p.was_open){desktop_keys().fill(0);p.was_open=open;}
    if(open)desktop_panel_neutral_input(out);
}
}

void desktop_panel_wrap_input_filter(){
    if(!p.configured)return;
    auto& filt=xbox_runtime::input_filter();
    if(filt==input_filter)return;
    p.inner=filt;filt=input_filter;
}

void desktop_panel_shutdown(){
    if(!p.configured)return;
    xbox_runtime::present_overlay()=nullptr;
    release_backend();
    if(ImGui::GetCurrentContext())ImGui::DestroyContext();
    p.configured=false;
}
}
