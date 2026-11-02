#include "curved_panel_mesh.hpp"
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
using namespace outrun::desktop;
namespace {unsigned checks;}
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("curve mesh line "+std::to_string(__LINE__)+": " #x);}while(0)
int main(){try{
    CHECK(CurvedSceneMesh::Columns==64&&CurvedSceneMesh::Rows==36);
    CHECK(CurvedSceneMesh::VertexCount==2405&&CurvedSceneMesh::IndexCount==13824);
    CHECK(sizeof(CurveMeshVertex)==16&&sizeof(CurvedSceneMesh{}.indices[0])==2);
    auto mesh=std::make_unique<CurvedSceneMesh>(),virtual_mesh=std::make_unique<CurvedSceneMesh>();
    const TripleRig real{708.417,398.484,660,61,73,17,3};const auto panels=rig_panels(real);
    for(double radius:{0.,400.,1500.,20000.}){
        auto rendered=real;rendered.eye_distance_mm=313;const auto virtual_panels=rig_panels(rendered);
        std::array<CurvedPanelWarp,3> physical{},moved{};
        for(unsigned p=0;p<3;++p){CHECK(curved_panel_warp(panels[p],{13,-7,0},radius,panels[p],physical[p])==CurveWarpError::none);
            CHECK(curved_panel_warp(panels[p],{13,-7,0},radius,virtual_panels[p],moved[p])==CurveWarpError::none);}
        CHECK(curved_scene_mesh(physical,*mesh)==CurveWarpError::none);
        CHECK(curved_scene_mesh(moved,*virtual_mesh)==CurveWarpError::none);
        CHECK(std::memcmp(mesh.get(),virtual_mesh.get(),sizeof(*mesh))==0); // rendered eye changes source matrices, never this UV mesh
        bool nonflat=false;
        for(unsigned p=0;p<3;++p)for(unsigned row=0;row<=36;++row)for(unsigned col=0;col<=64;++col){
            const double u=double(col)/64,v=double(row)/36;const auto& vertex=mesh->panels[p][row*65+col];
            std::array<double,2> source{};CHECK(curved_source_uv(physical[p],u,v,source)==CurveWarpError::none);
            CHECK(vertex.x==float(2*u-1)&&vertex.y==float(2*v-1));
            CHECK(vertex.u>=0&&vertex.u<=1&&vertex.v>=0&&vertex.v<=1);
            CHECK(std::abs(vertex.u-source[0])<4e-8&&std::abs(vertex.v-(1-source[1]))<4e-8);
            if(radius==0)CHECK(vertex.u==float(u)&&vertex.v==float(1-v));
            else if(std::abs(vertex.u-u)>1e-6||std::abs(vertex.v-(1-v))>1e-6)nonflat=true;
            if(row<36)CHECK(vertex.v>mesh->panels[p][(row+1)*65+col].v); // top-left texture V decreases as physical height rises
        }
        CHECK(radius==0||nonflat);
        std::map<std::pair<unsigned,unsigned>,unsigned> edges;double area=0;
        for(unsigned i=0;i<mesh->indices.size();i+=3){
            unsigned indices[3]{};for(unsigned j=0;j<3;++j){indices[j]=mesh->indices[i+j];CHECK(indices[j]<2405);}
            const auto& a=mesh->panels[0][indices[0]];const auto& b=mesh->panels[0][indices[1]];const auto& c=mesh->panels[0][indices[2]];
            const double twice=(double(b.x)-a.x)*(double(c.y)-a.y)-(double(b.y)-a.y)*(double(c.x)-a.x);
            CHECK(twice>0);area+=twice*.5;
            for(unsigned j=0;j<3;++j){unsigned a0=indices[j],b0=indices[(j+1)%3];CHECK(a0!=b0);if(a0>b0)std::swap(a0,b0);++edges[{a0,b0}];}
        }
        CHECK(std::abs(area-4)<1e-8);unsigned boundary=0;
        for(const auto& edge:edges){CHECK(edge.second==1||edge.second==2);if(edge.second!=1)continue;++boundary;
            const auto a=edge.first.first,b=edge.first.second;CHECK((a/65==0&&b/65==0)||(a/65==36&&b/65==36)||(a%65==0&&b%65==0)||(a%65==64&&b%65==64));}
        CHECK(boundary==200);
    }
    std::array<CurvedPanelWarp,3> warps{};for(unsigned i=0;i<3;++i)CHECK(curved_panel_warp(panels[i],{},1500,panels[i],warps[i])==CurveWarpError::none);
    const auto saved=std::make_unique<CurvedSceneMesh>(*mesh);const auto valid=warps;
    warps[2].valid=false;CHECK(curved_scene_mesh(warps,*mesh)==CurveWarpError::invalid_input&&std::memcmp(mesh.get(),saved.get(),sizeof(*mesh))==0);
    for(double bad:{0.,-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){warps=valid;warps[2].span_x=bad;
        CHECK(curved_scene_mesh(warps,*mesh)==CurveWarpError::invalid_input&&std::memcmp(mesh.get(),saved.get(),sizeof(*mesh))==0);}
    warps=valid;warps[2].span_x*=.01;CHECK(curved_scene_mesh(warps,*mesh)==CurveWarpError::projection&&std::memcmp(mesh.get(),saved.get(),sizeof(*mesh))==0);
    warps=valid;warps[2].eye.x=std::numeric_limits<double>::quiet_NaN();CHECK(curved_scene_mesh(warps,*mesh)!=CurveWarpError::none&&std::memcmp(mesh.get(),saved.get(),sizeof(*mesh))==0);
    std::printf("curved panel mesh: %u topology/UV/packing/refusal checks; no renderer\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
