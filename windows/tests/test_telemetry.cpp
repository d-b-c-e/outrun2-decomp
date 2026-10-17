// The desktop telemetry's pure parts (source/telemetry_forza): settings, the car fields, the Forza packets and the mod's
// shared-memory block, against the documented byte offsets (toolkit knowledge/TELEMETRY-FORZA.md). No socket, no game.
#include "telemetry_forza.hpp"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace outrun::desktop;
namespace fz=dbce::forza;
int checks=0;
void check(bool v,const std::string& what){++checks;if(!v)throw std::runtime_error(what);}
template<class T> T rd(const std::uint8_t* p,std::size_t off){T v;std::memcpy(&v,p+off,sizeof v);return v;}
template<class T> void wr(std::vector<std::uint8_t>& b,std::size_t off,T v){std::memcpy(b.data()+off,&v,sizeof v);}

TelemetryCar sample(){
    TelemetryCar c;c.hud_kmh_1f8=180.0f;c.speed_1c4=0.4f;c.gear_208=4;
    c.speed_delta_1d0=-0.5f;c.prior_speed_178=0.39f;c.edge_distance_264=0.25f;c.edge_distance_268=0.5f;
    return c;
}

void test_packets(){
    std::uint8_t b[400];
    const int h=telemetry_packet(sample(),true,-64,200,30,1234,fz::FORZA_HORIZON_324,b,sizeof b);
    check(h==324,"Horizon: 324 bytes");
    check(rd<std::int32_t>(b,0)==1&&rd<std::uint32_t>(b,4)==1234u,"IsRaceOn 1, timestamp");
    check(rd<float>(b,8)==TelemetryMaxRpm&&rd<float>(b,12)==TelemetryIdleRpm,"engine constants at 8 / 12");
    check(std::fabs(rd<float>(b,16)-(900.0f+0.2f*1.0f*7600.0f))<0.01f,"RPM: the mod's synthesis (gear 4 ratio 1.0, speed 0.4/2)");
    check(rd<float>(b,20)==0.0f,"accX unavailable: road-edge distances are not lateral acceleration");
    check(std::fabs(rd<float>(b,256)-50.0f)<1e-4f,"Speed@256 = HUD 180 km/h / 3.6 (STD-009)");
    check(b[315]==200&&b[316]==30&&b[319]==4,"Accel@315, Brake@316, Gear@319");
    check(std::int8_t(b[320])==-64,"Steer@320 is consumed steering, not speed delta");
    check(b[323]=='R',"the mod sentinel in the trailing byte");
    for(int i=232;i<244;++i)check(b[i]==0,"the Horizon insert stays zero");
    const int f=telemetry_packet(sample(),true,64,200,30,1234,fz::FORZA_FM7_DASH_311,b,sizeof b);
    check(f==311&&std::fabs(rd<float>(b,244)-50.0f)<1e-4f&&b[307]==4&&std::int8_t(b[308])==64,
          "FM7 Dash: 311 bytes, Speed@244, Gear@307, consumed Steer@308");
}

void test_edges(){
    std::uint8_t b[400];
    auto c=sample();
    c.hud_kmh_1f8=NAN;check(telemetry_packet(c,true,0,0,0,0,fz::FORZA_HORIZON_324,b,sizeof b)==0,"NaN HUD speed: no packet, not a zero");
    c.hud_kmh_1f8=-1.0f;check(telemetry_packet(c,true,0,0,0,0,fz::FORZA_HORIZON_324,b,sizeof b)==0,"negative HUD speed: no packet");
    c.hud_kmh_1f8=INFINITY;check(telemetry_packet(c,true,0,0,0,0,fz::FORZA_HORIZON_324,b,sizeof b)==0,"infinite HUD speed: no packet");
    c=sample();c.gear_208=0;telemetry_packet(c,true,0,0,0,0,fz::FORZA_HORIZON_324,b,sizeof b);
    check(b[319]==1,"gear 0 is sent as 1 (Forza shows 0 as reverse)");
    c.gear_208=12;telemetry_packet(c,true,0,0,0,0,fz::FORZA_HORIZON_324,b,sizeof b);check(b[319]==10,"gear clamped to 10");
    c=sample();c.speed_1c4=5.0f;c.gear_208=1;telemetry_packet(c,true,0,0,0,0,fz::FORZA_HORIZON_324,b,sizeof b);
    check(rd<float>(b,16)==TelemetryMaxRpm,"RPM clamped to the maximum");
    c=sample();c.speed_delta_1d0=NAN;c.edge_distance_264=NAN;c.edge_distance_268=3.0e38f;
    telemetry_packet(c,true,0,0,0,0,fz::FORZA_HORIZON_324,b,sizeof b);
    check(b[320]==0&&rd<float>(b,20)==0.0f,"neutral consumed steering and unavailable accX reject speed delta and edge distances");
    telemetry_packet(c,true,127,0,0,0,fz::FORZA_HORIZON_324,b,sizeof b);
    check(std::int8_t(b[320])==127&&rd<float>(b,20)==0.0f,"positive consumed steering ignores nonfinite legacy fields");
    telemetry_packet(c,true,-127,0,0,0,fz::FORZA_HORIZON_324,b,sizeof b);
    check(std::int8_t(b[320])==-127,"negative consumed steering ignores nonfinite legacy fields");
    telemetry_packet(c,true,500,0,0,0,fz::FORZA_HORIZON_324,b,sizeof b);
    check(std::int8_t(b[320])==127,"consumed steering clamps to the Forza signed byte range");
    c=sample();c.surface_24c[2]=4;telemetry_packet(c,true,0,0,0,0,fz::FORZA_HORIZON_324,b,sizeof b);
    bool all=true;for(int i=0;i<4;++i)all=all&&rd<float>(b,148+i*4)==1.0f;check(all,"rough surface: SurfaceRumble 1 on all wheels@148");
    c=sample();c.surface_24c[0]=1;telemetry_packet(c,true,0,0,0,0,fz::FORZA_HORIZON_324,b,sizeof b);
    check(rd<float>(b,148)==0.0f,"road-result 1: inherited heuristic has no rumble");
    check(telemetry_packet(sample(),true,0,0,0,0,fz::FORZA_HORIZON_324,b,100)==0,"a short buffer: nothing");
    const int p=telemetry_parked_packet(77,fz::FORZA_HORIZON_324,b,sizeof b);
    check(p==324&&rd<std::int32_t>(b,0)==0&&rd<float>(b,8)==TelemetryMaxRpm&&b[319]==1&&rd<float>(b,256)==0.0f,
          "parked: IsRaceOn 0, the same engine constants, gear 1, speed 0");
}

void test_car_and_modes(){
    std::vector<std::uint8_t> car(0x10f0,0);
    wr<std::uint32_t>(car,0x4,0x11u);wr<std::uint32_t>(car,0x8,0x22u);wr<float>(car,0x178,3.0f);wr<float>(car,0x1c4,0.7f);
    wr<float>(car,0x1d0,0.1f);wr<std::uint32_t>(car,0x1d8,2u);wr<float>(car,0x1f8,123.0f);wr<std::uint32_t>(car,0x208,3u);
    for(unsigned i=0;i<4;++i)wr<std::uint32_t>(car,0x24c+i*4,i+1);
    wr<float>(car,0x264,0.5f);wr<float>(car,0x268,-0.25f);
    const auto c=telemetry_car(car.data(),car.size());
    check(c.flags_4==0x11u&&c.state_8==0x22u&&c.prior_speed_178==3.0f&&c.speed_1c4==0.7f&&c.speed_delta_1d0==0.1f&&c.prev_gear_1d8==2u,"car fields 4..1D8 keep their actual producer meanings");
    check(c.hud_kmh_1f8==123.0f&&c.gear_208==3u&&c.surface_24c[3]==4u&&c.edge_distance_264==0.5f&&c.edge_distance_268==-0.25f,"car fields 1F8..268 keep the edge distances");
    check(telemetry_car(car.data(),0x200).gear_208==0u,"a short car buffer reads nothing");
    for(const std::uint32_t m:{13u,16u,18u,19u,20u})check(telemetry_in_gameplay(m),"gameplay mode "+std::to_string(m));
    for(const std::uint32_t m:{0u,3u,32u,17u})check(!telemetry_in_gameplay(m),"not gameplay "+std::to_string(m));
    OutRun2006TelemetryData o{};telemetry_shared(c,16,true,9,o);
    const auto* b=reinterpret_cast<const std::uint8_t*>(&o);
    check(rd<std::uint32_t>(b,0)==1u&&rd<std::uint32_t>(b,4)==9u&&rd<float>(b,8)==0.7f&&rd<std::uint32_t>(b,28)==3u&&rd<std::uint32_t>(b,32)==2u,
          "shared block: version, packetId, speed@8, gear@28, prevGear@32");
    check(rd<std::uint32_t>(b,36)==0x22u&&rd<std::uint32_t>(b,40)==0x11u&&rd<std::uint32_t>(b,44)==1u&&rd<std::uint32_t>(b,68)==16u&&b[72]==1,
          "shared block: stateFlags@36, carFlags@40, surface@44, gameMode@68, isInGameplay@72");
    check(rd<float>(b,12)==0.1f&&rd<float>(b,16)==0.5f&&rd<float>(b,20)==-0.25f&&rd<float>(b,24)==3.0f,
          "v1 raw-offset ABI stays exact although its steering/lateral/impact labels are misleading");
}

// [Telemetry] EngineRpm = Game: car +0x20C (the HUD tachometer's RPM) with the parameters' idle +0x15F8 and redline
// +0x1644 (rad/s x 60 / 2 pi); the synthesis otherwise, byte for byte.
void test_game_rpm(){
    constexpr float PerRadian=9.54929638f;
    std::vector<std::uint8_t> car(0x10f0,0),params(0x1700,0);
    wr<float>(car,0x1f8,150.0f);wr<float>(car,0x1c4,0.6f);wr<std::uint32_t>(car,0x208,3u);
    wr<std::int32_t>(car,0x48,7200);wr<std::int32_t>(car,0x20c,6800);
    wr<float>(params,0x15f8,100.0f);wr<float>(params,0x1644,900.0f);wr<float>(params,0x1690,1300.0f);
    auto c=telemetry_car(car.data(),car.size(),params.data(),params.size());
    check(c.engine_rpm_48==7200&&c.hud_rpm_20c==6800&&c.idle_15f8==100.0f&&c.redline_1644==900.0f,"engine RPM +0x48 / +0x20C and limits +0x15F8 / +0x1644 (not the clamp +0x1690)");
    const auto bare=telemetry_car(car.data(),car.size(),params.data(),0x1644);
    check(bare.hud_rpm_20c==6800&&bare.idle_15f8==0.0f&&bare.redline_1644==0.0f,"short parameters: no engine limits");
    check(telemetry_car(car.data(),car.size()).redline_1644==0.0f,"no parameters: no engine limits");
    std::uint8_t a[400],b[400];
    telemetry_packet(c,true,0,0,0,9,fz::FORZA_HORIZON_324,a,sizeof a);
    telemetry_packet(c,true,0,0,0,9,fz::FORZA_HORIZON_324,b,sizeof b,false);
    check(std::memcmp(a,b,324)==0&&rd<float>(a,8)==TelemetryMaxRpm,"Synthetic (the default) is the mod's packet, byte for byte");
    telemetry_packet(c,true,0,0,0,9,fz::FORZA_HORIZON_324,b,sizeof b,true);
    check(rd<float>(b,8)==900.0f*PerRadian&&rd<float>(b,12)==100.0f*PerRadian&&rd<float>(b,16)==6800.0f,
          "Game: redline@8, idle@12 from the parameters, current@16 the HUD tachometer's +0x20C");
    check(std::memcmp(a+20,b+20,324-20)==0,"Game changes only the three engine words");
    telemetry_packet(c,true,0,0,0,9,fz::FORZA_FM7_DASH_311,b,sizeof b,true);
    check(rd<float>(b,16)==6800.0f,"Game RPM in the FM7 Dash layout too");
    float cur{},idle{},red{};
    c.hud_rpm_20c=9000;check(telemetry_game_rpm(c,cur,idle,red)&&cur==red,"above the redline: clamped to it");
    c.hud_rpm_20c=-50;check(telemetry_game_rpm(c,cur,idle,red)&&cur==0.0f,"negative: zero");
    for(const auto& bad:{std::make_pair(0.0f,900.0f),std::make_pair(100.0f,0.0f),std::make_pair(900.0f,100.0f),
                         std::make_pair(100.0f,2500.0f),std::make_pair(NAN,900.0f),std::make_pair(100.0f,INFINITY)}){
        auto x=c;x.idle_15f8=bad.first;x.redline_1644=bad.second;
        check(!telemetry_game_rpm(x,cur,idle,red),"invalid limits refused");
        telemetry_packet(x,true,0,0,0,9,fz::FORZA_HORIZON_324,a,sizeof a,true);
        telemetry_packet(x,true,0,0,0,9,fz::FORZA_HORIZON_324,b,sizeof b,false);
        check(std::memcmp(a,b,324)==0,"invalid limits: Game falls back to the synthesis");
    }
}

void test_settings(){
    char dir[MAX_PATH];GetTempPathA(MAX_PATH,dir);
    const std::string path=std::string(dir)+"or2_desktop_telemetry_test.ini";
    auto load=[&](const char* body){{std::ofstream f(path,std::ios::binary);f<<body;}Ini i;i.load(path);return i;};
    auto t=telemetry_settings(load(""),nullptr,nullptr);
    check(t.enabled&&t.port==8000&&t.layout==fz::FORZA_HORIZON_324&&t.shared_name=="OutRun2006Telemetry"&&t.notes.empty(),"defaults: on, 8000, Horizon (STD-016)");
    check(!telemetry_settings(load("[Telemetry]\r\nEnabled = false\r\n"),nullptr,nullptr).enabled,"Enabled = false (Wheelkit's key)");
    check(!telemetry_settings(load("[Telemetry]\r\nEnable = false\r\n"),nullptr,nullptr).enabled,"Enable = false (the mod's key)");
    t=telemetry_settings(load("[Telemetry]\r\nPort = 18000\r\nLayout = FM7\r\nSharedMemName = none\r\n"),nullptr,nullptr);
    check(t.port==18000&&t.layout==fz::FORZA_FM7_DASH_311&&t.shared_name.empty(),"Port, FM7 layout, no shared memory");
    for(const char* bad:{"0","70000","80a","-1"}){
        t=telemetry_settings(load((std::string("[Telemetry]\r\nPort = ")+bad+"\r\n").c_str()),nullptr,nullptr);
        check(t.port==8000&&!t.notes.empty(),std::string("Port ")+bad+" refused, 8000 applies");
    }
    t=telemetry_settings(load("[Telemetry]\r\nLayout = Sled\r\n"),nullptr,nullptr);
    check(t.layout==fz::FORZA_HORIZON_324&&!t.notes.empty(),"an unknown layout: Horizon, noted");
    check(!telemetry_settings(load(""),"0",nullptr).enabled,"OR2_TELEMETRY=0 turns it off");
    check(telemetry_settings(load(""),"1",nullptr).enabled,"OR2_TELEMETRY=1 leaves it on");
    check(telemetry_settings(load(""),nullptr,"18001").port==18001,"OR2_TELEMETRY_PORT overrides the port");
    check(!telemetry_settings(load(""),nullptr,nullptr).game_rpm,"EngineRpm: Synthetic by default");
    check(telemetry_settings(load("[Telemetry]\r\nEngineRpm = Game\r\n"),nullptr,nullptr).game_rpm,"EngineRpm = Game");
    check(telemetry_settings(load("[Telemetry]\r\nEngineRpm = hud\r\n"),nullptr,nullptr).game_rpm,"EngineRpm = HUD, any case");
    t=telemetry_settings(load("[Telemetry]\r\nEngineRpm = Synthetic\r\n"),nullptr,nullptr);
    check(!t.game_rpm&&t.notes.empty(),"EngineRpm = Synthetic");
    t=telemetry_settings(load("[Telemetry]\r\nEngineRpm = real\r\n"),nullptr,nullptr);
    check(!t.game_rpm&&!t.notes.empty(),"an unknown EngineRpm: Synthetic, noted");
    std::remove(path.c_str());
}

int main(){
    try{test_packets();test_edges();test_car_and_modes();test_game_rpm();test_settings();}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    std::cout<<"desktop telemetry: "<<checks<<" checks passed\n";
    return 0;
}
