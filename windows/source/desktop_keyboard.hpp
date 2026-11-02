#pragma once
// The desktop window's keyboard (window_desktop.cpp): DIK key states, 0x80 while down, all up after the window
// loses the focus. Written and read on the game thread (the window's message pump runs in the game loop).
#include <array>
#include <cstdint>
namespace outrun::desktop {
inline std::array<std::uint8_t,256>& desktop_keys(){static std::array<std::uint8_t,256> keys{};return keys;}
inline bool desktop_key(unsigned dik){return (desktop_keys()[dik&0xffu]&0x80u)!=0;}
inline bool desktop_any_key(){for(auto k:desktop_keys())if(k&0x80u)return true;return false;}
// Dedicated desktop controls may act while coasting without handing the wheel's
// driving record to the keyboard. The keys themselves remain untouched, so they
// still reach native input when the keyboard already owns driving.
inline std::array<bool,256>& desktop_camera_shortcut_keys(){static std::array<bool,256> keys{};return keys;}
inline bool desktop_any_driving_key(){
    for(unsigned scan=0;scan<256;++scan){
        // Shift may arrive a frame before F7. Treat it as a camera modifier for
        // handover while F7 is registered; raw Shift still reaches native input
        // whenever the keyboard already owns driving.
        if(desktop_camera_shortcut_keys()[0x41]&&(scan==0x2a||scan==0x36))continue;
        if(desktop_key(scan)&&scan!=0x40u&&scan!=0x42u&&scan!=0x57u&&!desktop_camera_shortcut_keys()[scan])return true;
    }
    return false;
}
// How many times every key was released at once (desktop_keys_clear). A reader that compares it can tell such a clear
// from a key's own release (hud_toggle.hpp).
inline std::uint32_t& desktop_key_clears(){static std::uint32_t clears{};return clears;}
// Every key up at once, counted: the window lost the focus (window_desktop.cpp), or the F6 panel opened or closed (its
// hook took the key-ups meanwhile; desktop_panel.cpp). Every such clear goes through here.
inline void desktop_keys_clear(){desktop_keys().fill(0);++desktop_key_clears();}
}
