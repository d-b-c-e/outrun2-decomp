#pragma once
// The display's logical frame (the renderer's 1280x720, or the setup screen's size) on a swap chain of any size: the
// largest centred rectangle of the frame's aspect, in whole pixels, black around it. Never a stretch: before this, a
// window that was not 16:9 (an ultrawide, a three-monitor span) stretched the whole frame (TRIPLE-SCREENS.md).
// Pure, tested in windows/tests/test_triple_screens.cpp.
#include <cstdint>
#include <string>
#include <string_view>
namespace outrun::xbox_runtime {
struct DisplayFit { unsigned x{},y{},width{},height{}; };
inline DisplayFit display_fit(unsigned chain_w,unsigned chain_h,unsigned frame_w,unsigned frame_h){
    if(!chain_w||!chain_h||!frame_w||!frame_h)return {0,0,chain_w,chain_h};
    const std::uint64_t wide=std::uint64_t(chain_w)*frame_h,tall=std::uint64_t(chain_h)*frame_w;
    if(wide>tall){   // wider than the frame: full height, centred columns
        const unsigned w=unsigned((std::uint64_t(chain_h)*frame_w+frame_h/2u)/frame_h);
        return {(chain_w-w)/2u,0,w,chain_h};
    }
    if(wide<tall){   // taller than the frame: full width, centred rows
        const unsigned h=unsigned((std::uint64_t(chain_w)*frame_h+frame_w/2u)/frame_w);
        return {0,(chain_h-h)/2u,chain_w,h};
    }
    return {0,0,chain_w,chain_h};
}
// The planar span's scene width (TRIPLE-SCREENS.md step 2): the chosen height times the window's aspect, rounded to
// whole pixels and then up to even, as resolution_at does for 16:9.
inline unsigned planar_width(unsigned height,float aspect){
    const auto w=std::uint64_t(double(height)*double(aspect)+0.5);
    return unsigned((w+1u)&~std::uint64_t(1));
}
// [Graphics] UIScalingMode (the Tweaks key; windows/docs/UI-SCALING.md step 1). Where the pretransformed 2D layer goes
// on a frame wider than 4:3: rect on = a centred rectangle `aspect` wide per unit of height (the device's ui rect),
// off = stretched over the whole back buffer.
// - 2, keep centred (the default here until step 2 is seen): the centred 4:3, on 16:9 and on a span, as before.
// - 0, stretched: over the 16:9 frame; on a span over its centre panel only (a third of the span's width), so the HUD
//   stays on the centre monitor (STD-007).
// - Tweaks' 1, Online Arcade (keep centred plus HUD spacing to the edges), is not built yet: parse_ui_scaling_mode
//   gives 2 for it, with a note.
struct UiScalingRect { bool on{}; double aspect{}; };
inline UiScalingRect ui_scaling_rect(unsigned mode,bool widescreen,float span_aspect){
    const bool span=span_aspect>0.f;
    if(mode==0u)return span?UiScalingRect{true,double(span_aspect)/3.}:UiScalingRect{false,0.};
    return {widescreen||span,4./3.};
}
// The key's value: "0" or "2" (blanks around ignored; empty = 2). "1" and anything else give 2, and `note` says why.
inline unsigned parse_ui_scaling_mode(std::string_view text,std::string& note){
    while(!text.empty()&&(text.front()==' '||text.front()=='\t'))text.remove_prefix(1);
    while(!text.empty()&&(text.back()==' '||text.back()=='\t'||text.back()=='\r'))text.remove_suffix(1);
    note.clear();
    if(text.empty()||text=="2")return 2u;
    if(text=="0")return 0u;
    if(text=="1")note="UIScalingMode = 1 (Online Arcade: HUD spacing to the screen edges) is not built yet; 2 (keep centred) applies";
    else note="UIScalingMode = "+std::string(text.substr(0,32))+" refused (0, 1 or 2); 2 (keep centred) applies";
    return 2u;
}
}
