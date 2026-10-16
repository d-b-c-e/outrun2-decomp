#include "cd_switcher.hpp"
#include "desktop_keyboard.hpp"
#include "desktop_panel.hpp"
#include "desktop_settings.hpp"
#include "native_window.hpp"
#include "platform/cd_switcher.hpp"
#include "platform/native_runtime.hpp"
#include <windows.h>
#include <cstdio>
namespace outrun::desktop {
namespace {
struct State {
    bool configured{},in_race{};unsigned z{},x{};std::uint32_t clears{};
    std::uint64_t previous{},next{};platform::CdEdges edges;xbox_runtime::InputFilter inner{};
} s;
void input_filter(platform::NativeInputState& out,const platform::NativeRuntimeContext* rt){
    // Preserve the actual pad's connection before wheel/shim filters can mark
    // their own device connected. A disconnected pad must not look released.
    const auto pad=cd_switcher_pad_sample(out);
    if(s.inner)s.inner(out,rt);
    const bool race=rt&&rt->mode_state.current==16u;
    const bool cleared=desktop_key_clears()!=s.clears;s.clears=desktop_key_clears();
    const bool blocked=!race||!s.in_race||desktop_panel_open()||cleared;
    s.in_race=race;
    if(!platform::cd_title_race_owned(rt?rt->mode_state.current:0u,blocked,true))platform::cd_switcher().title.clear();
    const auto action=s.edges.step(desktop_key(s.z),desktop_key(s.x),pad.held,pad.connected,
                                   s.previous,s.next,blocked);
    if(!blocked)(void)platform::cd_switcher().change(action);
    // No keyboard, native pad, wheel or frontend input is consumed or rewritten.
}
}
void cd_switcher_configure(const Ini& settings,bool test_or_playback,bool recording){
    auto& core=platform::cd_switcher();core.enabled=false;core.tracks.clear();core.cursor=0;core.title={};
    if(!settings.flag("CDSwitcher","SwitcherEnable",false))return;
    if(test_or_playback||recording){std::printf("settings: CDSwitcher off in test, playback or recording process\n");return;}
    auto catalog=platform::parse_cd_tracks(settings.section_lines("CDTracks"));
    if(!catalog.error.empty()||catalog.tracks.empty()){
        std::printf("settings: CDSwitcher off: %s\n",catalog.error.empty()?"no CDTracks":catalog.error.c_str());return;}
    if(!platform::parse_cd_combo(settings.text("CDSwitcher","TrackPrevious","RS+Back"),s.previous)||
       !platform::parse_cd_combo(settings.text("CDSwitcher","TrackNext","Back"),s.next)){
        std::printf("settings: CDSwitcher off: invalid pad combination\n");return;}
    s.z=MapVirtualKeyW('Z',MAPVK_VK_TO_VSC)&0xffu;s.x=MapVirtualKeyW('X',MAPVK_VK_TO_VSC)&0xffu;
    if(!s.z||!s.x||s.z>0x7fu||s.x>0x7fu){std::printf("settings: CDSwitcher off: layout has no plain Z/X keys\n");return;}
    bool shuffled=settings.flag("CDSwitcher","SwitcherShuffleTracks",false)&&catalog.tracks.size()>1u;
    if(!platform::shuffle_cd_tracks(catalog.tracks,shuffled,[]{std::random_device entropy;return entropy();})){
        std::printf("settings: CDSwitcher startup shuffle unavailable; using listed order\n");shuffled=false;
    }
    s.clears=desktop_key_clears();s.edges={};s.in_race=false;s.configured=true;
    core.tracks=std::move(catalog.tracks);core.enabled=true;
    core.title.options=platform::cd_title_options(settings.flag("CDSwitcher","SwitcherDisplayTitle",false),
        settings.integer("CDSwitcher","SwitcherTitleFont",2),settings.number("CDSwitcher","SwitcherTitleFontSizeX",0.3f),
        settings.number("CDSwitcher","SwitcherTitleFontSizeY",0.5f),settings.integer("CDSwitcher","SwitcherTitlePositionX",375),
        settings.integer("CDSwitcher","SwitcherTitlePositionY",450));
    std::printf("settings: experimental CDSwitcher: %zu %s tracks, Z/X press or pad release, GAME only; input still reaches game\n",
        core.tracks.size(),shuffled?"shuffled":"listed");
    if(core.title.options.enabled)std::printf("settings: experimental CD title: 120 updates/45 fade, GAME HUD only, font %u, scale %.2f/%.2f, position %d/%d\n",
        core.title.options.font,double(core.title.options.scale_x),double(core.title.options.scale_y),core.title.options.x,core.title.options.y);
}
void cd_switcher_wrap_input_filter(){
    if(!s.configured)return;
    auto& filter=xbox_runtime::input_filter();if(filter==input_filter)return;
    s.inner=filter;filter=input_filter;
}
}
