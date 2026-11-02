#pragma once
#include "curved_panel_warp.hpp"
#include <cstdint>
#include <limits>
namespace outrun::desktop {
// Per-panel compositor input. Destination clip XY is local to that panel's
// viewport; source UV is local to its source image, D3D top-left origin. No
// atlas layout, source allocation, half-texel offset or live renderer is assumed.
struct CurveMeshVertex {float x{},y{},u{},v{};};
static_assert(sizeof(CurveMeshVertex)==16,"compositor vertex stride");
struct CurvedSceneMesh {
    static constexpr unsigned Columns=CurvedPanelWarp::GridColumns,Rows=CurvedPanelWarp::GridRows;
    static constexpr unsigned VertexCount=(Columns+1)*(Rows+1),IndexCount=Columns*Rows*6;
    std::array<std::array<CurveMeshVertex,VertexCount>,3> panels{};
    std::array<std::uint16_t,IndexCount> indices{}; // shared by all three draws
};
static_assert(CurvedSceneMesh::VertexCount<=std::numeric_limits<std::uint16_t>::max(),"16-bit mesh indices");
// Build a complete three-panel plan before publication. Allocation-free; out
// remains unchanged if a warp refuses or any source UV is outside its bounds.
inline CurveWarpError curved_scene_mesh(const std::array<CurvedPanelWarp,3>& warps,CurvedSceneMesh& out){
    for(const auto& w:warps)if(!w.valid||!std::isfinite(w.min_x)||!std::isfinite(w.min_y)||
        !std::isfinite(w.span_x)||!std::isfinite(w.span_y)||w.span_x<=0||w.span_y<=0)return CurveWarpError::invalid_input;
    CurvedSceneMesh candidate{};
    for(unsigned panel=0;panel<3;++panel)for(unsigned row=0;row<=CurvedSceneMesh::Rows;++row){
        const double v=double(row)/CurvedSceneMesh::Rows;
        for(unsigned col=0;col<=CurvedSceneMesh::Columns;++col){
            const double u=double(col)/CurvedSceneMesh::Columns;
            std::array<double,2> source{};const auto error=curved_source_uv(warps[panel],u,v,source);
            if(error!=CurveWarpError::none)return error;
            for(double t:source)if(!std::isfinite(t)||t< -1e-9||t>1.+1e-9)return CurveWarpError::projection;
            candidate.panels[panel][row*(CurvedSceneMesh::Columns+1)+col]={float(2*u-1),float(2*v-1),
                float(std::clamp(source[0],0.,1.)),float(1.-std::clamp(source[1],0.,1.))};
        }
    }
    unsigned at=0;
    for(unsigned row=0;row<CurvedSceneMesh::Rows;++row)for(unsigned col=0;col<CurvedSceneMesh::Columns;++col){
        const auto a=std::uint16_t(row*(CurvedSceneMesh::Columns+1)+col),b=std::uint16_t(a+1);
        const auto c=std::uint16_t(a+CurvedSceneMesh::Columns+1),d=std::uint16_t(c+1);
        // CCW in clip XY becomes clockwise after the D3D viewport Y flip.
        for(auto index:{a,b,c,b,d,c})candidate.indices[at++]=index;
    }
    out=candidate;return CurveWarpError::none;
}
}
