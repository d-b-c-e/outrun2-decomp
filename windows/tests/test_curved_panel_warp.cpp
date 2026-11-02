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
    const auto transform=[](RigVec p,const Mat4& matrix){std::array<double,4> out{};for(unsigned j=0;j<4;++j)out[j]=p.x*matrix[j]+p.y*matrix[4+j]+p.z*matrix[8+j]+matrix[12+j];return out;};
    PanelBounds center;CHECK(panel_bounds(virtual_panels[1],{},center)==RigError::none);
    const auto original=perspective_off_center_rh(center.left*.1,center.right*.1,center.bottom*.1,center.top*.1,.1,10000);
    TripleViews source_views{};std::array<CurvedPanelWarp,3> plans;bool behind_center=false;
    for(const double radius:{0.,1500.})for(const double shift:{0.,.17}){
        auto projection=original;projection[8]=shift;projection[9]=-shift;
        for(unsigned p=0;p<3;++p)CHECK(curved_panel_warp(panels[p],{},radius,virtual_panels[p],plans[p])==CurveWarpError::none);
        CHECK(curved_source_views(projection,{},plans,source_views));
        if(radius==0.&&shift==0.){
            TripleViews legacy;CHECK(triple_views(projection,{708.417,398.484,660,70,70,0,0},legacy));
            for(unsigned p=0;p<3;++p)for(unsigned k=0;k<16;++k)CHECK(std::abs(legacy.correction[p][k]-source_views.correction[p][k])<1e-10);
        }
        for(unsigned p=0;p<3;++p){
            const auto& source=plans[p].source;const RigVec corners[]{source.lower_left,source.lower_right,source.upper_left,source.lower_right+source.upper_left-source.lower_left};
            const auto direct=mat_mul(source_views.rotation[p],source_views.projection[p]);
            Frustum depth;CHECK(perspective_frustum(source_views.projection[p],depth)&&std::abs(depth.n-.1)<1e-12&&std::abs(depth.f-10000)<1e-4);
            for(unsigned i=0;i<4;++i){
                const auto clip=transform(corners[i],projection),expected=transform(corners[i],direct);
                behind_center=behind_center||clip[3]<0.;
                CHECK(expected[3]>0&&std::abs(expected[0]/expected[3]-(i%2?1.:-1.))<1e-10&&std::abs(expected[1]/expected[3]-(i>=2?1.:-1.))<1e-10);
                for(const double fix:{0.,1./1280.}){
                    auto fixed=clip;fixed[0]+=fix*fixed[3];fixed[1]-=fix*fixed[3];
                    const auto actual=triple_apply(source_views.correction[p],fix,fix,fixed);
                    auto wanted=expected;wanted[0]+=fix*wanted[3];wanted[1]-=fix*wanted[3];
                    for(unsigned k=0;k<4;++k)CHECK(std::abs(actual[k]-wanted[k])<1e-8);
                }
            }
        }
    }
    CHECK(behind_center); // the homogeneous correction recovers side-source corners behind the original eye plane
    const RigVec render_eye{12,-7,20};CHECK(curved_source_views(original,render_eye,plans,source_views));
    for(unsigned p=0;p<3;++p){
        const auto& source=plans[p].source;const RigVec corners[]{source.lower_left,source.lower_right,source.upper_left};
        const auto direct=mat_mul(source_views.rotation[p],source_views.projection[p]);
        for(unsigned i=0;i<3;++i){const auto clip=transform(corners[i]-render_eye,direct);
            CHECK(clip[3]>0.&&std::abs(clip[0]/clip[3]-(i==1?1.:-1.))<1e-10&&std::abs(clip[1]/clip[3]-(i==2?1.:-1.))<1e-10);}
    }
    const auto saved_views=source_views;auto invalid_projection=original;invalid_projection[0]=std::numeric_limits<double>::infinity();
    CHECK(!curved_source_views(invalid_projection,{},plans,source_views)&&source_views.correction[0]==saved_views.correction[0]);
    CHECK(!curved_source_views(mat_identity(),{},plans,source_views)&&source_views.correction[1]==saved_views.correction[1]);
    plans[2].valid=false;CHECK(!curved_source_views(original,{},plans,source_views)&&source_views.correction[2]==saved_views.correction[2]);
    std::printf("curved panel warp: %u checks, %u canonical managed rows; no renderer enabled\n",checks,rows);
}
