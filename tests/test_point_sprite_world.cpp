// Active point6 -> actual fixed-function point decoder -> deferred raster.
// No graphics device; compare panel expansion with the native point oracle.
#include "platform/pc_soft_d3d9.hpp"
#include "platform/pc_point_sprite.hpp"
#include "platform/pc_world_coverage.hpp"
#include "platform/pc_screen.hpp"
#include "platform/race_particles.hpp"
#include "triple_rig.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
using namespace outrun;using namespace platform;namespace math=outrun::desktop;
namespace {unsigned checks;bool refuse{},corrupt{};}
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("point world line "+std::to_string(__LINE__)+": " #x);}while(0)
unsigned bits(float f){unsigned u;std::memcpy(&u,&f,4);return u;}
bool corrections(const float* p,float* out){
    if(refuse)return false;math::Mat4 m{};for(unsigned i=0;i<16;++i)m[i]=p[i];math::TripleViews views;
    if(!math::triple_views(m,math::fixture_rig(),views))return false;
    for(unsigned panel=0;panel<3;++panel)for(unsigned i=0;i<16;++i)out[panel*16+i]=float(views.correction[panel][i]);
    if(corrupt)out[0]=std::numeric_limits<float>::quiet_NaN();return true;
}
struct Record {std::array<float,4> pos,colour;std::array<std::array<float,4>,8> tex;float fog;};
struct Capture:PcSoftD3D9Device {
    Capture():PcSoftD3D9Device(640,480){}
    bool permit{};std::vector<Record> records;
    bool begin_deferred_point_sprites()override{return permit&&pc_world_point_sprites(draw_phase(),logical_backbuffer_target(),g_pc_triple_corrections,transform_proj.data(),soft_viewport_.w,soft_viewport_.h);}
    void raster(const Vertex* p,unsigned n)override{CHECK(n==3);for(unsigned i=0;i<n;++i){Record r{};std::memcpy(r.pos.data(),p[i].pos,16);
        std::memcpy(r.colour.data(),&p[i].d[0],16);std::memcpy(r.tex.data(),p[i].t,128);r.fog=p[i].fog;records.push_back(r);}}
};
struct Input{float x,y,z;unsigned colour{0xff123456};};
std::array<double,4> clip(const math::Mat4& m,const Input& p){std::array<double,4> q{};for(unsigned j=0;j<4;++j)q[j]=p.x*m[j]+p.y*m[4+j]+p.z*m[8+j]+m[12+j];return q;}
struct Bridge:PcParticleDevice {
    Capture& d;explicit Bridge(Capture& v):d(v){}
    unsigned get_render_state(unsigned s)override{return d.get_render_state(s);}void set_render_state(unsigned s,unsigned v)override{d.set_render_state(s,v);}
    unsigned get_texture(unsigned s)override{return d.get_texture(s);}void set_texture(unsigned s,unsigned v)override{d.set_texture(s,v);}
    unsigned get_texture_stage_state(unsigned s,unsigned t)override{return d.get_texture_stage_state(s,t);}void set_texture_stage_state(unsigned s,unsigned t,unsigned v)override{d.set_texture_stage_state(s,t,v);}
    unsigned get_sampler_state(unsigned s,unsigned t)override{return d.get_sampler_state(s,t);}void set_sampler_state(unsigned s,unsigned t,unsigned v)override{d.set_sampler_state(s,t,v);}
    void set_transform(unsigned s,const float* p)override{d.set_transform(s,p);}void set_fvf(unsigned v)override{d.set_fvf(v);}
    void set_vertex_shader(unsigned v)override{d.set_vertex_shader(v);}void set_pixel_shader(unsigned v)override{d.set_pixel_shader(v);}
    void release(unsigned v)override{d.release(v);}void draw_primitive_up(unsigned t,unsigned n,const unsigned char* p,unsigned stride)override{d.draw_primitive_up(t,n,p,stride);}
};
int main(){try{
    Capture d;math::Mat4 projection=math::perspective_off_center_rh(-.1,.1,-.075,.075,.1,1000);std::array<float,16> pf{};
    for(unsigned i=0;i<16;++i)projection[i]=pf[i]=float(projection[i]);d.set_transform(3,pf.data());d.set_draw_phase(PcDrawPhase::World);
    d.set_fvf(0x42);d.set_render_state(156,1);d.set_render_state(157,0);d.set_render_state(154,bits(8));d.set_render_state(155,bits(1));d.set_render_state(166,bits(64));d.set_render_state(22,3);
    g_pc_triple_corrections=corrections;math::TripleViews views;CHECK(math::triple_views(projection,math::fixture_rig(),views));
    const auto panel=math::rig_panels(math::fixture_rig())[2];const auto v=panel.lower_left+(panel.lower_right-panel.lower_left)*.996+(panel.upper_left-panel.lower_left)*.5;
    const Input behind{float(v.x*.02),float(v.y*.02),float(v.z*.02)},front{0,0,-10};CHECK(behind.z>0);
    auto draw=[&](const Input& p){d.records.clear();const auto old=p;d.draw_primitive_up(1,1,&p,sizeof p);CHECK(d.errors.empty()&&std::memcmp(&old,&p,sizeof p)==0);};
    d.permit=false;draw(behind);CHECK(d.records.empty()&&d.point_sprites_deferred==0);
    draw(front);CHECK(d.records.size()==6);const auto legacy=d.records;
    d.permit=true;draw(behind);CHECK(d.records.size()==6&&d.point_sprites_deferred==1);
    auto conformance=[&](const Input& p,float size){draw(p);CHECK(d.records.size()==6);unsigned visible=0;float packed[48]{};CHECK(corrections(pf.data(),packed));
        for(unsigned panel_index=0;panel_index<3;++panel_index){const auto direct=clip(math::mat_mul(views.rotation[panel_index],views.projection[panel_index]),p);
            float centre[4];for(unsigned j=0;j<4;++j)centre[j]=float(direct[j]);float native[4][4];if(!pc_point_sprite_corners(centre,size,640,480,native))continue;++visible;
            for(const auto& r:d.records){const auto& meta=r.tex[7];if(meta[2]!=size||r.fog!=1)std::fprintf(stderr,"size got %.9g expected %.9g fog %.9g, point %.3g %.3g %.3g\n",meta[2],size,r.fog,p.x,p.y,p.z);CHECK(meta[2]==size&&r.fog==1);const unsigned k=unsigned(meta[0])+2*unsigned(meta[1]);CHECK(k<4);
                constexpr double fx=1./640,fy=1./480;std::array<double,4> fixed{r.pos[0]+fx*r.pos[3],r.pos[1]-fy*r.pos[3],r.pos[2],r.pos[3]};
                math::Mat4 c{};for(unsigned j=0;j<16;++j)c[j]=packed[panel_index*16+j];auto q=math::triple_apply(c,fx,fy,fixed);
                q[0]+=(2*meta[0]-1)*meta[2]*fx*q[3];q[1]+=(1-2*meta[1])*meta[2]*fy*q[3];
                for(unsigned j=0;j<4;++j){const double expected=native[k][j]+(j==0?fx*native[k][3]:j==1?-fy*native[k][3]:0);CHECK(std::abs(q[j]-expected)<3e-5*std::max(1.,std::abs(expected)));}
                for(unsigned t=0;t<7;++t)CHECK((r.tex[t]==std::array<float,4>{meta[0],meta[1],0,1}));
                CHECK((r.colour==std::array<float,4>{0x12/255.f,0x34/255.f,0x56/255.f,1}));
            }
        }return visible;};
    CHECK(conformance(behind,8)>=1);CHECK(conformance(Input{-behind.x,behind.y,behind.z},8)>=1);CHECK(conformance(Input{10.05f,0,-10},8)>=1);CHECK(conformance(front,8)>=1);draw(front);
    for(unsigned i=0;i<6;++i)CHECK(d.records[i].colour==legacy[i].colour&&d.records[i].tex[7][0]==legacy[i].tex[7][0]&&d.records[i].tex[7][1]==legacy[i].tex[7][1]);
    for(unsigned cull:{2u,3u}){d.set_render_state(22,cull);draw(front);CHECK(d.records[1].tex[7][0]==(cull==2?0:1));CHECK(conformance(behind,8)>=1);}
    d.set_render_state(154,bits(1000));CHECK(conformance(front,64)>=1);d.set_render_state(154,bits(.1f));CHECK(conformance(front,1)>=1);
    d.set_render_state(154,bits(.1f));d.set_render_state(158,0);d.set_render_state(159,0);d.set_render_state(160,bits(1));d.set_render_state(157,1);CHECK(conformance(front,4.8f)>=1);d.set_render_state(157,0);d.set_render_state(154,bits(8));
    for(const auto phase:{PcDrawPhase::Hud,PcDrawPhase::FrontendOverlay,PcDrawPhase::Unclassified,PcDrawPhase::WorldCoverage}){d.set_draw_phase(phase);draw(behind);CHECK(d.records.empty());}d.set_draw_phase(PcDrawPhase::World);
    refuse=true;draw(behind);CHECK(d.records.empty());refuse=false;corrupt=true;draw(behind);CHECK(d.records.empty());corrupt=false;
    g_pc_triple_corrections=nullptr;draw(behind);CHECK(d.records.empty());g_pc_triple_corrections=corrections;
    const auto texture=d.create_texture(640,480,1,1,21,0),surface=d.get_surface_level(texture,0);d.set_render_target(0,surface);draw(behind);CHECK(d.records.empty());d.set_render_target(0,Capture::BackBuffer);d.release(surface);d.release(texture);
    for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){Input p=front;p.x=bad;draw(p);CHECK(d.records.empty());}
    // Real PART_EFC source8/type6, one texture frame, its actual point draw.
    PcRaceMemory memory;PcParticleState state;state.map(memory);std::fill(state.data.begin(),state.data.end(),0);std::fill(state.tire_data.begin(),state.tire_data.end(),0);
    std::array<unsigned char,0x240> render{};memory.map(0x95d860,render.data(),render.size());std::array<unsigned char,8> screen{};memory.map(0x740c8c,screen.data(),8);memory.putf(0x740c8c,320);memory.putf(0x740c90,240);
    unsigned frame=1,tex=0;memory.map(0x95af0c,reinterpret_cast<unsigned char*>(&frame),4);memory.map(0x10000100,reinterpret_cast<unsigned char*>(&tex),4);
    constexpr unsigned Buffer=0x10000000,Source=0x8a8d18+8*0x84;std::array<unsigned char,0x28> particle{};memory.map(Buffer,particle.data(),particle.size());
    memory.put32(Buffer,0x80000000);memory.putf(Buffer+4,behind.x);memory.putf(Buffer+8,behind.y);memory.putf(Buffer+12,behind.z);
    memory.put32(0x74fe7c,1);memory.put32(Source,0x80000006);memory.put32(Source+4,1);memory.put32(Source+8,Buffer);memory.putf(Source+0x48,480);memory.put32(Source+0x4c,0xff123456);memory.put32(Source+0x54,0x10000100);memory.put32(Source+0x58,1);memory.put32(Source+0x70,5);memory.put32(Source+0x74,6);memory.put32(0x74e99c+6*4,0x41ae40);
    for(unsigned i=0;i<3;++i)memory.put32(0x64fe28+i*0x14,Buffer);
    std::array<unsigned char,512> arena{};driving::PcMatrixStack matrices{driving::Bytes(arena.data(),arena.size()),0,0,8};driving::pc_matrix_identity(matrices);
    PcRaceContext race{memory,matrices,[](const PcRaceCall& c){if(c.pc==0x408880||c.pc==0x411060||c.pc==0x409df0||c.pc==PcParticleRenderGlobal)return 0u;throw std::runtime_error("unexpected particle service");},nullptr};
    unsigned random=1234;Bridge bridge(d);PcParticleContext context{race,random,&bridge,&d};const auto authored=particle;
    std::vector<unsigned> saved;for(const auto& key:ParticleSavedStates41b5c0)saved.push_back(d.get_render_state(key[0]));
    d.permit=false;d.records.clear();particles_display_41bd10(context);CHECK(d.records.empty()&&memory.u32(0x8a8cec+8*4)==1);
    const auto deferred=d.point_sprites_deferred;d.permit=true;d.records.clear();particles_display_41bd10(context);
    CHECK(d.records.size()==6&&d.point_sprites_deferred==deferred+1&&memory.u32(0x8a8cec+8*4)==1);
    CHECK(particle==authored&&random==1234&&matrices.depth==0&&matrices.current_offset==0&&g_pc_gpu_zone==0);
    for(unsigned i=0;i<saved.size();++i)CHECK(d.get_render_state(ParticleSavedStates41b5c0[i][0])==saved[i]);CHECK(d.draw_phase()==PcDrawPhase::World);
    g_pc_triple_corrections=nullptr;std::printf("point world: %u actual consumer/panel assertions, CPU only\n",checks);return 0;
}catch(const std::exception& e){g_pc_triple_corrections=nullptr;std::fprintf(stderr,"%s\n",e.what());return 1;}}
