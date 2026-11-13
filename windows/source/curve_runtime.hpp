#pragma once
// The curve correction on curved screens: the Wheelkit runtime's warp (wheelkit.h wk_triples_curve_grid, the one the
// hook games and the Unity runtime use; wheelkit_runtime.hpp wheelkit_curve_grid) as the mesh the renderer's curve pass
// draws (xbox/source/d3d11_d3d9.cpp composite_curve: after the world, before the HUD and menus). Each screen's own flat
// view already covers its curved glass, so the views are drawn as they are (no widened source) and the pass bends each
// third of the back buffer onto its screen's arc. Tested offline (windows/tests/test_curve_runtime.cpp).
#include "curve_compositor_shader.hpp"
#include "platform/pc_curve_scene.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace outrun::desktop {
struct CurveVertex {float x{},y{},u{},v{};};   // clip x, y in the screen's viewport; u, v in its flat view (top-left)
struct CurveMesh {
    static constexpr unsigned Columns=64,Rows=36,VertexCount=(Columns+1)*(Rows+1),IndexCount=Columns*Rows*6;
    std::array<std::array<CurveVertex,VertexCount>,3> panels{};
    std::array<std::uint16_t,IndexCount> indices{};   // shared by the three draws
};
static_assert(CurveMesh::VertexCount==platform::PcCurveVertices&&CurveMesh::IndexCount==platform::PcCurveIndices&&
              sizeof(CurveVertex)==platform::PcCurveStride,"the renderer's curve mesh layout");
// From three grids of (Columns+1) x (Rows+1) u, v pairs, row by row from the top (wk_triples_curve_grid). False, out
// unchanged, for a value that is not finite or lies outside the view.
inline bool curve_mesh_from_grids(const std::array<const float*,3>& grids,CurveMesh& out){
    CurveMesh m{};
    constexpr unsigned C=CurveMesh::Columns,R=CurveMesh::Rows;
    for(unsigned panel=0;panel<3;++panel){
        const float* g=grids[panel];if(!g)return false;
        for(unsigned row=0;row<=R;++row)for(unsigned col=0;col<=C;++col){
            const unsigned k=row*(C+1)+col;const float u=g[2*k],v=g[2*k+1];
            if(!std::isfinite(u)||!std::isfinite(v)||u< -1e-4f||u>1.0001f||v< -1e-4f||v>1.0001f)return false;
            m.panels[panel][k]={2.f*float(col)/C-1.f,1.f-2.f*float(row)/R,std::clamp(u,0.f,1.f),std::clamp(v,0.f,1.f)};
        }
    }
    unsigned at=0;
    for(unsigned row=0;row<R;++row)for(unsigned col=0;col<C;++col){
        const auto a=std::uint16_t(row*(C+1)+col),b=std::uint16_t(a+1),c=std::uint16_t(a+C+1),d=std::uint16_t(c+1);
        for(auto index:{a,b,c,b,d,c})m.indices[at++]=index;
    }
    out=m;return true;
}
// What the renderer's mesh provider hands over for a back buffer of width x height (three equal thirds).
inline bool curve_mesh_view(const CurveMesh& mesh,unsigned width,unsigned height,platform::PcCurveMeshView& out){
    platform::PcCurveMeshView v{mesh.panels.data(),mesh.indices.data(),sizeof mesh.panels,CurveMesh::VertexCount,CurveMesh::IndexCount,
        sizeof(CurveVertex),CurveCompositorHlsl,{},{}};
    for(unsigned i=0;i<3;++i){
        CurveSampleRect r;if(!curve_sample_rect(width,height,i,r))return false;
        std::copy(r.map.begin(),r.map.end(),v.sample_rects[i].begin());std::copy(r.limits.begin(),r.limits.end(),v.sample_rects[i].begin()+4);
        v.inner[i]={0.f,0.f,1.f,1.f};   // each view is drawn whole, as without the curve
    }
    if(!platform::pc_curve_mesh_valid(v))return false;
    out=v;return true;
}
}
