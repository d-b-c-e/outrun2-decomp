// Projection/FOV contract only, no settings/device/renderer initialization.
#include "curved_panel_warp.hpp"
#include "curve_inner_rect.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace outrun::desktop;static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"native eye:%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    auto rig=fixture_rig(8);const auto panels=rig_panels(rig);
    for(double degrees:{20.,35.,45.,70.,100.,140.})for(double shift:{-.17,0.,.12})for(double near:{.05,.1,1.}){
        const double top=near*std::tan(degrees*3.141592653589793/360),right=top*16./9.;
        auto p=perspective_off_center_rh(-right,right,-top,top,near,10000);p[8]=shift;p[9]=-shift*.5;
        RigVec eye;CHECK(curve_native_eye(p,panels[1],eye));
        const double distance=rig.panel_height_mm/(2*std::tan(degrees*3.141592653589793/360));
        CHECK(std::abs(eye.z-(distance-rig.eye_distance_mm))<1e-9);
        PanelBounds physical;CHECK(panel_bounds(panels[1],eye,physical)==RigError::none);
        const auto reconstructed=perspective_off_center_rh(physical.left*near,physical.right*near,physical.bottom*near,physical.top*near,near,10000);
        for(unsigned i=0;i<16;++i)CHECK(std::abs(p[i]-reconstructed[i])<1e-11);
        for(double radius:{0.,1500.}){
            std::array<CurvedPanelWarp,3> warps;for(unsigned i=0;i<3;++i)CHECK(curved_panel_warp(panels[i],{},radius,panels[i],warps[i])==CurveWarpError::none);
            TripleViews views;CHECK(curved_source_views(p,eye,warps,views));
            CurveInnerRect rect;CHECK(curve_inner_rect(warps[1],rect));
            // Native centre clip -> source clip equals the exact inner viewport map.
            for(double x:{-1.,-.3,0.,.7,1.})for(double y:{-1.,0.,1.})for(double w:{.1,1.,100.}){
                const std::array<double,4> clip{x*w,y*w,.5*w,w};const auto q=triple_apply(views.correction[1],0,0,clip);
                CHECK(std::abs(q[0]/q[3]-(rect.width*x+2*rect.x+rect.width-1))<1e-10);
                CHECK(std::abs(q[1]/q[3]-(rect.height*y+1-2*rect.y-rect.height))<1e-10);
                CHECK(std::abs(q[2]-clip[2])<1e-10&&std::abs(q[3]-clip[3])<1e-10);
            }
            if(radius==0.)for(unsigned i=0;i<16;++i)CHECK(std::abs(views.correction[1][i]-(i%5==0?1.:0.))<1e-10);
        }
    }
    const RigVec saved{1,2,3};auto out=saved;auto p=perspective_off_center_rh(-.1,.1,-.075,.075,.1,1000); // wrong aspect for16:9 panel
    CHECK(!curve_native_eye(p,panels[1],out)&&out.x==saved.x&&out.z==saved.z);
    p=mat_identity();CHECK(!curve_native_eye(p,panels[1],out));p[0]=std::numeric_limits<double>::quiet_NaN();CHECK(!curve_native_eye(p,panels[1],out));
    auto rotated=panels[0];CHECK(!curve_native_eye(perspective_off_center_rh(-.1,.1,-.05625,.05625,.1,1000),rotated,out));
    CHECK(out.x==saved.x&&out.y==saved.y&&out.z==saved.z);
    std::printf("curve native eye: %u FOV/source/inner-viewport/depth/refusal checks, no renderer\n",checks);
}
