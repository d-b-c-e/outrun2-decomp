// Software-only producer@2 fixture. It drives the actual game input and transmission producers into the car record,
// then samples that record at the next input-filter seam. No window, wheel, DLL, game launch or physical output.
#include "ffb_producer2.hpp"
#include "platform/race_input.hpp"
#include "driving/pc_common_control.hpp"
#include "driving/pc_transmission.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace p2=outrun::desktop::ffb::producer2;
namespace pf=outrun::platform;
namespace dr=outrun::driving;
namespace {
int checks{};
void check(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
template<class T> void put(std::vector<std::uint8_t>& b,std::size_t off,T v){std::memcpy(b.data()+off,&v,sizeof v);}
template<class T> T get(const std::vector<std::uint8_t>& b,std::size_t off){T v;std::memcpy(&v,b.data()+off,sizeof v);return v;}
struct Car {
    std::vector<std::uint8_t> bytes=std::vector<std::uint8_t>(0x1000,0);
    dr::Bytes view(){return {bytes.data(),bytes.size()};}
    Car(){put<float>(bytes,0x1f8,72.f);put<std::uint32_t>(bytes,0x208,2u);}
    p2::Frame frame(std::uint64_t tick,std::uint32_t epoch=1,std::uint32_t generation=1)const{
        return {16u,epoch,generation,tick,true,bytes.data(),bytes.size(),1.f/60.f};
    }
};
void steer_from_real_producers(Car& c,pf::PcInputAnalog& analog,float& filter,std::int16_t axis){
    pf::PcInputDevice device{};
    const auto cfg=pf::pc_input_joystick_config(0);
    device.axes_94[cfg.steer_axis]=axis;
    pf::pc_input_analog_453860(analog,filter,device,cfg,4,16);
    dr::operation_input_49fad0(c.view(),{16,analog.current[0],0,0});
    check(get<std::int16_t>(c.bytes,0x202)==analog.current[0]*256,"OperationInput writes signed consumed steer at +202");
}
void test_game_producers_and_seam(){
    Car c;pf::PcInputAnalog analog{};float filter=0;
    steer_from_real_producers(c,analog,filter,30000);
    check(analog.current[0]>0&&analog.current[0]<=127,"positive input axis yields positive consumed steer");
    p2::Stream stream;
    auto row=stream.step(c.frame(10));
    check(row.state==p2::State::Primed&&row.shaped_force==0&&!row.request.gear_shift,"first car tick primes silently");
    row=stream.step(c.frame(11));
    check(row.state==p2::State::Computed&&row.request.has_steer&&row.request.steer>0&&row.shaped_force<0,
          "positive game-consumed steer produces negative centering force");
    check(std::fabs(row.request.speed_mps-20.f)<1e-5f,"HUD 72 km/h converts to 20 m/s");
    check(!row.request.has_steer_rate&&!row.request.has_front_lateral_force&&!row.request.has_lateral_g&&
          !row.request.has_longitudinal_g&&!row.request.has_slip&&!row.request.has_drift&&
          row.request.texture==0&&row.request.impact==0,"unmeasured force channels stay explicitly unavailable");
    // The desktop filter runs before the next game update (xbox/source/main.cpp:246). A newly computed input remains
    // outside the car record until OperationInput writes it; the candidate must therefore see the previous command.
    pf::PcInputDevice device{};auto cfg=pf::pc_input_joystick_config(0);device.axes_94[cfg.steer_axis]=-30000;
    pf::pc_input_analog_453860(analog,filter,device,cfg,4,16);
    check(analog.current[0]<0,"next game input has turned negative");
    row=stream.step(c.frame(12));
    check(row.request.steer>0&&row.shaped_force<0,"pre-update sample retains the previous completed car tick");
    dr::operation_input_49fad0(c.view(),{16,analog.current[0],0,0});
    row=stream.step(c.frame(13));
    check(row.request.steer<0&&row.shaped_force>0,"next completed car tick reverses the centering sign");

    std::vector<std::uint8_t> work(0x800),parameters(0x2650);
    put<std::uint32_t>(parameters,0x10a0,6u);
    dr::manual_transmission(c.view(),{work.data(),work.size()},{parameters.data(),parameters.size()},false,true,false);
    check(get<std::uint32_t>(c.bytes,0x208)==3u,"real manual transmission advances +208 from gear 2 to 3");
    row=stream.step(c.frame(14));
    check(row.request.gear_shift&&row.model_event,"gear transition emits one toolkit event");
    row=stream.step(c.frame(15));
    check(!row.request.gear_shift,"steady gear does not retrigger the event");
}
void test_unavailable_fields_and_guards(){
    Car a,b;pf::PcInputAnalog analog{};float filter=0;
    steer_from_real_producers(a,analog,filter,15000);
    b.bytes=a.bytes;
    put<float>(b.bytes,0x1d0,NAN);put<float>(b.bytes,0x1d4,INFINITY);
    put<float>(b.bytes,0x264,-1000.f);put<float>(b.bytes,0x268,1000.f);
    p2::Stream x,y;x.step(a.frame(1));y.step(b.frame(1));
    auto r=x.step(a.frame(2)),s=y.step(b.frame(2));
    check(r.state==p2::State::Computed&&s.state==p2::State::Computed&&r.shaped_force==s.shaped_force,
          "speed-delta and edge-distance legacy fields cannot affect producer@2");
    b.bytes=a.bytes;put<float>(b.bytes,0x1f8,NAN);
    check(y.step(b.frame(3)).reason==p2::Reason::InvalidSpeed,"invalid HUD speed refuses and neutralizes");
    check(y.step(a.frame(4)).state==p2::State::Primed,"after invalid speed, gear history and output re-prime");
    put<std::int16_t>(a.bytes,0x202,123);
    check(x.step(a.frame(3)).reason==p2::Reason::InvalidSteer,"non-producer-shaped steer is refused");
    put<std::int16_t>(a.bytes,0x202,0);
    auto f=a.frame(4);f.producer_fresh=false;
    check(x.step(f).reason==p2::Reason::Unproven,"unproven producer freshness is neutral");
    check(x.step(a.frame(5)).state==p2::State::Primed,"freshness loss resets history");
    // The real producer explicitly skips writing +202 when D50 > 120. The old value must never become new steering.
    put<std::int16_t>(a.bytes,0xd50,121);put<std::int16_t>(a.bytes,0x202,256);
    dr::operation_input_49fad0(a.view(),{16,-80,0,0});
    check(get<std::int16_t>(a.bytes,0x202)==256,"actual D50 gate leaves prior +202 word unchanged");
    check(x.step(a.frame(6)).reason==p2::Reason::StaleSteer,"stale +202 cannot enter the model");
    put<std::int16_t>(a.bytes,0xd50,0);put<float>(a.bytes,0x1f8,72.f);
    auto short_frame=a.frame(7);short_frame.car_bytes=0x200;
    check(x.step(short_frame).reason==p2::Reason::ShortCar,"short car record neutralizes");
    put<std::uint32_t>(a.bytes,0x208,8u);
    check(x.step(a.frame(8)).reason==p2::Reason::InvalidGear,"unvalidated gear index neutralizes");
}
void test_epoch_menu_respawn_and_trace(){
    Car c;p2::Stream s;
    check(s.step(c.frame(20)).state==p2::State::Primed,"race start is silent");
    auto f=c.frame(21);f.mode=18;
    check(s.step(f).state==p2::State::Neutral,"pause/menu clears output");
    put<std::uint32_t>(c.bytes,0x208,3u);
    auto r=s.step(c.frame(22));
    check(r.state==p2::State::Primed&&!r.request.gear_shift&&r.shaped_force==0,"return from pause cannot invent a shift");
    check(s.step(c.frame(23)).state==p2::State::Computed,"continuing car tick computes");
    r=s.step(c.frame(1,2,1));
    check(r.state==p2::State::Primed&&r.shaped_force==0,"new race epoch accepts a reset source counter and primes once");
    r=s.step(c.frame(1,2,2));
    check(r.state==p2::State::Primed&&r.shaped_force==0,"respawn generation accepts a reset source counter and primes once");
    check(s.step(c.frame(2,2,2)).state==p2::State::Computed,"respawn successor computes");
    check(s.step(c.frame(2,2,2)).reason==p2::Reason::DiscontinuousTick,"duplicate source tick refuses");
    check(s.step(c.frame(4,2,2)).state==p2::State::Primed,"discontinuity cannot synthesize a gear event");
    f=c.frame(5,2,2);f.dt_seconds=NAN;
    check(s.step(f).reason==p2::Reason::InvalidTime,"nonfinite dt neutralizes");
    const std::string header=p2::trace_header();
    check(header.find("outrun-producer@2")!=std::string::npos&&header.find("output=software-only")!=std::string::npos&&
          header.find("dt_seconds")!=std::string::npos&&r.dt_seconds==1.f/60.f,
          "trace identity, consumed dt and software-only boundary are explicit");
    const std::string csv=p2::trace_csv(r);
    check(csv.find("new-epoch-or-car")!=std::string::npos&&csv.find(",0,0,0,0,0,0,0,0,")!=std::string::npos,
          "trace records reset reason and unavailable channels");
}
void test_wall_rebound_observation(){
    // Observation only: +283 rebound timer, +4 bit 0x20000 wall contact and +281 side. No force channel reads it.
    Car c;p2::Stream s;
    auto contact=[&](bool on){put<std::uint32_t>(c.bytes,0x4,on?0x00020000u:0u);};
    auto timer=[&](std::uint8_t v){put<std::uint8_t>(c.bytes,0x283,v);};
    auto r=s.step(c.frame(1));
    check(r.state==p2::State::Primed&&r.wall_event==p2::WallEvent::None,"start primes with no wall event");
    timer(30);contact(true);put<std::uint8_t>(c.bytes,0x281,2);
    r=s.step(c.frame(2));
    check(r.state==p2::State::Computed&&r.wall_event==p2::WallEvent::Rebound&&r.rebound_283==30&&
          r.wall_contact&&r.contact_side_281==2,"a timer rise is one rebound, with its contact side");
    timer(29);contact(false);
    check(s.step(c.frame(3)).wall_event==p2::WallEvent::None,"the countdown is not a new rebound");
    timer(28);contact(true);
    check(s.step(c.frame(4)).wall_event==p2::WallEvent::Scrape,"contact without a timer rise is a scrape");
    timer(30);
    check(s.step(c.frame(5)).wall_event==p2::WallEvent::Rebound,"a re-arm during the countdown is a new rebound");
    check(s.step(c.frame(6)).wall_event==p2::WallEvent::Scrape,"an unchanged timer is not another rebound");
    timer(0);contact(false);
    check(s.step(c.frame(7)).wall_event==p2::WallEvent::None,"a reset to zero cannot fire");
    put<std::uint8_t>(c.bytes,0x282,24);
    check(s.step(c.frame(8)).wall_event==p2::WallEvent::None,"the +282 hit counter alone is ignored");
    timer(30);
    r=s.step(c.frame(1,1,2));
    check(r.state==p2::State::Primed&&r.wall_event==p2::WallEvent::None,"a respawn with a running timer primes silently");
    timer(29);
    check(s.step(c.frame(2,1,2)).wall_event==p2::WallEvent::None,"the respawn countdown stays quiet");
    auto f=c.frame(3,1,2);f.mode=18;
    check(s.step(f).state==p2::State::Neutral,"pause or menu is neutral");
    timer(30);
    r=s.step(c.frame(4,1,2));
    check(r.state==p2::State::Primed&&r.wall_event==p2::WallEvent::None,"a timer raised across a pause primes, no rebound");
    check(s.step(c.frame(6,1,2)).reason==p2::Reason::DiscontinuousTick,"a source-tick gap is neutral");
    r=s.step(c.frame(7,1,2));
    check(r.state==p2::State::Primed&&r.wall_event==p2::WallEvent::None,"after a gap the running timer primes silently");
    // The model sees the same inputs with or without the wall bytes: impact stays unavailable and unwired.
    Car plain,walled;
    for(Car* car:{&plain,&walled})put<std::int16_t>(car->bytes,0x202,std::int16_t(16384));
    p2::Stream a,b;a.step(plain.frame(1));b.step(walled.frame(1));
    put<std::uint8_t>(walled.bytes,0x283,30);put<std::uint32_t>(walled.bytes,0x4,0x00020000u);
    const auto pr=a.step(plain.frame(2)),wr=b.step(walled.frame(2));
    check(wr.wall_event==p2::WallEvent::Rebound&&pr.wall_event==p2::WallEvent::None,"only the walled car reports a rebound");
    check(wr.model_force==pr.model_force&&wr.shaped_force==pr.shaped_force&&wr.request.impact==0&&
          wr.request.texture==0,"a rebound changes no force");
}
}
int main(){
    try{test_game_producers_and_seam();test_unavailable_fields_and_guards();test_epoch_menu_respawn_and_trace();
        test_wall_rebound_observation();}
    catch(const std::exception& e){std::cerr<<"producer@2: FAIL: "<<e.what()<<"\n";return 1;}
    std::cout<<"producer@2 software preview: "<<checks<<" checks passed\n";
}
