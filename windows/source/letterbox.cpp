// UILetterboxing's bars (letterbox.hpp).
#include "letterbox.hpp"
#include "display_fit.hpp"
#include "native_window.hpp"
#include "renderer.hpp"
#include "platform/native_runtime.hpp"
#include "platform/pc_screen.hpp"
#include <d3d11_1.h>
#include <atomic>
#include <cstdio>
namespace outrun::desktop {
namespace {
std::atomic<bool> in_race{false};
ID3D11DeviceContext* context_for{};ID3D11DeviceContext1* context1{};bool missing_logged{};
}
void letterbox_note(const platform::NativeRuntimeContext* rt){
    if(rt)in_race.store(platform::pc_letterbox_in_race(rt->mode_state.current,rt->start_mode.stage),std::memory_order_relaxed);
}
void letterbox_draw(ID3D11DeviceContext* ctx,ID3D11RenderTargetView* view,unsigned width,unsigned height){
    using namespace xbox_runtime;
    if(!ctx||!view||!width||!height)return;
    if(!platform::pc_letterbox_now(platform::g_pc_ui_letterboxing,g_ui_scaling_mode,in_race.load(std::memory_order_relaxed)))return;
    const auto ui=ui_scaling_rect(g_ui_scaling_mode,g_widescreen,platform::g_pc_wide_aspect,double(width)/double(height));
    if(!ui.on||ui.aspect!=4./3.)return;   // the 2D is not in the centred 4:3 (4:3 output, or UIScalingMode 0)
    const auto bars=platform::pc_letterbox_bars(std::int32_t(width),std::int32_t(height));
    if(!bars.any())return;
    if(ctx!=context_for){
        if(context1){context1->Release();context1=nullptr;}
        context_for=ctx;
        if(FAILED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(&context1))))context1=nullptr;
        if(!context1&&!missing_logged){missing_logged=true;
            std::printf("UILetterboxing: the D3D11 context has no ID3D11DeviceContext1 (ClearView); no bars are drawn\n");}
    }
    if(!context1)return;
    D3D11_RECT r[2]{};UINT n=0;
    if(bars.left_end>0)r[n++]=D3D11_RECT{0,0,LONG(bars.left_end),LONG(height)};
    if(bars.right_begin<bars.width)r[n++]=D3D11_RECT{LONG(bars.right_begin),0,LONG(bars.width),LONG(height)};
    const FLOAT black[4]{0.f,0.f,0.f,1.f};
    context1->ClearView(view,black,r,n);
}
void letterbox_release(){
    if(context1){context1->Release();context1=nullptr;}
    context_for=nullptr;
}
}
