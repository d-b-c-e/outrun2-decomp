#pragma once
#include "platform/pc_screen.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace outrun::platform {
// Camera-view-space planes from the same Pcenter * Cpanel the geometry shader uses.
// Built at projection/profile changes, not for each node; all planes point into the D3D clip volume.
struct PcPanelFrusta {
    struct Panel {
        std::array<std::array<double,4>,6> planes{};
        std::array<double,4> depth{};
        double vertical_scale{},near_distance{};
    };
    std::array<Panel,3> panels{};
    bool valid{};
};
inline PcPanelFrusta pc_panel_frusta(const float* projection,PcTripleCorrections corrections){
    if(!projection||!corrections)return {};
    float c[48]{};
    for(unsigned k=0;k<16;++k)if(!std::isfinite(projection[k]))return {};
    if(!corrections(projection,c))return {};
    PcPanelFrusta out;
    for(unsigned panel=0;panel<3;++panel){
        double m[4][4]{};
        for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k){
            if(!std::isfinite(c[panel*16+k*4+j]))return {};
            m[i][j]+=double(projection[i*4+k])*c[panel*16+k*4+j];
        }
        auto& p=out.panels[panel];
        for(unsigned i=0;i<4;++i){
            p.planes[0][i]=m[i][3]+m[i][0];p.planes[1][i]=m[i][3]-m[i][0];
            p.planes[2][i]=m[i][3]+m[i][1];p.planes[3][i]=m[i][3]-m[i][1];
            p.planes[4][i]=m[i][2];p.planes[5][i]=m[i][3]-m[i][2];
        }
        for(auto& plane:p.planes){
            const double n=std::hypot(plane[0],plane[1],plane[2]);
            if(!(n>0.)||!std::isfinite(n))return {};
            for(auto& v:plane){v/=n;if(!std::isfinite(v))return {};}
        }
        const double wn=std::hypot(m[0][3],m[1][3],m[2][3]);
        if(!(wn>0.)||!std::isfinite(wn))return {};
        for(unsigned i=0;i<4;++i)p.depth[i]=m[i][3]/wn;
        // Remove the projection's off-axis vertical shift before measuring its vertical FOV scale.
        const double along=(m[0][1]*m[0][3]+m[1][1]*m[1][3]+m[2][1]*m[2][3])/(wn*wn);
        p.vertical_scale=std::hypot(m[0][1]-along*m[0][3],m[1][1]-along*m[1][3],m[2][1]-along*m[2][3])/wn;
        p.near_distance=-p.planes[4][3];
        if(!(p.vertical_scale>0.)||!std::isfinite(p.vertical_scale)||!(p.near_distance>0.))return {};
    }
    out.valid=true;return out;
}
struct PcPanelVisibility { bool visible{};float projected_radius{}; };
inline PcPanelVisibility pc_panel_sphere(const PcPanelFrusta& frusta,float x,float y,float z,float radius){
    PcPanelVisibility out;
    if(!frusta.valid||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(radius)||radius<0.f)return out;
    const auto dot=[&](const std::array<double,4>& p){return p[0]*x+p[1]*y+p[2]*z+p[3];};
    for(const auto& panel:frusta.panels){
        bool inside=true;
        for(const auto& p:panel.planes)if(dot(p)<-double(radius)){inside=false;break;}
        if(!inside)continue;
        out.visible=true;
        // A sphere crossing a panel near plane can have its center behind that eye plane.
        // Clamp that panel's depth to near; never feed negative center-camera depth into LOD.
        const double size=panel.vertical_scale*radius/std::max(panel.near_distance,dot(panel.depth));
        out.projected_radius=std::max(out.projected_radius,float(size));
    }
    return out;
}
}
