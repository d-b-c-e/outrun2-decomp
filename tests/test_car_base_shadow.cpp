// CarBaseShadowOpacity (pc_screen.hpp): the race model draw 469600 records Tweaks' CalcPeraShadow (4056D0 with the
// layout's shadow object, alpha = the PC's argument x the factor, colour 0, byte -1) under the car's +B0 matrix raised
// 0.05, in place of the bare RET at 469EB4; at 0 the draw list is the PC's.
#include "platform/vehicle_model_draw.hpp"
#include "platform/embedded_vehicle_draw_data.hpp"
#include "platform/pc_screen.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace outrun;
using namespace outrun::platform;
using driving::Bytes;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"car base shadow line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
float f(std::uint32_t u){float v;std::memcpy(&v,&u,4);return v;}
float mat(const PcVehicleDrawCall& c,unsigned k){float v;std::memcpy(&v,c.matrix.data()+k*4,4);return v;}
struct Run { std::vector<PcVehicleDrawCall> calls; };
Run draw(std::int32_t index,std::uint32_t flags,float c2c8,float c2dc,float opacity){
    std::vector<std::uint8_t> car(0xea0),body(0x600),arena(16*64);
    Bytes c(car.data(),car.size());
    c.put32(4,flags);c.put8(0x11,0);c.putf(0x2c8,c2c8);c.putf(0x2dc,c2dc);
    for(unsigned k=0;k<16;++k)c.putf(0xb0+k*4,(k%5u)==0u?1.f:0.f);
    c.putf(0xb0+12*4,10.f);c.putf(0xb0+14*4,5.f);                       // +B0 translation (10, 0, 5)
    driving::PcMatrixStack s{Bytes(arena.data(),arena.size()),0,0,16};driving::pc_matrix_identity(s);
    PcRaceModelDraw in{c,index,0,Bytes(body.data(),body.size()),0x10,0};
    g_pc_car_base_shadow_opacity=opacity;
    Run r;vehicle_race_model_draw_469600(in,s,r.calls);
    g_pc_car_base_shadow_opacity=1.f;
    return r;
}
const PcVehicleDrawCall* shadow(const Run& r,std::uint32_t object){
    for(const auto& c:r.calls)if(c.pc==0x4056d0u&&c.args[0]==object)return &c;
    return nullptr;
}
}
int main(){
    // A layout with a shadow object (+0C).
    std::int32_t index=-1;std::uint32_t object=0;
    for(std::int32_t i=0;i<30&&index<0;++i){
        std::uint32_t o;std::memcpy(&o,EmbeddedVehicleLayouts+std::size_t(i)*0x128+0xc,4);
        if(o!=0xffffffffu){index=i;object=o;}
    }
    CHECK(index>=0);
    const std::uint32_t vm1=1u<<14;                                       // (+4 >> 14) & 3 == 1: the full model
    // +2C8 > 0: alpha (0.5 - 0.2) * 2 = 0.6, times the factor; colour 0, byte -1; matrix +B0 raised 0.05.
    {const auto r=draw(index,vm1,1.f,0.2f,1.f);const auto* c=shadow(r,object);
     CHECK(c&&c->argc==4u&&f(c->args[1])==(0.5f-0.2f)*2.0f&&c->args[2]==0u&&c->args[3]==0xffffffffu);
     CHECK(mat(*c,12)==10.f&&mat(*c,13)==0.05f&&mat(*c,14)==5.f&&mat(*c,0)==1.f);}
    {const auto r=draw(index,vm1,1.f,0.2f,0.5f);const auto* c=shadow(r,object);
     CHECK(c&&f(c->args[1])==(0.5f-0.2f)*2.0f*0.5f);}
    // The floor 2^-23 (+2DC >= 0.5), +2C8 <= 0 and the +4 bit 0x800000 give alpha 1 / the floor as the PC.
    {const auto r=draw(index,vm1,1.f,0.9f,1.f);const auto* c=shadow(r,object);CHECK(c&&f(c->args[1])==1.1920929e-07f);}
    {const auto r=draw(index,vm1,0.f,0.2f,1.f);const auto* c=shadow(r,object);CHECK(c&&f(c->args[1])==1.f);}
    {const auto r=draw(index,vm1|0x800000u,1.f,0.2f,1.f);const auto* c=shadow(r,object);CHECK(c&&f(c->args[1])==1.f);}
    // Off: the PC's own list (no 4056D0 of the shadow object), otherwise identical.
    {const auto on=draw(index,vm1,1.f,0.2f,1.f),off=draw(index,vm1,1.f,0.2f,0.f);
     CHECK(!shadow(off,object)&&on.calls.size()==off.calls.size()+1);}
    std::printf("car base shadow: %u checks\n",checks);
    return 0;
}
