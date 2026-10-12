#pragma once
// [Graphics] UIScalingMode 1's HUD spacing (windows/docs/UI-SCALING.md step 2a; Tweaks hooks_uiscaling.cpp). The
// reconstructed HUD sets a scope around the routines Tweaks spaces (UiSpacingGuard restores the previous one on every
// exit); sprani_draw_428a10 stores it in each queued draw (race_hud.hpp SpraniDraw::spacing); layer_429350 moves that
// draw's footage layers by pc_screen.hpp g_pc_ui_spacing. Pure apart from the scope itself (pc_sprite_2d.cpp).
#include <cstdint>
namespace outrun::platform {
enum class UiSpacing : std::uint8_t { off, detect, left, right };
UiSpacing& ui_spacing_scope();
struct UiSpacingGuard {
    UiSpacing saved;
    explicit UiSpacingGuard(UiSpacing s):saved(ui_spacing_scope()){ui_spacing_scope()=s;}
    ~UiSpacingGuard(){ui_spacing_scope()=saved;}
    UiSpacingGuard(const UiSpacingGuard&)=delete;
    UiSpacingGuard& operator=(const UiSpacingGuard&)=delete;
};
// Tweaks' drawFootage rule: the shift of a footage layer whose matrix puts it at x (640-wide UI units). Detect moves the
// left third (x < 640/3) by -spacing and the right third (x >= 1280/3) by +spacing; left / right force the direction;
// off, or spacing 0, moves nothing.
inline float ui_spacing_shift(UiSpacing s,float x,float spacing){
    if(!(spacing>0.0f)||s==UiSpacing::off)return 0.0f;
    if(s==UiSpacing::left||(s==UiSpacing::detect&&x<640.0f/3.0f))return -spacing;
    if(s==UiSpacing::right||(s==UiSpacing::detect&&x>=1280.0f/3.0f))return spacing;
    return 0.0f;
}
}
