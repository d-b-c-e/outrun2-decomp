#pragma once
// Optional Tweaks mode 2: reuse the desktop's frame wait/Present and the
// native frame loop. No separate timer or file-service loop. Main thread only.
#include <cstdint>
#include <string_view>

namespace outrun::enhancements {
class FastLoad {
public:
    // Ini::text already removes surrounding whitespace/comments. Only 0 and 2
    // are supported; an absent key uses "0". Refused values always turn it off.
    bool configure(std::string_view value){
        enabled_=value=="2";
        return value=="0"||enabled_;
    }
    bool enabled()const{return enabled_;}
    void bind(const std::uint32_t& mode,const std::uint32_t& stage){
        mode_=&mode;stage_=&stage;loading_frame_=false;
    }
    void unbind(){mode_=nullptr;stage_=nullptr;loading_frame_=false;}
    // Native START stages end at 65. From 65 START owns the visible race's
    // camera/countdown, so testing only mode 13 would also uncap gameplay.
    bool loading()const{return enabled_&&mode_&&stage_&&*mode_==13u&&*stage_<65u;}
    void begin_frame(){loading_frame_=loading();}
    bool loading_frame()const{return loading_frame_;}
    // Read live progress as well: a tick may reach 65 before this frame's
    // Present. Restore the interval/wait immediately, not one frame later.
    bool active()const{return loading_frame_&&loading();}
    bool wait_for_latency()const{return !active();}
    unsigned present_interval(unsigned normal)const{return active()?0u:normal;}
    unsigned cooperative_level(bool display_frames)const{
        return loading_frame_||!display_frames?2u:1u;
    }
    // Rebase the existing timing accumulator only on leaving an accelerated
    // frame (including an abort to another mode), after normal presentation.
    bool end_frame(){
        const bool exited=loading_frame_&&!loading();
        loading_frame_=false;
        return exited;
    }
private:
    bool enabled_{},loading_frame_{};
    const std::uint32_t* mode_{};
    const std::uint32_t* stage_{};
};
inline FastLoad& fast_load(){static FastLoad state;return state;}
// Bind only while the context's native loop is alive. Setup/teardown cannot
// inherit an uncapped Present or retain pointers to a destroyed context.
class FastLoadBinding {
public:
    FastLoadBinding(FastLoad& state,const std::uint32_t& mode,const std::uint32_t& stage):state_(state){state_.bind(mode,stage);}
    ~FastLoadBinding(){state_.unbind();}
    FastLoadBinding(const FastLoadBinding&)=delete;
    FastLoadBinding& operator=(const FastLoadBinding&)=delete;
private:
    FastLoad& state_;
};
}
