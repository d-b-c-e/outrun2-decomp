#pragma once
#include "curved_panel_warp.hpp"
namespace outrun::desktop {
// Native single-panel fallback -> physical panel's inner sub-rect of its widened
// source. D3D top-left normalized coordinates; depth and draw order stay native.
// A future backend must use this for both viewport and scissor, before warp.
struct CurveInnerRect {double x{},y{},width{},height{};};
inline bool curve_inner_rect(const CurvedPanelWarp& warp,CurveInnerRect& out){
    if(!warp.valid||!std::isfinite(warp.min_x)||!std::isfinite(warp.min_y)||
       !std::isfinite(warp.span_x)||!std::isfinite(warp.span_y)||warp.span_x<=0||warp.span_y<=0||
       !std::isfinite(warp.basis.width)||!std::isfinite(warp.basis.height)||warp.basis.width<=0||warp.basis.height<=0)return false;
    CurveInnerRect r{-warp.min_x/warp.span_x,1.-(warp.basis.height-warp.min_y)/warp.span_y,
        warp.basis.width/warp.span_x,warp.basis.height/warp.span_y};
    if(!std::isfinite(r.x)||!std::isfinite(r.y)||!std::isfinite(r.width)||!std::isfinite(r.height)||
       r.x< -1e-12||r.y< -1e-12||r.width<=0||r.height<=0||r.x+r.width>1.+1e-12||r.y+r.height>1.+1e-12)return false;
    out=r;return true;
}
}
