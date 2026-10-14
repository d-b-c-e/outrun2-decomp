// [Bugfixes] FixRightSideBunkiAnimations: the production 44F190 object control
// reaches the 44F6DC model flag store with a right or left branch. No renderer,
// game, media or device is used.
#include "platform/race_area.hpp"
#include "platform/pc_screen.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <vector>
using namespace outrun::platform;
namespace {
constexpr std::uint32_t Car=0x7f700000u,Record=0x7f701000u,Instance=0x7f702000u,Object=0x7f703000u;
bool run(bool right,bool fix){
    PcRaceMemory m;
    std::array<std::uint8_t,0x748> area{};
    std::array<std::uint8_t,0x20> modes{};
    std::array<std::uint8_t,8> cache{};
    std::array<std::uint8_t,4> car_pointer{},object_pointer{};
    std::array<std::uint8_t,1> open{{3}};
    std::vector<std::uint8_t> car(0x1000),record(0x40),instance(0x40),object(0x200);
    m.map(0x7d2d80u,area.data(),area.size());
    m.map(0x780250u,modes.data(),modes.size());
    m.map(0x635f2cu,cache.data(),cache.size());
    m.map(0x799d18u,car_pointer.data(),car_pointer.size());
    m.map(0x79ecc8u,object_pointer.data(),object_pointer.size());
    m.map(0x79fca4u,open.data(),open.size());
    m.map(Car,car.data(),car.size());m.map(Record,record.data(),record.size());
    m.map(Instance,instance.data(),instance.size());m.map(Object,object.data(),object.size());
    m.put32(0x799d18u,Car);m.put32(0x79ecc8u,Object);
    m.put32(Car+0x5cu,1u);                 // object control's active-car path
    m.put8(0x7d33d0u,1u);                 // branch choice already established
    m.put32(0x7d2d80u,0x14u);             // no transition/release state
    m.put32(0x7d349cu,Record);m.put32(Record+0xcu,1u);
    m.put32(Record+0x20u,Instance);m.put16(Object+0x90u,0x55aau);
    const std::array<float,16> identity{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
    std::memcpy(m.at(0x7d3190u,64,true),identity.data(),64);
    std::vector<std::uint8_t> arena(8*64);
    outrun::driving::PcMatrixStack stack{outrun::driving::Bytes(arena.data(),arena.size()),0,0,8};
    outrun::driving::pc_matrix_identity(stack);
    unsigned calls=0;
    PcRaceContext ctx{m,stack,[&](const PcRaceCall& c)->std::uint32_t{
        ++calls;
        if(c.pc==0x450380u||c.pc==0x450130u)return 0;
        if(c.pc==0x451350u)return right?1u:0u;
        throw std::runtime_error("unexpected race-area service");
    },nullptr};
    g_pc_fix_right_side_bunki_animations=fix;
    race_area_objects_44f190(ctx);
    return calls==3&&m.u32(Object+0x88u)==1u&&m.u32(Instance+0x30u)==1u&&
        m.u32(0x7d34a0u)==1u&&m.u16(Object+0x90u)==(right&&!fix?1u:0x55aau);
}
}
int main(){try{
    if(!g_pc_fix_right_side_bunki_animations||!run(true,true)||!run(true,false)||!run(false,true)||!run(false,false)){
        std::fprintf(stderr,"bunki animation flag: right/left or enabled/disabled mismatch\n");return 1;
    }
    g_pc_fix_right_side_bunki_animations=true;
    std::puts("bunki animation flag: four production-path cases passed");return 0;
}catch(const std::exception& e){
    g_pc_fix_right_side_bunki_animations=true;
    std::fprintf(stderr,"bunki animation flag: %s\n",e.what());return 1;
}}
