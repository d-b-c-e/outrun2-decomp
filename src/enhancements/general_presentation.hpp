#pragma once
// Optional desktop presentation policy. Simulation uses the existing 60 Hz
// accumulator; pacing uses the host's existing steady clock and sleep adapter.
#include <cstdint>
#include <string_view>
namespace outrun::enhancements {
struct GeneralPresentation {
    bool enabled{};
    std::uint32_t limit{}; // zero: no CPU cap, not a promise about physical scanout
    bool configure(bool requested,std::string_view text,bool ordinary=true){
        enabled=false;limit=0;
        if(!requested||!ordinary)return true;
        if(text.empty()||text.size()>4u)return false;
        unsigned value=0;
        for(char c:text){if(c<'0'||c>'9')return false;value=value*10u+unsigned(c-'0');}
        if(value!=0u&&(value<60u||value>1000u))return false;
        enabled=true;limit=value;return true;
    }
};
inline GeneralPresentation& general_presentation(){static GeneralPresentation policy;return policy;}
inline bool general_presentation_enabled(){return general_presentation().enabled;}
// Nanosecond deadlines only; no OS timer, busy-wait or simulation-clock mutation.
// Late frames rebase, avoiding a burst of presentations trying to catch up.
class PresentationDeadline {
public:
    void reset(){next_=0;remainder_=0;}
    std::uint64_t wait_ns(std::uint64_t now,unsigned rate){
        if(!rate){reset();return 0;}
        if(!next_||now>next_){next_=now;remainder_=0;}
        const auto wait=next_>now?next_-now:0;
        next_+=1000000000ull/rate;
        remainder_+=1000000000ull%rate;
        if(remainder_>=rate){next_+=remainder_/rate;remainder_%=rate;}
        return wait;
    }
private:
    std::uint64_t next_{},remainder_{};
};
}
