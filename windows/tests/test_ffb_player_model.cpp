// Pure player-model selection and producer request/gate fixture. The sink records software rows;
// no game, WheelFfb DLL, delivery device or physical force is involved.
#include "ffb_player_model.hpp"
#include "desktop_settings.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace player=outrun::desktop::ffb::player;
namespace shadow=outrun::desktop::ffb::shadow;
namespace producer=outrun::desktop::ffb::producer2;
namespace {
int checks{};
void check(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
outrun::desktop::Ini ini(const std::string& body){outrun::desktop::Ini i;i.load_text(body,"fixture.ini");return i;}
template<class T> void put(std::array<std::uint8_t,0xd52>& car,std::size_t off,T value){
    std::memcpy(car.data()+off,&value,sizeof value);
}
struct Run {
    std::array<std::uint8_t,0xd52> car{};
    shadow::Gate gate{};
    std::vector<producer::Row> rows;
    player::SelectedRequests requests;
    explicit Run(const player::Selection& choice):requests(choice,[&](const shadow::Gate&,const producer::Row& row){rows.push_back(row);}){
        put(car,0x202,std::int16_t(16384));put(car,0x1f8,72.f);put(car,0x208,std::uint32_t(2));
        gate.mode=16;gate.generation=1;gate.car_active=true;
    }
    producer::Row step(std::uint64_t attempt){
        gate.frame_counter=std::uint32_t(attempt);gate.attempt=attempt;
        requests.before_frame(gate);
        shadow::Sample s{};s.gate=gate;s.car=car.data();s.car_bytes=car.size();s.producer_fresh=true;
        requests.observe(s);
        return rows.empty()?producer::Row{}:rows.back();
    }
    producer::Row recover(std::uint64_t attempt,std::uint32_t token,bool pending){
        gate.frame_counter=std::uint32_t(attempt);gate.attempt=attempt;
        requests.before_frame(gate); // The real callback changes continuity inside this car control, not before it.
        gate.continuity=token;gate.recovery_pending=pending;
        shadow::Sample s{};s.gate=gate;s.car=car.data();s.car_bytes=car.size();s.producer_fresh=true;
        requests.observe(s);
        return rows.back();
    }
};
void test_selection(){
    auto s=player::select(ini("[FFB]\nFFBGlobalStrength = 0.10\nFFBGearShift = 1.90\n"));
    check(s.model==player::Model::Legacy&&s.refusal.empty(),"absent model is explicit legacy default");
    s=player::select(ini("[FFB]\nModel = outrun-legacy@1\n[FFBProducer2]\nSteeringStrengthPercent = bogus\n"));
    check(s.model==player::Model::Legacy,"legacy selection ignores separate producer values");
    s=player::select(ini("[FFB]\nModel = outrun-producer@2\nFFBGlobalStrength = 0.10\nFFBGearShift = 1.90\n"));
    check(s.model==player::Model::Producer2&&s.strengths.steering_percent==50&&s.strengths.shift_percent==50,
          "producer uses independent 50 references, never migrated legacy gains");
    s=player::select(ini("[FFB]\nModel = outrun-producer@2\n[FFBProducer2]\nSteeringStrengthPercent = 25\nShiftStrengthPercent = 75\n"));
    check(s.model==player::Model::Producer2&&s.strengths.steering_percent==25&&s.strengths.shift_percent==75,
          "separate steering and shift values parse independently");
    for(const auto& id:{"", "unknown", "OUTRUN-PRODUCER@2"}){
        s=player::select(ini(std::string("[FFB]\nModel = ")+id+"\n"));
        check(s.model==player::Model::Refused&&!s.refusal.empty(),"empty/unknown model refuses without legacy fallback");
    }
    for(const auto& bad:{"-1","101","50.0","50junk","nan","99999999999999999999",""}){
        s=player::select(ini(std::string("[FFB]\nModel = outrun-producer@2\n[FFBProducer2]\nSteeringStrengthPercent = ")+bad+"\n"));
        check(s.model==player::Model::Refused&&s.refusal.find("SteeringStrengthPercent")!=std::string::npos,
              "invalid steering percent refuses the whole selection");
        s=player::select(ini(std::string("[FFB]\nModel = outrun-producer@2\n[FFBProducer2]\nShiftStrengthPercent = ")+bad+"\n"));
        check(s.model==player::Model::Refused&&s.refusal.find("ShiftStrengthPercent")!=std::string::npos,
              "invalid shift percent refuses the whole selection");
    }
    s=player::select(ini("[FFB]\nModel = outrun-producer@2\n[FFBProducer2]\nSteeringStrengthPercent = 0\nShiftStrengthPercent = 100\n"));
    check(s.model==player::Model::Producer2&&s.strengths.steering_percent==0&&s.strengths.shift_percent==100,
          "inclusive 0..100 bounds accepted");
}
void test_fake_sink(){
    const auto choose=[](int steering,int shift){
        return player::select(ini("[FFB]\nModel = outrun-producer@2\n[FFBProducer2]\nSteeringStrengthPercent = "+
            std::to_string(steering)+"\nShiftStrengthPercent = "+std::to_string(shift)+"\n"));
    };
    Run baseline(choose(50,50)),no_steer(choose(0,50)),double_steer(choose(100,50));
    check(baseline.requests.active()&&no_steer.requests.active()&&double_steer.requests.active(),
          "selected producer has software request sinks");
    check(baseline.step(1).state==producer::State::Primed&&baseline.rows.back().shaped_force==0.f,
          "first direct car sample primes at zero");
    no_steer.step(1);double_steer.step(1);
    const auto b=baseline.step(2),z=no_steer.step(2),d=double_steer.step(2);
    check(b.state==producer::State::Computed&&b.shaped_force<0.f&&z.shaped_force==0.f&&
          std::fabs(d.shaped_force-2.f*b.shaped_force)<1e-5f,
          "steering 0/50/100 scales only the pre-composition centering term");

    Run no_shift(choose(50,0)),double_shift(choose(50,100)),shift_only(choose(0,100));
    for(Run* run:{&no_shift,&double_shift,&shift_only}){run->step(1);run->step(2);put(run->car,0x208,std::uint32_t(3));}
    const auto q=no_shift.step(3),v=double_shift.step(3),e=shift_only.step(3);
    check(q.request.gear_shift&&v.request.gear_shift&&e.request.gear_shift&&
          std::fabs(q.shaped_force-b.shaped_force)<1e-5f&&v.shaped_force>q.shaped_force&&e.shaped_force>0.f,
          "shift 0/100 changes only its event; zero steering preserves the independent cue");
    check(!double_shift.step(4).request.gear_shift,"unchanged gear does not retrigger the event");

    const auto count=baseline.rows.size();
    auto pending=baseline.recover(3,1,true);
    check(baseline.rows.size()==count+1&&pending.state==producer::State::Neutral&&pending.shaped_force==0.f,
          "recovery-pending reaches fake sink as zero");
    auto snap=baseline.recover(4,2,false);
    check(snap.state==producer::State::Neutral&&snap.shaped_force==0.f,"recovery snap reaches fake sink as zero");
    check(baseline.step(5).state==producer::State::Primed&&baseline.rows.back().shaped_force==0.f,
          "same-generation post-snap sample primes silently");
    check(baseline.step(6).state==producer::State::Computed,"post-prime direct sample computes");
    put(baseline.car,0x1f8,NAN);
    check(baseline.step(7).state==producer::State::Neutral&&baseline.rows.back().shaped_force==0.f,
          "invalid selected request neutralizes the fake sink");
    put(baseline.car,0x1f8,72.f);
    check(baseline.step(8).state==producer::State::Primed,"valid successor re-primes after invalid input");
    baseline.gate.mode=18;baseline.step(9);
    check(baseline.rows.back().state==producer::State::Neutral&&baseline.rows.back().shaped_force==0.f,
          "pause invalidation reaches fake sink as zero");
    const auto legacy=player::select(ini(""));
    player::SelectedRequests legacy_requests(legacy,[&](const shadow::Gate&,const producer::Row&){
        throw std::runtime_error("legacy must not enter producer sink");
    });
    check(!legacy_requests.active(),"default legacy never selects producer request path");
    auto forged=choose(50,50);forged.strengths.shift_percent=101;
    player::SelectedRequests refused(forged,[&](const shadow::Gate&,const producer::Row&){
        throw std::runtime_error("forged strengths must not enter producer sink");
    });
    check(!refused.active(),"direct callers cannot bypass independent gain bounds");
}
}
int main(){
    try{test_selection();test_fake_sink();}
    catch(const std::exception& e){std::cerr<<"ffb player model: FAIL: "<<e.what()<<"\n";return 1;}
    std::cout<<"ffb player model: "<<checks<<" checks passed, software fake sink only\n";
}
