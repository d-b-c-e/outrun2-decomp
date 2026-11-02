// Actual 474D30 trim/flag -> 477090 racer bridge -> 477B60 score consumer.
// Course progress/selection/speed queries are controlled boundaries, not a run
// of the AI or a natural mission. EXE constants read only; no renderer/device.
#include "platform/mission_manager.hpp"
#include "platform/race_traffic.hpp"
#include "platform/pc_address_view.hpp"
#include "system/exe_image.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <cstring>
using namespace outrun;using namespace platform;
namespace {unsigned checks;}
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("rival drift line "+std::to_string(__LINE__)+": " #x);}while(0)
int main(){try{
    if(!exe_image_autoload()){std::printf("SKIP: set OR2_EXE to the game's cache for traffic constants\n");return 77;}
    PcRaceMemory memory;for(std::size_t i=0;i<EmbeddedExeRangeCount;++i)memory.map_const(EmbeddedExeRanges[i].base,EmbeddedExeRanges[i].data,EmbeddedExeRanges[i].size);
    PcTrafficState traffic;traffic.map(memory);
    constexpr std::uint32_t Car=0x7804b0u,Racer=0x10000000u;
    std::array<std::uint8_t,0x1100> car_bytes{};driving::Bytes car(car_bytes.data(),car_bytes.size());memory.map(Car,car_bytes.data(),car_bytes.size());
    std::uint32_t car_handle=Car,config=0;memory.map(0x799b38u,reinterpret_cast<std::uint8_t*>(&car_handle),4);memory.map(0x80fb0cu,reinterpret_cast<std::uint8_t*>(&config),4);
    RacerSetupState racers;racers.racers_80fb00.resize(0xa0);racers.count_80fb04=1;memory.map(Racer,racers.racers_80fb00.data(),racers.racers_80fb00.size());
    driving::Bytes racer(racers.racers_80fb00.data(),racers.racers_80fb00.size());racer.put32(0x4c,0);
    std::array<std::uint8_t,16*64> arena{};driving::PcMatrixStack matrices{driving::Bytes(arena.data(),arena.size()),0,0,16};driving::pc_matrix_identity(matrices);
    bool selected=true,fast=false;unsigned progress_calls=0;
    PcRaceContext context{memory,matrices,[&](const PcRaceCall& call){
        if(call.pc==0x4a6cf0u){++progress_calls;return 0u;} // valid zero course progress, float bits
        if(call.pc==0x4957f0u)return selected?1u:0u;
        if(call.pc==0x4960a0u)return fast?1u:0u;
        throw std::runtime_error("unexpected traffic service "+std::to_string(call.pc));},nullptr};
    RaceAssetPack pack;pack.race_count=1;pack.bytes.resize(0x44);std::uint32_t type=2;std::memcpy(pack.bytes.data()+0x20,&type,4);
    MissionManagerState mission;mission.record_83637c=0;mission.selection_836374=1;
    MissionManagerServices services;services.races=&pack;services.variant_780258=6;
    MissionRaceServices race;race.racers=&racers;
    const auto sample=[&](std::int16_t yaw){
        car.put16(0x162,std::uint16_t(yaw));car.put16(0xc26,1);car.put16(0xc2c,0);
        traffic_control_part(context,0x474d30u,Car,0,0);traffic_control_part(context,0x477090u,Racer,0,0);
        CHECK(matrices.depth==0);CHECK(mission_rivals_477b60(mission,services,race));
    };
    sample(0);CHECK(!(car.u32(0xc)&0x100000u)&&!(racer.u8(0x71)&2u)&&racer.u32(0x98)==0);
    sample(0x4000);CHECK((car.u32(0xc)&0x100000u)&&(racer.u8(0x71)&2u)&&racer.u32(0x98)==10);
    sample(-0x4000);CHECK(racer.u32(0x98)==20); // both drift directions
    fast=true;sample(0x4000);CHECK((racer.u8(0x71)&8u)&&racer.u32(0x98)==40);
    sample(0);CHECK(!(racer.u8(0x71)&2u)&&racer.u32(0x98)==40); // stopping a drift stops scoring
    racer.put32(0x6c,1);sample(0x4000);CHECK(racer.u32(0x98)==40);racer.put32(0x6c,0);
    mission.selection_836374=0;sample(0x4000);CHECK(racer.u32(0x98)==40);mission.selection_836374=1;
    type=3;std::memcpy(pack.bytes.data()+0x20,&type,4);sample(0x4000);CHECK(racer.u32(0x98)==40); // other mission uses another activity bit
    type=2;std::memcpy(pack.bytes.data()+0x20,&type,4);fast=false;sample(0x4000);CHECK(racer.u32(0x98)==50&&progress_calls==9);
    racers.count_80fb04=2;CHECK(!mission_rivals_477b60(mission,services,race)&&race.missing==0x477b80u); // corrupt count refuses the out-of-range record
    std::printf("rival drift score: %u checks; trim flag/racer/score chain passed; natural mission unqualified\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
