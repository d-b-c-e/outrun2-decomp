#pragma once
#include "platform/pc_screen.hpp"
#include <array>
#include <cmath>
namespace outrun::platform {
// Borrowed immutable process-lifetime mesh; 3 contiguous XY/UV panels, shared indices.
inline constexpr unsigned PcCurveVertices=65*37,PcCurveIndices=64*36*6,PcCurveStride=16;
struct PcCurveMeshView {
    const void* vertices{};const std::uint16_t* indices{};
    std::size_t vertex_bytes{};unsigned vertices_per_panel{},index_count{},stride{};
    const char* shader_source{};std::array<std::array<float,8>,3> sample_rects{};
    std::array<std::array<float,4>,3> inner{}; // top-left x,y,width,height in each source
};
inline bool pc_curve_mesh_valid(const PcCurveMeshView& v){
    if(!v.vertices||!v.indices||!v.shader_source||!*v.shader_source||v.vertices_per_panel!=PcCurveVertices||v.index_count!=PcCurveIndices||
       v.stride!=PcCurveStride||v.vertex_bytes!=3u*PcCurveVertices*PcCurveStride)return false;
    for(const auto& r:v.inner){for(float n:r)if(!std::isfinite(n))return false;
        if(r[0]<-1e-6f||r[1]<-1e-6f||r[2]<=0||r[3]<=0||r[0]+r[2]>1.000001f||r[1]+r[3]>1.000001f)return false;}
    for(const auto& rect:v.sample_rects)for(float f:rect)if(!std::isfinite(f))return false;
    return true;
}
using PcCurveMeshProvider=bool(*)(unsigned width,unsigned height,PcCurveMeshView&);
inline PcCurveMeshProvider g_pc_curve_mesh_provider=nullptr;
enum class PcCurveMode:unsigned {unavailable,active,flat};
enum class PcCurveRefusal:unsigned {none,projection,resources,resize};
inline std::atomic<PcCurveMode> g_pc_curve_mode{PcCurveMode::unavailable};
inline std::atomic<PcCurveRefusal> g_pc_curve_refusal{PcCurveRefusal::none};
inline bool pc_curve_active(){return g_pc_curve_mode.load(std::memory_order_acquire)==PcCurveMode::active;}
inline void pc_curve_request_flat(PcCurveRefusal why){auto empty=PcCurveRefusal::none;g_pc_curve_refusal.compare_exchange_strong(empty,why,std::memory_order_release);}
// Resource owner calls these only after preparation or at an owned frame boundary.
inline void pc_curve_publish(PcCurveMode mode){g_pc_curve_mode.store(mode,std::memory_order_release);pc_triple_profile_changed();}
}
