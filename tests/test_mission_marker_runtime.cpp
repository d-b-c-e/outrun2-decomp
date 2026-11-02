// Actual native mission wrapper, inert runtime records/UI, no renderer/device.
#include "platform/native_runtime.hpp"
#include "platform/race_hud_runtime.hpp"
#include "system/exe_image.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
using namespace outrun;using namespace platform;
namespace{unsigned checks;}
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"mission marker runtime line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    if(!exe_image_autoload()){std::printf("SKIP: set OR2_EXE to the game's cache for native runtime constants\n");return 77;}
    auto runtime=std::make_unique<NativeRuntimeContext>();auto& c=*runtime;c.mode_state.current=16;c.game_mode.game_variant=6;
    auto& m=c.mission;auto& cs=c.event_function36.car_select;
    driving::Bytes car(cs.car_799d18.data(),cs.car_799d18.size()),camera(cs.camera_79fe10.data(),cs.camera_79fe10.size());
    for(unsigned i=0;i<16;++i){car.putf(0xb0+i*4,i%5==0?1.f:0.f);camera.putf(0x140+i*4,i%5==0?1.f:0.f);}
    car.put8(0xc36,0);camera.put8(0x34a,2);camera.putf(0xb0,320);camera.putf(0xb4,240);
    CHECK(c.event_function36.frontend_sprites.bind_timing(0x2b,std::vector<FrontendSpriteTiming>(256,{240,60})));
    RaceAssetPack pack;pack.race_count=1;pack.bytes.resize(0x44);driving::Bytes record(pack.bytes.data(),pack.bytes.size());
    record.put32(0,1);record.put32(4,1);record.put32(0x20,6);record.putf(0x28,30);record.put32(0x38,1000);record.put32(0x3c,2000);
    m.manager.record_83637c=0;m.manager.stage_836384=4;m.manager.selection_836374=1;m.manager.timer_836388=1800;m.manager.v8363a0=1800;
    m.racers.count_80fb04=1;m.racers.v80fb2c=2;m.racers.v64e194=0;m.racers.racers_80fb00.resize(0xa0);
    driving::Bytes rival(m.racers.racers_80fb00.data(),m.racers.racers_80fb00.size());rival.put32(0x4c,~0u);rival.put32(0x54,1);rival.put8(0x74,1);rival.put32(0x98,3);
    MissionManagerServices services;services.races=&pack;services.variant_780258=6;
    const auto camera_before=cs.camera_79fe10;const auto car_before=cs.car_799d18;
    for(unsigned i=0;i<3;++i){
        const bool ok=native_race_hud_mission_control(c,services);
        if(!ok)std::fprintf(stderr,"wrapper failure: missing=%x rival=%u %s\n",m.missing,rival.u32(0x98),native_race_hud_status(c).c_str());
        CHECK(ok&&m.missing==0);CHECK(rival.u32(0x98)==2-i);CHECK(cs.camera_79fe10==camera_before);
    }
    CHECK(c.race.manager.state.over_state_7d38f0==1&&m.manager.v8363a4==1);
    CHECK(rival.u32(0x54)==0&&rival.u32(0x58)==1);
    CHECK(cs.car_799d18.size()==car_before.size());
    CHECK(std::memcmp(cs.car_799d18.data(),car_before.data(),0xdbc)==0&&
          std::memcmp(cs.car_799d18.data()+0xdc0,car_before.data()+0xdc0,car_before.size()-0xdc0)==0); // controller owns car+DBC boost only here
    CHECK(c.race_hud&&c.race_hud->missing.empty());
    // Type2's live score/grade marker also projects in chase, through this same
    // wrapper, rather than the direct-controller fixture's supplied stack.
    record.put32(0x20,2);m.manager.timer_836388=0;m.manager.v8363a0=0;m.manager.v8363a4=0;
    c.race.manager.state.over_state_7d38f0=0;rival.put32(0x54,1);rival.put32(0x98,100);rival.put8(0x71,2);
    car.put8(0xd36,1);car.putf(0x1c4,1.f);
    for(unsigned i=1;i<=3;++i){CHECK(native_race_hud_mission_control(c,services));
        CHECK(m.manager.timer_836388==i*10&&rival.u32(0x98)==100+i*10&&m.missing==0);CHECK(cs.camera_79fe10==camera_before);}
    c.game_mode.start_countdown=1;CHECK(native_race_hud_mission_control(c,services));
    CHECK(m.manager.timer_836388==30&&rival.u32(0x98)==130);c.game_mode.start_countdown=0;
    CHECK(c.race_hud->missing.empty());
    std::printf("mission marker runtime: %u checks; actual chase wrapper/projection/scoring passed, no natural mission claim\n",checks);
}
