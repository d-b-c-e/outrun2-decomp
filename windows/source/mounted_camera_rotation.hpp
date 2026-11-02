#pragma once
#include "driving/pc_d3dx.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace outrun::desktop {
// Render-only counterpart of Art BonnetCamera.Smooth: shortest-arc quaternion
// exponential, 0.06 s time constant, snap across a >20 degree reset/cut.
using CameraQuaternion=std::array<float,4>; // x,y,z,w
inline float camera_rotation_dot(const CameraQuaternion& a,const CameraQuaternion& b){float d=0;for(unsigned i=0;i<4;++i)d+=a[i]*b[i];return d;}
inline CameraQuaternion camera_rotation_normalize(CameraQuaternion q){const float n=std::sqrt(camera_rotation_dot(q,q));if(!(n>0.f)||!std::isfinite(n))return {0,0,0,1};for(auto& v:q)v/=n;return q;}
inline CameraQuaternion camera_rotation_from_world(const driving::PcMatrix16& m){
    // Transpose the native row-vector matrix for the usual column-vector formula.
    const auto a=[&](unsigned r,unsigned c){return m[c*4+r];};
    CameraQuaternion q{};const float trace=a(0,0)+a(1,1)+a(2,2);
    if(trace>0.f){const float s=2.f*std::sqrt(trace+1.f);q={(a(2,1)-a(1,2))/s,(a(0,2)-a(2,0))/s,(a(1,0)-a(0,1))/s,.25f*s};}
    else{unsigned i=0;if(a(1,1)>a(i,i))i=1;if(a(2,2)>a(i,i))i=2;const unsigned j=(i+1)%3,k=(i+2)%3;
        const float s=2.f*std::sqrt(std::max(0.f,1.f+a(i,i)-a(j,j)-a(k,k)));
        if(!(s>0.f))return {0,0,0,1};q[i]=.25f*s;q[j]=(a(i,j)+a(j,i))/s;q[k]=(a(i,k)+a(k,i))/s;q[3]=(a(k,j)-a(j,k))/s;}
    return camera_rotation_normalize(q);
}
inline driving::PcMatrix16 camera_rotation_world(CameraQuaternion q,const driving::PcMatrix16& position){
    q=camera_rotation_normalize(q);const float x=q[0],y=q[1],z=q[2],w=q[3];
    return {1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),0,
            2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),0,
            2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y),0,
            position[12],position[13],position[14],1};
}
inline CameraQuaternion camera_rotation_slerp(CameraQuaternion a,CameraQuaternion b,float t){
    float dot=camera_rotation_dot(a,b);if(dot<0.f){for(auto& v:b)v=-v;dot=-dot;}
    dot=std::clamp(dot,0.f,1.f);float x=1.f-t,y=t;
    if(dot<.9995f){const float angle=std::acos(dot),denom=std::sin(angle);x=std::sin((1.f-t)*angle)/denom;y=std::sin(t*angle)/denom;}
    for(unsigned i=0;i<4;++i)a[i]=a[i]*x+b[i]*y;return camera_rotation_normalize(a);
}
struct MountedCameraRotation {
    CameraQuaternion previous{0,0,0,1},current{0,0,0,1};bool started{};
    void step(CameraQuaternion target,float dt){
        target=camera_rotation_normalize(target);
        if(!started||std::abs(camera_rotation_dot(current,target))<.984807753f){previous=current=target;started=true;return;} // cos(20deg/2)
        previous=current;
        if(std::isfinite(dt)&&dt>0.f)current=camera_rotation_slerp(current,target,-std::expm1(-dt/.06f));
    }
    CameraQuaternion display(float blend)const{return camera_rotation_slerp(previous,current,std::clamp(blend,0.f,1.f));}
};
}
