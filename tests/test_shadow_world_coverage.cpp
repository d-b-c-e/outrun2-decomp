// Actual 422740 -> 4220F0 packet/phase boundary and the constants consumed by
// D3D11's existing GS. CPU only: no graphics device or rendered-pixel claim.
#include "platform/pc_shadow_volume.hpp"
#include "platform/pc_render_flush.hpp"
#include "platform/pc_soft_d3d9.hpp"
#include "platform/pc_world_coverage.hpp"
#include "system/exe_image.hpp"
#include "triple_rig.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
using namespace outrun;using namespace platform;
namespace {unsigned checks;}
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("shadow coverage line "+std::to_string(__LINE__)+": " #x);}while(0)
struct Device:PcSoftD3D9Device {
    Device():PcSoftD3D9Device(32,32){}
    unsigned draws{},fvf{},type{},count{},stride{};bool fail{};
    PcDrawPhase phase{};PcDrawDomain domain{};bool admitted{};
    std::array<unsigned,8> states{};std::vector<std::uint8_t> bytes;
    std::vector<unsigned> releases,restored;
    unsigned get_texture(unsigned stage)override{return 200+stage;}
    void set_texture(unsigned,unsigned value)override{restored.push_back(value);}
    void release(unsigned value)override{releases.push_back(value);}
    void set_fvf(unsigned value)override{fvf=value;PcSoftD3D9Device::set_fvf(value);}
    void draw_primitive_up(unsigned t,unsigned n,const void* p,unsigned s)override{
        ++draws;type=t;count=n;stride=s;phase=draw_phase();domain=draw_domain(true,logical_backbuffer_target());
        admitted=pc_world_coverage_draw(phase,true,logical_backbuffer_target());
        const unsigned keys[]{7,52,27,19,20,57,56,55};for(unsigned i=0;i<8;++i)states[i]=get_render_state(keys[i]);
        const auto* data=static_cast<const std::uint8_t*>(p);bytes.assign(data,data+(n+2)*s);
        if(fail)throw std::runtime_error("synthetic draw failure");
    }
};
int main(){try{
    if(!exe_image_autoload()){std::puts("SKIP: set OR2_EXE to verified OR2006C2C.cache (renderer reset constants)");return 77;}
    std::array<std::uint8_t,0xa8> object{};driving::Bytes b(object.data(),object.size());
    for(unsigned i=0;i<6;++i){const unsigned off=0x20+i*20;b.putf(off,i%2?640.f:0.f);b.putf(off+4,i<2?0.f:i<4?440.f:480.f);
        b.putf(off+8,0);b.putf(off+12,1);b.put32(off+16,0x5f000000);}
    const auto authored=object;
    PcRaceMemory memory;memory.map(0x1000,object.data(),object.size());
    std::array<std::uint8_t,128> arena{};driving::PcMatrixStack matrices{driving::Bytes(arena.data(),arena.size()),0,0,2};driving::pc_matrix_identity(matrices);
    for(const auto caller:{PcDrawPhase::World,PcDrawPhase::Hud,PcDrawPhase::FrontendOverlay,PcDrawPhase::Unclassified}){
        for(const bool back:{true,false}){
            Device d;d.set_draw_phase(caller);PcRenderGlobals globals;PcFlushContext flush{d,globals};PcShadowServices services{memory,d};PcShadowFrame frame{flush,matrices};
            if(!back){const auto texture=d.create_texture(32,32,1,1,21,0),target=d.get_surface_level(texture,0);CHECK(target!=0);d.set_render_target(0,target);}
            CHECK(d.logical_backbuffer_target()==back);
            d.releases.clear(); // target binding released its previous CPU-state surface
            shadow_darken_422740(services,frame,0x1000);
            CHECK(d.draws==1&&d.type==5&&d.count==4&&d.stride==20&&d.fvf==0x44);
            CHECK(d.bytes==std::vector<std::uint8_t>(object.begin()+0x20,object.begin()+0x98));
            CHECK((d.states==std::array<unsigned,8>({0,1,1,5,6,0,2,1})));
            CHECK(d.phase==(caller==PcDrawPhase::World?PcDrawPhase::WorldCoverage:caller));
            CHECK(d.draw_phase()==caller&&object==authored&&matrices.depth==0&&matrices.current_offset==0);
            CHECK(d.admitted==(back&&caller==PcDrawPhase::World));
            CHECK(d.domain==pc_draw_domain(caller,true,back));
            CHECK(d.releases==std::vector<unsigned>({200,201,202,203}));
            CHECK(d.restored.size()>=4&&std::equal(d.restored.begin(),d.restored.begin()+4,std::array<unsigned,4>{200,201,202,203}.begin()));
            CHECK(globals.w(0x89ede0)==0&&globals.w(0x89ede8)==0);
        }
    }
    {Device d;d.fail=true;d.set_draw_phase(PcDrawPhase::World);PcRenderGlobals globals;PcFlushContext flush{d,globals};PcShadowServices services{memory,d};PcShadowFrame frame{flush,matrices};
        bool failed=false;try{shadow_darken_422740(services,frame,0x1000);}catch(const std::runtime_error& ex){failed=std::string(ex.what())=="synthetic draw failure";}
        CHECK(failed&&d.phase==PcDrawPhase::WorldCoverage&&d.draw_phase()==PcDrawPhase::World);}
    {Device d;d.set_draw_phase(PcDrawPhase::World);PcRenderGlobals globals;PcFlushContext flush{d,globals};PcShadowServices services{memory,d};PcShadowFrame frame{flush,matrices};
        shadow_darken_422740(services,frame,0);CHECK(d.draws==0&&d.draw_phase()==PcDrawPhase::World);}
    for(const float fx:{1.f/640,1.f/1280})for(const float fy:{1.f/480,1.f/1440}){
        std::array<float,52> constants{};CHECK(pc_world_coverage_constants(PcDrawPhase::WorldCoverage,true,true,fx,fy,constants));
        CHECK(constants[48]==fx&&constants[49]==fy&&constants[50]==0&&constants[51]==0);
        for(unsigned panel=0;panel<3;++panel){desktop::Mat4 c{};for(unsigned i=0;i<16;++i){CHECK(constants[panel*16+i]==(i%5==0?1.f:0.f));c[i]=constants[panel*16+i];}
            for(double w:{.1,1.,10.})for(double z:{0.,.5,1.})for(double x:{-1.,0.,1.})for(double y:{-1.,0.,1.}){
                const std::array<double,4> p{x*w,y*w,z*w,w};const auto q=desktop::triple_apply(c,fx,fy,p);
                for(unsigned i=0;i<4;++i)CHECK(std::fabs(q[i]-p[i])<1e-12);
            }
        }
    }
    std::array<float,52> untouched{};untouched.fill(7.f);auto c=untouched;
    for(const auto phase:{PcDrawPhase::World,PcDrawPhase::Hud,PcDrawPhase::FrontendOverlay,PcDrawPhase::Unclassified})CHECK(!pc_world_coverage_constants(phase,true,true,1,1,c)&&c==untouched);
    CHECK(!pc_world_coverage_constants(PcDrawPhase::WorldCoverage,false,true,1,1,c)&&c==untouched);
    CHECK(!pc_world_coverage_constants(PcDrawPhase::WorldCoverage,true,false,1,1,c)&&c==untouched);
    for(float bad:{0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        CHECK(!pc_world_coverage_constants(PcDrawPhase::WorldCoverage,true,true,bad,1,c)&&c==untouched);
        CHECK(!pc_world_coverage_constants(PcDrawPhase::WorldCoverage,true,true,1,bad,c)&&c==untouched);
    }
    std::printf("shadow world coverage: %u checks passed (CPU only)\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
