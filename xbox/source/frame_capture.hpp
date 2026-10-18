#pragma once
// A rendering test's bounded frame burst (windows/source/render_test.hpp, sana 215675): the present path writes what the
// player sees (d3d11_display.cpp screenshot, after the overlay) as frame-NNNN.ppm into the one folder a latched test run
// owns, box-downscaled. Off unless a test run configures it (folder empty). Armed once, at the race entry; then one frame
// every `every` presented frames from `after` on, `frames` at most, and never past `byte_limit` of pixel data.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace outrun::xbox_runtime {
struct FrameCapture {
    std::wstring folder;                 // empty: off
    unsigned frames{},every{1},after{},scale{2};
    std::uint64_t byte_limit{1ull<<30};
    std::atomic<bool> armed{false};      // set by the input thread at the race entry
    unsigned since_armed{},written{};    // the present thread's own
    std::uint64_t bytes{};
};
inline FrameCapture& frame_capture(){static FrameCapture c;return c;}
// Whether presented frame `since_armed` (0: the first after arming) is captured when `written` are already written.
inline bool frame_capture_due(unsigned since_armed,unsigned written,unsigned frames,unsigned every,unsigned after){
    return written<frames&&every>0u&&since_armed>=after&&(since_armed-after)%every==0u;
}
// The first three bytes of each 4-byte pixel (as the development screenshot writes them), averaged over scale x scale
// blocks (1, 2 or 4; a partial edge block is dropped). Rows are `pitch` bytes apart. out_w / out_h receive the size.
inline std::vector<std::uint8_t> frame_capture_rgb(const std::uint8_t* pixels,std::size_t pitch,unsigned width,unsigned height,
                                                   unsigned scale,unsigned& out_w,unsigned& out_h){
    if(scale!=1u&&scale!=2u&&scale!=4u)scale=1u;
    out_w=width/scale;out_h=height/scale;
    std::vector<std::uint8_t> rgb(std::size_t(out_w)*out_h*3u);
    const unsigned n=scale*scale;
    for(unsigned y=0;y<out_h;++y)for(unsigned x=0;x<out_w;++x)for(unsigned c=0;c<3u;++c){
        unsigned sum=0;
        for(unsigned dy=0;dy<scale;++dy){
            const std::uint8_t* row=pixels+std::size_t(y*scale+dy)*pitch;
            for(unsigned dx=0;dx<scale;++dx)sum+=row[std::size_t(x*scale+dx)*4u+c];
        }
        rgb[(std::size_t(y)*out_w+x)*3u+c]=std::uint8_t((sum+n/2u)/n);
    }
    return rgb;
}
}
