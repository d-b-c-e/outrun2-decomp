#pragma once
// [Misc] HudToggleKey (the Tweaks key; Tweaks hooks_input.cpp HudToggleUpdate / StringToVK): one keyboard key whose
// release shows or hides the race HUD, through NaviPub_Disp's display flag (platform/pc_screen.hpp). Unset, nothing.
// Tweaks keeps the key in [Controls]; here that section is the ini-controls-1 contract (any other key is an invalid
// binding), so it lives in [Misc], like DefaultManualTransmission.
//
// Replay: the toggle is presentation, outside the driving tape. It never posts in a recording, playback or test
// process, so a tape is never made or played with a toggled HUD, and the tape format does not change. The key itself is
// not consumed: the game, its menus and a recording see it exactly as before (Tweaks reads it with GetAsyncKeyState and
// consumes nothing either). A key the game also binds therefore does both, as in Tweaks. F6 (the settings panel), F8
// (the panel's Stop FFB) and F11 (stops a recording) are refused.
//
// Pure parts here: the key name, the release edge and the process policy. hud_toggle.cpp polls the window's keyboard
// (desktop_keyboard.hpp) on the game thread.
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace outrun::desktop {
// The key as desktop_keys() holds it: the scan code, | 0x80 for an extended key (window_desktop.cpp). A name may stand
// for more than one key (SHIFT: either Shift, RETURN: either Enter), as GetAsyncKeyState's VK does.
struct HudToggleKey {
    std::vector<unsigned> keys;   // empty: off
    std::string why;              // why it is off; empty when unset or accepted
};
// The scan code the current keyboard layout gives a letter or digit key (MapVirtualKeyW in the game), 0 if none.
using ScanOfChar=unsigned(*)(char);
inline HudToggleKey parse_hud_toggle_key(std::string_view text,ScanOfChar scan_of_char){
    while(!text.empty()&&std::isspace(static_cast<unsigned char>(text.front())))text.remove_prefix(1);
    while(!text.empty()&&std::isspace(static_cast<unsigned char>(text.back())))text.remove_suffix(1);
    if(text.empty())return {};
    std::string name(text);
    for(auto& ch:name)ch=char(std::toupper(static_cast<unsigned char>(ch)));
    HudToggleKey k;
    // Tweaks' names (StringToVK): a letter or digit, by the layout, or a virtual-key name, matched here by key position.
    struct Named{const char* name;std::vector<unsigned> keys;};
    static const std::vector<Named> named={
        {"BACK",{0x0e}},{"TAB",{0x0f}},{"CLEAR",{0x4c}},{"RETURN",{0x1c,0x9c}},{"SHIFT",{0x2a,0x36}},{"CONTROL",{0x1d,0x9d}},
        {"MENU",{0x38,0xb8}},{"CAPITAL",{0x3a}},{"ESCAPE",{0x01}},{"SPACE",{0x39}},{"PRIOR",{0xc9}},{"NEXT",{0xd1}},
        {"END",{0xcf}},{"HOME",{0xc7}},{"LEFT",{0xcb}},{"UP",{0xc8}},{"RIGHT",{0xcd}},{"DOWN",{0xd0}},{"INSERT",{0xd2}},
        {"DELETE",{0xd3}},{"LWIN",{0xdb}},{"RWIN",{0xdc}},{"APPS",{0xdd}},
        {"NUMPAD0",{0x52}},{"NUMPAD1",{0x4f}},{"NUMPAD2",{0x50}},{"NUMPAD3",{0x51}},{"NUMPAD4",{0x4b}},
        {"NUMPAD5",{0x4c}},{"NUMPAD6",{0x4d}},{"NUMPAD7",{0x47}},{"NUMPAD8",{0x48}},{"NUMPAD9",{0x49}},
        {"MULTIPLY",{0x37}},{"ADD",{0x4e}},{"SUBTRACT",{0x4a}},{"DECIMAL",{0x53}},{"DIVIDE",{0xb5}},
        {"F1",{0x3b}},{"F2",{0x3c}},{"F3",{0x3d}},{"F4",{0x3e}},{"F5",{0x3f}},{"F6",{0x40}},{"F7",{0x41}},{"F8",{0x42}},
        {"F9",{0x43}},{"F10",{0x44}},{"F11",{0x57}},{"F12",{0x58}},{"F13",{0x64}},{"F14",{0x65}},{"F15",{0x66}},
        {"F16",{0x67}},{"F17",{0x68}},{"F18",{0x69}},{"F19",{0x6a}},{"F20",{0x6b}},{"F21",{0x6c}},{"F22",{0x6d}},
        {"F23",{0x6e}},{"F24",{0x76}},{"SCROLL",{0x46}}};
    // Tweaks' other names, which this keyboard path cannot take.
    static const std::vector<std::pair<const char*,const char*>> refused={
        {"LBUTTON","a mouse button"},{"RBUTTON","a mouse button"},{"MBUTTON","a mouse button"},{"XBUTTON1","a mouse button"},
        {"XBUTTON2","a mouse button"},{"CANCEL","Ctrl+Break"},{"PAUSE","not supported here"},{"NUMLOCK","not supported here"},
        {"SNAPSHOT","Print Screen sends no key-down to the game window"},{"SELECT","not on a standard keyboard"},
        {"PRINT","not on a standard keyboard"},{"EXECUTE","not on a standard keyboard"},{"HELP","not on a standard keyboard"},
        {"SLEEP","not on a standard keyboard"},{"SEPARATOR","not on a standard keyboard"}};
    if(name.size()==1&&std::isalnum(static_cast<unsigned char>(name[0]))){
        const unsigned scan=scan_of_char?scan_of_char(name[0]):0u;
        if(!scan||scan>0x7fu){k.why="this keyboard layout has no key for "+name;return k;}
        k.keys={scan};
    }else{
        for(const auto& n:named)if(name==n.name){k.keys=n.keys;break;}
        if(k.keys.empty()){
            for(const auto& r:refused)if(name==r.first){k.why=std::string(r.second)+"; choose another key";return k;}
            k.why="unknown key name (Tweaks' names: a letter or digit, F1-F24, HOME, END, INSERT, DELETE, SPACE, NUMPAD0-NUMPAD9 ...)";
            return k;
        }
    }
    for(const unsigned key:k.keys){
        const char* taken=key==0x40u?"F6 opens the settings panel":key==0x42u?"F8 is the panel's Stop FFB":key==0x57u?"F11 stops a recording":nullptr;
        if(taken){k.keys.clear();k.why=taken;return k;}
    }
    return k;
}
// Blocked: the F6 panel is open (it takes the keys), or the window's keyboard was cleared since the last check
// (desktop_keys_clear: a focus loss, or the panel opening or closing, releases every key at once, which is not a release
// of the HUD key, even if the key is down again by the next check). `seen` keeps the clear count of the last check. The window need not be in front: only keys delivered to the game's own window count, so a key typed in
// another program never reaches it (Tweaks reads the global key state).
inline bool hud_toggle_blocked(bool panel_open,std::uint32_t clears,std::uint32_t& seen){
    const bool cleared=clears!=seen;
    seen=clears;
    return panel_open||cleared;
}
// Tweaks' edge: the toggle is on the key's release. While blocked, and after it until the key is released, nothing
// toggles: a key held into or out of the panel, or across a focus loss, is not a press.
struct HudToggleEdge {
    bool prev{},wait{};
    bool step(bool down,bool blocked){
        if(blocked){wait=true;prev=down;return false;}
        if(wait){wait=down;prev=down;return false;}
        if(prev==down)return false;
        prev=down;
        return !down;
    }
};
// Toggles post only in ordinary play that is not recording: never in a test (latched) or playback process, and never
// in a Play with Recording process, so no tape is made or replayed with a HUD the player toggled.
inline bool hud_toggle_allowed(bool test_or_playback,bool recording){return !test_or_playback&&!recording;}
class Ini;
// Once, before the game runs: reads [Misc] HudToggleKey and logs what it does in this process.
void hud_toggle_configure(const Ini& settings,bool test_or_playback,bool recording);
// The toggle's input filter after the current one (before the F6 panel's, which stays outermost). Configured off: none.
void hud_toggle_wrap_input_filter();
}
