// The force from the steering load (outrun_force.hpp): reading the body work, the mapping to the wheel's force, the
// probe and its sign check. No game.
//
// The drive of 2026-11-12 (0.5.1-alpha) logged no "ffb probe" line: about 70 s of race, too little steering past the
// old probe's 20% above 58 km/h. The values below take what that run's runtime.log did give: the player car's tyre loads
// 273.547 / 271.760 (front) and 373.240 / 371.453 (rear) at mu 0.700, so a front grip of about 191.5 + 190.2 N-units.
#include "outrun_force.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
int checks=0;
void check(bool v,const char* what){++checks;if(!v){std::fprintf(stderr,"outrun force: FAIL: %s\n",what);std::exit(1);}}
bool near(float a,float b,float e=1e-4f){return std::fabs(a-b)<=e;}
struct Body {
    std::array<std::uint8_t,0x634> bytes{};
    void put(std::size_t wheel,std::size_t at,float v){std::memcpy(bytes.data()+0x258u+wheel*0xf4u+at,&v,4);}
    Body(float fl_side,float fr_side,float scale=1.f){
        const float grip[4]{273.547f*0.7f,271.760f*0.7f,373.240f*0.7f,371.453f*0.7f};
        for(std::size_t w=0;w<4;++w){put(w,0xc0,grip[w]);put(w,0xe4,1.f);}
        put(0,0xac,fl_side);put(1,0xac,fr_side);put(0,0xe4,scale);
    }
};
}
int main(){
    using namespace outrun::desktop;
    const float grip=273.547f*0.7f+271.760f*0.7f;
    // Reading: the front pair's side force over its grip; grip only while no wheel slides.
    {   Body b(-60.f,-55.f);const auto l=front_load(b.bytes.data(),b.bytes.size());
        check(l.valid&&near(l.load,-115.f/grip)&&l.grip,"a third of the front grip, to the left");}
    {   Body b(400.f,400.f,0.8f);const auto l=front_load(b.bytes.data(),b.bytes.size());
        check(l.valid&&near(l.load,1.f)&&!l.grip,"past the grip: 1, and a sliding front wheel is not grip");}
    {   Body b(0.f,0.f);check(!front_load(b.bytes.data(),0x400).valid,"a short body is not read");
        b.put(0,0xc0,0.f);b.put(1,0xc0,0.f);check(!front_load(b.bytes.data(),b.bytes.size()).valid,"no grip (airborne): no load");}
    // The mapping: +load x 0.30, faded in from 5 to 20 km/h, clamped.
    check(near(steering_force(-115.f/grip,120.f),-115.f/grip*0.30f),"a typical corner: about -0.09");
    check(near(steering_force(1.f,200.f),0.30f)&&near(steering_force(-1.f,200.f),-0.30f),"the grip limit: 0.30 each way");
    check(steering_force(1.f,4.f)==0.f&&near(steering_force(1.f,12.5f),0.15f)&&near(steering_force(1.f,20.f),0.30f),"the fade at low speed");
    check(steering_force(std::nanf(""),100.f)==0.f&&steering_force(1.f,std::nanf(""))==0.f,"nothing finite, no force");
    // The sign: in a right-hand grip corner (steer > 0) the load is negative, so the force pulls the rim left, to the center.
    check(steering_force(-0.3f,100.f)<0.f,"steering right, the force pulls left");
    // The probe: 300 grip corners with the load against the steering confirm the sign; with it reversed, it says so.
    {   ForceProbe p;
        for(int i=0;i<299;++i)p.add(-0.3f,0.4f,100.f,true);
        check(p.sign()==0,"not enough corners yet");
        p.add(-0.3f,0.4f,100.f,true);check(p.sign()==1,"load against the steering: the sign holds");
        for(int i=0;i<200;++i)p.add(0.9f,-0.2f,150.f,false);   // a counter-steered slide is not counted for the sign
        check(p.sign()==1&&p.corners==300u,"slides do not decide the sign");
        check(p.right==300u&&p.left==200u&&p.force_right/p.right<0.0&&p.force_left/p.left>0.0,"the force asked, by the wheel's side");
        check(near(p.percentile(.5),0.3f,0.011f)&&near(p.percentile(.95),0.9f,0.011f)&&near(p.peak,0.9f),"the size above 12 km/h");}
    {   ForceProbe p;for(int i=0;i<300;++i)p.add(0.3f,0.4f,100.f,true);
        check(p.sign()==-1,"load with the steering in grip corners: reversed, the force stops");}
    {   ForceProbe p;for(int i=0;i<1000;++i)p.add(0.3f,0.4f,100.f,true);
        for(int i=0;i<1000;++i)p.add(-0.3f,0.4f,100.f,true);
        check(p.sign()==0,"half and half decides nothing");}
    {   ForceProbe p;p.add(0.5f,0.5f,10.f,true);p.add(0.5f,0.01f,100.f,true);p.add(0.5f,0.5f,25.f,true);
        check(p.moving==2&&p.corners==0,"below 12 km/h not sized; corners need 30 km/h and the wheel past 5%");}
    {   ForceProbe p;p.add(-0.3f,0.4f,100.f,true);const auto line=p.line();
        check(line.find("ffb probe:")==0&&line.find("p50 0.30")!=std::string::npos&&line.find("100%")!=std::string::npos,"the log line");}
    std::printf("outrun force: %d checks passed\n",checks);
    return 0;
}
