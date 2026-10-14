// [Misc] HudToggleKey (hud_toggle.hpp): the window's keyboard, polled in an input filter on the game thread. Nothing is
// consumed or changed in the game's input; a release of the key posts one toggle (platform/pc_screen.hpp).
#include "hud_toggle.hpp"
#include "desktop_keyboard.hpp"
#include "desktop_panel.hpp"
#include "desktop_settings.hpp"
#include "native_window.hpp"
#include "platform/pc_screen.hpp"
#include <windows.h>
#include <cstdio>
namespace outrun::desktop {
namespace {
struct State {
    bool configured{};
    std::vector<unsigned> keys;
    HudToggleEdge edge;
    std::uint32_t clears{};   // desktop_key_clears() at the last check
    xbox_runtime::InputFilter inner{};
} h;
// Letters and digits by the layout, as Tweaks' VK: never extended keys, so the plain scan code is the key.
unsigned layout_scan(char ch){return MapVirtualKeyW(unsigned(static_cast<unsigned char>(ch)),MAPVK_VK_TO_VSC)&0xffu;}
void input_filter(platform::NativeInputState& out,const platform::NativeRuntimeContext* rt){
    if(h.inner)h.inner(out,rt);
    bool down=false;
    for(const unsigned key:h.keys)down=down||desktop_key(key);
    const bool blocked=hud_toggle_blocked(desktop_panel_open(),desktop_key_clears(),h.clears);
    if(h.edge.step(down,blocked))platform::g_pc_hud_toggle_requests.fetch_add(1u);
}
}
void hud_toggle_configure(const Ini& settings,bool test_or_playback,bool recording){
    const std::string text=settings.text("Misc","HudToggleKey");
    const auto key=parse_hud_toggle_key(text,layout_scan);
    if(key.keys.empty()){
        if(!key.why.empty())std::printf("settings: [Misc] HudToggleKey = %.32s refused (%s); no HUD toggle\n",text.c_str(),key.why.c_str());
        return;
    }
    if(!hud_toggle_allowed(test_or_playback,recording)){
        std::printf("settings: [Misc] HudToggleKey = %.32s is off in this process: a recording, playback or test run keeps the HUD as "
            "the game sets it, so no tape holds a toggled HUD\n",text.c_str());
        return;
    }
    h.keys=key.keys;h.clears=desktop_key_clears();h.configured=true;
    std::printf("settings: [Misc] HudToggleKey = %.32s: each release shows or hides the race HUD; the key still reaches the game\n",text.c_str());
}
void hud_toggle_wrap_input_filter(){
    if(!h.configured)return;
    auto& filter=xbox_runtime::input_filter();
    if(filter==input_filter)return;
    h.inner=filter;filter=input_filter;
}
}
