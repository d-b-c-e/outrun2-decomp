// Real XYZ UP decode -> triangle -> virtual raster boundary, no GPU device.
#include "platform/pc_soft_d3d9.hpp"
#include "platform/pc_world_coverage.hpp"
#include "platform/pc_screen.hpp"
#include "triple_rig.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
using namespace outrun;using namespace platform;namespace math=outrun::desktop;
namespace {unsigned checks;}
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("world depth clip line "+std::to_string(__LINE__)+": " #x);}while(0)
struct Capture:PcSoftD3D9Device {
    Capture():PcSoftD3D9Device(640,480){}
    bool gpu{true};std::vector<std::vector<Vertex>> packets;
    bool defer_triangle_depth_clip()const noexcept override{return gpu&&pc_world_gpu_depth_clip(draw_phase(),logical_backbuffer_target(),g_pc_triple_corrections!=nullptr);}
    void raster(const Vertex* v,unsigned n)override{packets.emplace_back(v,v+n);}
    std::array<float,4> position(unsigned v)const{std::array<float,4> p{};std::memcpy(p.data(),packets.at(0).at(v).pos,16);return p;}
    std::array<float,4> colour(unsigned v)const{const auto& c=packets.at(0).at(v).d[0];return {c.x,c.y,c.z,c.w};}
    std::array<float,4> uv(unsigned v)const{const auto& t=packets.at(0).at(v).t[0];return {t.x,t.y,t.z,t.w};}
};
struct Input {float x,y,z;unsigned colour;float u,v;};
std::array<double,4> clip(const math::Mat4& m,const Input& p){
    std::array<double,4> q{};for(unsigned i=0;i<4;++i)q[i]=p.x*m[i]+p.y*m[4+i]+p.z*m[8+i]+m[12+i];return q;
}
int main(){try{
    const auto projection=math::perspective_off_center_rh(-.1,.1,-.075,.075,.1,1000);
    std::array<float,16> pf{};for(unsigned i=0;i<16;++i)pf[i]=float(projection[i]);
    Capture d;d.set_transform(3,pf.data());d.set_fvf(0x142);d.set_draw_phase(PcDrawPhase::World);
    const auto angled=[](const float*,float*){return false;};
    const auto draw=[&](const std::array<Input,3>& p){d.packets.clear();d.draw_primitive_up(4,1,p.data(),sizeof(Input));CHECK(d.errors.empty());};
    const std::array<Input,3> front{{{-1,-1,-10,0xff123456,0,0},{1,-1,-10,0xff123456,1,0},{0,1,-10,0xff123456,.5,1}}};
    g_pc_triple_corrections=nullptr;draw(front);CHECK(d.packets.size()==1&&d.packets[0].size()==3);
    const auto old=d.packets[0];g_pc_triple_corrections=angled;draw(front);
    CHECK(d.packets.size()==1&&d.packets[0].size()==3);
    for(unsigned i=0;i<3;++i){CHECK(std::memcmp(old[i].pos,d.packets[0][i].pos,16)==0);
        CHECK((d.colour(i)==std::array<float,4>({0x12/255.f,0x34/255.f,0x56/255.f,1.f})));
        CHECK((d.uv(i)==std::array<float,4>({front[i].u,front[i].v,0,1})));}
    const auto panel=math::rig_panels(math::fixture_rig())[2];
    const auto point=[&](double u,double v){const auto p=panel.lower_left+(panel.lower_right-panel.lower_left)*u+(panel.upper_left-panel.lower_left)*v;
        return Input{float(p.x*.02),float(p.y*.02),float(p.z*.02),0xff123456,float(u),float(v)};};
    const std::array<Input,3> behind{{point(.994,.49),point(.998,.49),point(.994,.51)}};
    math::TripleViews views;CHECK(math::triple_views(projection,math::fixture_rig(),views));
    const auto right=math::mat_mul(views.rotation[2],views.projection[2]);
    for(const auto& v:behind){CHECK(v.z>0);const auto q=clip(right,v);CHECK(q[3]>0&&std::abs(q[0])<q[3]&&std::abs(q[1])<q[3]&&q[2]>0&&q[2]<q[3]);}
    draw(behind);CHECK(d.packets.size()==1&&d.packets[0].size()==3);
    for(unsigned i=0;i<3;++i){const auto q=clip(projection,behind[i]);const auto p=d.position(i);CHECK(p[3]<0);
        for(unsigned j=0;j<4;++j)CHECK(std::abs(p[j]-q[j])<1e-5);}
    auto crossing=front;crossing[0].z=-.01f;
    g_pc_triple_corrections=nullptr;draw(crossing);CHECK(d.packets.size()==1&&d.packets[0].size()==4);
    g_pc_triple_corrections=angled;draw(crossing);CHECK(d.packets.size()==1&&d.packets[0].size()==3);
    for(const auto phase:{PcDrawPhase::Hud,PcDrawPhase::FrontendOverlay,PcDrawPhase::Unclassified}){d.set_draw_phase(phase);draw(behind);CHECK(d.packets.empty());}
    d.set_draw_phase(PcDrawPhase::World);d.gpu=false;draw(behind);CHECK(d.packets.empty());d.gpu=true;
    g_pc_triple_corrections=nullptr;draw(behind);CHECK(d.packets.empty());g_pc_triple_corrections=angled;
    const auto texture=d.create_texture(640,480,1,1,21,0),surface=d.get_surface_level(texture,0);d.set_render_target(0,surface);
    draw(behind);CHECK(d.packets.empty());d.set_render_target(0,Capture::BackBuffer);d.release(surface);d.release(texture);
    for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){auto invalid=front;invalid[1].x=bad;draw(invalid);CHECK(d.packets.empty());}
    g_pc_triple_corrections=nullptr;
    std::printf("world depth clip: %u actual decode/triangle/raster checks, CPU only\n",checks);return 0;
}catch(const std::exception& e){g_pc_triple_corrections=nullptr;std::fprintf(stderr,"%s\n",e.what());return 1;}}
