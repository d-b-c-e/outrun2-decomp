#include "level_select_input.hpp"
#include "desktop_keyboard.hpp"
#include "desktop_panel.hpp"
#include "native_window.hpp"
#include "platform/level_select.hpp"
#include "platform/runtime_input.hpp"
namespace outrun::desktop {
namespace {
bool enabled{};
xbox_runtime::InputFilter inner{};
void input_filter(platform::NativeInputState& out,const platform::NativeRuntimeContext* rt){
    if(inner)inner(out,rt);
    // The snapshot belongs to the game's window and is cleared on focus loss.
    // Keep all original records intact; only 4E7800 consumes this separate bit.
    out.level_select_shift=platform::level_select_shift_sample(enabled,desktop_panel_open(),desktop_keys());
}
}
void level_select_input_configure(bool on,bool test_or_playback){enabled=on&&!test_or_playback;}
void level_select_wrap_input_filter(){
    if(!enabled)return;
    auto& filter=xbox_runtime::input_filter();
    if(filter==input_filter)return;
    inner=filter;filter=input_filter;
}
}
