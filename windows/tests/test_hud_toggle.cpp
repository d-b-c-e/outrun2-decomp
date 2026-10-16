// [Misc] HudToggleKey (windows/source/hud_toggle.hpp, src/platform/pc_screen.hpp): the key name as Tweaks' StringToVK
// takes it, its release edge, the process policy and Tweaks' flip of the display flag. No game, window or keyboard.
#include "hud_toggle.hpp"
#include "platform/pc_screen.hpp"
#include <cstdio>
#include <cstdlib>
namespace {
int checks=0;
void check(bool v,const char* what){++checks;if(!v){std::fprintf(stderr,"hud toggle: FAIL: %s\n",what);std::exit(1);}}
// A US layout's letter and digit keys (the game asks MapVirtualKeyW): P is 0x19, 1 is 0x02; no key for Q here.
unsigned fake_layout(char c){return c=='P'?0x19u:c=='1'?0x02u:c=='X'?0xe0u:0u;}
bool keys(const outrun::desktop::HudToggleKey& k,std::vector<unsigned> want){return k.keys==want&&k.why.empty();}
}
int main(){
    using namespace outrun::desktop;
    // Names: unset is off without a reason; Tweaks' names, any case, trimmed; a name may cover both keys of a pair.
    check(parse_hud_toggle_key("",fake_layout).keys.empty()&&parse_hud_toggle_key("  ",fake_layout).why.empty(),"unset: off, nothing to report");
    check(keys(parse_hud_toggle_key("F10",fake_layout),{0x44}),"F10");
    check(keys(parse_hud_toggle_key(" home ",fake_layout),{0xc7}),"home, lower case and spaces: the extended Home key");
    check(keys(parse_hud_toggle_key("p",fake_layout),{0x19}),"a letter goes through the layout (upper-cased first)");
    check(keys(parse_hud_toggle_key("1",fake_layout),{0x02}),"a digit is the top-row key");
    check(keys(parse_hud_toggle_key("SHIFT",fake_layout),{0x2a,0x36})&&keys(parse_hud_toggle_key("RETURN",fake_layout),{0x1c,0x9c}),"SHIFT and RETURN: either key");
    check(keys(parse_hud_toggle_key("NUMPAD5",fake_layout),{0x4c})&&keys(parse_hud_toggle_key("F24",fake_layout),{0x76}),"numpad and F13-F24 positions");
    check(parse_hud_toggle_key("Q",fake_layout).why.find("no key for Q")!=std::string::npos,"a letter the layout lacks is refused");
    check(parse_hud_toggle_key("X",fake_layout).keys.empty(),"a layout answer that is not a plain scan code is refused");
    // Refused: the desktop's own keys, mouse buttons, keys this path cannot see, unknown names. Never silently off.
    check(parse_hud_toggle_key("F6",fake_layout).why=="F6 opens the settings panel","F6 is the panel's");
    check(parse_hud_toggle_key("f8",fake_layout).why=="F8 is the panel's Stop FFB","F8 is the panel's Stop FFB");
    check(parse_hud_toggle_key("F11",fake_layout).why=="F11 stops a recording","F11 stops a recording");
    check(parse_hud_toggle_key("LBUTTON",fake_layout).why.find("mouse button")!=std::string::npos,"a mouse button is refused");
    check(parse_hud_toggle_key("SNAPSHOT",fake_layout).why.find("no key-down")!=std::string::npos,"Print Screen is refused");
    check(parse_hud_toggle_key("PAUSE",fake_layout).keys.empty()&&!parse_hud_toggle_key("PAUSE",fake_layout).why.empty(),"Pause is refused with a reason");
    check(parse_hud_toggle_key("BANANA",fake_layout).why.find("unknown key name")!=std::string::npos,"an unknown name is refused");
    check(parse_hud_toggle_key("-",fake_layout).why.find("unknown key name")!=std::string::npos,"a symbol is not a Tweaks name");
    // F6's complete settings vocabulary: every offered key parses, except a layout can lack a letter/digit.
    const auto& choices=hud_toggle_setting_choices();
    check(choices.size()==97u&&choices.front().empty(),"Off plus all 96 supported ASCII key names");
    for(std::size_t i=1;i<choices.size();++i){
        const auto parsed=parse_hud_toggle_key(choices[i],+[](char){return 1u;});
        check(!parsed.keys.empty()&&parsed.why.empty(),"every offered name is accepted by the existing parser");
        check(hud_toggle_setting_index(choices[i])==int(i),"choices have no duplicates and round-trip");
    }
    check(hud_toggle_setting_index(" home ")>=0&&choices[std::size_t(hud_toggle_setting_index(" home "))]=="HOME","case and spaces select the saved key");
    check(hud_toggle_setting_index("  ")==0,"blank is Off");
    for(const char* name:{"F6","F8","F11","PAUSE","BANANA","!"})
        check(hud_toggle_setting_index(name)==-1,"reserved/unsupported saved text keeps its own entry");
    check(hud_toggle_setting_name(" owner-unknown ")=="OWNER-UNKNOWN","unknown names stay distinct from Off");
    // The edge: a toggle on the release only (Tweaks), once per press; holding does nothing.
    {   HudToggleEdge e;
        check(!e.step(false,false)&&!e.step(true,false)&&!e.step(true,false),"press and hold: nothing yet");
        check(e.step(false,false),"release: one toggle");
        check(!e.step(false,false),"still released: nothing more");
        check(!e.step(true,false)&&e.step(false,false),"the next press and release: the next toggle");}
    // Blocked (panel open, window not in front): a key held into or out of it is not a press; the next one is.
    {   HudToggleEdge e;
        check(!e.step(true,false)&&!e.step(true,true)&&!e.step(false,false),"pressed, then the panel opens, released after: no toggle");
        check(!e.step(true,false)&&e.step(false,false),"after that, a press and release toggles");
        check(!e.step(false,true)&&!e.step(true,false)&&!e.step(false,false),"held at the moment it unblocks: waits for that release");
        check(!e.step(true,false)&&e.step(false,false),"then toggles again");}
    // Blocked by a counted clear of the window's keyboard (desktop_keys_clear), not by the foreground window: keys posted
    // to the game's window count whether or not it is in front (sana 89823).
    {   std::uint32_t seen=0;
        check(!hud_toggle_blocked(false,0u,seen)&&hud_toggle_blocked(true,0u,seen),"the open panel blocks; nothing else at a steady count");
        check(hud_toggle_blocked(false,1u,seen)&&seen==1u&&!hud_toggle_blocked(false,1u,seen),"a clear blocks the one check that sees it");}
    {   // Held, then a focus loss clears it: the poll sees the key up, but that is not its release.
        HudToggleEdge e;std::uint32_t seen=0,clears=0;
        check(!e.step(true,hud_toggle_blocked(false,clears,seen)),"held");
        ++clears;
        check(!e.step(false,hud_toggle_blocked(false,clears,seen)),"focus loss clears every key: no toggle");
        check(!e.step(false,hud_toggle_blocked(false,clears,seen)),"the next poll, key up: the wait ends");
        check(!e.step(true,hud_toggle_blocked(false,clears,seen))&&e.step(false,hud_toggle_blocked(false,clears,seen)),"the next real press and release toggles");}
    {   // A focus loss, then the focus back and the key down again before the next poll: still not a press of its own.
        HudToggleEdge e;std::uint32_t seen=0,clears=0;
        check(!e.step(true,hud_toggle_blocked(false,clears,seen)),"held");
        ++clears;   // cleared, refocused and pressed again between two polls
        check(!e.step(true,hud_toggle_blocked(false,clears,seen)),"down again across a clear: blocked, waits for the release");
        check(!e.step(false,hud_toggle_blocked(false,clears,seen)),"that release does not toggle");
        check(!e.step(true,hud_toggle_blocked(false,clears,seen))&&e.step(false,hud_toggle_blocked(false,clears,seen)),"the press after it does");}
    {   // The F6 panel: blocked while open; its closing clear blocks one more check; then presses count again.
        HudToggleEdge e;std::uint32_t seen=0,clears=0;
        check(!e.step(true,hud_toggle_blocked(true,clears,seen))&&!e.step(false,hud_toggle_blocked(true,clears,seen)),"pressed and released with the panel open: nothing");
        ++clears;   // the panel closed: its filter clears the keyboard
        check(!e.step(false,hud_toggle_blocked(false,clears,seen)),"the close's clear: nothing");
        check(!e.step(false,hud_toggle_blocked(false,clears,seen)),"the next poll, key up: the wait ends");
        check(!e.step(true,hud_toggle_blocked(false,clears,seen))&&e.step(false,hud_toggle_blocked(false,clears,seen)),"after the panel: a press and release toggles");}
    // Policy: only ordinary play that is not recording; never a test, playback or recording process.
    check(hud_toggle_allowed(false,false)&&!hud_toggle_allowed(true,false)&&!hud_toggle_allowed(false,true)&&!hud_toggle_allowed(true,true),
        "the toggle posts only in ordinary play without a recording");
    // Tweaks' flip of 8447F8, once per request.
    using namespace outrun::platform;
    check(PcNaviDisplayFlag==0x8447f8u,"NaviPub_Disp's flag (Tweaks' 4447F8 above the image base)");
    check(pc_hud_toggle_apply(1u,0u)==1u&&pc_hud_toggle_apply(1u,1u)==0u&&pc_hud_toggle_apply(0u,1u)==1u,"one request flips 1 and 0");
    check(pc_hud_toggle_apply(1u,2u)==1u&&pc_hud_toggle_apply(0u,3u)==1u&&pc_hud_toggle_apply(1u,4u)==1u,"several requests flip in turn");
    check(pc_hud_toggle_apply(5u,1u)==0u&&pc_hud_toggle_apply(5u,2u)==1u,"any non-zero value counts as shown, as in Tweaks");
    check(g_pc_hud_toggle_requests.load()==0u,"no request before a press");
    g_pc_hud_toggle_requests.fetch_add(1u);g_pc_hud_toggle_requests.fetch_add(1u);g_pc_hud_toggle_requests.fetch_add(1u);
    check(g_pc_hud_toggle_requests.exchange(0u)==3u&&g_pc_hud_toggle_requests.load()==0u,"the HUD takes all requests posted since its last display");
    std::printf("hud toggle: %d checks passed\n",checks);
    return 0;
}
