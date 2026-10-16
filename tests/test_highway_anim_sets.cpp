// Production 44D410 mode substitution with synthetic mapped tables/services.
// No game, renderer, input or device. Authored source only until a named slot.
#include "platform/race_area.hpp"
#include "platform/pc_screen.hpp"
#include <array>
#include <cstdio>
#include <exception>
#include <functional>
#include <stdexcept>
#include <vector>
using namespace outrun::platform;
namespace {
unsigned checks{};
const char* phase="defaults";
std::uint32_t actual_mode{},effective_mode{};
void check(bool ok,const char* why){
    ++checks;if(ok)return;
    std::fprintf(stderr,"RandomHighwayAnimSets: %s; native=%u effective=%u rows=%u: %s\n",
        phase,unsigned(actual_mode),unsigned(effective_mode),unsigned(g_pc_random_highway_animation_rows),why);
    throw std::runtime_error(why);
}
constexpr std::uint32_t Car=0x7f700000u,None=0xffffffffu;
// Literal independent v1 stream coordinates; the fixture never computes a draw
// with the implementation under test to derive its expected row or mode.
constexpr std::array<std::uint32_t,12> States{{0x2a128d3fu,0x0d1f4c45u,0x76645fceu,0x4e21c100u,
    0xb63eba00u,0x4eafdd5fu,0x15889c7du,0xc6e0d0beu,0x4218a665u,0x8c518cefu,0x044a7329u,0x0fc1b1feu}};
constexpr std::array<unsigned,12> Modes{{0,0,1,1,2,1,0,3,1,2,0,0}};
constexpr std::array<std::uint32_t,4> BeforeMode{{0x4f523253u,0x0d1f4c45u,0x4e21c100u,0x15889c7du}};
constexpr std::array<std::uint32_t,4> AfterMode{{0x2a128d3fu,0x76645fceu,0xb63eba00u,0xc6e0d0beu}};
constexpr std::array<std::uint32_t,8> NativeModes{{0,1,2,3,4,5,6,0xffffffffu}};
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
std::vector<Case> event_cases(){
    const auto none=[](Fixture&){};
    const auto deep=[](Fixture& f){f.m.put32(0x7d3090u,60);f.m.put32(0x7d308cu,0x113);};
    return {
        {0,0,[](Fixture& f){f.m.put32(0x7d3074u,1);}},
        {1,0,[](Fixture& f){f.m.put32(0x7d3078u,1);}},
        {2,1,[](Fixture& f){f.m.put32(0x7d307cu,1);}},
        {0,1,[](Fixture& f){f.m.put32(0x7d3088u,20);}},
        {0,2,none},
        {0,3,[](Fixture& f){f.m.put32(0x7d3090u,1);}},
        {0,4,[](Fixture& f){f.m.put32(0x7d3090u,1);f.m.put32(0x7d308cu,0x104);}},
        {0,4,[](Fixture& f){f.m.put32(0x7d3090u,59);f.m.put32(0x7d308cu,0x113);}},
        {0,4,[deep](Fixture& f){deep(f);f.m.put32(0x635f30u,0);}},
        {0,5,[deep](Fixture& f){deep(f);f.branch_x=2;}},
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
        {0,2,[](Fixture& f){f.m.put32(0x7d3094u,0);}}, // NaN retains native comparison behavior
        {1,2,[](Fixture& f){f.m.put32(0x7d3094u,0);}},
        {2,2,[](Fixture& f){f.m.put32(0x7d3094u,0);}},
        {0,1,[](Fixture& f){f.m.put32(0x7d3088u,1);f.m.put32(0x7d3094u,0);}}, // +infinity
        {1,1,[](Fixture& f){f.m.put32(0x7d3088u,1);f.m.put32(0x7d3094u,0);}},
        {2,1,[](Fixture& f){f.m.put32(0x7d3088u,1);f.m.put32(0x7d3094u,0);}}
    };
}
void mode_conditions(){
    phase="mode replacement and conditions";
    for(const auto& c:event_cases()){
        // Disabled is the stock/native-mode control, with no new random draw.
        Fixture baseline(c.mode);c.prepare(baseline);const auto before=baseline.bytes();
        g_pc_random_highway_anim_sets=false;g_pc_random_highway_animation_rows=false;
        g_pc_highway_anim_sets.state=States[11];
        actual_mode=effective_mode=c.mode;
        check(baseline.run()==entry(c.mode,c.column),"disabled keeps native table and condition column");
        check(g_pc_highway_anim_sets.state==States[11]&&baseline.bytes()==before,"disabled does not advance state or write game bytes");
        const auto calls=baseline.calls;
        for(const auto native:NativeModes)for(const bool rows:{false,true}){
            Fixture f(native);c.prepare(f);const auto bytes=f.bytes();
            actual_mode=native;effective_mode=c.mode;
            g_pc_random_highway_anim_sets=true;g_pc_random_highway_animation_rows=rows;
            g_pc_highway_anim_sets.state=BeforeMode[c.mode];
            g_pc_highway_animation_rows.state=0xdf19cc0cu;
            check(f.run()==entry(c.mode,c.column),"both table and conditions follow chosen mode, including from native C2C");
            check(g_pc_highway_anim_sets.state==AfterMode[c.mode],"eligible call consumes one bounded mode draw");
            check(g_pc_highway_animation_rows.state==0xdf19cc0cu,"exact sets takes precedence without advancing Rows");
            check(f.calls==calls,"same effective mode preserves native service arguments/order");
            check(f.bytes()==bytes&&f.m.u32(0x780258u)==native,"local mode substitution never writes actual mode/game bytes");
        }
    }
}
void c2c_conditions(){
    phase="chosen C2C mode";effective_mode=3;
    const std::vector<std::vector<std::uint32_t>> calls{{0x455b80u,0,0,1,0x55},{0x455ba0u,0,0,1,0x55}};
    for(const auto native:NativeModes)for(const bool rows:{false,true})for(const int missing:{-1,0,1}){
        Fixture f(native);if(missing==0)f.c2c_a=0;if(missing==1)f.c2c_b=0;
        const auto bytes=f.bytes();actual_mode=native;
        g_pc_random_highway_anim_sets=true;g_pc_random_highway_animation_rows=rows;
        g_pc_highway_anim_sets.state=BeforeMode[3];g_pc_highway_animation_rows.state=0xdf19cc0cu;
        check(f.run()==(missing<0?entry(1,2):None),"chosen mode3 follows course table or original missing-course refusal");
        check(f.calls==calls,"chosen mode3 enters native C2C services even when actual mode is not C2C");
        check(g_pc_highway_anim_sets.state==AfterMode[3]&&g_pc_highway_animation_rows.state==0xdf19cc0cu,"one Sets draw, no Rows draw");
        check(f.bytes()==bytes,"C2C substitution does not rewrite mapped game state");
    }
    phase="disabled native C2C";
    for(const auto native:{3u,4u})for(const bool rows:{false,true}){
        Fixture f(native);actual_mode=effective_mode=native;
        g_pc_random_highway_anim_sets=false;g_pc_random_highway_animation_rows=rows;
        g_pc_highway_anim_sets.state=States[11];g_pc_highway_animation_rows.state=0xdf19cc0cu;
        check(f.run()==entry(1,2)&&f.calls==calls,"disabled keeps original C2C branch and calls");
        check(g_pc_highway_anim_sets.state==States[11]&&g_pc_highway_animation_rows.state==0xdf19cc0cu,"native C2C bypasses both independent streams when Sets is off");
    }
    phase="Rows compatibility control";
    Fixture rows_only(1);g_pc_random_highway_animation_rows=true;g_pc_random_highway_anim_sets=false;
    g_pc_highway_animation_rows={};g_pc_highway_anim_sets.state=States[11];
    check(rows_only.run()==entry(0,4),"Rows still changes only row while native mode1 chooses column4");
    check(g_pc_highway_animation_rows.state==0x2a16ad1eu&&g_pc_highway_anim_sets.state==States[11],"Rows and Sets streams remain independent");
}
void forced_override(){
    phase="forced override before any mode/car access";
    for(const auto forced:{0u,17u,0xfffffffeu})for(const bool sets:{false,true})for(const bool rows:{false,true}){
        PcRaceMemory m;std::array<std::uint8_t,4> value{};std::array<std::uint8_t,64> arena{};
        m.map(0x6368c4u,value.data(),value.size());m.put32(0x6368c4u,forced);
        // Mode/car/table are deliberately unmapped: reading any would throw.
        outrun::driving::PcMatrixStack matrices{outrun::driving::Bytes(arena.data(),arena.size()),0,0,1};
        PcRaceContext ctx{m,matrices,[](const PcRaceCall&)->std::uint32_t{throw std::runtime_error("forced selector called a service");},nullptr};
        g_pc_random_highway_anim_sets=sets;g_pc_random_highway_animation_rows=rows;
        g_pc_highway_anim_sets.state=States[11];g_pc_highway_animation_rows.state=0xdf19cc0cu;
        check(race_area_ending_44d410(ctx)==forced,"forced rendition returns before any randomness, mode or car read");
        check(g_pc_highway_anim_sets.state==States[11]&&g_pc_highway_animation_rows.state==0xdf19cc0cu,"forced rendition consumes neither stream");
    }
}
std::vector<std::uint32_t> cold_sequence(){
    phase="cold process and repeated area/menu contexts";
    g_pc_highway_anim_sets={};g_pc_highway_animation_rows={};
    std::vector<std::uint32_t> selected;
    for(unsigned i=0;i<Modes.size();++i){
        const auto native=NativeModes[i%NativeModes.size()];actual_mode=native;effective_mode=Modes[i];
        Fixture f(native,(i&1u)?2u:16u);f.m.put32(0x7d307cu,1);
        g_pc_random_highway_anim_sets=true;g_pc_random_highway_animation_rows=(i&1u)!=0;
        const auto rows_before=g_pc_highway_animation_rows.state;
        selected.push_back(f.run());
        check(selected.back()==(Modes[i]==3?entry(1,2):entry(Modes[i],1)),"literal golden modes determine original table/service result");
        check(g_pc_highway_anim_sets.state==States[i]&&g_pc_highway_animation_rows.state==rows_before,"Sets advances once across changing area contexts; Rows is not advanced");
        const auto saved=g_pc_highway_anim_sets.state;
        g_pc_random_highway_anim_sets=false;g_pc_random_highway_animation_rows=true;
        Fixture rows_only(2);rows_only.m.put32(0x7d307cu,1);(void)rows_only.run();
        Fixture c2c(4);(void)c2c.run();
        g_pc_random_highway_anim_sets=true;
        Fixture forced;forced.m.put32(0x6368c4u,17);(void)forced.run();
        check(g_pc_highway_anim_sets.state==saved,"disabled, independent Rows and forced calls preserve next Sets draw");
    }
    return selected;
}
void reset(){
    g_pc_random_highway_anim_sets=false;g_pc_random_highway_animation_rows=false;
    g_pc_highway_anim_sets={};g_pc_highway_animation_rows={};
}
}
int main(){try{
    check(!g_pc_random_highway_anim_sets&&!g_pc_random_highway_animation_rows,"both existing defaults remain off");
    check(PcHighwayAnimSets::Version==1&&g_pc_highway_anim_sets.state==0x4f523253u,"version1 starts at literal fixed nonzero process seed");
    mode_conditions();c2c_conditions();forced_override();
    const auto first=cold_sequence(),second=cold_sequence();
    check(first==second,"identical cold-start settings and call sequence reproduce selections");
    reset();std::printf("RandomHighwayAnimSets: %u checks PASS (synthetic selector/services only)\n",checks);return 0;
}catch(const std::exception& e){reset();std::fprintf(stderr,"RandomHighwayAnimSets: %s\n",e.what());return 1;}}
