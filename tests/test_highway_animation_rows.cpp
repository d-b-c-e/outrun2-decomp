// Production 44D410 selector, with synthetic table rows and scripted external services.
// No game, renderer, media or device. Recreated fixtures model separate area/menu call contexts;
// they do not qualify the complete runtime lifecycle or arbitrary midprocess state restoration.
#include "platform/race_area.hpp"
#include "platform/pc_screen.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace outrun::platform;
namespace {
unsigned checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
constexpr std::uint32_t Car=0x7f700000u,None=0xffffffffu;
constexpr std::array<std::uint32_t,12> States{{0x2a16ad1e,0x09174a44,0xeba8f70b,0x5c74584f,
    0x38c7add1,0x62c0868f,0x303aaf67,0xdf19cc0c,0x359fd740,0x2181af9b,0x9a2a71e2,0x56dd26c9}};
constexpr std::array<unsigned,12> Rows{{0,0,3,1,0,1,0,3,0,0,2,1}};
std::uint32_t entry(unsigned row,unsigned column){return 1000u+row*10u+column;}
struct Fixture {
    PcRaceMemory m;
    std::array<std::uint8_t,0x748> area{};
    std::array<std::uint8_t,0xc98> data{};
    std::array<std::uint8_t,4> mode{},game{},car_pointer{};
    std::array<std::uint8_t,0x80> car{};
    std::array<std::uint8_t,64> arena{};
    std::vector<std::vector<std::uint32_t>> calls;
    std::uint32_t c2c_a=2,c2c_b=3,branch_x=0;
    explicit Fixture(unsigned native_mode=2,unsigned game_state=16){
        m.map(0x7d2d80u,area.data(),area.size());m.map(0x635f2cu,data.data(),data.size());
        m.map(0x780258u,mode.data(),mode.size());m.map(0x78026cu,game.data(),game.size());
        m.map(0x799d18u,car_pointer.data(),car_pointer.size());m.map(Car,car.data(),car.size());
        m.put32(0x6368c4u,None);m.put32(0x780258u,native_mode);m.put32(0x78026cu,game_state);
        m.put32(0x799d18u,Car);m.put32(Car+0x68u,0x1234);
        m.put32(0x635f2cu,0x1234);m.put32(0x635f30u,0x55); // preexisting lookup cache: no fixture write on lookup
        m.put32(0x7d3094u,100);
        for(unsigned row=0;row<7;++row)for(unsigned column=0;column<7;++column)
            m.put32(0x6366b8u+row*28u+column*4u,entry(row,column));
    }
    std::vector<std::uint8_t> bytes()const{
        std::vector<std::uint8_t> out;
        const auto append=[&](const auto& a){out.insert(out.end(),a.begin(),a.end());};
        append(area);append(data);append(mode);append(game);append(car_pointer);append(car);
        return out;
    }
    std::uint32_t run(){
        calls.clear();
        outrun::driving::PcMatrixStack matrices{outrun::driving::Bytes(arena.data(),arena.size()),0,0,1};
        PcRaceContext ctx{m,matrices,[&](const PcRaceCall& k){
            std::vector<std::uint32_t> call{k.pc,k.eax,k.ecx,k.argc};
            call.insert(call.end(),k.args.begin(),k.args.begin()+k.argc);calls.push_back(call);
            if(k.pc==0x455b80u)return c2c_a;
            if(k.pc==0x455ba0u)return c2c_b;
            if(k.pc==0x450630u)return branch_x;
            if(k.pc==0x47fbd0u)return 1u;
            throw std::runtime_error("unexpected service: feature must not consume the game RNG");
        },nullptr};
        return race_area_ending_44d410(ctx);
    }
};
struct Case {unsigned mode,column;std::function<void(Fixture&)> prepare;};
void native_conditions(){
    const auto none=[](Fixture&){};
    const auto deep=[](Fixture& f){f.m.put32(0x7d3090u,60);f.m.put32(0x7d308cu,0x113);};
    const std::vector<Case> cases{
        {0,0,[](Fixture& f){f.m.put32(0x7d3074u,1);}},
        {1,0,[](Fixture& f){f.m.put32(0x7d3078u,1);}},
        {2,1,[](Fixture& f){f.m.put32(0x7d307cu,1);}},
        {0,1,[](Fixture& f){f.m.put32(0x7d3088u,20);}},
        {0,2,none},
        {0,3,[](Fixture& f){f.m.put32(0x7d3090u,1);}},
        {0,4,[](Fixture& f){f.m.put32(0x7d3090u,1);f.m.put32(0x7d308cu,0x104);}},
        {0,4,[](Fixture& f){f.m.put32(0x7d3090u,59);f.m.put32(0x7d308cu,0x113);}},
        {0,4,[&](Fixture& f){deep(f);f.m.put32(0x635f30u,0);}},
        {0,5,[&](Fixture& f){deep(f);f.branch_x=2;}},
        {0,6,deep},
        {1,2,[](Fixture& f){f.m.put32(0x7d3088u,1);}},
        {1,3,[](Fixture& f){f.m.put32(0x7d3080u,1);}},
        {1,4,none},
        {1,4,[](Fixture& f){f.m.put32(0x7d30a4u,600);f.m.put32(0x635f30u,0);}},
        {1,5,[](Fixture& f){f.m.put32(0x7d30a4u,600);f.m.put32(0x7d308cu,0x112);}},
        {1,6,[](Fixture& f){f.m.put32(0x7d30a4u,600);f.m.put32(0x7d308cu,0x113);}},
        {2,2,[](Fixture& f){f.m.put32(0x7d3088u,1);}},
        {2,3,[](Fixture& f){f.m.put32(0x7d3080u,1);}},
        {2,4,[](Fixture& f){f.m.put32(0x7d3084u,60);}},
        {2,2,[](Fixture& f){f.m.put32(0x7d3084u,1);f.m.put32(0x7d30a4u,299);}},
        {2,5,[](Fixture& f){f.m.put32(0x7d3084u,1);f.m.put32(0x7d30a4u,300);}},
        {2,6,[](Fixture& f){f.m.put32(0x7d30a4u,300);}},
        {5,2,none},{6,2,none}
    };
    for(const auto& c:cases){
        Fixture f(c.mode);c.prepare(f);const auto before=f.bytes();
        g_pc_random_highway_animation_rows=false;g_pc_highway_animation_rows.state=States[1];
        check(f.run()==entry(c.mode,c.column),"disabled selector keeps the native table row and event column");
        check(g_pc_highway_animation_rows.state==States[1],"disabled selector does not advance feature state");
        const auto calls=f.calls;
        g_pc_random_highway_animation_rows=true;
        // The third v1 draw is row3. Using it as a mode would wrongly enter the C2C branch.
        check(f.run()==entry(3,c.column),"enabled selector changes only the row, preserving the actual mode's conditions");
        check(g_pc_highway_animation_rows.state==States[2],"one eligible selector call advances exactly once");
        check(f.calls==calls,"native service arguments and order survive table-row selection");
        check(f.bytes()==before,"selector leaves all mapped game data, mode and lookup state unchanged");
    }
}
void bypasses(){
    for(bool enabled:{false,true}){
        g_pc_random_highway_animation_rows=enabled;g_pc_highway_animation_rows.state=States[4];
        for(unsigned value:{0u,77u}){
            Fixture forced;forced.m.put32(0x6368c4u,value);const auto before=forced.bytes();
            check(forced.run()==value&&forced.calls.empty()&&forced.bytes()==before,"forced override returns unchanged without external calls");
            check(g_pc_highway_animation_rows.state==States[4],"forced override never advances feature state");
        }
        for(unsigned mode:{3u,4u})for(int missing:{-1,0,1}){
            Fixture f(mode);if(missing==0)f.c2c_a=0;if(missing==1)f.c2c_b=0;const auto bytes=f.bytes();
            check(f.run()==(missing<0?entry(1,2):None),"C2C keeps course-based selection and missing-course refusal");
            check(f.calls==std::vector<std::vector<std::uint32_t>>{{0x455b80u,0,0,1,0x55},{0x455ba0u,0,0,1,0x55}},"C2C calls preserve exact arguments and order");
            check(f.bytes()==bytes&&g_pc_highway_animation_rows.state==States[4],"C2C does not mutate mapped game data or feature state");
        }
    }
}
std::vector<std::uint32_t> cold_sequence(){
    g_pc_highway_animation_rows={};g_pc_random_highway_animation_rows=true;
    std::vector<std::uint32_t> result;
    for(unsigned i=0;i<Rows.size();++i){
        // Recreate area memory and alternate race/menu frame words without resetting process feature state.
        Fixture f(2,(i&1u)?2u:16u);f.m.put32(0x7d307cu,1);
        result.push_back(f.run());
        check(result.back()==entry(Rows[i],1)&&g_pc_highway_animation_rows.state==States[i],"v1 golden row/state persists across repeated area/menu call contexts");
        const auto saved=g_pc_highway_animation_rows.state;
        Fixture disabled;g_pc_random_highway_animation_rows=false;(void)disabled.run();
        g_pc_random_highway_animation_rows=true;
        Fixture forced;forced.m.put32(0x6368c4u,77);(void)forced.run();
        Fixture c2c(3);(void)c2c.run();
        check(g_pc_highway_animation_rows.state==saved,"interleaved disabled/override/C2C calls do not consume the next selection");
    }
    return result;
}
}
int main(){try{
    check(!g_pc_random_highway_animation_rows,"visual variant defaults off");
    check(PcHighwayAnimationRows::Version==1&&g_pc_highway_animation_rows.state==0x4f523252u,"versioned algorithm has a fixed initial process state");
    native_conditions();bypasses();
    const auto first=cold_sequence(),second=cold_sequence();
    check(first==second,"equal cold-start state and call sequences produce equal selections");
    g_pc_random_highway_animation_rows=false;g_pc_highway_animation_rows={};
    std::printf("highway animation rows: %u checks passed\n",checks);return 0;
}catch(const std::exception& e){
    g_pc_random_highway_animation_rows=false;g_pc_highway_animation_rows={};
    std::fprintf(stderr,"highway animation rows: %s\n",e.what());return 1;
}}
