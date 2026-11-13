#pragma once
// Force feedback from the car's own steering load (wheelkit_runtime.cpp sends it through wk_ffb_set_force every tick in
// the race). Pure; tested offline (windows/tests/test_outrun_force.cpp).
//
// The load: the front tyres' side force after the friction circle over their grip, -1..1, 1 at the grip limit. Body work
// 82E7F0 holds four wheel records of 0xF4 bytes from +0x258 (front left, front right, rear left, rear right); SideForce
// (5019C0, src/driving/pc_wheel_dynamics.cpp side_force) writes +0xAC, the friction circle (5026F0) limits it against the
// grip +0xC0 and leaves the scale in +0xE4 (1 inside the grip, less when the tyre slides).
//
// Its sign, from the source (the drive of 2026-11-12 logged no probe line, see the decisions): side_force gives +0xAC
// the sign of the wheel's slip, its heading +0x32 minus its velocity's direction +0xEC (times load / (1 + cornering x
// load), both positive). A front wheel's heading is -k x the steering command +0x202 (pc_steering.cpp
// steering_operation: response = -1/b x command x p18A4, k > 0 for positive car parameters), and 49FAD0 writes +0x202 as
// the consumed steering 7D6810[0] << 8, which is the wheel's steer with its sign (453860: positive is right). A tyre's
// aligning force turns the wheel to reduce its slip, so the rim is pushed toward +command for a positive load: the force
// is +load, in Wheelkit's convention (positive pushes the rim to the right). In a grip corner the wheel points more into
// the turn than the car travels, so load and steering have opposite signs and the force pulls toward the center; in a
// counter-steered slide the front still pulls toward the slide, as a real car's does. The probe below checks this on
// every drive: in grip corners load x steer must be negative; if it is positive over 300 such samples, the force stops.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
namespace outrun::desktop {
// The game's force level (wk_ffb_set_level, Wheelkit's games/outrun-2006/force-level.json): the wheel's force at Steering
// strength 50 is the load times this (the runtime multiplies by it, then by strength / 50). The grip limit (load 1)
// gives 0.30, art of rally's 95th percentile (0.29 of the wheel above 12 km/h, Wheelkit docs/recording-and-playback.md
// section 11). Measured in an injected race of 2026-11-13: the load's p50 above 12 km/h was 0.32, so 0.096 of the wheel
// against art of rally's median 0.09.
inline constexpr float ForceFactor=0.30f;
// Below 5 km/h the slip angle has no meaning (the velocity is tiny); the force fades in to full at 20 km/h.
inline float force_fade(float kmh){return std::clamp((kmh-5.f)/15.f,0.f,1.f);}

// grip: neither front wheel sliding (both +0xE4 at 1). The rear is not asked: at full throttle it is past its limit
// most of the time (the injected race of 2026-11-12 had no sample with all four at 1).
struct FrontLoad { float load{};bool valid{};bool grip{}; };
inline FrontLoad front_load(const std::uint8_t* body,std::size_t size){
    FrontLoad out;
    if(!body||size<0x258u+4u*0xf4u)return out;
    auto f=[&](std::size_t wheel,std::size_t at){float v;std::memcpy(&v,body+0x258u+wheel*0xf4u+at,4);return v;};
    const float grip=f(0,0xc0)+f(1,0xc0),side=f(0,0xac)+f(1,0xac);
    if(!(grip>0.f)||!std::isfinite(grip)||!std::isfinite(side))return out;
    out.load=std::clamp(side/grip,-1.f,1.f);out.valid=true;
    out.grip=f(0,0xe4)>=0.999f&&f(1,0xe4)>=0.999f;
    return out;
}
// The force to send (wk_ffb_set_force), -1..1, before the game's level (ForceFactor) and Steering strength.
inline float steering_force(float load,float kmh){
    if(!std::isfinite(load)||!std::isfinite(kmh))return 0.f;
    return std::clamp(load*force_fade(kmh),-1.f,1.f);
}

// What one drive says about the load: its size above 12 km/h (as art of rally's distribution is measured) and its sign
// against the steering in grip corners above 30 km/h with the wheel turned past 5%.
struct ForceProbe {
    std::array<unsigned,100> size{};unsigned moving{};float peak{};   // |load| above 12 km/h, in hundredths
    unsigned corners{},opposite{};                                     // grip corners; those with load and steer opposite
    double force_right{},force_left{};unsigned right{},left{};         // the force asked for with the wheel turned, above 30 km/h
    void add(float load,float steer,float kmh,bool grip){
        if(!std::isfinite(load)||!std::isfinite(steer)||!std::isfinite(kmh))return;
        if(kmh>12.f){++size[std::min<std::size_t>(99,std::size_t(std::fabs(load)*100.f))];++moving;peak=std::max(peak,std::fabs(load));}
        if(kmh>30.f&&steer>0.05f){force_right+=steering_force(load,kmh)*ForceFactor;++right;}
        if(kmh>30.f&&steer< -0.05f){force_left+=steering_force(load,kmh)*ForceFactor;++left;}
        if(grip&&kmh>30.f&&std::fabs(steer)>0.05f&&load!=0.f){++corners;if((load<0.f)!=(steer<0.f))++opposite;}
    }
    float percentile(double p)const{
        if(!moving)return 0.f;unsigned seen=0;
        for(std::size_t i=0;i<size.size();++i){seen+=size[i];if(seen>=p*moving)return float(i)/100.f;}
        return 1.f;
    }
    // 1 the force's sign holds (load and steering opposite in grip corners), -1 it is reversed, 0 not enough corners yet.
    int sign()const{
        if(corners<300u)return 0;
        const double share=double(opposite)/corners;
        return share>=0.8?1:share<=0.2?-1:0;
    }
    std::string line()const{
        char text[480];
        std::snprintf(text,sizeof text,"ffb probe: front side force over grip above 12 km/h: %u samples, p50 %.2f, p95 %.2f, peak %.2f "
            "(force = load x %.2f: p50 %.3f, p95 %.3f of the wheel at strength 50); grip corners: %u, load against the steering in %.0f%% "
            "(the sign holds above 80%%); mean force asked with the wheel right %+.3f (%u), left %+.3f (%u) (to the center: right negative, "
            "left positive)",moving,double(percentile(.5)),double(percentile(.95)),double(peak),double(ForceFactor),
            double(percentile(.5)*ForceFactor),double(percentile(.95)*ForceFactor),corners,corners?100.0*opposite/corners:0.0,
            right?force_right/right:0.0,right,left?force_left/left:0.0,left);
        return text;
    }
};
}
