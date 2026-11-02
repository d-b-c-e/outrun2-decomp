#pragma once
#include <array>
#include <cmath>
#include <limits>
namespace outrun::desktop {
// Matches cbuffer CurveRect, two float4 registers. Three equal source images
// occupy the existing full-span color atlas; source UV is already D3D top-left.
struct CurveSampleRect {std::array<float,4> map{},limits{};};
static_assert(sizeof(CurveSampleRect)==32,"curve pixel constants");
inline bool curve_sample_rect(unsigned width,unsigned height,unsigned panel,CurveSampleRect& out){
    if(width<6||height<2||width>16384||height>16384||width%3||panel>=3)return false;
    const double left=double(panel)*(width/3),right=left+width/3;
    const auto inward=[](double value,bool lower){return std::nextafter(float(value),lower?
        std::numeric_limits<float>::infinity():-std::numeric_limits<float>::infinity());};
    CurveSampleRect r{{1.f/3.f,1.f,float(panel)/3.f,0.f},
        {inward((left+.5)/width,true),inward(.5/height,true),
         inward((right-.5)/width,false),inward(1.-.5/height,false)}};
    if(r.limits[0]>r.limits[2]||r.limits[1]>r.limits[3])return false;
    out=r;return true;
}
// A later backend must bind POSITION float2 at0, TEXCOORD float2 at8,
// stride16; linear/clamp sampler, no blending/depth, and restore its state.
inline constexpr const char* CurveCompositorHlsl=R"HLSL(
struct CurveIn{float2 p:POSITION;float2 uv:TEXCOORD0;};
struct CurveVarying{float4 p:SV_Position;float2 uv:TEXCOORD0;};
CurveVarying vs_main(CurveIn v){CurveVarying o;o.p=float4(v.p,0,1);o.uv=v.uv;return o;}
cbuffer CurveRect:register(b0){float4 uvMap;float4 uvLimits;};
Texture2D sceneSource:register(t0);SamplerState linearClamp:register(s0);
float4 ps_main(CurveVarying v):SV_Target{
 float2 uv=clamp(v.uv*uvMap.xy+uvMap.zw,uvLimits.xy,uvLimits.zw);
 return sceneSource.Sample(linearClamp,uv);
}
)HLSL";
}
