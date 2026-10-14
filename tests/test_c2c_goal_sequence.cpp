// Coast 2 Coast GOAL sequencing (ROADMAP 7, upstream #2: "about 30 s from GOAL to OutRun Miles, no flagman rank").
// Source sequencing only: the production mode 19 init/control (race_end_modes.cpp), the goal camera override control
// (race_goal_camera.cpp) and the GAD_PUB GOAL display, over the EXE image's tables (OR2_EXE, read only; skipped
// without it). The PC services around them are a recorder that models the race-end runtime's mode-global semantics
// (race_end_runtime.cpp: 43F900/43FA90/43F910/43F8C0/43FA00) and answers the mission accessors with chosen values.
// It does not run the real sprite, sound or mission services, so it cannot qualify a natural Flagman 1 finish.
#include "platform/race_end_modes.hpp"
#include "platform/race_goal_camera.hpp"
#include "platform/race_hud.hpp"
#include "platform/pc_address_view.hpp"
#include "driving/pc_matrix_stack.hpp"
#include "system/exe_image.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
using namespace outrun;
using namespace outrun::platform;
static unsigned checks=0;
static void req(bool v,const std::string& m){++checks;if(!v){std::fprintf(stderr,"FAIL: %s\n",m.c_str());std::exit(1);}}

namespace {
std::string hex_(std::uint32_t v){char b[16];std::snprintf(b,sizeof b,"%x",v);return b;}
// The mission accessors' answers for one run (race_end_runtime.cpp 646..: 495B20 = the Races record +0x20,
// 495820 = [8363A4], 496440 = [80FB2C] racers, 496510 = [67E6B0] grade, 495830 = [67E6B8]).
struct Mission { std::uint32_t kind=0,suspend=0,racers=6,grade=2,banner_token=0x2c0010u,flag_780260=0; };
// The synthetic fault's own type, so no other failure (unmapped memory, a refused service) can pass for it.
struct SyntheticRankFault {};
struct Trace {
    int frames=0,exit_frame=-1,rank_frame=-1,cut_frame=-1,camera_off_frame=-1;
    std::uint32_t exit_mode=0,rank_token=0,expected_rank=0;
    std::vector<std::uint32_t> voices,unexpected;
    std::vector<std::uint32_t> countdown_sets;
    bool faulted=false;
};
struct World {
    PcRaceEndState st;
    PcRaceAreaState area;   // the AREA module block and .data (run_end maps it while the race area exists)
    GadPubState gad;
    std::uint32_t variant=6u,preset=0u,update=0u,frames_8367f4=0u,handle_67f614=0u,player_ptr=0x7804b0u,camera_79f574=0x79fe10u;
    std::uint32_t mode=0x13u,book=PcRaceEndState::BookBase,goal_6840ec=0,goal_6840f0=0,goal_6840f4=0,lock_7d6764=0;
    float value_84bd00=0.0f;
    std::vector<std::uint8_t> models=std::vector<std::uint8_t>(0x2c),player=std::vector<std::uint8_t>(0x10f0),
        license=std::vector<std::uint8_t>(0x400),common=std::vector<std::uint8_t>(0x800),override_82e7c0=std::vector<std::uint8_t>(0x30),
        body=std::vector<std::uint8_t>(0x900),camera=std::vector<std::uint8_t>(0x400),navi=std::vector<std::uint8_t>(0x180),
        arena=std::vector<std::uint8_t>(16*64);
    void map(PcRaceMemory& m){
        // As run_end (race_end_runtime.cpp 225..300) for the words these functions touch.
        for(std::size_t i=0;i<EmbeddedExeRangeCount;++i)m.map_const(EmbeddedExeRanges[i].base,EmbeddedExeRanges[i].data,EmbeddedExeRanges[i].size);
        area.map(m);
        goal_camera_map_tables(m);
        st.map(m);race_end_map_tables(m);
        m.map(0x780258u,reinterpret_cast<std::uint8_t*>(&variant),4);m.map(0x78024cu,reinterpret_cast<std::uint8_t*>(&preset),4);
        m.map(0x8a8cdcu,reinterpret_cast<std::uint8_t*>(&update),4);m.map(0x8367f4u,reinterpret_cast<std::uint8_t*>(&frames_8367f4),4);
        m.map(0x8367c0u,models.data(),models.size());m.map(0x67f614u,reinterpret_cast<std::uint8_t*>(&handle_67f614),4);
        m.map(0x84bd00u,reinterpret_cast<std::uint8_t*>(&value_84bd00),4);
        m.map(0x799d18u,reinterpret_cast<std::uint8_t*>(&player_ptr),4);m.map(0x7804b0u,player.data(),player.size());
        m.map(0x7c23e0u,license.data(),license.size());m.map(0x7b17f8u,common.data(),common.size());
        m.map(0x6840ecu,reinterpret_cast<std::uint8_t*>(&goal_6840ec),4);m.map(0x6840f0u,reinterpret_cast<std::uint8_t*>(&goal_6840f0),4);
        m.map(0x6840f4u,reinterpret_cast<std::uint8_t*>(&goal_6840f4),4);m.map(0x7d6764u,reinterpret_cast<std::uint8_t*>(&lock_7d6764),4);
        m.map(0x82e7c0u,override_82e7c0.data(),override_82e7c0.size());m.map(0x82e7f0u,body.data(),body.size());
        m.map(0x79f574u,reinterpret_cast<std::uint8_t*>(&camera_79f574),4);m.map(0x79fe10u,camera.data(),camera.size());
        m.map(0x78026cu,reinterpret_cast<std::uint8_t*>(&mode),4);
        m.map_const(0x63960cu,reinterpret_cast<const std::uint8_t*>(&book),4);
        m.map(GadPubState::Base,gad.block.data(),gad.block.size());m.map(GadPubState::DataBase,gad.data.data(),gad.data.size());
        m.map(GadPubState::Data2Base,gad.data2.data(),gad.data2.size());
        m.map(0x7f2560u,navi.data(),navi.size());
    }
};

// One GOAL from the mode 19 init: per 60 Hz tick the PC frame (pc_common_control.cpp runtime_frame_step_417c7b) runs
// mode control (43FA20 -> 49CC50) then event control (43FAB0: the camera override event's 4874A0), then once per
// display the event displays (449050: GAD_PUB 4998C0 -> 498670). One tick per display, as at 60 fps.
// fault_rank_sprite: SYNTHETIC. The GOAL rank sprite request (428320 with a 5C2184 token) throws, and the next display
// tries again. This only shows what the sequence does if that one display step keeps failing; it is not a model of the
// runtime's soft-fault handling or evidence that the real service fails.
Trace run_unguarded(const Mission& ms,bool fault_rank_sprite,int limit);
// Any failure other than the synthetic one is a test failure, named with its PC or address.
Trace run(const Mission& ms,bool fault_rank_sprite=false,int limit=3600){
    try{return run_unguarded(ms,fault_rank_sprite,limit);}
    catch(const PcRaceEndUndefined& x){req(false,"PcRaceEndUndefined at "+std::to_string(x.pc)+" (0x"+hex_(x.pc)+")");}
    catch(const PcRaceEndUnreachable& x){req(false,"PcRaceEndUnreachable at 0x"+hex_(x.pc));}
    catch(const std::exception& x){req(false,std::string("exception: ")+x.what());}
    return {};
}
Trace run_unguarded(const Mission& ms,bool fault_rank_sprite,int limit){
    World w;Trace t;
    std::int32_t countdown=0;std::uint32_t next_handle=0x100u;
    PcRaceMemory m;w.map(m);
    driving::PcMatrixStack stack{driving::Bytes(w.arena.data(),w.arena.size()),0,0,16};driving::pc_matrix_identity(stack);
    const std::uint32_t rank_base=0x5c2184u;
    t.expected_rank=m.u32(rank_base+ms.grade*4u);   // 498670 case 6 step 1: [5C2184 + 496510() * 4]
    PcRaceContext c{m,stack,[&](const PcRaceCall& k)->std::uint32_t{
        const auto* a=k.args.data();
        switch(k.pc){
        // mode globals, as race_end_runtime.cpp 356/357/721/366/859 (780248 pause flag 0)
        case 0x43f900u:countdown=std::int32_t(a[0]);t.countdown_sets.push_back(a[0]);if(a[0]==0x3cu&&t.cut_frame<0)t.cut_frame=t.frames;return 0u;
        case 0x43fa90u:--countdown;return countdown<=0?1u:0u;
        case 0x43f910u:return std::uint32_t(countdown);
        case 0x43f8c0u:if(t.exit_frame<0){t.exit_frame=t.frames;t.exit_mode=a[0];}return 0u;
        case 0x43fa00u:return 1u;
        case 0x43f860u:return ms.flag_780260;
        case 0x4872e0u:return m.u32(0x82e7d8u);                // race_end_runtime.cpp 720
        // mission accessors
        case 0x495b20u:return ms.kind;
        case 0x495820u:return ms.suspend;
        case 0x496440u:return ms.racers;
        case 0x496510u:return ms.grade;
        case 0x495830u:return ms.banner_token;
        // sound requests 427700(mask): nothing pending (approximation: the GOAL voices end at once)
        case 0x427700u:return 0u;
        case 0x424940u:t.voices.push_back(a[0]);return 0u;
        // sprites: handles; the rank sprite is the 428320 call with a 5C2184 table token
        case 0x428320u:{
            bool rank=false;
            for(std::uint32_t i=0;i<8u;++i)if(a[0]==m.u32(rank_base+i*4u)&&a[1]==8u&&a[2]==5u)rank=true;
            if(rank&&fault_rank_sprite)throw SyntheticRankFault{};
            if(rank&&t.rank_frame<0){t.rank_frame=t.frames;t.rank_token=a[0];}
            return next_handle++;}
        case 0x428460u:return next_handle++;
        // calls this sequence makes with no state the GOAL timing depends on
        case 0x443ea0u:case 0x4035f0u:case 0x427630u:case 0x4278c0u:case 0x4505f0u:case 0x451180u:case 0x450320u:case 0x448990u:
        case 0x44b7b0u:case 0x450380u:case 0x44dc50u:case 0x440a10u:case 0x4287b0u:case 0x4285a0u:case 0x429530u:case 0x4b8e20u:
        case 0x4bc990u:case 0x42ca60u:case 0x42cc60u:case 0x42ccb0u:case 0x4294c0u:case 0x401000u:case 0x4249f0u:case 0x42e020u:
        case 0x401030u:case 0x401050u:case 0x48b1e0u:case 0x427aa0u:case 0x4493c0u:case 0x450240u:case 0x440bb0u:case 0x487b70u:
        case 0x41fb10u:case 0x49a650u:case 0x4532e0u:case 0x41c420u:case 0x4a7d70u:case 0x49f7c0u:case 0x440a30u:case 0x49f7d0u:
        case 0x4502e0u:case 0x44ff10u:case 0x4957f0u:case 0x495c10u:
        // goal_place (camera op 7) placement queries: which matrix the car is put on, not when (0: the course matrix)
        case 0x46c3f0u:case 0x495b10u:case 0x48b310u:case 0x48b320u:case 0x495490u:case 0x4954f0u:
        // 497560 rank rows: the racer record of a slot only feeds the row text (0: an empty slot, no text)
        case 0x476710u:
            return 0u;
        default:t.unexpected.push_back(k.pc);return 0u;
        }
    },nullptr};
    gad_pub_init_4970f0(w.gad);          // event 389 init at race start (race_hud.cpp 127)
    mode19_init_49c9c0(c);
    const bool camera=m.u32(0x82e7d8u)!=0u;
    for(t.frames=0;t.frames<limit&&t.exit_frame<0;++t.frames){
        mode19_control_49cc50(c);
        if(t.exit_frame>=0)break;
        const bool before=m.u32(0x82e7d8u)!=0u;
        camera_override_control_4874a0(c);
        if(before&&m.u32(0x82e7d8u)==0u&&t.camera_off_frame<0)t.camera_off_frame=t.frames;
        try{gad_display_4998c0(c);}
        catch(const SyntheticRankFault&){t.faulted=true;}
    }
    if(!camera)t.camera_off_frame=-2;
    return t;
}
std::string hex(std::uint32_t v){char b[16];std::snprintf(b,sizeof b,"%x",v);return b;}
std::string describe(const char* label,const Mission& ms,const Trace& t){
    char b[400];
    std::snprintf(b,sizeof b,"%s kind %u suspend %u racers %u grade %u: exit frame %d (mode %s), rank sprite frame %d token %s, 0x3C cut frame %d, camera off frame %d, voices %zu",
        label,ms.kind,ms.suspend,ms.racers,ms.grade,t.exit_frame,hex(t.exit_mode).c_str(),t.rank_frame,hex(t.rank_token).c_str(),t.cut_frame,t.camera_off_frame,t.voices.size());
    return b;
}
}

int main(){
    if(!exe_image_autoload()){std::printf("SKIP: set OR2_EXE to the game's OR2006C2C.cache (the GOAL tables come from it)\n");return 77;}
    // Normal C2C GOAL. Every branch-distinct case below is printed; the assertions are the sequence's own steps.
    // exit: 49CC50 leaves for 0x21, or 0x22 when 43F860 is set for variant 6.
    struct Case { const char* label; Mission ms; std::uint32_t exit=0x21u; };
    std::vector<Case> cases{
        {"ranked mission (kind 0)",{}},
        {"flagman-style kind 6 (no rank rows, no banner)",{6u}},
        {"kind 1 with grade 0 (banner replaced, voice 0x1F0)",{1u,0u,6u,0u}},
        {"kind 6 that suspends the race (no goal camera)",{6u,1u}},
        {"grade 0 (first table entry)",{0u,0u,6u,0u}},
        {"grade 5 (last voice entry)",{0u,0u,6u,5u}},
        {"seven racers (clamped to six rows)",{0u,0u,7u,2u}},
        {"43F860 set (exit to mode 0x22)",{0u,0u,6u,2u,0x2c0010u,1u},0x22u},
    };
    for(const auto& k:cases){
        const Trace t=run(k.ms);
        std::printf("%s\n",describe(k.label,k.ms,t).c_str());
        {std::string list;for(auto pc:t.unexpected)if(list.find(hex(pc))==std::string::npos)list+=" "+hex(pc);
         req(t.unexpected.empty(),std::string(k.label)+": unexpected PC services"+list);}
        req(t.countdown_sets.size()>=1&&t.countdown_sets.front()==0x4b0u,std::string(k.label)+": mode 19 init sets the 20 s countdown first");
        req(t.rank_frame>=0&&t.rank_token==t.expected_rank,std::string(k.label)+": the rank sprite is 5C2184[grade]");
        req(t.cut_frame>t.rank_frame,std::string(k.label)+": the countdown is cut to 0x3C after the rank");
        req(t.exit_frame>0&&t.exit_frame<0x4b0,std::string(k.label)+": GOAL exits before the 20 s countdown: "+std::to_string(t.exit_frame));
        req(t.exit_mode==k.exit,std::string(k.label)+": exit mode "+hex(t.exit_mode)+", expected "+hex(k.exit));
    }
    // SYNTHETIC: the rank sprite request keeps failing at its display step.
    {
        const Mission ms{};
        const Trace t=run(ms,true);
        std::printf("SYNTHETIC %s\n",describe("rank sprite request failing every display",ms,t).c_str());
        req(t.unexpected.empty(),"synthetic fault: no other PC service reached");
        req(t.faulted&&t.rank_frame<0&&t.cut_frame<0,"synthetic fault: no rank sprite and no 0x3C cut");
        req(t.exit_frame>=0x4b0,"synthetic fault: GOAL waits for the full 20 s countdown: "+std::to_string(t.exit_frame));
    }
    std::printf("PASS: c2c_goal_sequence, %u checks (source sequencing only; real sprite, sound and mission services not run)\n",checks);
    return 0;
}
