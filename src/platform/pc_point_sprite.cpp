// D3D9 point sprites: size and corners (pc_point_sprite.hpp).
#include "platform/pc_point_sprite.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace outrun::platform {
bool pc_point_sprite_size(const PcPointSpriteParams& p,const float eye[3],float viewport_h,float& size){
    // Every parameter in the finite domain (romi 29547): a NaN limit would otherwise fall through the comparisons.
    for(const float v:{p.size,p.min,p.max,p.a,p.b,p.c,p.max_point_size})if(!std::isfinite(v))return false;
    double s=p.size;
    if(p.scale){
        if(!std::isfinite(eye[0])||!std::isfinite(eye[1])||!std::isfinite(eye[2])||!std::isfinite(viewport_h))return false;
        const double de=std::sqrt(double(eye[0])*eye[0]+double(eye[1])*eye[1]+double(eye[2])*eye[2]);
        const double q=double(p.a)+double(p.b)*de+double(p.c)*de*de;
        if(!(q>=0.0))return false;                                   // sqrt of a negative (or NaN)
        const double k=double(viewport_h)*double(p.size);
        if(q==0.0){if(k==0.0)return false;s=k>0.0?std::numeric_limits<double>::infinity():-std::numeric_limits<double>::infinity();}
        else s=k*std::sqrt(1.0/q);
    }
    if(std::isnan(s))return false;
    const double pmax=std::min(double(p.max_point_size),double(p.max));
    if(s>pmax)s=pmax;
    if(s<double(p.min))s=p.min;
    if(!std::isfinite(s)||s<0.0)return false;
    size=float(s);
    return true;
}
bool pc_point_sprite_corners(const float clip[4],float size,float viewport_w,float viewport_h,float corners[4][4]){
    const float x=clip[0],y=clip[1],z=clip[2],w=clip[3];
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(w)||!std::isfinite(size))return false;
    if(!(w>0.f)||!(z>=0.f&&z<=w))return false;                         // depth outside MinZ..MaxZ
    if(!(viewport_w>0.f&&viewport_h>0.f)||!std::isfinite(viewport_w)||!std::isfinite(viewport_h)||size<0.f)return false;
    // Half the side in clip units: S/2 pixels are (S/2) / (V/2) in normalized coordinates, times w.
    const float hx=size/viewport_w*w,hy=size/viewport_h*w;
    if(!std::isfinite(hx)||!std::isfinite(hy)||!std::isfinite(x-hx)||!std::isfinite(x+hx)||!std::isfinite(y-hy)||!std::isfinite(y+hy))
        return false;                                                   // a finite input that overflows
    if(x-hx>w||x+hx<-w||y-hy>w||y+hy<-w)return false;                  // entirely outside the viewport
    // Screen y runs down and clip y up: the top edge is y + hy.
    const float xs[4]{x-hx,x+hx,x-hx,x+hx},ys[4]{y+hy,y+hy,y-hy,y-hy};
    for(unsigned k=0;k<4;++k){corners[k][0]=xs[k];corners[k][1]=ys[k];corners[k][2]=z;corners[k][3]=w;}
    return true;
}
}
