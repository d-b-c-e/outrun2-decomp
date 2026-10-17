// Forza telemetry and the OutRun 2006 mod's shared-memory block (telemetry_forza.hpp).
#include "telemetry_forza.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace outrun::desktop {
namespace {
bool digits(const std::string& v){return !v.empty()&&v.size()<=5&&std::all_of(v.begin(),v.end(),[](char c){return c>='0'&&c<='9';});}
std::string lower(std::string v){for(auto& c:v)c=char(std::tolower(static_cast<unsigned char>(c)));return v;}
float finite(float v){return std::isfinite(v)?v:0.0f;}
template<class T> T at(const std::uint8_t* car,std::size_t off){T v;std::memcpy(&v,car+off,sizeof v);return v;}
}

TelemetrySettings telemetry_settings(const Ini& settings,const char* env_off,const char* env_port){
    TelemetrySettings t;
    t.enabled=settings.flag("Telemetry","Enabled",settings.flag("Telemetry","Enable",true));   // Wheelkit's key, the mod's key
    const auto port=settings.text("Telemetry","Port");
    if(!port.empty()){
        if(digits(port)&&std::stoul(port)>=1u&&std::stoul(port)<=65535u)t.port=std::uint16_t(std::stoul(port));
        else t.notes+="[Telemetry] Port = "+port.substr(0,16)+" refused (1..65535); 8000 applies. ";
    }
    const auto layout=lower(settings.text("Telemetry","Layout"));
    if(layout=="fm7"||layout=="fm7dash"||layout=="311")t.layout=dbce::forza::FORZA_FM7_DASH_311;
    else if(!layout.empty()&&layout!="horizon"&&layout!="horizon324"&&layout!="324")
        t.notes+="[Telemetry] Layout = "+layout.substr(0,16)+" refused (Horizon or FM7); Horizon applies. ";
    const auto name=settings.text("Telemetry","SharedMemName","OutRun2006Telemetry");
    t.shared_name=lower(name)=="none"||lower(name)=="off"?std::string():name;
    const auto rpm=lower(settings.text("Telemetry","EngineRpm"));
    if(rpm=="game"||rpm=="hud")t.game_rpm=true;
    else if(!rpm.empty()&&rpm!="synthetic")t.notes+="[Telemetry] EngineRpm = "+rpm.substr(0,16)+" refused (Synthetic or Game); Synthetic applies. ";
    if(env_off&&std::strcmp(env_off,"0")==0){t.enabled=false;t.notes+="off: OR2_TELEMETRY=0. ";}
    if(env_port){
        const std::string v=env_port;
        if(digits(v)&&std::stoul(v)>=1u&&std::stoul(v)<=65535u)t.port=std::uint16_t(std::stoul(v));
        else t.notes+="OR2_TELEMETRY_PORT="+v.substr(0,16)+" refused (1..65535). ";
    }
    return t;
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

bool telemetry_in_gameplay(std::uint32_t mode){return mode==13u||mode==16u||mode==18u||mode==19u||mode==20u;}

int telemetry_packet(const TelemetryCar& c,bool race_on,std::int32_t consumed_steer,std::uint8_t accel,std::uint8_t brake,std::uint32_t ms,
                     dbce::forza::Layout layout,std::uint8_t* out,int size,bool game_rpm){
    if(!std::isfinite(c.hud_kmh_1f8)||c.hud_kmh_1f8<0.0f)return 0;   // the HUD's km/h base (4BCF10 draws it x1 or x0.6215)
    dbce::forza::Sled s{};dbce::forza::Dash d{};
    s.isRaceOn=race_on?1:0;s.timestampMs=ms;s.engineMaxRpm=TelemetryMaxRpm;s.engineIdleRpm=TelemetryIdleRpm;
    const std::uint32_t g=c.gear_208;
    float current{},idle{},redline{};
    if(game_rpm&&telemetry_game_rpm(c,current,idle,redline)){
        s.engineMaxRpm=redline;s.engineIdleRpm=idle;s.currentEngineRpm=current;
    }else{
        // The mod's synthesis: a gear ratio times the normalized speed (+0x1C4, about 0..2) across idle..max.
        static constexpr float Ratios[7]{0.0f,3.5f,2.1f,1.4f,1.0f,0.8f,0.65f};
        const float ratio=g>0u&&g<7u?Ratios[g]:1.0f;
        const float norm=std::clamp(finite(c.speed_1c4)/2.0f,0.0f,1.0f);
        s.currentEngineRpm=std::clamp(TelemetryIdleRpm+norm*ratio*(TelemetryMaxRpm-TelemetryIdleRpm),TelemetryIdleRpm,TelemetryMaxRpm);
    }
    // Car +0x264/+0x268 are signed distances to road edges, not lateral acceleration. Leave accX unavailable (zero).
    bool rough=false;for(const auto f:c.surface_24c)rough=rough||f>1u;     // inherited Tweaks road-result heuristic; taxonomy unqualified
    if(rough)for(auto& r:s.surfaceRumble)r=1.0f;
    d.speed=c.hud_kmh_1f8/3.6f;
    d.gear=std::uint8_t(std::clamp<std::uint32_t>(g==0u?1u:g,1u,10u));
    d.steer=std::int8_t(std::clamp(consumed_steer,-127,127));
    d.accel=accel;d.brake=brake;
    return dbce::forza::build(layout,s,d,out,size,std::uint8_t('R'));
}

int telemetry_parked_packet(std::uint32_t ms,dbce::forza::Layout layout,std::uint8_t* out,int size){
    dbce::forza::Sled s{};dbce::forza::Dash d{};
    s.timestampMs=ms;s.engineMaxRpm=TelemetryMaxRpm;s.engineIdleRpm=TelemetryIdleRpm;s.currentEngineRpm=TelemetryIdleRpm;
    d.gear=1;
    return dbce::forza::build(layout,s,d,out,size,std::uint8_t('R'));
}

void telemetry_shared(const TelemetryCar& c,std::uint32_t mode,bool gameplay,std::uint32_t packet_id,OutRun2006TelemetryData& o){
    o.version=1;o.packetId=packet_id;
    // Version 1 copies the Tweaks fork's raw offsets byte for byte. Its legacy field names are semantically wrong:
    // +0x1D0 is speed delta, +0x264/+0x268 are road-edge distances, and +0x178 is prior speed.
    o.speed=c.speed_1c4;o.steeringAngle=c.speed_delta_1d0;o.lateralG1=c.edge_distance_264;
    o.lateralG2=c.edge_distance_268;o.impactForce=c.prior_speed_178;
    o.gear=c.gear_208;o.prevGear=c.prev_gear_1d8;o.stateFlags=c.state_8;o.carFlags=c.flags_4;
    for(unsigned i=0;i<4;++i)o.surfaceType[i]=c.surface_24c[i];
    o.vibrationLeft=0.0f;o.vibrationRight=0.0f;
    o.gameMode=mode;o.isInGameplay=gameplay?1u:0u;
}
}
