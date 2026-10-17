#pragma once
// Portable adaptation of Tweaks' Xbox CalcVibrationValues (0x114C60).
// Inputs are read-only native car state; delivery owns only the selected WGI pad.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <charconv>
#include <string_view>
#include <utility>
namespace outrun::enhancements {
struct PadMotors {double left{},right{},left_trigger{},right_trigger{};};
inline bool silent(const PadMotors& m){return m.left==0&&m.right==0&&m.left_trigger==0&&m.right_trigger==0;}
inline double motor_level(double v){return std::isfinite(v)?std::clamp(v,0.0,1.0):0.0;}
inline PadMotors sanitize(PadMotors m){return {motor_level(m.left),motor_level(m.right),motor_level(m.left_trigger),motor_level(m.right_trigger)};}
struct PadVibrationSettings {unsigned mode{},strength{7},impulse{};double left_gain{.25},right_gain{.25};bool allowed{};};
inline PadVibrationSettings pad_vibration_settings(std::string_view mode,std::string_view strength,std::string_view impulse,
    std::string_view left,std::string_view right,std::string_view controller,bool ordinary,bool muted){
    PadVibrationSettings s;bool valid=true;
    const auto integer=[&](std::string_view text,unsigned max,unsigned& value){unsigned n=0;
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),n);
        if(text.empty()||parsed.ec!=std::errc()||parsed.ptr!=text.data()+text.size()||n>max){valid=false;return;}value=n;};
    const auto gain=[&](std::string_view text,double& value){double n=0;
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),n);
        if(text.empty()||parsed.ec!=std::errc()||parsed.ptr!=text.data()+text.size()||!std::isfinite(n)||n<0||n>1){valid=false;return;}value=n;};
    integer(mode,3,s.mode);integer(strength,10,s.strength);integer(impulse,3,s.impulse);gain(left,s.left_gain);gain(right,s.right_gain);
    s.allowed=valid&&controller=="0"&&ordinary&&!muted;return s;
}
struct PadVibrationGate {
    bool ordinary{},muted{},focused{},panel{},connected{},car_ready{};std::uint32_t mode{},pause_depth{};
    bool eligible()const{return ordinary&&!muted&&focused&&!panel&&connected&&car_ready&&mode==16u&&pause_depth==0u;}
};
inline void motor_mode(unsigned mode,double& left,double& right){if(mode==2u)std::swap(left,right);else if(mode==3u)left=right=std::max(left,right);}
inline PadMotors map_pad_motors(PadMotors m,const PadVibrationSettings& s){
    if(!s.allowed||s.mode==0u||s.mode>3u||s.impulse>3u)return {};
    m=sanitize(m);motor_mode(s.mode,m.left,m.right);
    m.left_trigger=m.right_trigger=0;
    if(s.impulse){m.left_trigger=m.left;m.right_trigger=m.right;motor_mode(s.impulse,m.left_trigger,m.right_trigger);
        m.left_trigger*=motor_level(s.left_gain);m.right_trigger*=motor_level(s.right_gain);}
    return sanitize(m);
}
struct PadVibrationCar {
    std::uint32_t flags{},state{},gear{},prior_gear{};
    int brake{},slip{};
    float speed{},prior_speed{},delta{},prior_delta{},road_left{},road_right{},lift_a{},lift_b{},suspension_a{},suspension_b{},roughness{};
    bool water{},other_known{};float other_speed{};
    bool valid()const{
        if(slip<-32768||slip>32767)return false;
        for(const auto v:{speed,prior_speed,delta,prior_delta,road_left,road_right,lift_a,lift_b,suspension_a,suspension_b,roughness})
            if(!std::isfinite(v))return false;
        return !other_known||std::isfinite(other_speed);
    }
};
class PadVibrationModel {
    unsigned idle_{},hold_{};double left_{};
public:
    void reset(){idle_=hold_=0;left_=0;}
    PadMotors tick(const PadVibrationCar& c,unsigned strength){
        if(!c.valid()||strength==0u||strength>10u){reset();return {};}
        const double speed=c.speed;double left=0,right=0;unsigned hold=0;
        // Xbox corrects this transient state bit from the car flags. Keep the
        // correction local: this optional output must never mutate simulation.
        auto state=c.state;if(state&0x100u)state=(state&~0x100u)|((c.flags>>23u)&0x100u);
        if(speed<=0)state&=~0x140u;
        else if(state&0x40u){left=c.prior_speed*.5;hold=20;}
        else{
            const double rough=std::clamp(double(c.roughness),0.0,1.0);
            right=rough*speed*.1;
            double edge=-1;
            if(rough>.3&&c.road_right<.1f)edge=.1-c.road_right;
            else if(rough>.3&&c.road_left>-.1f)edge=c.road_left+.1;
            if(edge>=0){right+=std::min(edge,2.0)*speed*.01;left=right*3;hold=10;}
            else if(rough>.7&&!c.water){left=rough*speed*.3;right*=.75;hold=10;}
            else{
                if(rough>.7&&c.water){if(speed>.95){left=(rough-.7)*speed*.75;hold=2;}right=(rough-.7)*right*5;}
                const int slip=std::abs(c.slip)-2000;
                if(slip>0){const auto v=slip*.00001;right+=v*2;left+=v*1.5;hold=2;}
                else if(c.flags&0x08000000u){const auto v=speed*.1;if(v>.1){right+=v;left+=v;hold=2;}}
                else if(state&0x100u)right*=speed*10+1;
                else right=0;
            }
        }
        if((state&0x40u)&&!(state&0x80u)){left+=c.prior_speed*.5;hold+=20;}
        else if((!(state&0x40u)&&(state&0x80u))||(c.lift_a!=0&&c.lift_b==0)||(c.suspension_b<0&&c.suspension_a>=0)){
            left+=.75;hold+=15;hold_=0;
        }else if(c.flags&0x20000u){
            if(state&0x800u)right+=speed;
            else{left+=.75;hold=std::min(20u,hold+unsigned(std::min(20.0,std::abs(double(c.delta))*10)));hold_=0;}
        }else if(c.flags&0x10000u){
            if(state&0x1000u)right+=speed*.5;
            else if(c.other_known){hold=std::min(20u,hold+unsigned(std::min(20.0,std::abs(double(c.other_speed)-speed)*25)));
                if(hold){left+=.75;hold_=0;}}
        }else if(state&0x2000u){left+=speed*.5;hold=2;}
        else if(!hold){
            if(c.gear!=c.prior_gear){left+=.2;hold=1;}
            else if(std::abs(c.delta)>.0018f&&((c.delta>=0&&c.prior_delta<0)||(c.delta<0&&c.prior_delta>=0))){left+=speed*.25;hold=2;}
            else if(c.delta>0&&speed<.33&&c.delta>.001f){left+=.3;hold=10;}
            else if(c.delta<-.002f){left+=(c.delta<-.004f&&c.brake>0)?speed*.2:.2;hold=2;}
        }
        if(speed>=.01)idle_=0;
        else if(speed<.001){hold_=0;left_=0;return {};}
        else{idle_=std::min(idle_+1u,601u);if(idle_>600u){hold_=0;left_=0;return {};}}
        const double scale=strength*.1;
        left=std::clamp(left,0.0,.75);right=std::clamp(right,0.0,.5)*scale;
        if(hold_){--hold_;if(left>left_){left_=left*scale;hold_=std::min(hold,25u);}}
        else{left_=left*scale;hold_=std::min(hold,25u);}
        return sanitize({left_,right,0,0});
    }
};
// One target selected by the existing input reader. A failed zero prevents
// handover; failed writes latch until selection changes, and stop can retry zero.
class PadVibrationSession {
public:
    using Writer=std::function<bool(const PadMotors&)>;
    bool select(Writer writer){if(!stop())return false;writer_=std::move(writer);failed_=false;return true;}
    bool submit(PadMotors m){
        m=sanitize(m);if(!writer_||failed_)return false;
        if(silent(m))return stop();
        active_=true;
        try{if(writer_(m))return true;}catch(...){}
        failed_=true;stop();return false;
    }
    bool stop(){
        if(!active_)return true;
        try{if(writer_&&writer_({})){active_=false;return true;}}catch(...){}
        failed_=true;return false;
    }
    bool failed()const{return failed_;}
private:
    Writer writer_;bool active_{},failed_{};
};
}
