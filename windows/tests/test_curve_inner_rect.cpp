// Viewport contract only: real curve/source projections, no renderer/device.
#include "curve_inner_rect.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace outrun::desktop;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"inner rect:%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static std::array<double,4> transform(RigVec p,const Mat4& m){return {p.x*m[0]+p.y*m[4]+p.z*m[8]+m[12],p.x*m[1]+p.y*m[5]+p.z*m[9]+m[13],p.x*m[2]+p.y*m[6]+p.z*m[10]+m[14],p.x*m[3]+p.y*m[7]+p.z*m[11]+m[15]};}
int main(){
    TripleRig physical{708.417,398.484,660,61,73,85,8};const auto panels=rig_panels(physical);
    for(double radius:{0.,400.,1500.,20000.})for(RigVec eye:{RigVec{},RigVec{13,-27,100},RigVec{-20,15,-100}}){
        std::array<CurvedPanelWarp,3> warps{};
        for(unsigned panel=0;panel<3;++panel){
            auto rendered=panels[panel];rendered.lower_left=rendered.lower_left+RigVec{0,0,-100};rendered.lower_right=rendered.lower_right+RigVec{0,0,-100};rendered.upper_left=rendered.upper_left+RigVec{0,0,-100};
            CHECK(curved_panel_warp(panels[panel],{},radius,rendered,warps[panel])==CurveWarpError::none);
            CurveInnerRect rect;CHECK(curve_inner_rect(warps[panel],rect));
            CHECK(rect.x>=-1e-12&&rect.y>=-1e-12&&rect.x+rect.width<=1.+1e-12&&rect.y+rect.height<=1.+1e-12);
            if(radius==0.)CHECK(rect.x==0&&rect.y==0&&rect.width==1&&rect.height==1);
            PanelBounds native_bounds,source_bounds;
            CHECK(panel_bounds(rendered,eye,native_bounds)==RigError::none&&panel_bounds(warps[panel].source,eye,source_bounds)==RigError::none);
            const auto rotation=panel_rotation(rendered);
            const auto native=mat_mul(rotation,perspective_off_center_rh(native_bounds.left*.1,native_bounds.right*.1,native_bounds.bottom*.1,native_bounds.top*.1,.1,10000));
            const auto source=mat_mul(rotation,perspective_off_center_rh(source_bounds.left*.1,source_bounds.right*.1,source_bounds.bottom*.1,source_bounds.top*.1,.1,10000));
            for(double u:{0.,.1,.5,.9,1.})for(double v:{0.,.3,.5,1.})for(double depth:{.1,1.,3.}){
                const auto plane=rendered.lower_left+(rendered.lower_right-rendered.lower_left)*u+(rendered.upper_left-rendered.lower_left)*v;
                const auto relative=(plane-eye)*depth;const auto a=transform(relative,native),b=transform(relative,source);
                CHECK(a[3]>0&&b[3]>0);
                const double x=rect.x+(a[0]/a[3]+1)*.5*rect.width,y=rect.y+(1-a[1]/a[3])*.5*rect.height;
                CHECK(std::abs(x-(b[0]/b[3]+1)*.5)<1e-12&&std::abs(y-(1-b[1]/b[3])*.5)<1e-12);
                CHECK(a[3]==b[3]&&std::abs(a[2]/a[3]-b[2]/b[3])<1e-12); // unchanged w/depth, not a foreground overlay
                for(unsigned width:{256u,2560u,5461u}){
                    const float vx=float(rect.x*width),vy=float(rect.y*16384),vw=float(rect.width*width),vh=float(rect.height*16384);
                    const double px=vx+(a[0]/a[3]+1)*.5*vw,py=vy+(1-a[1]/a[3])*.5*vh;
                    CHECK(std::abs(px-x*width)<.002&&std::abs(py-y*16384)<.002);
                }
            }
        }
    }
    CurveInnerRect saved{1,2,3,4},out=saved;CurvedPanelWarp bad{};CHECK(!curve_inner_rect(bad,out)&&out.x==saved.x&&out.height==saved.height);
    const auto panel=panels[1];CurvedPanelWarp good;CHECK(curved_panel_warp(panel,{},1500,panel,good)==CurveWarpError::none);
    for(double value:{0.,-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){
        bad=good;bad.span_x=value;CHECK(!curve_inner_rect(bad,out)&&out.x==saved.x&&out.width==saved.width);
    }
    bad=good;bad.min_x=1;CHECK(!curve_inner_rect(bad,out));bad=good;bad.span_y=good.basis.height*.5;CHECK(!curve_inner_rect(bad,out));
    std::printf("curve inner rect: %u projection/viewport/depth/refusal assertions, no renderer\n",checks);
}
