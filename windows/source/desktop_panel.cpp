// The F6 panel (desktop_panel.hpp): the toolkit panel's frame sequence on our own D3D11 device, as the toolkit's
// overlay runs it (dbce_overlay_d3d11.cpp render11 / drawFrame), called from the renderer's present instead of a hook.
#include "desktop_panel.hpp"
#include "desktop_keyboard.hpp"
#include "desktop_settings.hpp"
#include "native_window.hpp"
#include "platform/runtime_input.hpp"
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
// The toolkit's own lines already start with "panel:"; ours get the prefix here.
void log_line(const char* line){std::printf(std::strncmp(line,"panel:",6)==0?"%s\n":"panel: %s\n",line);std::fflush(stdout);}
// The Help page's own lines (the panel draws the version, the UI scale and the frame rate itself).
void draw_page(dbce::panel::Page page,bool){
    if(page!=dbce::panel::PAGE_HELP)return;
    dbce::panel::Status("Settings file",p.latched?"none in a test run (nothing is saved)":p.ini.c_str());
    dbce::panel::Text("The other pages arrive with the settings they control; controls are bound in Wheelkit.");
}
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
    dbce::panel::Config c;
    c.modName=p.name.c_str();
    c.iniPath=latched?nullptr:p.ini.c_str();   // a test process saves nothing
    c.drawPage=draw_page;
    c.log=log_line;
    c.devFile=p.dev.empty()?nullptr:p.dev.c_str();
    dbce::panel::Configure(c);
    xbox_runtime::present_overlay()=overlay;
    p.configured=true;
    std::printf("panel: F6 settings panel ready (Help page; %s; dev channel %s)\n",latched?"a test run: nothing saved":"[Panel] kept in the settings file",
        p.dev.empty()?"off":p.dev.c_str());
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
