// The car state telemetry reports (telemetry_forza.hpp).
#include "telemetry_forza.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace outrun::desktop {
namespace {
float finite(float v){return std::isfinite(v)?v:0.0f;}
template<class T> T at(const std::uint8_t* car,std::size_t off){T v;std::memcpy(&v,car+off,sizeof v);return v;}
}

TelemetryCar telemetry_car(const std::uint8_t* car,std::size_t bytes,const std::uint8_t* parameters,std::size_t parameter_bytes){
    TelemetryCar c;
    if(!car||bytes<0x26cu)return c;
    c.flags_4=at<std::uint32_t>(car,0x4);c.state_8=at<std::uint32_t>(car,0x8);c.prior_speed_178=at<float>(car,0x178);
    c.speed_1c4=at<float>(car,0x1c4);c.speed_delta_1d0=at<float>(car,0x1d0);c.prev_gear_1d8=at<std::uint32_t>(car,0x1d8);
    c.hud_kmh_1f8=at<float>(car,0x1f8);c.gear_208=at<std::uint32_t>(car,0x208);
    for(unsigned i=0;i<4;++i)c.surface_24c[i]=at<std::uint32_t>(car,0x24c+i*4u);
    c.edge_distance_264=at<float>(car,0x264);c.edge_distance_268=at<float>(car,0x268);
    c.engine_rpm_48=at<std::int32_t>(car,0x48);c.hud_rpm_20c=at<std::int32_t>(car,0x20c);
    if(parameters&&parameter_bytes>=0x1648u){c.idle_15f8=at<float>(parameters,0x15f8);c.redline_1644=at<float>(parameters,0x1644);}
    return c;
}

bool telemetry_game_rpm(const TelemetryCar& c,float& current,float& idle,float& redline){
    constexpr float PerRadian=9.54929638f;   // the PC's 4118C9EB (pc_wheel_dynamics.cpp): rad/s to RPM
    idle=c.idle_15f8*PerRadian;redline=c.redline_1644*PerRadian;
    if(!std::isfinite(idle)||!std::isfinite(redline)||!(idle>0.0f)||!(redline>idle)||redline>20000.0f)return false;
    current=std::clamp(float(c.hud_rpm_20c),0.0f,redline);
    return true;
}

float telemetry_synthetic_rpm(const TelemetryCar& c){
    static constexpr float Ratios[7]{0.0f,3.5f,2.1f,1.4f,1.0f,0.8f,0.65f};
    const std::uint32_t g=c.gear_208;
    const float ratio=g>0u&&g<7u?Ratios[g]:1.0f;
    const float norm=std::clamp(finite(c.speed_1c4)/2.0f,0.0f,1.0f);
    return std::clamp(TelemetryIdleRpm+norm*ratio*(TelemetryMaxRpm-TelemetryIdleRpm),TelemetryIdleRpm,TelemetryMaxRpm);
}

bool telemetry_in_gameplay(std::uint32_t mode){return mode==13u||mode==16u||mode==18u||mode==19u||mode==20u;}
}
