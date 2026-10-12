// D3D9 point sprites (pc_point_sprite.hpp): the size rule, the corners and clipping, and one sprite drawn by the soft
// device (a square of the documented size, centred, with (0,0) at its top-left on screen).
#include "platform/pc_point_sprite.hpp"
#include "platform/pc_soft_d3d9.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
using namespace outrun::platform;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"point sprite line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
bool near(float a,float b,float e=1e-4f){return std::fabs(a-b)<=e*std::max(1.f,std::fabs(b));}
std::uint32_t bits(float f){std::uint32_t u;std::memcpy(&u,&f,4);return u;}
}
int main(){
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    // Size: without scaling the render state size in pixels, clamped.
    PcPointSpriteParams p;p.size=8.f;p.min=1.f;p.max=64.f;
    float s=0;const float eye[3]{3.f,4.f,0.f};                         // De = 5
    CHECK(pc_point_sprite_size(p,eye,480.f,s)&&s==8.f);
    p.size=100.f;CHECK(pc_point_sprite_size(p,eye,480.f,s)&&s==64.f);   // Pmax
    p.size=0.5f;CHECK(pc_point_sprite_size(p,eye,480.f,s)&&s==1.f);    // Pmin
    // With scaling: Vh * Si * sqrt(1 / (A + B De + C De^2)); the game's A=0 B=0 C=1 gives Vh * Si / De.
    p.scale=true;p.a=0.f;p.b=0.f;p.c=1.f;p.size=0.1f;p.min=5.f;
    CHECK(pc_point_sprite_size(p,eye,1440.f,s)&&near(s,1440.f*0.1f/5.f));   // 28.8
    p.a=1.f;p.b=2.f;p.c=3.f;p.size=1.f;p.min=0.f;
    CHECK(pc_point_sprite_size(p,eye,100.f,s)&&near(s,float(100.0/std::sqrt(1.0+10.0+75.0))));
    p.max_point_size=4.f;CHECK(pc_point_sprite_size(p,eye,100.f,s)&&s==4.f);      // the device cap binds when smaller
    p.max_point_size=8192.f;
    p.min=20.f;p.max=10.f;CHECK(pc_point_sprite_size(p,eye,100.f,s)&&s==20.f);    // min after max, as documented
    p.min=0.f;p.max=64.f;
    p.a=-1.f;p.b=0.f;p.c=0.f;CHECK(!pc_point_sprite_size(p,eye,100.f,s));          // sqrt of a negative
    p.a=0.f;p.c=1.f;const float origin[3]{0.f,0.f,0.f};
    CHECK(pc_point_sprite_size(p,origin,100.f,s)&&s==64.f);                       // at the eye: infinite, so Pmax
    p.size=0.f;CHECK(!pc_point_sprite_size(p,origin,100.f,s));                    // 0 * infinity
    p.size=1.f;const float bad[3]{nan,0.f,1.f};CHECK(!pc_point_sprite_size(p,bad,100.f,s));
    // Corners: a centred point, side 8 in a 64 x 32 viewport: half 8/64*w in x, 8/32*w in y; top edge first.
    float c[4][4];const float centre[4]{0.f,0.f,0.5f,2.f};
    CHECK(pc_point_sprite_corners(centre,8.f,64.f,32.f,c));
    CHECK(near(c[0][0],-0.25f)&&near(c[0][1],0.5f)&&near(c[1][0],0.25f)&&near(c[1][1],0.5f));     // top-left, top-right
    CHECK(near(c[2][0],-0.25f)&&near(c[2][1],-0.5f)&&near(c[3][0],0.25f)&&near(c[3][1],-0.5f));   // bottom-left, -right
    CHECK(c[0][2]==0.5f&&c[3][3]==2.f&&PcPointSpriteUv[0][0]==0.f&&PcPointSpriteUv[0][1]==0.f&&PcPointSpriteUv[3][0]==1.f);
    const float behind[4]{0.f,0.f,0.5f,-1.f},zero_w[4]{0.f,0.f,0.f,0.f},front[4]{0.f,0.f,-0.1f,1.f},back[4]{0.f,0.f,1.5f,1.f};
    CHECK(!pc_point_sprite_corners(behind,8.f,64.f,32.f,c)&&!pc_point_sprite_corners(zero_w,8.f,64.f,32.f,c));
    CHECK(!pc_point_sprite_corners(front,8.f,64.f,32.f,c)&&!pc_point_sprite_corners(back,8.f,64.f,32.f,c));  // depth
    const float edge[4]{1.1f,0.f,0.5f,1.f},away[4]{1.3f,0.f,0.5f,1.f},nanc[4]{nan,0.f,0.5f,1.f};
    CHECK(pc_point_sprite_corners(edge,8.f,64.f,32.f,c));              // centre off screen, square partly on: drawn
    CHECK(!pc_point_sprite_corners(away,8.f,64.f,32.f,c));             // entirely outside
    CHECK(!pc_point_sprite_corners(nanc,8.f,64.f,32.f,c)&&!pc_point_sprite_corners(centre,inf,64.f,32.f,c));
    CHECK(!pc_point_sprite_corners(centre,8.f,0.f,32.f,c));
    // The soft device: one point at the origin, side 8 (no scaling), red, in a 64 x 64 target: an 8 x 8 square
    // centred on (32, 32), the pixels with centres 28..35 (top-left rule).
    {
        PcSoftD3D9Device dev(64,64);
        dev.clear(0xff000000u,1.f);
        dev.set_fvf(0x42);dev.set_render_state(7,0);dev.set_render_state(22,3);
        dev.set_render_state(154,bits(8.f));dev.set_render_state(155,bits(1.f));dev.set_render_state(166,bits(64.f));
        dev.set_render_state(156,1);dev.set_render_state(157,0);
        struct V{float x,y,z;std::uint32_t c;} v{0.f,0.f,0.5f,0xffff0000u};
        dev.draw_primitive_up(1,1,&v,sizeof v);
        CHECK(dev.errors.empty()&&dev.point_sprites_drawn==1u&&dev.skipped_up[1]==0u);
        const auto& px=dev.rgba();unsigned red=0;int x0=64,x1=-1,y0=64,y1=-1;
        for(int y=0;y<64;++y)for(int x=0;x<64;++x){const auto* q=&px[std::size_t(y*64+x)*4];
            if(q[0]>200&&q[1]<50){++red;x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);y1=std::max(y1,y);}}
        std::printf("soft device square: %u pixels, x %d..%d, y %d..%d\n",red,x0,x1,y0,y1);
        CHECK(red==64u&&x1-x0==7&&y1-y0==7&&x0>=27&&x0<=28&&y0>=27&&y0<=28);
        // Without POINTSPRITEENABLE the point is still skipped (and counted) as before.
        dev.set_render_state(156,0);dev.draw_primitive_up(1,1,&v,sizeof v);
        CHECK(dev.skipped_up[1]==1u&&dev.point_sprites_drawn==1u);
    }
    std::printf("point sprite: %u checks\n",checks);
    return 0;
}
