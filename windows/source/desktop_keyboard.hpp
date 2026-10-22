#pragma once
// The desktop window's keyboard (window_desktop.cpp): DIK key states, 0x80 while down, all up after the window
// loses the focus. Written and read on the game thread (the window's message pump runs in the game loop).
#include <array>
#include <cstdint>
namespace outrun::desktop {
inline std::array<std::uint8_t,256>& desktop_keys(){static std::array<std::uint8_t,256> keys{};return keys;}
inline bool desktop_key(unsigned dik){return (desktop_keys()[dik&0xffu]&0x80u)!=0;}
// Presentation shortcuts must not hand steering custody from a wheel to the keyboard.
inline bool (*&desktop_presentation_key())(unsigned){static bool (*test)(unsigned)=nullptr;return test;}
inline bool desktop_any_key(){
    for(unsigned i=0;i<desktop_keys().size();++i){
        if((desktop_keys()[i]&0x80u)&&(!desktop_presentation_key()||!desktop_presentation_key()(i)))return true;
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
