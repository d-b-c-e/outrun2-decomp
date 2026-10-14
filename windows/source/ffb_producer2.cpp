#include "ffb_producer2.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace outrun::desktop::ffb::producer2 {
namespace {
template<class T> T at(const std::uint8_t* bytes,std::size_t offset){T value;std::memcpy(&value,bytes+offset,sizeof value);return value;}
constexpr std::size_t MinimumCarBytes=0xd52; // +D50 is OperationInput's stale-steer gate
}

dbce::force::ModelSettings model_settings(const Strengths& strengths){
    dbce::force::ModelSettings s;
    s.spring_strength=0.30f*(float(strengths.steering_percent)/50.f);
    s.spring_full_speed_mps=30.0f;
    s.shift_strength=0.05f*(float(strengths.shift_percent)/50.f);
    // Damping, lateral load, texture, impact and weight transfer stay unavailable/off.
    return s;
}
dbce::force::ModelSettings preview_model_settings(){return model_settings(Strengths{});}
dbce::force::ShaperSettings preview_shaper_settings(){
    dbce::force::ShaperSettings s;
    s.strength=50;
    s.deadzone=0.0f;s.output_deadband=0.0f;
    s.attack_smoothing=0.0f;s.decay_smoothing=0.0f;
    s.fade_start_kmh=0.0f;s.fade_full_kmh=0.0f; // model already fades spring by speed
    s.ramp_seconds=0.0f;
    return s;
}
const char* reason_name(Reason reason){
    switch(reason){
    case Reason::None:return "none";
    case Reason::OutsideGame:return "outside-game";
    case Reason::Unproven:return "unproven-freshness";
    case Reason::ShortCar:return "short-car";
    case Reason::StaleSteer:return "stale-steer-d50";
    case Reason::InvalidSteer:return "invalid-steer-202";
    case Reason::InvalidSpeed:return "invalid-hud-speed-1f8";
    case Reason::InvalidGear:return "invalid-gear-208";
    case Reason::InvalidTime:return "invalid-dt";
    case Reason::DiscontinuousTick:return "discontinuous-source-tick";
    case Reason::NewEpochOrCar:return "new-epoch-or-car";
    case Reason::Rejected:return "toolkit-rejected";
    }
    return "unknown";
}

std::string trace_header(){
    return std::string("# ")+TraceId+",model="+ModelId+",output=software-only,seam=previous-completed-car-tick\n"
        "state,reason,epoch,car_generation,source_tick,dt_seconds,steer_202_signed_shift8,hud_kmh_1f8,gear_208,"
        "has_steer,steer_normalized,speed_mps,gear_shift,has_steer_rate,has_front_lateral_force,has_lateral_g,"
        "has_longitudinal_g,has_slip,has_drift,texture,impact,model_force,shaped_force,model_event\n";
}
std::string trace_csv(const Row& row){
    char b[512];
    const auto& i=row.request;
    std::snprintf(b,sizeof b,"%u,%s,%u,%u,%llu,%.9g,%d,%.9g,%u,%u,%.9g,%.9g,%u,%u,%u,%u,%u,%u,%u,%.9g,%.9g,%.9g,%.9g,%u\n",
        unsigned(row.state),reason_name(row.reason),row.epoch,row.car_generation,
        static_cast<unsigned long long>(row.source_tick),double(row.dt_seconds),int(row.steer_202),double(row.hud_kmh_1f8),row.gear_208,
        unsigned(i.has_steer),double(i.steer),double(i.speed_mps),unsigned(i.gear_shift),unsigned(i.has_steer_rate),
        unsigned(i.has_front_lateral_force),unsigned(i.has_lateral_g),unsigned(i.has_longitudinal_g),
        unsigned(i.has_slip),unsigned(i.has_drift),double(i.texture),double(i.impact),
        double(row.model_force),double(row.shaped_force),unsigned(row.model_event));
    return b;
}

Stream::Stream(const Strengths& strengths):model_(model_settings(strengths)),shaper_(preview_shaper_settings()){}
void Stream::reset(){model_.reset();shaper_.reset();epoch_=generation_=gear_=0;tick_=0;armed_=false;}
Row Stream::step(const Frame& frame){
    Row row;
    row.epoch=frame.race_epoch;row.car_generation=frame.car_generation;row.source_tick=frame.source_tick;
    row.dt_seconds=frame.dt_seconds;
    const auto neutral=[&](Reason why){
        reset();row.state=State::Neutral;row.reason=why;row.request=dbce::force::Inputs{};
        row.model_force=row.shaped_force=0.f;row.model_event=false;return row;
    };
    if(frame.mode!=16u)return neutral(Reason::OutsideGame);
    if(!frame.producer_fresh||!frame.car||!frame.race_epoch||!frame.car_generation||!frame.source_tick)
        return neutral(Reason::Unproven);
    if(frame.car_bytes<MinimumCarBytes)return neutral(Reason::ShortCar);
    if(!(std::isfinite(frame.dt_seconds)&&frame.dt_seconds>0.f&&frame.dt_seconds<=0.1f))return neutral(Reason::InvalidTime);
    // OperationInput's D50>120 path does not write +202: never re-label the retained word as this tick's steering.
    if(at<std::int16_t>(frame.car,0xd50)>120)return neutral(Reason::StaleSteer);
    row.steer_202=at<std::int16_t>(frame.car,0x202);
    row.hud_kmh_1f8=at<float>(frame.car,0x1f8);
    row.gear_208=at<std::uint32_t>(frame.car,0x208);
    if(row.steer_202<-32512||row.steer_202>32512||row.steer_202%256!=0)return neutral(Reason::InvalidSteer);
    if(!std::isfinite(row.hud_kmh_1f8)||row.hud_kmh_1f8<0.f||row.hud_kmh_1f8>600.f)return neutral(Reason::InvalidSpeed);
    if(row.gear_208>7u)return neutral(Reason::InvalidGear);
    if(!armed_||epoch_!=frame.race_epoch||generation_!=frame.car_generation){
        reset();epoch_=frame.race_epoch;generation_=frame.car_generation;gear_=row.gear_208;tick_=frame.source_tick;armed_=true;
        row.state=State::Primed;row.reason=Reason::NewEpochOrCar;return row; // no output on start/respawn
    }
    if(frame.source_tick<=tick_||frame.source_tick-tick_!=1u)return neutral(Reason::DiscontinuousTick);
    row.request.has_steer=true;
    row.request.steer=float(row.steer_202)/32512.f;
    row.request.speed_mps=row.hud_kmh_1f8/3.6f;
    row.request.gear_shift=gear_!=0u&&row.gear_208!=0u&&gear_!=row.gear_208;
    row.model_force=model_.compute(row.request,frame.dt_seconds);
    row.model_event=model_.last_was_event;
    row.shaped_force=shaper_.shape(row.model_force,row.hud_kmh_1f8,frame.dt_seconds,row.model_event);
    if(model_.last_input_rejected||shaper_.last_input_rejected)return neutral(Reason::Rejected);
    gear_=row.gear_208;tick_=frame.source_tick;
    row.state=State::Computed;row.reason=Reason::None;
    return row;
}
}
