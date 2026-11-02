#pragma once
#include "curved_panel_mesh.hpp"
#include "curve_inner_rect.hpp"
#include "curve_compositor_shader.hpp"
#include "platform/pc_curve_scene.hpp"
namespace outrun::desktop {
struct CurveSceneProfile {std::array<CurvedPanelWarp,3> warps{};CurvedSceneMesh mesh{};std::array<std::array<float,4>,3> inner{};};
static_assert(CurvedSceneMesh::VertexCount==platform::PcCurveVertices&&CurvedSceneMesh::IndexCount==platform::PcCurveIndices&&sizeof(CurveMeshVertex)==platform::PcCurveStride,"curve bridge layout");
inline bool curve_scene_profile(const TripleRig& rig,double radius,CurveSceneProfile& out){
    if(rig_check(rig)!=RigError::none||!std::isfinite(radius)||radius<0||std::abs(rig.panel_height_mm-rig.panel_width_mm*9./16.)>1e-6*rig.panel_height_mm)return false;
    CurveSceneProfile p;const auto panels=rig_panels(rig);
    for(unsigned i=0;i<3;++i){
        if(curved_panel_warp(panels[i],{},radius,panels[i],p.warps[i])!=CurveWarpError::none)return false;
        CurveInnerRect r;if(!curve_inner_rect(p.warps[i],r))return false;
        p.inner[i]={float(r.x),float(r.y),float(r.width),float(r.height)};
    }
    if(curved_scene_mesh(p.warps,p.mesh)!=CurveWarpError::none)return false;
    out=std::move(p);return true;
}
inline bool curve_scene_mesh_view(const CurveSceneProfile& p,unsigned width,unsigned height,platform::PcCurveMeshView& out){
    platform::PcCurveMeshView v{p.mesh.panels.data(),p.mesh.indices.data(),sizeof p.mesh.panels,
        CurvedSceneMesh::VertexCount,CurvedSceneMesh::IndexCount,sizeof(CurveMeshVertex),CurveCompositorHlsl,{},p.inner};
    for(unsigned i=0;i<3;++i){CurveSampleRect r;if(!curve_sample_rect(width,height,i,r))return false;
        std::copy(r.map.begin(),r.map.end(),v.sample_rects[i].begin());std::copy(r.limits.begin(),r.limits.end(),v.sample_rects[i].begin()+4);
    }
    if(!platform::pc_curve_mesh_valid(v))return false;out=v;return true;
}
inline bool curve_scene_corrections(const CurveSceneProfile& p,const float* original,float* out){
    if(!original||!out)return false;Mat4 m{};for(unsigned i=0;i<16;++i)m[i]=original[i];
    RigVec eye;if(!curve_native_eye(m,p.warps[1].panel,eye))return false;
    TripleViews views;if(!curved_source_views(m,eye,p.warps,views))return false;
    std::array<float,48> c{};for(unsigned panel=0;panel<3;++panel)for(unsigned k=0;k<16;++k){
        c[panel*16+k]=float(views.correction[panel][k]);if(!std::isfinite(c[panel*16+k]))return false;}
    std::copy(c.begin(),c.end(),out);return true;
}
}
