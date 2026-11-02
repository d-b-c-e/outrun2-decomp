#pragma once
// Explicit stencil-darkening coverage, never inferred from RHW alone. The
// renderer enables this only while Angled's three-view callback is installed.
#include "platform/pc_d3d9.hpp"
#include <cmath>
namespace outrun::platform {
inline bool pc_world_coverage_draw(PcDrawPhase phase,bool rhw,bool backbuffer){
    return phase==PcDrawPhase::WorldCoverage&&rhw&&backbuffer;
}
// Existing three-view GS layout: three identity corrections, pixel-centre fix,
// two padding floats. Identity preserves clip depth and covers each viewport
// independently of the scene projection. Refusal leaves the output unchanged.
inline bool pc_world_coverage_constants(PcDrawPhase phase,bool rhw,bool backbuffer,
                                       float fx,float fy,std::array<float,52>& out){
    if(!pc_world_coverage_draw(phase,rhw,backbuffer)||!std::isfinite(fx)||!std::isfinite(fy)||fx<=0||fy<=0)return false;
    std::array<float,52> c{};
    for(unsigned panel=0;panel<3;++panel)for(unsigned k=0;k<16;k+=5)c[panel*16+k]=1.f;
    c[48]=fx;c[49]=fy;out=c;return true;
}
}
