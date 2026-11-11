// The H-pattern gearbox (hpattern.hpp). Pure: no Windows headers, no runtime access.
#include "hpattern.hpp"
namespace outrun::desktop {
void HPattern::set(State s,const std::string& why){if(s!=state_||why!=status_){state_=s;status_=why;}}
void HPattern::reset(){state_=State::Idle;status_.clear();target_=0;direction_=0;tries_=0;deadline_us_=0;gear_at_pulse_=0;}

int HPattern::step_mode(const bool gates[6],std::uint32_t mode,bool car,std::uint32_t gear,std::int64_t now){
    if(mode!=mode_){reset();released_tick();mode_=mode;return 0;}
    if(mode==18u){released_tick();return 0;}
    return step(gates,mode==16u,car,gear,now);
}

int HPattern::step(const bool gates[6],bool race,bool car,std::uint32_t gear,std::int64_t now){
    const bool just_pulsed=pulsed_last_;
    pulsed_last_=false;
    int count=0;std::uint32_t target=0;
    for(int k=0;k<6;++k)if(gates[k]){++count;target=std::uint32_t(k+1);}
    if(count==0){if(state_!=State::Idle)reset();return 0;}
    if(count>1){target_=0;set(State::Ambiguous,"ambiguous: "+std::to_string(count)+" gates held");return 0;}
    if(target!=target_){reset();target_=target;}   // a new gate restarts the sequence and its retries
    if(!race||!car){set(State::Unavailable,"gear unavailable (not racing)");return 0;}
    // A missing or corrupt read is never a gear: no pulse is sent on it (slice 2a review 1).
    if(gear<1||gear>6){set(State::Unavailable,"gear word "+std::to_string(gear)+" is not a gear 1-6");return 0;}
    if(state_==State::Stopped)return 0;            // logged once; only a gate change restarts
    switch(state_){
    case State::Pulse:
        set(State::Release,"released after a shift pulse");   // at least one released tick
        return 0;
    case State::Release:
        set(State::Wait,"waiting for the gear to change");
        [[fallthrough]];
    case State::Wait:
        if(gear!=gear_at_pulse_){tries_=0;break;}   // the consumed gear moved: next step, if any
        if(now<deadline_us_)return 0;
        if(++tries_>cfg_.retries){
            set(State::Stopped,"gear target not reached (gear "+std::to_string(gear)+", target "+std::to_string(target_)+")");
            return 0;
        }
        break;                                      // retry the same step
    default:break;
    }
    if(gear==target_){set(State::AtTarget,"at gear "+std::to_string(gear));return 0;}
    // The tick after any pulse is released, even when the gate or the direction changed meanwhile (slice 2a review 2).
    // The state is left as it is, so the pulse goes out on the next tick.
    if(just_pulsed)return 0;
    direction_=gear<target_?1:-1;
    gear_at_pulse_=gear;
    deadline_us_=now+cfg_.deadline_us;
    set(State::Pulse,std::string("shift ")+(direction_>0?"up":"down")+" toward gear "+std::to_string(target_));
    pulsed_last_=true;
    return direction_;
}
}
