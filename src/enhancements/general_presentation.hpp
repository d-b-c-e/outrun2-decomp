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
// Independent desktop presentation controls. Empty VSync preserves the legacy
// 60/120 pacing (and experimental interval0); explicit values match Tweaks.
struct PresentationControls {
    unsigned mode{}; // 0 efficient sleep/yield, 1 accurate processor-pause wait
    int vsync{-1};
    bool configure(std::string_view limiter,std::string_view sync,bool ordinary=true){
        mode=0;vsync=-1;
        if(!ordinary)return true;
        const bool valid_mode=limiter=="0"||limiter=="1";
        const bool valid_sync=sync.empty()||sync=="0"||sync=="1"||sync=="2";
        if(valid_mode)mode=limiter=="1"?1u:0u;
        if(valid_sync&&!sync.empty())vsync=int(sync[0]-'0');
        return valid_mode&&valid_sync;
    }
    unsigned cpu_limit(const GeneralPresentation& general,unsigned ordinary_rate)const{
        return general.enabled?general.limit:(vsync>=0?ordinary_rate:0u);
    }
    unsigned present_interval(const GeneralPresentation& general,unsigned legacy)const{
        return vsync>=0?unsigned(vsync):(general.enabled?0u:legacy);
    }
};
inline PresentationControls& presentation_controls(){static PresentationControls controls;return controls;}
// Nanosecond deadlines; no new OS timer or simulation-clock mutation.
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
    // Uses the host's existing clock/sleep. Accurate mode intentionally spins;
    // efficient mode sleeps most of a long wait, then yields near the deadline.
    // Callbacks also let the affected policy fixture advance a synthetic clock.
    template<class Clock,class Sleep,class Spin>
    void pace(unsigned rate,unsigned mode,Clock clock,Sleep sleep,Spin spin){
        const auto start=clock();
        const auto deadline=start+wait_ns(start,rate);
        for(auto now=start;now<deadline;now=clock()){
            const auto remaining=deadline-now;
            if(mode==1u)spin();
            else sleep(remaining>2000000ull?remaining-1000000ull:0ull);
        }
    }
private:
    std::uint64_t next_{},remainder_{};
};
}
