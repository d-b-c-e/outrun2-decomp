#pragma once
#include "triple_rig.hpp"
#include <algorithm>
namespace outrun::desktop {
// Native adapter for canonical CurvedPanelWarp.WithSourceOn (toolkit 149cc66).
// Millimetres, RH/+X right/+Y up/-Z into the screen; UV lower-left. The real
// measured eye defines UVs. The rendered panel only relocates the source plane.
// Eye distance is to the chord plane: never subtract sagitta here.
// No allocations, settings, targets, drawing or GPU dependencies. Outputs stay
// unchanged on failure. This component is not yet connected to presentation.
enum class CurveWarpError { none, invalid_input, panel, eye_behind, size_mismatch, projection };
namespace curve_detail {
inline bool finite(RigVec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
struct Basis {RigVec right{},up{},normal{};double width{},height{};};
inline bool basis(const RigPanel& p,Basis& out){
    if(!finite(p.lower_left)||!finite(p.lower_right)||!finite(p.upper_left))return false;
    const auto horizontal=p.lower_right-p.lower_left,vertical=p.upper_left-p.lower_left;
    const double width=length(horizontal),height=length(vertical);
    if(!std::isfinite(width)||!std::isfinite(height)||width<=1e-9||height<=1e-9)return false;
    const auto right=horizontal*(1./width),up=vertical*(1./height);
    if(std::abs(dot(right,up))>1e-6)return false;
    const auto normal=normalized(cross(right,up));if(!finite(normal))return false;
    out={right,up,normal,width,height};return true;
}
inline bool radius_ok(double width,double radius){return std::isfinite(radius)&&radius>=0.&&(radius==0.||width<=2.*radius);}
inline RigVec point(const RigPanel& panel,const Basis& b,double radius,double u,double v){
    if(radius==0.)return panel.lower_left+b.right*(b.width*u)+b.up*(b.height*v);
    const double half=std::asin(b.width/(2.*radius)),phi=(2.*u-1.)*half;
    const double sag=-2.*std::sin((half+phi)/2.)*std::sin((half-phi)/2.)*radius;
    return panel.lower_left+b.right*(b.width/2.+radius*std::sin(phi))+b.normal*sag+b.up*(b.height*v);
}
inline bool chord(const RigPanel& panel,const Basis& b,RigVec eye,RigVec point,std::array<double,2>& out){
    const auto ray=point-eye;const double denominator=dot(ray,b.normal);
    if(!std::isfinite(denominator)||denominator==0.)return false;
    const auto hit=eye+ray*(dot(panel.lower_left-eye,b.normal)/denominator);
    const std::array<double,2> candidate{{dot(hit-panel.lower_left,b.right),dot(hit-panel.lower_left,b.up)}};
    if(!std::isfinite(candidate[0])||!std::isfinite(candidate[1]))return false;
    out=candidate;return true;
}
}
struct CurvedPanelWarp {
    static constexpr unsigned GridColumns=64,GridRows=36;
    RigPanel panel{},source{};RigVec eye{};double radius{};
    curve_detail::Basis basis{};double min_x{},min_y{},span_x{},span_y{};bool valid{};
};
inline CurveWarpError curved_panel_point(const RigPanel& panel,double radius,double u,double v,RigVec& out){
    curve_detail::Basis basis;
    if(!curve_detail::basis(panel,basis))return CurveWarpError::panel;
    if(!curve_detail::radius_ok(basis.width,radius)||!std::isfinite(u)||!std::isfinite(v)||u<0.||u>1.||v<0.||v>1.)return CurveWarpError::invalid_input;
    const auto candidate=curve_detail::point(panel,basis,radius,u,v);
    if(!curve_detail::finite(candidate))return CurveWarpError::projection;
    out=candidate;return CurveWarpError::none;
}
inline CurveWarpError curved_panel_warp(const RigPanel& real_panel,RigVec real_eye,double radius,const RigPanel& rendered_panel,CurvedPanelWarp& out){
    curve_detail::Basis real,rendered;
    if(!curve_detail::basis(real_panel,real)||!curve_detail::basis(rendered_panel,rendered))return CurveWarpError::panel;
    if(!curve_detail::finite(real_eye)||!curve_detail::radius_ok(real.width,radius))return CurveWarpError::invalid_input;
    const double distance=dot(real_eye-real_panel.lower_left,real.normal);
    if(!std::isfinite(distance))return CurveWarpError::invalid_input;
    if(distance<=1e-9)return CurveWarpError::eye_behind;
    if(std::abs(real.width-rendered.width)>1e-6||std::abs(real.height-rendered.height)>1e-6)return CurveWarpError::size_mismatch;
    double min_x=0.,max_x=real.width,min_y=0.,max_y=real.height;
    if(radius>0.)for(unsigned i=0;i<=256;++i){
        const double t=double(i)/256.;
        const std::array<std::array<double,2>,4> samples{{{{t,0.}},{{t,1.}},{{0.,t}},{{1.,t}}}};
        for(const auto& uv:samples){
            std::array<double,2> xy;
            if(!curve_detail::chord(real_panel,real,real_eye,curve_detail::point(real_panel,real,radius,uv[0],uv[1]),xy))return CurveWarpError::projection;
            min_x=std::min(min_x,xy[0]);max_x=std::max(max_x,xy[0]);min_y=std::min(min_y,xy[1]);max_y=std::max(max_y,xy[1]);
        }
    }
    const double span_x=max_x-min_x,span_y=max_y-min_y;
    const auto origin=rendered_panel.lower_left+rendered.right*min_x+rendered.up*min_y;
    const RigPanel source=radius==0.?rendered_panel:RigPanel{origin,origin+rendered.right*span_x,origin+rendered.up*span_y};
    if(!std::isfinite(span_x)||!std::isfinite(span_y)||span_x<=0.||span_y<=0.||!curve_detail::finite(source.lower_left)||!curve_detail::finite(source.lower_right)||!curve_detail::finite(source.upper_left))return CurveWarpError::projection;
    out={real_panel,source,real_eye,radius,real,min_x,min_y,span_x,span_y,true};return CurveWarpError::none;
}
inline CurveWarpError curved_source_uv(const CurvedPanelWarp& warp,double u,double v,std::array<double,2>& out){
    if(!warp.valid||!std::isfinite(u)||!std::isfinite(v)||u<0.||u>1.||v<0.||v>1.)return CurveWarpError::invalid_input;
    if(warp.radius==0.){out={u,v};return CurveWarpError::none;}
    std::array<double,2> xy;
    if(!curve_detail::chord(warp.panel,warp.basis,warp.eye,curve_detail::point(warp.panel,warp.basis,warp.radius,u,v),xy))return CurveWarpError::projection;
    const std::array<double,2> candidate{{(xy[0]-warp.min_x)/warp.span_x,(xy[1]-warp.min_y)/warp.span_y}};
    if(!std::isfinite(candidate[0])||!std::isfinite(candidate[1]))return CurveWarpError::projection;
    out=candidate;return CurveWarpError::none;
}
// Recover the virtual render eye that makes the original native projection see
// the flat centre panel. Thus Game/Realistic and saved FOV offsets stay encoded
// in P; the real eye still defines curve UVs. Centre must use native X/Y axes.
inline bool curve_native_eye(const Mat4& original,const RigPanel& center,RigVec& out){
    Frustum fr;curve_detail::Basis b;
    if(!perspective_frustum(original,fr)||!curve_detail::basis(center,b))return false;
    if(length(b.right-RigVec{1,0,0})>1e-9||length(b.up-RigVec{0,1,0})>1e-9)return false;
    const double dx=b.width*fr.n/(fr.r-fr.l),dy=b.height*fr.n/(fr.t-fr.b);
    if(!std::isfinite(dx)||!std::isfinite(dy)||dx<=0||dy<=0||std::abs(dx-dy)>1e-6*std::max({1.,dx,dy}))return false;
    const auto middle=center.lower_left+b.right*(b.width*.5)+b.up*(b.height*.5);
    const auto eye=middle-b.right*((fr.l+fr.r)*dx/(2.*fr.n))-b.up*((fr.b+fr.t)*dx/(2.*fr.n))+b.normal*dx;
    if(!curve_detail::finite(eye))return false;
    out=eye;return true;
}
// Source frusta for the existing row-vector geometry-shader correction. Physical
// panel coordinates are relative to render_eye; game view-space vertices remain
// relative to their own camera. Depth follows the original native RH projection.
// Unlike legacy flat triple_views, a widened center source need not be identity.
inline bool curved_source_views(const Mat4& original,RigVec render_eye,const std::array<CurvedPanelWarp,3>& warps,TripleViews& out){
    if(!curve_detail::finite(render_eye))return false;
    for(const auto value:original)if(!std::isfinite(value))return false;
    Frustum fr;if(!perspective_frustum(original,fr))return false;
    Mat4 inverse;if(!mat_inverse(original,inverse))return false;
    TripleViews candidate{};
    for(unsigned i=0;i<3;++i){
        curve_detail::Basis basis;PanelBounds bounds;
        if(!warps[i].valid||!curve_detail::basis(warps[i].source,basis)||panel_bounds(warps[i].source,render_eye,bounds)!=RigError::none)return false;
        candidate.rotation[i]=panel_rotation(warps[i].source);
        candidate.projection[i]=perspective_off_center_rh(bounds.left*fr.n,bounds.right*fr.n,bounds.bottom*fr.n,bounds.top*fr.n,fr.n,fr.f);
        candidate.correction[i]=mat_mul(mat_mul(inverse,candidate.rotation[i]),candidate.projection[i]);
        for(const auto* matrix:{&candidate.rotation[i],&candidate.projection[i],&candidate.correction[i]})
            for(const auto value:*matrix)if(!std::isfinite(value))return false;
    }
    out=candidate;return true;
}
}
