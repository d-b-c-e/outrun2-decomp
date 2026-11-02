#include "curved_panel_warp.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
using namespace outrun::desktop;
namespace{unsigned checks;}
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"curve warp line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(int argc,char** argv){
    CHECK(argc==2);std::ifstream input(argv[1]);CHECK(bool(input));unsigned rows=0;std::string line;
    while(std::getline(input,line)){
        if(line.empty()||line[0]=='#')continue;
        std::array<double,29> a{};std::istringstream stream(line);for(auto& value:a)CHECK(bool(stream>>value));
        TripleRig real{a[0],a[1],a[2],a[4],a[5],a[6],a[7]},rendered=real;rendered.eye_distance_mm=a[3];
        const unsigned panel=unsigned(a[12]);CHECK(panel<3);const auto rp=rig_panels(real),vp=rig_panels(rendered);
        CurvedPanelWarp warp;CHECK(curved_panel_warp(rp[panel],{a[8],a[9],a[10]},a[11],vp[panel],warp)==CurveWarpError::none);
        RigVec point;CHECK(curved_panel_point(rp[panel],a[11],a[13],a[14],point)==CurveWarpError::none);
        const RigVec vectors[]{point,warp.source.lower_left,warp.source.lower_right,warp.source.upper_left};
        for(unsigned i=0;i<4;++i){CHECK(std::abs(vectors[i].x-a[15+i*3])<1e-8);CHECK(std::abs(vectors[i].y-a[16+i*3])<1e-8);CHECK(std::abs(vectors[i].z-a[17+i*3])<1e-8);}
        std::array<double,2> uv;CHECK(curved_source_uv(warp,a[13],a[14],uv)==CurveWarpError::none);
        CHECK(std::abs(uv[0]-a[27])<1e-10&&std::abs(uv[1]-a[28])<1e-10);
        CHECK(uv[0]>=-1e-9&&uv[0]<=1.+1e-9&&uv[1]>=-1e-9&&uv[1]<=1.+1e-9);
        ++rows;
    }
    CHECK(rows==270);
    const auto panels=rig_panels({708.417,398.484,660,70,70,0,0});CurvedPanelWarp warp;
    CHECK(curved_panel_warp(panels[1],{},1500,panels[1],warp)==CurveWarpError::none);
    const auto good=warp;auto invalid=panels[1];invalid.lower_right=invalid.lower_left;
    CHECK(curved_panel_warp(invalid,{},1500,panels[1],warp)==CurveWarpError::panel&&warp.valid&&warp.span_x==good.span_x);
    for(const double bad:{-1.,300.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
        CHECK(curved_panel_warp(panels[1],{},bad,panels[1],warp)==CurveWarpError::invalid_input&&warp.radius==good.radius);
    CHECK(curved_panel_warp(panels[1],{0,0,-2000},1500,panels[1],warp)==CurveWarpError::eye_behind);
    auto smaller=panels[1];smaller.lower_right.x-=1;
    CHECK(curved_panel_warp(panels[1],{},1500,smaller,warp)==CurveWarpError::size_mismatch);
    std::array<double,2> uv{{123,456}};
    CHECK((curved_source_uv(warp,-.1,.5,uv)==CurveWarpError::invalid_input&&uv==std::array<double,2>({123,456})));
    RigVec point{123,456,789};CHECK(curved_panel_point(panels[1],300,.5,.5,point)==CurveWarpError::invalid_input&&point.x==123&&point.y==456&&point.z==789);
    CHECK(curved_panel_warp(panels[1],{},0,panels[1],warp)==CurveWarpError::none);
    CHECK(warp.source.lower_left.x==panels[1].lower_left.x&&warp.source.lower_left.z==-660.&&warp.source.lower_right.x==panels[1].lower_right.x);
    CHECK(curved_source_uv(warp,.3,.8,uv)==CurveWarpError::none&&uv[0]==.3&&uv[1]==.8);
    CHECK(curved_panel_point(panels[1],1500,.5,.5,point)==CurveWarpError::none&&point.z<-660.); // chord stays at -660, glass bows away
    const auto virtual_panels=rig_panels({708.417,398.484,313,70,70,0,0});
    const auto rotate=[](RigVec p){const double c=std::cos(.3),s=std::sin(.3);return RigVec{p.x,p.y*c-p.z*s,p.y*s+p.z*c};};
    const auto rotated=[&](const RigPanel& p){return RigPanel{rotate(p.lower_left),rotate(p.lower_right),rotate(p.upper_left)};};
    for(unsigned p=0;p<3;++p){
        CurvedPanelWarp physical,moved,pitched;
        CHECK(curved_panel_warp(panels[p],{},1500,panels[p],physical)==CurveWarpError::none);
        CHECK(curved_panel_warp(panels[p],{},1500,virtual_panels[p],moved)==CurveWarpError::none);
        CHECK(curved_panel_warp(rotated(panels[p]),{},1500,rotated(virtual_panels[p]),pitched)==CurveWarpError::none);
        CHECK(length(pitched.source.lower_left-rotate(moved.source.lower_left))<1e-8);
        for(unsigned row=0;row<=36;++row)for(unsigned column=0;column<=64;++column){
            const double u=double(column)/64.,v=double(row)/36.;std::array<double,2> a,b,c;
            CHECK(curved_source_uv(physical,u,v,a)==CurveWarpError::none&&curved_source_uv(moved,u,v,b)==CurveWarpError::none&&curved_source_uv(pitched,u,v,c)==CurveWarpError::none);
            CHECK(a==b&&std::abs(a[0]-c[0])<1e-10&&std::abs(a[1]-c[1])<1e-10);
            CHECK(a[0]>=-1e-9&&a[0]<=1.+1e-9&&a[1]>=-1e-9&&a[1]<=1.+1e-9);
        }
    }
    std::printf("curved panel warp: %u checks, %u canonical managed rows; no renderer enabled\n",checks,rows);
}
