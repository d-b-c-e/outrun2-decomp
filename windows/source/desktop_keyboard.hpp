#pragma once
// The desktop window's keyboard (window_desktop.cpp): DIK key states, 0x80 while down, all up after the window
// loses the focus. Written and read on the game thread (the window's message pump runs in the game loop).
#include <array>
#include <cstdint>
namespace outrun::desktop {
inline std::array<std::uint8_t,256>& desktop_keys(){static std::array<std::uint8_t,256> keys{};return keys;}
inline bool desktop_key(unsigned dik){return (desktop_keys()[dik&0xffu]&0x80u)!=0;}
inline bool desktop_any_key(){for(auto k:desktop_keys())if(k&0x80u)return true;return false;}
}
