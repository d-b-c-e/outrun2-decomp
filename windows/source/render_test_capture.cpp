// A rendering test's frame burst (render_test.hpp): the present path's frame_capture, armed by the race entry.
#include "render_test.hpp"
#include "frame_capture.hpp"
#include "native_window.hpp"
#include "platform/native_runtime.hpp"
namespace outrun::desktop {
namespace {
xbox_runtime::InputFilter inner=nullptr;
// The first race input sample arms the burst: GAME (16) or 18, as the input shim's race flag.
void arm_filter(platform::NativeInputState& out,const platform::NativeRuntimeContext* rt){
    if(inner)inner(out,rt);
    if(rt&&(rt->mode_state.current==16u||rt->mode_state.current==18u))xbox_runtime::frame_capture().armed.store(true,std::memory_order_relaxed);
}
}
void render_test_start_capture(const RenderTest& test,const std::wstring& run_root){
    if(!test.requested||test.frames==0u)return;
    auto& cap=xbox_runtime::frame_capture();
    cap.folder=run_root+L"\\frames";cap.frames=test.frames;cap.every=test.every;cap.after=test.after;cap.scale=test.scale;
    auto& filter=xbox_runtime::input_filter();
    if(filter==arm_filter)return;
    inner=filter;filter=arm_filter;
}
}
