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
// - 1, Online Arcade (step 2a, partial): the centred 4:3 plus `spacing`, (aspect * 480 - 640) / 2 UI units of the frame
//   (or of a span's centre panel), by which the device widens the 2D mapping at the same scale and the scoped HUD moves.
struct UiScalingRect { bool on{}; double aspect{}; double spacing{}; };
inline UiScalingRect ui_scaling_rect(unsigned mode,bool widescreen,float span_aspect,double frame_aspect=16./9.){
    const bool span=span_aspect>0.f;
    if(mode==0u)return span?UiScalingRect{true,double(span_aspect)/3.,0.}:UiScalingRect{false,0.,0.};
    const bool on=widescreen||span;
    if(mode==1u&&on){
        const double a=span?double(span_aspect)/3.:frame_aspect,spacing=(a*480.-640.)/2.;
        return {true,4./3.,spacing>0.?spacing:0.};
    }
    return {on,4./3.,0.};
}
// The key's value: "0", "1" or "2" (blanks around ignored; empty = 2); anything else gives 2, and `note` says why. 1 is
// partial for now (step 2a), and `note` says so.
inline unsigned parse_ui_scaling_mode(std::string_view text,std::string& note){
    while(!text.empty()&&(text.front()==' '||text.front()=='\t'))text.remove_prefix(1);
    while(!text.empty()&&(text.back()==' '||text.back()=='\t'||text.back()=='\r'))text.remove_suffix(1);
    note.clear();
    if(text.empty()||text=="2")return 2u;
    if(text=="0")return 0u;
    if(text=="1"){note="UIScalingMode = 1 (Online Arcade) is partial: the main race HUD (NaviPub_Disp) is spaced to the edges; "
                       "time attack, the ghost gap, C2C and the put_scroll positions are not yet";return 1u;}
    note="UIScalingMode = "+std::string(text.substr(0,32))+" refused (0, 1 or 2); 2 (keep centred) applies";
    return 2u;
}
}
