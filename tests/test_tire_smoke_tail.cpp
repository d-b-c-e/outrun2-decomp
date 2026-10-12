// The tire smoke emit loops' full-source shortcut (race_particles_cars.cpp): byte-exact against the full loop.
// A huge travel on a tire (tens of thousands of iterations) is run once with the shortcut off (the reference: every
// iteration's get_work scan) and once with it on, from the same memory; the whole particle module memory, the
// generator and the CRT random state must match, for the player (41CB30) and the rivals (41CED0) emitter, with
// source 4 full from the first iteration and with a few free records filled first.
#include "platform/race_particles_port.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace outrun;
using namespace outrun::platform;
using namespace outrun::platform::particles_detail;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"tire smoke tail line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
constexpr std::uint32_t CarBase=0x7f700000u,BankBase=0x7f710000u;
struct World {
    PcParticleState st;
    std::vector<std::uint8_t> car=std::vector<std::uint8_t>(0x1000);
    std::array<std::uint8_t,4> car_pointer{},frame{};
    std::array<std::uint8_t,0x48> resource57{};std::array<std::uint8_t,0x40> bank57{};   // 448810(0x57): an empty bank
    std::vector<std::uint8_t> lights=std::vector<std::uint8_t>(0x960);                   // 899B98 environment lights
    std::array<std::uint8_t,0x30> override_82e7c0{};                                     // 82E7C0 race camera override (off)
    std::vector<std::uint8_t> arena=std::vector<std::uint8_t>(8*64);
    std::uint32_t crt=12345;
    bool same(const World& o)const{
        return st.data==o.st.data&&st.bss==o.st.bss&&st.glow==o.st.glow&&st.misc==o.st.misc&&st.tire_bss==o.st.tire_bss&&
            st.tire_data==o.st.tire_data&&car==o.car&&lights==o.lights&&crt==o.crt;
    }
    void map(PcRaceMemory& m){
        st.map(m);particles_map_tables(m);
        std::memcpy(car_pointer.data(),&CarBase,4);std::memcpy(resource57.data(),&BankBase,4);
        m.map(0x799d18u,car_pointer.data(),4);m.map(CarBase,car.data(),car.size());m.map(0x95af0cu,frame.data(),4);
        m.map(P::resource(0x57),resource57.data(),resource57.size());m.map(BankBase,bank57.data(),bank57.size());
        m.map(0x899b98u,lights.data(),lights.size());m.map(0x82e7c0u,override_82e7c0.data(),override_82e7c0.size());
    }
};
// `f` with a particle context over the world's memory (no PC service: any callee is a test failure).
template<class F> void with(World& w,F f){
    PcRaceMemory m;w.map(m);
    driving::PcMatrixStack stack{driving::Bytes(w.arena.data(),w.arena.size()),0,0,8};driving::pc_matrix_identity(stack);
    PcRaceContext rc{m,stack,[](const PcRaceCall& k)->std::uint32_t{
        char t[48];std::snprintf(t,sizeof t,"unexpected PC call %08x",k.pc);throw std::runtime_error(t);},nullptr};
    PcParticleContext pc{rc,w.crt};
    f(pc);
}
// A world with source 4 set up by its own init and all but `free_slots` of its records taken.
World make(std::uint32_t record,float travel,unsigned free_slots){
    World w;
    with(w,[&](PcParticleContext& pc){
        particles_tire_reset_41fb10_41c420(pc);
        P p(pc);
        auto& src=source(p,SourceTireSmoke);
        CHECK(src.slots>=16u&&src.free_list==0u&&src.stride>=0x20u);
        for(std::uint32_t k=0;k+free_slots<src.slots;++k)p.at<Particle>(src.buffer+k*src.stride).function=0x41d1e0u;
        // The tire record: travel at +0, previous / current positions +04.. / +1C.., the smoke colour table +58.
        p.putf(record,travel);
        for(unsigned k=0;k<3;++k){p.putf(record+4+k*4,float(k)*1.5f);p.putf(record+0x1c+k*4,float(k)*1.5f+travel*0.5f);}
        p.put(record+0x54,0);p.put(record+0x58,p.u(0x74ff44u));
        p.putf(record+0x50,1.0f);
    });
    return w;
}
void run(World& w,bool shortcut,bool rivals,std::uint32_t record){
    with(w,[&](PcParticleContext& pc){
        P p(pc);
        tire_smoke_shortcut()=shortcut;
        try{if(rivals)tire_smoke_41ced0(p,record,1.0f);else tire_smoke_41cb30(p,record,0.5f);}
        catch(...){tire_smoke_shortcut()=true;throw;}
        tire_smoke_shortcut()=true;
    });
}
}
int main(){try{
    // 41CB30 (player tire 0) and 41CED0 (car 1, tire 2): full from the start, then three free records first.
    struct Case{bool rivals;std::uint32_t record;unsigned free_slots;};
    for(const Case k:{Case{false,0x934448u,0},Case{true,0x9345b8u+2u*0x5cu,0},Case{false,0x934448u,3},Case{true,0x9345b8u+2u*0x5cu,3}}){
        const float travel=3500.0f;   // 10000 iterations at 0.35
        smoke_stats()=SmokeEmitStats{};   // per case: the longest loop must be this case's
        World a=make(k.record,travel,k.free_slots),b=a;
        CHECK(a.same(b));
        const auto before=smoke_stats();
        run(a,false,k.rivals,k.record);
        const auto full=smoke_stats();
        run(b,true,k.rivals,k.record);
        const auto fast=smoke_stats();
        CHECK(a.same(b));                                              // byte-exact module memory, generator, CRT
        CHECK(full.full_tails==before.full_tails&&fast.full_tails==full.full_tails+1);
        CHECK(fast.skipped-full.skipped>9000u);                         // the tail did skip the scans
        CHECK(fast.longest>=10000u&&fast.longest_record==k.record&&fast.longest_function==(k.rivals?0x41ced0u:0x41cb30u));
        if(k.free_slots){World c=make(k.record,travel,k.free_slots);CHECK(!c.same(a));}   // the free records did fill
    }
    std::printf("tire smoke tail: %u checks\n",checks);
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"tire smoke tail: %s\n",e.what());return 1;}
}
