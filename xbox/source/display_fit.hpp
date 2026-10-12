#pragma once
// The display's logical frame (the renderer's 1280x720, or the setup screen's size) on a swap chain of any size: the
// largest centred rectangle of the frame's aspect, in whole pixels, black around it. Never a stretch: before this, a
// window that was not 16:9 (an ultrawide, a three-monitor span) stretched the whole frame (TRIPLE-SCREENS.md).
// Pure, tested in windows/tests/test_triple_screens.cpp.
#include <cstdint>
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
}
