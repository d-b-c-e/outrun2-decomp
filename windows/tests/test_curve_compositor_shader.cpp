// Packed shader inputs and a CPU sampling reference. Does not execute HLSL.
#include "curve_compositor_shader.hpp"
#include "curved_panel_mesh.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
using namespace outrun::desktop;
namespace {unsigned checks;}
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("curve shader line "+std::to_string(__LINE__)+": " #x);}while(0)
std::array<float,2> sample_uv(const CurveSampleRect& r,float u,float v){return {
    std::clamp(u*r.map[0]+r.map[2],r.limits[0],r.limits[2]),
    std::clamp(v*r.map[1]+r.map[3],r.limits[1],r.limits[3])};}
int main(){try{
    CHECK(sizeof(CurveMeshVertex)==16&&offsetof(CurveMeshVertex,x)==0&&offsetof(CurveMeshVertex,u)==8);
    CHECK(sizeof(CurveSampleRect)==32&&offsetof(CurveSampleRect,map)==0&&offsetof(CurveSampleRect,limits)==16);
    const std::array<std::array<unsigned,2>,6> sizes{{{{6,2}},{{1920,1080}},{{3840,720}},{{7680,1440}},{{8190,2160}},{{16383,16384}}}};
    for(const auto& size:sizes)for(unsigned p=0;p<3;++p){
        const unsigned w=size[0],h=size[1],panel_w=w/3;CurveSampleRect r;
        CHECK(curve_sample_rect(w,h,p,r));CHECK(r.map[0]==1.f/3&&r.map[1]==1&&r.map[2]==float(p)/3&&r.map[3]==0);
        CHECK(double(r.limits[0])*w>=double(p)*panel_w+.5&&double(r.limits[2])*w<=double(p+1)*panel_w-.5);
        CHECK(double(r.limits[1])*h>=.5&&double(r.limits[3])*h<=double(h)-.5);
        for(unsigned y:{0u,h/2,h-1})for(unsigned x=0;x<panel_w;++x){
            const auto uv=sample_uv(r,float((double(x)+.5)/panel_w),float((double(y)+.5)/h));
            CHECK(std::abs(double(uv[0])*w-(double(p)*panel_w+x+.5))<.004);
            CHECK(std::abs(double(uv[1])*h-(double(y)+.5))<.004);
        }
        // A constant-color source panel has no mathematical bilinear tap outside
        // its own first/last texel centers, even at exact mesh edge UVs.
        for(float v:{-1.f,0.f,.5f,1.f,2.f})for(float u:{-1.f,0.f,.5f,1.f,2.f}){
            const auto uv=sample_uv(r,u,v);const double x=double(uv[0])*w-.5,y=double(uv[1])*h-.5;
            CHECK(x>=double(p)*panel_w&&x<=double(p+1)*panel_w-1&&y>=0&&y<=h-1);
        }
        const auto top=sample_uv(r,.5f,0),bottom=sample_uv(r,.5f,1);CHECK(top[1]<bottom[1]); // shader does not flip mesh UV again
    }
    CurveSampleRect out{{7,7,7,7},{8,8,8,8}};const auto saved=out;
    for(const auto& invalid:std::array<std::array<unsigned,3>,8>{{{{0,1440,0}},{{3,2,0}},{{6,1,0}},{{7681,1440,0}},{{7680,1440,3}},{{16386,1440,0}},{{7680,16385,0}},{{7680,0,0}}}}){
        CHECK(!curve_sample_rect(invalid[0],invalid[1],invalid[2],out)&&std::memcmp(&out,&saved,sizeof out)==0);
    }
    std::printf("curve compositor shader: %u layout/atlas/pixel-reference/refusal checks; no GPU\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
