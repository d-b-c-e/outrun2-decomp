// Actual PART_EFC display, synthetic particles/matrices and recording COM boundary.
// Establishes the existing CPU rejection/packet contract, not corrected triples.
#include "platform/race_particles.hpp"
#include "platform/pc_screen.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
using namespace outrun;using namespace platform;
namespace {unsigned checks;}
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("particle admission line "+std::to_string(__LINE__)+": " #x);}while(0)
struct Device:PcParticleDevice {
    std::map<unsigned,unsigned> rs,tex,stage,sampler;unsigned fvf{},shader{};
    struct Draw{unsigned type,count,stride,fvf;std::vector<std::uint8_t> bytes;};std::vector<Draw> draws;
    std::vector<unsigned> released;
    unsigned get_render_state(unsigned s)override{return rs[s];}void set_render_state(unsigned s,unsigned v)override{rs[s]=v;}
    unsigned get_texture(unsigned s)override{return tex[s];}void set_texture(unsigned s,unsigned v)override{tex[s]=v;}
    unsigned get_texture_stage_state(unsigned s,unsigned t)override{return stage[s*256+t];}void set_texture_stage_state(unsigned s,unsigned t,unsigned v)override{stage[s*256+t]=v;}
    unsigned get_sampler_state(unsigned s,unsigned t)override{return sampler[s*256+t];}void set_sampler_state(unsigned s,unsigned t,unsigned v)override{sampler[s*256+t]=v;}
    void set_transform(unsigned,const float*)override{}
    void draw_primitive_up(unsigned t,unsigned n,const std::uint8_t* p,unsigned stride)override{
        if(!n)return;const auto vertices=t==4?n*3:t==5?n+2:n;
        draws.push_back({t,n,stride,fvf,{p,p+std::size_t(vertices)*stride}});
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
    g_pc_triple_corrections=nullptr;g_pc_fix_particle_rendering=true;
    std::printf("screen particle admission: %u checks; actual RHW/front/behind/restoration contract, no corrected-render claim\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
