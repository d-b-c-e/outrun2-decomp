#pragma once
// Tweaks' remaining UIScalingMode=1 C2C girlfriend bubble, heart and rank positions.
// bulk_tr.cpp is generated: match the external UI call by its return PC instead of editing that file.
#include "platform/race_area.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
namespace outrun::platform {
struct C2cUiSpacingSite {
    std::uint32_t tweaks_pc;    // Tweaks' hook before the translated call
    std::uint32_t return_pc;    // return address pushed by the translated call
    std::uint32_t target;       // 465860 configure or 4653C0 motion
    unsigned x_arg;             // zero-based stack argument in PcRaceCall
};
// In return-PC order. For example, Tweaks' 4FD60C hook edits [esp] before one more push, so its
// 4653C0 call receives the shifted float in arg 1; the other 4653C0 sites use arg 0.
inline constexpr std::array<C2cUiSpacingSite,18> C2cUiSpacingSites{{
    {0x4fc84eu,0x4fc867u,0x465860u,5u}, {0x4fc882u,0x4fc899u,0x465860u,5u},
    {0x4fc8b4u,0x4fc8cbu,0x465860u,5u}, {0x4fc9ebu,0x4fca03u,0x465860u,5u},
    {0x4fca1eu,0x4fca36u,0x465860u,5u}, {0x4fca51u,0x4fca69u,0x465860u,5u},
    {0x4fcb20u,0x4fcb32u,0x465860u,5u}, {0x4fcdc1u,0x4fcdc6u,0x4653c0u,0u},
    {0x4fcdeau,0x4fcdefu,0x4653c0u,0u}, {0x4fceb0u,0x4fceb5u,0x4653c0u,0u},
    {0x4fced9u,0x4fcedeu,0x4653c0u,0u}, {0x4fcf22u,0x4fcf27u,0x4653c0u,0u},
    {0x4fcf4fu,0x4fcf54u,0x4653c0u,0u}, {0x4fd591u,0x4fd596u,0x4653c0u,0u},
    {0x4fd5cdu,0x4fd5e1u,0x465860u,5u}, {0x4fd60cu,0x4fd614u,0x4653c0u,1u},
    {0x4fd652u,0x4fd666u,0x465860u,5u}, {0x4fe8b1u,0x4fe8b6u,0x4653c0u,0u},
}};
inline bool shift_c2c_translated_ui_call(PcRaceCall& call,std::uint32_t return_pc,float hud_spacing){
    // Modes 0 and 2 carry zero spacing: leave every argument bit-identical, including unusual float payloads.
    if(!(hud_spacing>0.0f)||(call.pc!=0x465860u&&call.pc!=0x4653c0u))return false;
    const auto site=std::lower_bound(C2cUiSpacingSites.begin(),C2cUiSpacingSites.end(),return_pc,
        [](const C2cUiSpacingSite& s,std::uint32_t pc){return s.return_pc<pc;});
    if(site==C2cUiSpacingSites.end()||site->return_pc!=return_pc||site->target!=call.pc||site->x_arg>=call.argc)return false;
    float x;
    std::memcpy(&x,&call.args[site->x_arg],sizeof x);
    x+=hud_spacing;
    std::memcpy(&call.args[site->x_arg],&x,sizeof x);
    return true;
}
}
