// Source-only until a named build/test slot. Actual native frame loop with a
// synthetic existing clock/presenter: no game assets, device, sleep or threads.
#include "enhancements/fast_load.hpp"
#include "enhancements/frame_rate.hpp"
#include "platform/native_runtime.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <vector>

namespace en=outrun::enhancements;
namespace pf=outrun::platform;
namespace {
unsigned checks{};
void check(bool ok,const char* why){
    ++checks;if(ok)return;
    std::fprintf(stderr,"fast_load: %s\n",why);std::exit(1);
}
void policy(){
    en::FastLoad p;
    check(!p.enabled()&&!p.loading()&&!p.active(),"default off/unbound");
    check(p.configure("2"),"accept mode 2");
    check(!p.loading()&&p.present_interval(2)==2u,"enabled but unbound stays paced");
    std::uint32_t mode=13,stage=0;
    {
        en::FastLoadBinding binding(p,mode,stage);
        for(const auto m:{0u,12u,13u,16u,29u,32u})for(const auto s:{0u,1u,64u,65u,66u,0xffffffffu}){
            mode=m;stage=s;p.begin_frame();
            const bool expected=m==13u&&s<65u;
            check(p.active()==expected,"exact START/stage gate");
            check(p.present_interval(1)==(expected?0u:1u),"normal interval 1 preserved");
            check(p.present_interval(2)==(expected?0u:2u),"normal interval 2 preserved");
            check(p.wait_for_latency()!=expected,"only loading skips latency wait");
            check(p.cooperative_level(true)==(expected?2u:1u),"120 Hz loading forces one update");
            check(p.cooperative_level(false)==2u,"60 Hz retains single update");
            check(!p.end_frame(),"stable frame never requests a clock rebase");
        }
        mode=13;stage=64;p.begin_frame();
        stage=65;
        check(!p.active()&&p.wait_for_latency()&&p.present_interval(2)==2u,"stage 65 restores inside final loading frame");
        check(p.loading_frame()&&p.end_frame(),"final loading tick requests one rebase");
        check(!p.end_frame(),"no repeated rebase");
        stage=0;p.begin_frame();mode=32;
        check(!p.active()&&p.end_frame(),"abort restores without reaching stage 65");
        mode=13;stage=0;p.begin_frame();
    }
    check(!p.loading_frame()&&!p.active()&&p.present_interval(2)==2u,"scope exit releases live pointers/override");
    for(const auto text:{"0","1","3","-1","2junk","02","true","","2147483648"}){
        p.configure("2");
        const bool accepted=p.configure(text);
        check(accepted==(std::string_view(text)=="0")&&!p.enabled(),"unsupported values refuse to off");
    }
}
struct Fixture {
    std::unique_ptr<pf::NativeRuntimeContext> context=std::make_unique<pf::NativeRuntimeContext>();
    std::uint64_t now=60000;
    unsigned frame{},replays{},restores{},file_services{};
    std::uint32_t exit_mode=13;
    std::array<unsigned,4> updates{},intervals{},replay_counts{};
    std::array<bool,4> waits{};
    std::array<float,4> blends{};
    std::int64_t exit_counter{},exit_remainder{};
    static std::uint32_t call(void* user,std::uint32_t pc,const std::uint32_t*,std::size_t){
        auto& f=*static_cast<Fixture*>(user);auto& c=*f.context;
        if(pc==0x455c20u){
            f.waits[f.frame]=en::fast_load().wait_for_latency();
            return en::fast_load().cooperative_level(en::display_frames());
        }
        if(pc==0x43fa20u){
            if(f.frame==0u)c.start_mode.stage=64;
            if(f.frame==1u){c.start_mode.stage=65;c.mode_state.current=f.exit_mode;}
        }
        if(pc==0x43fab0u){
            en::display_note(&f,[&f]{++f.replays;},[&f]{++f.restores;});
        }
        if(pc==0x449050u){
            f.updates[f.frame]=c.frame_state.updates_95af48;
            f.intervals[f.frame]=en::fast_load().present_interval(2);
            f.blends[f.frame]=en::display_blend();
            f.replay_counts[f.frame]=f.replays;
            // Simulate work and the restored final-loading Present. Without
            // the production rebase this becomes 600 gameplay catch-up ticks.
            f.now+=f.frame==1u?600010u:10u;
        }
        if(pc==0x44fba0u)++f.file_services;
        return 0;
    }
    void run(unsigned rate,std::uint32_t next_mode){
        exit_mode=next_mode;auto& c=*context;
        en::set_display_rate(rate);en::fast_load().configure("2");
        c.mode_state.current=13;c.start_mode.stage=0;
        pf::NativeRuntimePlatform platform{};platform.user=this;
        platform.ticks=[](void* u)->std::uint64_t{return static_cast<Fixture*>(u)->now;};
        platform.frequency=[](void*)->std::uint64_t{return 60000u;};
        platform.sleep_ns=[](void*,std::uint64_t){check(false,"fixture must not sleep");};
        platform.main_loop=[](void* u){auto& f=*static_cast<Fixture*>(u);f.now+=500;return ++f.frame<4u;};
        pf::NativeRuntimeLoopServices services{};
        services.setup={this,[](void*,std::uint32_t,const std::uint32_t*,std::size_t)->std::uint32_t{return 0;}};
        services.cleanup={this,[](void*,std::uint32_t,std::uint32_t,std::uint32_t){}};
        services.frame={this,call};services.platform=&platform;
        services.user=this;
        services.continue_running=[](void* u,std::uint32_t completed){
            auto& f=*static_cast<Fixture*>(u);
            if(completed==2u){
                f.exit_counter=f.context->timing_state.previous_counter_8a8c90;
                f.exit_remainder=f.context->timing_state.accumulator_8a8cd0;
                check(f.exit_counter==static_cast<std::int64_t>(f.now-500u),"existing clock sampled after restored Present");
            }
            return true;
        };
        check(pf::run_native_frame_loop(c,services)==0u,"native loop exits normally");
        check(updates[0]==1u&&updates[1]==1u,"one update per loading frame at either display rate");
        check(intervals==std::array<unsigned,4>{{0,2,2,2}},"normal Present restored in stage-65/abort frame");
        check(waits==std::array<bool,4>{{false,false,true,true}},"normal pre-input waits restored on next frame");
        check(exit_remainder==0,"no loading accumulator debt on exit");
        check(file_services==4u,"original file-service call remains once per frame");
        check(en::display_rate()==rate,"configured display rate never changed");
        check(!en::fast_load().active()&&!en::fast_load().loading(),"loop cleanup unbinds policy");
        if(rate==120u){
            check(updates[2]==0u&&updates[3]==1u,"120 Hz resumes 0/1 ticks without loading catch-up");
            check(blends[0]==1.f&&blends[1]==1.f,"loading renders current tick without interpolation");
            check(replay_counts[0]==1u&&replay_counts[1]==1u&&replay_counts[2]==1u,
                  "last loading history cleared before first ordinary display-only frame");
        }else check(updates==std::array<unsigned,4>{{1,1,1,1}},"60 Hz retains one update per frame");
        en::set_display_rate(60);en::fast_load().configure("0");
    }
};
// Compare complete production-loop service order/timing against the option
// disabled in modes where gameplay/menu pacing must remain unchanged.
std::vector<std::int64_t> ordinary(unsigned rate,bool enabled,std::uint32_t mode,std::uint32_t stage){
    struct Trace {
        std::unique_ptr<pf::NativeRuntimeContext> context=std::make_unique<pf::NativeRuntimeContext>();
        std::vector<std::int64_t> values;
        std::uint64_t now=60000;
        unsigned frames{};
    } trace;
    trace.context->mode_state.current=mode;trace.context->start_mode.stage=stage;
    en::set_display_rate(rate);en::fast_load().configure(enabled?"2":"0");
    pf::NativeRuntimePlatform platform{};platform.user=&trace;
    platform.ticks=[](void* u)->std::uint64_t{return static_cast<Trace*>(u)->now;};
    platform.frequency=[](void*)->std::uint64_t{return 60000u;};
    platform.sleep_ns=[](void*,std::uint64_t){check(false,"ordinary comparison must not sleep");};
    platform.main_loop=[](void* u){auto& t=*static_cast<Trace*>(u);t.now+=500;return ++t.frames<4u;};
    pf::NativeRuntimeLoopServices services{};services.platform=&platform;
    services.setup={&trace,[](void*,std::uint32_t,const std::uint32_t*,std::size_t)->std::uint32_t{return 0;}};
    services.cleanup={&trace,[](void*,std::uint32_t,std::uint32_t,std::uint32_t){}};
    services.frame={&trace,[](void* u,std::uint32_t pc,const std::uint32_t*,std::size_t)->std::uint32_t{
        auto& t=*static_cast<Trace*>(u);t.values.push_back(pc);
        if(pc==0x455c20u)return en::fast_load().cooperative_level(en::display_frames());
        if(pc==0x449050u){
            const auto& timing=t.context->timing_state;
            t.values.push_back(t.context->frame_state.updates_95af48);
            t.values.push_back(timing.current_counter_8a8ca0);
            t.values.push_back(timing.previous_counter_8a8c90);
            t.values.push_back(timing.accumulator_8a8cd0);
            t.values.push_back(en::fast_load().present_interval(2));
            t.values.push_back(en::fast_load().wait_for_latency()?1:0);
            t.now+=10;
        }
        return 0;
    }};
    check(pf::run_native_frame_loop(*trace.context,services)==0u,"ordinary comparison exits");
    if(enabled&&mode==13u&&stage==0u)
        check(en::fast_load().enabled()&&!en::fast_load().loading()&&!en::fast_load().active()&&en::fast_load().present_interval(2)==2u,
              "exit while still loading releases binding before context destruction");
    en::set_display_rate(60);en::fast_load().configure("0");
    return trace.values;
}
}
int main(){
    policy();
    for(const auto rate:{60u,120u})for(const auto mode:{13u,16u,32u}){
        Fixture fixture;fixture.run(rate,mode);
    }
    for(const auto rate:{60u,120u})for(const auto mode:{13u,16u,29u,32u}){
        const auto stage=mode==13u?65u:0u;
        const auto disabled=ordinary(rate,false,mode,stage);
        check(ordinary(rate,true,mode,stage)==disabled,"stage 65/gameplay/menu service order and timing match disabled option");
    }
    ordinary(120,true,13,0);
    std::printf("fast_load: %u checks passed\n",checks);
}
