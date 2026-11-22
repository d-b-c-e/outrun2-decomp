#pragma once
// [Graphics] WideMenus (on by default; Wheelkit issue outrun-2006-0015): the menus fill the 16:9 screen (on three
// screens, the center screen) instead of a 4:3 panel with black beside it. The game's menus are 640x480 pictures, so
// nothing is redrawn: each 2D draw of a menu frame is placed by what it is, from its rectangle in the 640x480 UI.
// - The backgrounds (the full-height panels, the header bar, the button strip at the bottom, the header's rule) are
//   stretched across the 16:9 width about the center. They carry no text or cars.
// - The dark tab under the header, drawn from beyond the left edge, is lengthened to the screen's edge; its end cap
//   stays.
// - The header's status line and title move to the left side, the badge to the right; the button prompts at the bottom
//   move to their side (left half left, right half right). They keep their size: Tweaks' HUD amount (`shift`).
// - Everything else (the menu's own text, cars, maps, people) stays where the 4:3 menu had it, at its size, and is
//   clipped to the 4:3 as before, so the pieces the game parks beyond its edges stay hidden.
// The race HUD is not touched (UIScalingMode applies there). Pure, tested in windows/tests/test_triple_screens.cpp.
#include <cmath>
namespace outrun::xbox_runtime {
enum class MenuPlace { keep, stretch, extend, shift };
struct MenuDraw { MenuPlace place{MenuPlace::keep}; float dx{}; };
// x0..x1, y0..y1: the draw's rectangle in 640x480 UI units.
inline MenuDraw menu_place(float x0,float x1,float y0,float y1,float shift){
    const float w=x1-x0,h=y1-y0,cx=(x0+x1)*.5f;
    const bool left_edge=x0<=4.f,right_edge=x1>=636.f;
    if(y0<=2.f&&y1>=478.f)return {MenuPlace::stretch,0.f};                                    // a full-height panel
    if(w>=600.f)return {MenuPlace::stretch,0.f};                                               // the header's rule
    if(y0<=2.f&&y1>=60.f&&y1<=100.f&&w>=100.f&&(left_edge||right_edge))return {MenuPlace::stretch,0.f};   // the header bar
    if(y0>=400.f&&h>=60.f)return {MenuPlace::stretch,0.f};                                     // the button strip's tiles
    if(y1<=100.f){                                                                             // the header's band
        if((x0<-1.f||x1>641.f)&&w>=300.f)return {MenuPlace::extend,0.f};                       // the tab under the header
        if(y1<=20.f)return {MenuPlace::shift,-shift};                                          // the status line
        if(y0>=25.f&&y1<=76.f)return {MenuPlace::shift,-shift};                                // the title and its glint
        if(x0>=480.f&&x1<=641.f)return {MenuPlace::shift,shift};                               // the badge
        return {};
    }
    if(y0>=405.f&&y1<=482.f)return {MenuPlace::shift,cx<320.f?-shift:shift};                  // the button prompts
    return {};
}
// Where an x of a draw goes (UI units; `margin` the 16:9's extra width on each side of the 640).
inline float menu_x(MenuPlace place,float dx,float x,float margin){
    switch(place){
    case MenuPlace::stretch:return 320.f+(x-320.f)*(640.f+2.f*margin)/640.f;
    case MenuPlace::extend:return x<=0.f?x-margin:x>=640.f?x+margin:x;
    case MenuPlace::shift:return x+dx;
    default:return x;
    }
}
// A glyph run (the same texture and line, each glyph at most `gap` past the last) moves as one: the first glyph's
// placement holds for the run, so a word is never split between two places.
struct MenuRun {
    unsigned texture{};float y0{},y1{},x0{},x1{};MenuDraw draw{};bool live{};
    bool continues(unsigned t,float a0,float a1,float b0)const{
        return live&&t==texture&&std::fabs(a0-y0)<.5f&&std::fabs(a1-y1)<.5f&&b0>=x0-1.f&&b0<=x1+12.f;}
};
}
