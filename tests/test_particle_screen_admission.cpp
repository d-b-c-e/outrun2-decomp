// Actual PART_EFC display, synthetic particles/matrices and recording COM boundary.
// Establishes the existing CPU rejection/packet contract, not corrected triples.
#include "platform/race_particles.hpp"
#include "platform/pc_screen.hpp"
#include "platform/pc_soft_d3d9.hpp"
#include "triple_rig.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <limits>
#include <stdexcept>
using namespace outrun;using namespace platform;
namespace {unsigned checks;}
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("particle admission line "+std::to_string(__LINE__)+": " #x);}while(0)
struct Device:PcParticleDevice {
    std::map<unsigned,unsigned> rs,tex,stage,sampler;unsigned fvf{},shader{};
    struct Draw{unsigned type,count,stride,fvf,lighting,fog;std::vector<std::uint8_t> bytes;};std::vector<Draw> draws;
    std::vector<unsigned> released;
    unsigned get_render_state(unsigned s)override{return rs[s];}void set_render_state(unsigned s,unsigned v)override{rs[s]=v;}
    unsigned get_texture(unsigned s)override{return tex[s];}void set_texture(unsigned s,unsigned v)override{tex[s]=v;}
    unsigned get_texture_stage_state(unsigned s,unsigned t)override{return stage[s*256+t];}void set_texture_stage_state(unsigned s,unsigned t,unsigned v)override{stage[s*256+t]=v;}
    unsigned get_sampler_state(unsigned s,unsigned t)override{return sampler[s*256+t];}void set_sampler_state(unsigned s,unsigned t,unsigned v)override{sampler[s*256+t]=v;}
    void set_transform(unsigned,const float*)override{}
    void draw_primitive_up(unsigned t,unsigned n,const std::uint8_t* p,unsigned stride)override{
        if(!n)return;const auto vertices=t==4?n*3:t==5?n+2:n;
        draws.push_back({t,n,stride,fvf,rs[0x89],rs[0x1c],{p,p+std::size_t(vertices)*stride}});
    }
    void set_fvf(unsigned v)override{fvf=v;}void set_vertex_shader(unsigned v)override{shader=v;}
    void set_pixel_shader(unsigned)override{}void release(unsigned object)override{released.push_back(object);}
};
int main(){try{
    PcRaceMemory memory;PcParticleState state;state.map(memory);
    // All .data needed by this fixture is synthetic; no EXE image is loaded.
    std::fill(state.data.begin(),state.data.end(),0);std::fill(state.tire_data.begin(),state.tire_data.end(),0);
    std::array<std::uint8_t,0x240> render{};memory.map(0x95d860,render.data(),render.size());
    std::array<std::uint8_t,8> screen{};memory.map(0x740c8c,screen.data(),screen.size());memory.putf(0x740c8c,320);memory.putf(0x740c90,240);
    unsigned frame=1;memory.map(0x95af0c,reinterpret_cast<std::uint8_t*>(&frame),4);
    std::array<std::uint8_t,16*64> arena{};driving::PcMatrixStack matrices{driving::Bytes(arena.data(),arena.size()),0,0,16};driving::pc_matrix_identity(matrices);
    const auto identity=[&](unsigned address){for(unsigned i=0;i<16;++i)memory.putf(address+4*i,i%5==0?1.f:0.f);};
    identity(0x95d860);identity(0x95d8a0);memory.putf(0x95d8a0+40,-1);memory.putf(0x95d8a0+44,-1);memory.putf(0x95d8a0+56,-.1f);memory.putf(0x95d8a0+60,0);
    memory.put32(0x74fe7c,1);memory.put32(0x74e99c+2*4,0x418cf0);memory.putf(0x74f7bc,100);memory.putf(0x74f7c0,1);
    constexpr unsigned Source=0x8a8d18,Buffer=0x10000000;
    std::array<std::uint8_t,4*0x50> particles{};memory.map(Buffer,particles.data(),particles.size());
    for(unsigned i=0;i<3;++i)memory.put32(0x64fe28+i*0x14,Buffer); // valid address even for a zero-count tire-mark call
    memory.put32(Source,0x80000002);memory.put32(Source+4,4);memory.put32(Source+8,Buffer);memory.putf(Source+0x44,1);memory.put32(Source+0x50,123);
    memory.put32(Source+0x70,5);memory.put32(Source+0x74,6);
    for(unsigned i=0;i<3;++i){const auto p=Buffer+i*0x50;memory.put32(p,0x80000000);memory.putf(p+0x3c,1);memory.put32(p+0x38,0xff123456);memory.putf(p+0x48,1);memory.putf(p+0x4c,1);}
    memory.putf(Buffer+12,-10);memory.putf(Buffer+0x50+12,10);memory.putf(Buffer+0xa0+12,0); // front, behind, eye-plane
    Device device;for(const auto& saved:ParticleSavedStates41b5c0)device.rs[saved[0]]=1000+saved[0];
    for(unsigned i=0;i<4;++i)device.tex[i]=2000+i;const auto saved_rs=device.rs,saved_tex=device.tex;
    unsigned resets=0,render_globals=0;PcRaceContext race{memory,matrices,[&](const PcRaceCall& call){
        if(call.pc==0x408880){++resets;return 0u;}
        if(call.pc==0x411060||call.pc==0x409df0)return 0u; // renderer transform boundary; draw packets still recorded
        if(call.pc==PcParticleRenderGlobal){CHECK(call.args[0]==0x89ede0||call.args[0]==0x89ede8);CHECK(call.args[1]==0);++render_globals;return 0u;}
        throw std::runtime_error("unexpected particle service "+std::to_string(call.pc));},nullptr};
    unsigned random=1234;PcParticleContext context{race,random,&device};
    const auto run=[&](bool fix){g_pc_fix_particle_rendering=fix;device.draws.clear();device.released.clear();resets=render_globals=0;
        const auto authored=particles;particles_display_41bd10(context);
        CHECK(particles==authored&&random==1234&&matrices.depth==0&&matrices.current_offset==0);
        for(const auto& saved:ParticleSavedStates41b5c0)CHECK(device.rs[saved[0]]==saved_rs.at(saved[0]));
        CHECK(device.tex==saved_tex&&device.released==std::vector<unsigned>({2000,2001,2002,2003,2000,2001,2002,2003}));
        CHECK(resets==2&&render_globals==4&&g_pc_gpu_zone==0);
    };
    run(false);CHECK(device.draws.size()==1&&memory.u32(0x8a8cec)==1);const auto original=device.draws[0];
    CHECK(original.type==4&&original.count==2&&original.stride==0x1c&&original.fvf==0x144&&original.bytes.size()==6*0x1c);
    driving::Bytes quad(const_cast<std::uint8_t*>(original.bytes.data()),original.bytes.size());
    CHECK(std::fabs(quad.f32(0)-288)<.0001f&&std::fabs(quad.f32(4)-208)<.0001f&&std::fabs(quad.f32(8)-.99f)<.0001f);
    CHECK(quad.u32(12)==0x3f800000&&quad.u32(16)==0xff123456&&quad.f32(20)==0&&quad.f32(24)==0);
    CHECK(quad.f32(0x1c+20)==1&&quad.f32(0x1c+24)==0&&quad.f32(2*0x1c+20)==0&&quad.f32(2*0x1c+24)==1);
    run(true);CHECK(device.draws.size()==1);driving::Bytes fixed(device.draws[0].bytes.data(),device.draws[0].bytes.size());
    CHECK(fixed.f32(0)==144&&fixed.f32(4)==104&&fixed.f32(8)==quad.f32(8)&&fixed.u32(16)==quad.u32(16));
    // Angled presence cannot recover CPU-rejected quads or change their RHW format.
    g_pc_triple_corrections=[](const float*,float*){throw std::runtime_error("screen particle must not request panel corrections yet");return false;};
    run(false);CHECK(device.draws.size()==1&&device.draws[0].bytes==original.bytes);
    memory.put32(Buffer,0);run(false);CHECK(device.draws.empty()&&memory.u32(0x8a8cec)==0); // behind + eye-plane remain rejected
    memory.putf(Buffer+0x50+12,-10);run(false);CHECK(device.draws.size()==1&&memory.u32(0x8a8cec)==1);
    // Type3 is a different layout and route: smoke/water/backfire are world XYZ
    // billboards already, including behind-center particles. Do not substitute
    // it blindly for the 0x50-byte screen-particle records above.
    std::array<std::uint8_t,0x200> camera{};constexpr unsigned Camera=0x10030000;
    memory.map(Camera,camera.data(),camera.size());unsigned camera_pointer=Camera;
    memory.map(0x79f574,reinterpret_cast<std::uint8_t*>(&camera_pointer),4);identity(Camera+0x140);
    memory.put32(Source,0x80000003);memory.put32(Source+4,2);memory.put32(Source+0x7c,1);
    memory.put32(0x74e99c+3*4,0x419210);memory.put32(0x74f7c4,3);
    particles.fill(0);
    for(unsigned i=0;i<2;++i){const auto p=Buffer+i*0x5c;memory.put32(p,0x80000000);memory.putf(p+12,i?10.f:-10.f);memory.putf(p+0x20,2);
        for(unsigned j=0;j<4;++j)memory.put32(p+0x24+j*4,0xff112200+j);
        memory.putf(p+0x34,0);memory.putf(p+0x38,0);memory.putf(p+0x3c,1);memory.putf(p+0x40,1);
    }
    run(false);CHECK(device.draws.size()==1&&memory.u32(0x8a8cec)==2);const auto billboard=device.draws[0];
    CHECK(billboard.type==4&&billboard.count==4&&billboard.stride==0x18&&billboard.fvf==0x142&&billboard.bytes.size()==12*0x18);
    driving::Bytes bill(const_cast<std::uint8_t*>(billboard.bytes.data()),billboard.bytes.size());
    CHECK(bill.f32(0)==-2&&bill.f32(4)==-2&&bill.f32(8)==-10&&bill.f32(6*0x18+8)==10);
    CHECK(bill.u32(12)==0xff112200&&bill.u32(0x18+12)==0xff112201&&bill.u32(2*0x18+12)==0xff112202&&bill.u32(5*0x18+12)==0xff112203);
    CHECK(bill.f32(16)==0&&bill.f32(20)==0&&bill.f32(0x18+16)==0&&bill.f32(0x18+20)==1&&bill.f32(2*0x18+16)==1&&bill.f32(2*0x18+20)==0);
    run(true);CHECK(device.draws.size()==1&&device.draws[0].bytes==billboard.bytes); // RHW half-resolution fix does not resize XYZ billboards
    // The native World/backbuffer route keeps type2 quads in XYZ. Use the real
    // target owner for admission, still an inert recording boundary for draws.
    PcSoftD3D9Device target(32,32);target.set_draw_phase(PcDrawPhase::World);context.d3d=&target;
    CHECK(target.logical_backbuffer_target());memory.putf(0x740c8c,640);memory.putf(0x740c90,480);
    memory.putf(0x95d8a0+40,1000.f/(.1f-1000.f));memory.putf(0x95d8a0+56,100.f/(.1f-1000.f)); // finite native near/far
    memory.put32(Source,0x80000002);memory.put32(Source+4,4);particles.fill(0);
    for(unsigned i=0;i<3;++i){const auto p=Buffer+i*0x50;memory.put32(p,0x80000000);memory.putf(p+0x3c,1);memory.put32(p+0x38,0xff123456);memory.putf(p+0x48,1);memory.putf(p+0x4c,1);}
    memory.putf(Buffer+12,-10);memory.putf(Buffer+0x50+4,10);memory.putf(Buffer+0x50+12,2);
    g_pc_triple_corrections=nullptr;run(true);CHECK(device.draws.size()==1);const auto legacy=device.draws[0];
    g_pc_triple_corrections=[](const float*,float*){throw std::runtime_error("CPU must leave panel replication to the backend");return false;};
    run(true);CHECK(device.draws.size()==1&&memory.u32(0x8a8cec)==3);const auto xyz=device.draws[0];
    CHECK(xyz.fvf==0x142&&xyz.stride==0x18&&xyz.count==6&&xyz.bytes.size()==18*0x18&&xyz.lighting==0&&xyz.fog==0);
    driving::Bytes emitted(const_cast<std::uint8_t*>(xyz.bytes.data()),xyz.bytes.size());
    desktop::Mat4 projection{};for(unsigned i=0;i<16;++i)projection[i]=memory.f32(0x95d8a0+i*4);
    const auto clip=[&](const desktop::Mat4& m,double x,double y,double z){std::array<double,4> out{};for(unsigned j=0;j<4;++j)out[j]=x*m[j]+y*m[4+j]+z*m[8+j]+m[12+j];return out;};
    for(unsigned i=0;i<18;++i)for(unsigned axis=0;axis<3;++axis)CHECK(std::isfinite(emitted.f32(i*0x18+axis*4)));
    const auto compare_front=[&](const Device::Draw& old,const Device::Draw& current){
        driving::Bytes prior(const_cast<std::uint8_t*>(old.bytes.data()),old.bytes.size()),vertices(const_cast<std::uint8_t*>(current.bytes.data()),current.bytes.size());
        for(unsigned i=0;i<6;++i){const auto a=i*0x18,b=i*0x1c;const auto p=clip(projection,vertices.f32(a),vertices.f32(a+4),vertices.f32(a+8));
            CHECK(std::abs(320.*(p[0]/p[3]+1.)-prior.f32(b))<1e-4&&std::abs(240.*(1.-p[1]/p[3])-prior.f32(b+4))<1e-4&&std::abs(p[2]/p[3]-prior.f32(b+8))<1e-5);
            CHECK(vertices.u32(a+12)==prior.u32(b+16)&&vertices.u32(a+16)==prior.u32(b+20)&&vertices.u32(a+20)==prior.u32(b+24));
        }
    };compare_front(legacy,xyz);
    desktop::RigVec side{};for(unsigned i=6;i<12;++i)side=side+desktop::RigVec{emitted.f32(i*0x18),emitted.f32(i*0x18+4),emitted.f32(i*0x18+8)}*(1./6.);
    CHECK(side.z>0);desktop::TripleViews views;CHECK(desktop::triple_views(projection,desktop::fixture_rig(),views));
    const auto right=clip(desktop::mat_mul(views.rotation[2],views.projection[2]),side.x,side.y,side.z);
    CHECK(right[3]>0&&std::abs(right[0])<right[3]&&std::abs(right[1])<right[3]&&right[2]>0&&right[2]<right[3]);
    memory.putf(Buffer+0x3c,20);memory.putf(0x74f7c0,.25f);g_pc_triple_corrections=nullptr;run(true);const auto clamped=device.draws[0];
    g_pc_triple_corrections=[](const float*,float*){return false;};run(true);CHECK(device.draws.size()==1&&device.draws[0].fvf==0x142);compare_front(clamped,device.draws[0]);
    memory.putf(Buffer+0x3c,1);memory.putf(0x74f7c0,1);
    // Existing target and phase boundaries retain the exact ordinary RHW bytes.
    const auto texture=target.create_texture(8,8,1,1,21,0),surface=target.get_surface_level(texture,0);
    target.set_render_target(0,surface);CHECK(!target.logical_backbuffer_target());run(true);CHECK(device.draws.size()==1&&device.draws[0].bytes==legacy.bytes&&device.draws[0].fvf==0x144);
    target.set_render_target(0,PcSoftD3D9Device::BackBuffer);target.release(surface);target.release(texture);
    target.set_draw_phase(PcDrawPhase::Hud);run(true);CHECK(device.draws.size()==1&&device.draws[0].bytes==legacy.bytes);
    target.set_draw_phase(PcDrawPhase::World);run(false);CHECK(device.draws.size()==1&&device.draws[0].fvf==0x144);
    const auto saved_render=render;for(unsigned i=0;i<16;++i)memory.putf(0x95d8a0+i*4,0);
    run(true);CHECK(device.draws.empty()&&device.fvf==0x144);render=saved_render;
    memory.putf(Buffer+4,std::numeric_limits<float>::quiet_NaN());run(true);CHECK(device.draws.size()==1&&memory.u32(0x8a8cec)==2);memory.putf(Buffer+4,0);
    memory.putf(0x740c8c,320);memory.putf(0x740c90,240);run(true);CHECK(device.draws.size()==1&&device.draws[0].fvf==0x144); // unknown logical mapping stays legacy
    memory.putf(0x740c8c,640);memory.putf(0x740c90,480);
    std::vector<std::uint8_t> crowd(600*0x50);for(unsigned i=0;i<600;++i)std::memcpy(crowd.data()+i*0x50,particles.data(),0x50);
    const auto crowd_before=crowd;memory.map(0x10050000,crowd.data(),crowd.size());memory.put32(Source+8,0x10050000);memory.put32(Source+4,600);
    run(true);CHECK(device.draws.size()==1&&device.draws[0].fvf==0x142&&device.draws[0].count==1024&&memory.u32(0x8a8cec)==512&&crowd==crowd_before);
    g_pc_triple_corrections=nullptr;g_pc_fix_particle_rendering=true;
    std::printf("particle admission: %u checks; actual RHW rejection and XYZ billboard/restoration contracts, no corrected-render claim\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
