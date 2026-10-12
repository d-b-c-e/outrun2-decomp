// OutRun force feedback, F2 (ffb_live.hpp): the model computed in the game for a trace. No device code.
#include "ffb_live.hpp"
#include "native_window.hpp"
#include "platform/native_runtime.hpp"
#include "platform/runtime_input.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
namespace outrun::desktop {
namespace {
template<class T> T at(const std::uint8_t* car,std::size_t off){T v;std::memcpy(&v,car+off,sizeof v);return v;}
struct Row:ffb::Sink {
    bool sent{};std::int32_t constant_value{};
    void constant(std::int32_t v)override{sent=true;constant_value=v;}
    void periodic(int,float,float)override{}   // the CF fallback (no device): never called
};
struct Live {
    bool on{};
    std::unique_ptr<ffb::LegacyForce> model;
    std::FILE* trace{};
    std::string trace_path;
    xbox_runtime::InputFilter inner{};
    std::uint32_t last_mode{},race_ticks{},constants{},resets{};
    std::int32_t min_constant{},max_constant{};
};
Live f;
void frame(const platform::NativeRuntimeContext* rt){
    const std::uint32_t mode=rt?rt->mode_state.current:0u;
    // The mod computes in the car's control (GamePlCar_Ctrl), that is GAME; a new race starts from a clean state.
    if(mode!=16u){f.last_mode=mode;return;}
    if(f.last_mode!=16u){f.model->reset();++f.resets;}
    f.last_mode=16u;
    const auto& b=rt->event_function36.car_select.car_799d18;
    const auto car=ffb::car_signals(b.data(),b.size());
    // The surface: the game's LUT over the four tyres (the mod's Surface: max roughness), stage unknown (-1).
    float roughness=0.0f;std::uint32_t water=0;
    if(b.size()>=0x260u){
        const int load_coli=int(at<std::uint32_t>(b.data(),0x5c));
        for(unsigned i=0;i<4;++i)roughness=std::max(roughness,float(ffb::surface_roughness(at<std::uint32_t>(b.data(),0x24c+i*4u),load_coli,-1,&water)));
    }
    Row row;
    f.model->calculate(car,roughness,water,row);
    ++f.race_ticks;
    if(row.sent){
        f.min_constant=f.constants?std::min(f.min_constant,row.constant_value):row.constant_value;
        f.max_constant=f.constants?std::max(f.max_constant,row.constant_value):row.constant_value;
        ++f.constants;
    }
    if(f.trace)std::fprintf(f.trace,"%u,%lu,%.6g,%.6g,%.6g,%.6g,%.6g,%u,%u,%u,%.6g,%u,%d,%d\n",f.race_ticks,GetTickCount(),
        double(car.speed_1c4),double(car.steer_1d0),double(car.steer_rate_1d4),double(car.lateral_264),double(car.lateral_268),
        car.gear_208,car.state_8,car.pedal_34,double(roughness),water,row.sent?1:0,row.constant_value);
}
void filter(platform::NativeInputState& out,const platform::NativeRuntimeContext* rt){
    if(f.inner)f.inner(out,rt);
    if(f.on)frame(rt);
}
}

void ffb_configure(const Ini& settings,bool latched){
    f=Live{};
    std::string notes;
    const auto s=ffb_settings(settings,notes);
    if(!notes.empty())std::printf("ffb: %s\n",notes.c_str());
    const char* trace=std::getenv("OR2_FFB_TRACE");
    if(latched||!trace||!*trace){
        std::printf("ffb: no force output in this build; the model is computed only with OR2_FFB_TRACE%s\n",latched?" (never in a latched test process)":"");
        std::fflush(stdout);return;
    }
    f.model=std::make_unique<ffb::LegacyForce>(s);
    if(!f.model->invalid().empty()){std::printf("ffb: settings refused (%s); nothing computed\n",f.model->invalid().c_str());std::fflush(stdout);f.model.reset();return;}
    f.model->set_periodics(false,0,1);   // no device: the CF fallback, as the mod without hardware periodics
    f.trace_path=trace;
    f.trace=std::fopen(trace,"w");
    if(!f.trace){std::printf("ffb: trace %s not writable; nothing computed\n",trace);std::fflush(stdout);f.model.reset();return;}
    std::fprintf(f.trace,"# dbce.outrun-decomp.ffb-trace,1,model=outrun-legacy@1,output=none (no device),periodics=fallback,stage=unknown\n");
    std::fprintf(f.trace,"# settings: FFBGlobalStrength=%g FFBSpringStrength=%g FFBDamperStrength=%g FFBSteeringWeight=%g FFBWallImpact=%g "
        "FFBGearShift=%g FFBRoadTexture=%g FFBTireSlip=%g FFBGripLoss=%g FFBWeightTransfer=%g FFBLateralDeadzone=%g FFBEngineIdle=%g "
        "FFBInvertForce=%d\n",double(s.global_strength),double(s.spring),double(s.damper),double(s.steering_weight),double(s.wall_impact),
        double(s.gear_shift),double(s.road_texture),double(s.tire_slip),double(s.grip_loss),double(s.weight_transfer),
        double(s.lateral_deadzone),double(s.engine_idle),s.invert?1:0);
    std::fprintf(f.trace,"tick,ms,speed_1c4,steer_1d0,steer_rate_1d4,lateral_264,lateral_268,gear_208,state_8,pedal_34,roughness,water,sent,constant\n");
    f.on=true;
    std::printf("ffb: outrun-legacy@1 computed for a trace (%s), output none (no device); periodics fallback; stage unknown\n",trace);
    std::fflush(stdout);
}

void ffb_wrap_input_filter(){
    if(!f.on)return;
    auto& filt=xbox_runtime::input_filter();
    if(filt==filter)return;
    f.inner=filt;filt=filter;
}

void ffb_shutdown(){
    if(f.model)std::printf("ffb: %u race ticks computed, %u constant updates in %d..%d, %u race starts, %u refused, %u non-finite; trace %s\n",
        f.race_ticks,f.constants,f.min_constant,f.max_constant,f.resets,f.model->refused_ticks(),f.model->nonfinite_ticks(),f.trace_path.c_str());
    if(f.trace){std::fclose(f.trace);f.trace=nullptr;}
    std::fflush(stdout);
    f.on=false;
}
}
