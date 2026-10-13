// UILetterboxing (src/platform/pc_screen.hpp): Tweaks' rule (hooks_overlay.cpp D3DEndScene with game_addrs.hpp
// is_in_game) and the bars at the device's own 4:3 edges (xbox/source/d3d11_d3d9.cpp apply_state). No device, no game.
#include "platform/pc_screen.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
namespace {
int checks=0;
void check(bool v,const char* what){++checks;if(!v){std::fprintf(stderr,"letterbox: FAIL: %s\n",what);std::exit(1);}}
// apply_state's arithmetic for the centred 4:3 at a back buffer of w x h (the full 640 soft viewport, mode 2):
// ox = floor((w - h*4/3) / 2), the viewport and scissor from floats, the scissor's right edge lround(vx + vw).
void device_edges(int w,int h,long& left,long& right){
    const double ui=double(h)*4/3;const double ox=std::floor((w-ui)/2);
    const float vx=float(ox),vw=float(640.*(ui/640.));
    left=std::lround(vx);right=std::lround(vx+vw);
}
}
int main(){
    using namespace outrun::platform;
    check(g_pc_ui_letterboxing==1,"1 by default, as Tweaks");
    // Tweaks' is_in_game.
    const unsigned race[]{0x10u,0x12u,0x13u,0x14u,0x22u,0x23u};
    bool all=true;for(unsigned m:race)all=all&&pc_letterbox_in_race(m,0u);
    check(all,"GAME, SMPAUSEMENU, GOAL, TIMEUP, OUTRUNMILES and TRYAGAIN are the race");
    check(!pc_letterbox_in_race(0x0du,64u)&&pc_letterbox_in_race(0x0du,65u),"START is the race from its stage 65 only");
    const unsigned menus[]{0x00u,0x0bu,0x0cu,0x11u,0x15u,0x19u,0x20u,0x21u,0x24u};
    bool none=true;for(unsigned m:menus)none=none&&!pc_letterbox_in_race(m,65u);
    check(none,"other modes are outside the race (START's stage counts only in START)");
    // The rule: setting, UIScalingMode, race.
    check(pc_letterbox_now(1,2u,false)&&!pc_letterbox_now(1,2u,true),"1: outside the race only");
    check(pc_letterbox_now(2,2u,true)&&pc_letterbox_now(2,1u,false),"2: always");
    check(!pc_letterbox_now(0,2u,false)&&!pc_letterbox_now(0,1u,true),"0: never");
    check(!pc_letterbox_now(1,0u,false)&&!pc_letterbox_now(2,0u,true),"UIScalingMode 0: never, as Tweaks");
    check(pc_letterbox_now(1,1u,false),"UIScalingMode 1 takes it too");
    // The bars end where the device's 2D begins and ends, at every size.
    struct Size{int w,h;};
    const Size sizes[]{{1280,720},{1920,1080},{2560,1440},{3840,2160},{1366,768},{1365,768},{3440,1440},{2560,1080},{7680,1440},{5760,1080},{1281,721}};
    bool edges=true;
    for(const auto& s:sizes){long l,r;device_edges(s.w,s.h,l,r);const auto b=pc_letterbox_bars(s.w,s.h);
        edges=edges&&b.left_end==l&&b.right_begin==r&&b.width==s.w&&b.any();}
    check(edges,"the bars meet the device's 4:3 edges");
    const auto b169=pc_letterbox_bars(1280,720);
    check(b169.left_end==160&&b169.right_begin==1120&&b169.width==1280,"1280x720: 160 px each side");
    const auto span=pc_letterbox_bars(7680,1440);
    check(span.left_end==2880&&span.right_begin==4800,"a 7680x1440 span: the 4:3 in the middle of the centre screen");
    const auto odd=pc_letterbox_bars(1365,768);
    check(odd.left_end==170&&odd.right_begin==1194,"odd margins: the left floored, the right from the device's rounding");
    check(!pc_letterbox_bars(1024,768).any()&&!pc_letterbox_bars(1280,1024).any(),"no bars at 4:3 or narrower");
    check(!pc_letterbox_bars(0,720).any()&&!pc_letterbox_bars(1280,0).any(),"no bars without a frame");
    std::printf("letterbox: %d checks passed\n",checks);
    return 0;
}
