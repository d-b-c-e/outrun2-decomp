#include "enhancements/gamepad_vibration.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <vector>
namespace en=outrun::enhancements;
static unsigned checks;
static void check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"gamepad vibration: %s\n",why);std::exit(1);}}
static bool near(double a,double b){return std::abs(a-b)<.00001;}
static en::PadVibrationSettings settings(){return en::pad_vibration_settings("1","10","1","0.25","0.5","0",true,false);}
static en::PadVibrationCar car(){en::PadVibrationCar c;c.speed=1;c.gear=c.prior_gear=1;c.road_left=-1;c.road_right=1;return c;}
static void policies(){
    const auto defaults=en::pad_vibration_settings("0","7","0","0.25","0.25","0",true,false);
    check(defaults.mode==0&&defaults.impulse==0&&en::silent(en::map_pad_motors({1,1,1,1},defaults)),"default body and trigger output are off");
    for(const auto mode:{"0","1","2","3"})for(const auto impulse:{"0","1","2","3"}){
        const auto s=en::pad_vibration_settings(mode,"7",impulse,"0.25","0.25","0",true,false);
        check(s.allowed&&s.mode==unsigned(mode[0]-'0')&&s.impulse==unsigned(impulse[0]-'0'),"exact supported modes parse");
    }
    for(const auto invalid:{"-1","4","1junk","","4294967296"})check(!en::pad_vibration_settings(invalid,"7","0","0.25","0.25","0",true,false).allowed,"invalid body mode refuses output");
    for(const auto invalid:{"-1","11","7junk",""})check(!en::pad_vibration_settings("1",invalid,"0","0.25","0.25","0",true,false).allowed,"invalid strength refuses output");
    for(const auto invalid:{"nan","inf","-0.1","1.1","0.5junk",""})check(!en::pad_vibration_settings("1","7","1",invalid,"0.25","0",true,false).allowed,"invalid gain refuses output");
    check(!en::pad_vibration_settings("1","7","1","0.25","0.25","1",true,false).allowed,"arbitrary controller index cannot bypass selected input owner");
    check(!en::pad_vibration_settings("1","7","1","0.25","0.25","0",false,false).allowed,"test/record/playback excluded");
    check(!en::pad_vibration_settings("1","7","1","0.25","0.25","0",true,true).allowed,"global force mute excludes output");
    auto s=settings();auto m=en::map_pad_motors({.2,.6,1,1},s);
    check(near(m.left,.2)&&near(m.right,.6)&&near(m.left_trigger,.05)&&near(m.right_trigger,.3),"normal body and independent trigger gains");
    s.mode=2;m=en::map_pad_motors({.2,.6,0,0},s);
    check(near(m.left,.6)&&near(m.right,.2)&&near(m.left_trigger,.15)&&near(m.right_trigger,.1),"body swap precedes trigger mapping");
    s.impulse=2;m=en::map_pad_motors({.2,.6,0,0},s);
    check(near(m.left_trigger,.05)&&near(m.right_trigger,.3),"trigger swap is independent of body swap");
    s.mode=3;s.impulse=3;m=en::map_pad_motors({.2,.6,0,0},s);
    check(near(m.left,.6)&&near(m.right,.6)&&near(m.left_trigger,.15)&&near(m.right_trigger,.3),"merge uses maximum before independent gains");
    s.impulse=0;m=en::map_pad_motors({.2,.6,1,1},s);check(m.left_trigger==0&&m.right_trigger==0,"trigger Off writes zero even with stale trigger values");
    m=en::sanitize({-1,2,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()});
    check(m.left==0&&m.right==1&&m.left_trigger==0&&m.right_trigger==0,"output boundary clamps and refuses nonfinite levels");
    en::PadVibrationGate gate;gate.ordinary=gate.focused=gate.connected=gate.car_ready=true;gate.mode=16;
    check(gate.eligible(),"owned active gameplay admits output");
    for(unsigned reason=0;reason<8;++reason){auto g=gate;switch(reason){case 0:g.ordinary=false;break;case 1:g.muted=true;break;case 2:g.focused=false;break;
        case 3:g.panel=true;break;case 4:g.connected=false;break;case 5:g.car_ready=false;break;case 6:g.mode=18;break;case 7:g.pause_depth=1;break;}
        check(!g.eligible(),"each ownership/focus/panel/disconnect/pause/exclusion gate stops output");}
    for(const unsigned mode:{0u,13u,18u,19u,20u,32u}){gate.mode=mode;check(!gate.eligible(),"menus/loading/results never vibrate");}
}
static void envelopes(){
    en::PadVibrationModel model;auto c=car();
    check(en::silent(model.tick(c,10)),"smooth steady native car is quiet");
    c.roughness=.5f;c.road_right=0;auto m=model.tick(c,10);
    check(near(m.right,.051)&&near(m.left,.153),"native surface/road-edge contribution retains Xbox envelope");
    model.reset();c=car();c.roughness=1;c.speed=20;m=model.tick(c,10);
    check(near(m.left,.75)&&near(m.right,.5),"source envelope caps both body motors");
    model.reset();c=car();c.gear=2;m=model.tick(c,5);check(near(m.left,.1)&&m.right==0,"native gear change uses strength scale");
    check(en::silent(model.tick(c,0)),"zero strength immediately clears a held envelope");
    model.reset();c=car();c.speed=.2f;c.delta=.01f;m=model.tick(c,10);check(near(m.left,.3),"native low-speed acceleration pulse");
    c.delta=0;for(unsigned n=0;n<10;++n)check(near(model.tick(c,10).left,.3),"native pulse hold advances once per tick");
    check(en::silent(model.tick(c,10)),"native pulse expires without a new event");
    model.reset();c=car();c.flags=0x20000;m=model.tick(c,10);check(near(m.left,.75),"native wall-impact flag produces bounded left pulse");
    model.reset();c=car();c.flags=0x10000;c.other_known=true;c.other_speed=2;m=model.tick(c,10);check(near(m.left,.75),"known native collision peer speed contributes");
    model.reset();c.other_known=false;check(en::silent(model.tick(c,10)),"unknown collision peer does not synthesize an impact");
    model.reset();c=car();c.state=0x40;c.prior_speed=.4f;m=model.tick(c,10);check(near(m.left,.4),"native transient impact state contribution");
    c.speed=0;check(en::silent(model.tick(c,10)),"native stopped car silences held pulse");
    model.reset();c=car();c.speed=.005f;c.gear=2;for(unsigned n=0;n<600;++n)check(!en::silent(model.tick(c,10)),"low-speed source idle window remains bounded");
    check(en::silent(model.tick(c,10)),"source idle timeout silences output");
    for(unsigned n=0;n<10;++n)check(en::silent(model.tick(c,10)),"idle timeout cannot periodically restart the motors");
    c.speed=1;check(!en::silent(model.tick(c,10)),"moving again clears the native idle timeout");
    c=car();c.speed=std::numeric_limits<float>::quiet_NaN();check(en::silent(model.tick(c,10)),"nonfinite native state clears model");
    c=car();check(en::silent(model.tick(c,11)),"out-of-contract strength refuses output");
    c.gear=2;model.tick(c,10);model.reset();c.gear=1;check(en::silent(model.tick(c,10)),"stop/retarget resets prior effect state");
}
static void delivery(){
    en::PadVibrationSession session;std::vector<int> calls;bool refuse_zero=false;
    check(!session.submit({1,1,1,1})&&calls.empty(),"no selected reader target means no output");
    check(session.select([&](const en::PadMotors& m){calls.push_back(en::silent(m)?-1:1);return true;}),"first target selected");
    check(session.stop()&&calls.empty(),"unused/default target receives no zero or positive command");
    check(session.submit({.2,.4,.1,.2})&&calls==std::vector<int>{1},"owned target receives mapped command");
    check(session.select([&](const en::PadMotors& m){calls.push_back(en::silent(m)?-2:2);return !refuse_zero||!en::silent(m);})&&calls==std::vector<int>({1,-1}),"retarget zeros previous owner before changing writer");
    check(session.submit({.2,0,0,0})&&calls.back()==2,"new target receives output only after handover");
    refuse_zero=true;check(!session.select([&](const en::PadMotors&){calls.push_back(3);return true;})&&calls.back()==-2&&session.failed(),"failed zero refuses handover");
    check(!session.submit({1,1,1,1})&&calls.back()==-2,"latched failure cannot send another positive command");
    refuse_zero=false;check(session.select({})&&calls.back()==-2,"disconnect/shutdown retries zero on the actual previous owner");
    check(!session.submit({1,1,1,1}),"cleared target cannot receive output");
    unsigned positive=0,zero=0;
    session.select([&](const en::PadMotors& m){if(en::silent(m)){++zero;return true;}++positive;throw std::runtime_error("fake driver failure");});
    check(!session.submit({1,1,1,1})&&positive==1&&zero==1&&session.failed(),"write exception immediately attempts zero and latches");
    check(!session.submit({1,1,1,1})&&positive==1,"failed target cannot silently resume");
    check(session.stop()&&zero==1,"already-zero failure cleanup is idempotent");
}
int main(){policies();envelopes();delivery();std::printf("Gamepad vibration: %u checks PASS (fake output only; no controller/reader/device)\n",checks);}
