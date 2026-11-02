#pragma once
#include "camera_controls.hpp"
#include "shim.hpp"
namespace outrun::desktop {
// Ordinary resolved-frame consumer, outside latched/replay/record ownership.
inline void camera_controls_shim_frame(const dbce::shim::Frame& frame,bool blocked){
    std::array<CameraResolvedAction,11> actions{};
    for(unsigned i=0;i<actions.size();++i){
        const int index=dbce::shim::action_index(CameraControlActions[i]);if(index<0)continue;
        const auto& a=frame.action[std::size_t(index)];actions[i]={a.value>=.5f,a.status==dbce::shim::Status::Live};
    }
    camera_controls_resolved_actions(actions,blocked||frame.latched||frame.run!=dbce::shim::Run::None||frame.tick_us<0,
                                    frame.tick_us<0?0u:std::uint64_t(frame.tick_us)/1000u);
}
}
