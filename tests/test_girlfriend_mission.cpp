// Issue 4: real 477510 ranks -> 496A30/477B60 last-place timers and elimination.
// Course fractional progress and car-release are controlled services. No AI,
// event scheduler, retail mission selection, renderer, audio or device is run.
#include "platform/mission_manager.hpp"
#include "platform/race_traffic.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace outrun;using namespace platform;
namespace {unsigned checks;}
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("girlfriend mission line "+std::to_string(__LINE__)+": " #x);}while(0)
int main(){try{
    constexpr std::uint32_t Player=0x10000000u,Racers=0x10010000u,Table=0x10020000u;
    PcRaceMemory memory;PcTrafficState traffic;traffic.map(memory);
    std::array<std::uint8_t,0x1100> player_bytes{},camera_bytes{};
    driving::Bytes player(player_bytes.data(),player_bytes.size()),camera(camera_bytes.data(),camera_bytes.size());
    memory.map(Player,player_bytes.data(),player_bytes.size());
    RacerSetupState racers;racers.count_80fb04=1;racers.racers_80fb00.resize(0xa0);racers.table_80fb1c.resize(2*0x14);
    driving::Bytes rival(racers.racers_80fb00.data(),racers.racers_80fb00.size());
    rival.put32(0x4c,~0u);rival.put32(0x54,1); // unspawned active racer; rank still follows its course position
    rival.put32(0x98,3);rival.put32(0x9c,0);
    std::uint32_t player_pointer=Player,racer_pointer=Racers,table_pointer=Table;
    const auto map_word=[&](std::uint32_t address,auto& value){memory.map(address,reinterpret_cast<std::uint8_t*>(&value),sizeof(value));};
    map_word(0x799d18u,player_pointer);map_word(0x80fb00u,racer_pointer);map_word(0x80fb04u,racers.count_80fb04);
    map_word(0x80fb1cu,table_pointer);map_word(0x80fb18u,racers.v80fb18);map_word(0x80fb2cu,racers.v80fb2c);
    map_word(0x64e190u,racers.v64e190);map_word(0x64e194u,racers.v64e194);map_word(0x8037b8u,racers.v8037b8);
    memory.map(Racers,racers.racers_80fb00.data(),racers.racers_80fb00.size());
    memory.map(Table,racers.table_80fb1c.data(),racers.table_80fb1c.size());
    std::array<std::uint8_t,16*64> arena{};driving::PcMatrixStack matrices{driving::Bytes(arena.data(),arena.size()),0,0,16};
    driving::pc_matrix_identity(matrices);float fraction=.25f;bool ranking_blocked=false;
    PcRaceContext context{memory,matrices,[&](const PcRaceCall& call){
        if(call.pc==0x44ff10u)return ranking_blocked?1u:0u;
        if(call.pc==0x4a6cf0u){CHECK(call.args[0]==Player);std::uint32_t bits;std::memcpy(&bits,&fraction,4);return bits;}
        throw std::runtime_error("unexpected ranking service "+std::to_string(call.pc));},nullptr};
    RaceAssetPack pack;pack.race_count=1;pack.bytes.resize(0x44);driving::Bytes record(pack.bytes.data(),pack.bytes.size());
    record.put32(0,1);record.put32(4,1);record.put32(0x20,6);record.putf(0x28,30);record.put32(0x38,1000);record.put32(0x3c,2000);
    MissionManagerState mission;mission.record_83637c=0;mission.stage_836384=4;mission.selection_836374=1;mission.timer_836388=1800;
    FrontendSprites sprites;CHECK(sprites.bind_timing(0x2b,std::vector<FrontendSpriteTiming>(256,{240,60})));
    FrontendUiResources ui{sprites};mission_manager_construct_ui_465160(mission,ui);
    PcLicense profile;profile.fill(7);NaviVoiceQueue voice;unsigned releases=0;
    MissionManagerServices services;services.races=&pack;services.variant_780258=6;
    MissionRaceServices race;race.racers=&racers;race.car=player;race.camera=camera;race.ui=&ui;race.profile=&profile;race.voice=&voice;
    race.mode_78026c=16;camera.put32(0x354,1); // skip projected marker; not part of the ranking/timer assertion
    race.user=&releases;race.release_car_4763d0=[](void* p,std::uint32_t index){CHECK(index==0);++*static_cast<unsigned*>(p);return true;};
    const auto rank=[&](unsigned player_section,unsigned rival_section,float rival_fraction){
        player.put16(0x260,std::uint16_t(player_section));rival.put32(0x3c,rival_section);rival.putf(0x40,rival_fraction);
        traffic_ranks_477510(context);CHECK(matrices.depth==0);
    };
    const auto tick=[&]{CHECK(mission_manager_control_496a30(mission,services,race));CHECK(!race.missing&&!ui.missing_pc);};
    // No hand-authored rank: real section ordering drives the decrement.
    rank(11,10,.75f);CHECK(player.u8(0xc36)==0&&rival.u8(0x74)==1&&racers.v80fb2c==2&&racers.v64e194==0);
    tick();CHECK(rival.u32(0x98)==2&&rival.u32(0x9c)==1&&mission.timer_836388==1800&&race.race_end_7d38f0==0);
    rank(9,10,.75f);CHECK(player.u8(0xc36)==1&&rival.u8(0x74)==0&&racers.v64e194==-1);
    tick();CHECK(rival.u32(0x98)==2&&rival.u32(0x9c)==0&&mission.timer_836388==1799);
    // Same section: actual fractional ordering, overtake and re-overtake.
    rank(10,10,.75f);CHECK(player.u8(0xc36)==1);fraction=.9f;rank(10,10,.75f);CHECK(player.u8(0xc36)==0);
    const auto saved=mission.timer_836388;
    mission.selection_836374=0;tick();CHECK(rival.u32(0x98)==2&&mission.timer_836388==saved);mission.selection_836374=1;
    race.pause_8367bc=1;tick();CHECK(rival.u32(0x98)==2&&mission.timer_836388==saved);race.pause_8367bc=0;
    race.mode_78026c=19;tick();CHECK(rival.u32(0x98)==2);race.mode_78026c=16;
    race.race_end_7d38f0=2;tick();CHECK(rival.u32(0x98)==2);race.race_end_7d38f0=0;
    // Linked car release boundary is observed once when the real timer reaches zero.
    rival.put32(0x4c,0);tick();CHECK(rival.u32(0x98)==1&&releases==0&&race.race_end_7d38f0==0);
    tick();CHECK(rival.u32(0x98)==0&&releases==1&&race.race_end_7d38f0==1&&mission.v8363a4==1);
    CHECK(rival.u32(0x54)==0&&rival.u32(0x58)==1&&profile[0x1dd]==4&&mission.v67e6b8==0x2b004e);
    tick();CHECK(releases==1&&rival.u32(0x98)==0); // completed-race guard prevents repeat release/underflow
    // The player can also lose at zero; no release of the leading rival.
    race.race_end_7d38f0=0;mission.v8363a4=0;mission.timer_836388=1;rival.put32(0x4c,~0u);rival.put32(0x54,1);rival.put32(0x98,5);
    rank(9,10,.75f);tick();CHECK(mission.timer_836388==0&&race.race_end_7d38f0==2&&releases==1&&rival.u32(0x98)==5);
    CHECK(mission.v8363a4==1&&mission.v67e6b0==0);
    // A blocked rank refresh preserves the previous ranking, matching the existing native guard.
    ranking_blocked=true;rank(12,10,.75f);CHECK(player.u8(0xc36)==1&&rival.u8(0x74)==0);
    std::printf("girlfriend mission: %u checks; real ranks/timers/win/loss passed; natural mission unqualified\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
