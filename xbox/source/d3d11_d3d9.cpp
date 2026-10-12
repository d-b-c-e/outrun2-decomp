#include "d3d11_d3d9.hpp"
#include "pc_hlsl_shaders.hpp"
#include "fxaa_hlsl.hpp"
#include <cmath>
#include <d3dcompiler.h>
#include "runtime/game_host.hpp"
#include "platform/pc_screen.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <vector>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
namespace outrun::xbox_runtime {
using namespace platform::d3d9;
namespace {
constexpr unsigned KBuffer=1,KDecl=2,KVs=3,KTexture=5,KSurface=6;
void check(HRESULT hr,const char* what){if(FAILED(hr))throw std::runtime_error(std::string(what)+" failed (HRESULT "+std::to_string(unsigned(hr))+")");}
constexpr UINT CompileFlags=D3DCOMPILE_OPTIMIZATION_LEVEL3;   // also part of the shader cache key
// The three-view geometry shader (TRIPLE-SCREENS.md step 4): each triangle three times, panel i in viewport i with its
// clip correction C[i], applied before the D3D9 pixel-centre fix (undone, then re-applied: every viewport is the same
// size). windows/source/triple_rig.hpp triple_apply is the same arithmetic, unit-tested. Its Varying is not a copy: it
// is taken from the generated vertex source (pc_hlsl_shaders.hpp native_vs) when the shader is built, so its input is
// the vertex programs' output and its output the pixel programs' input plus the viewport index, last (nene 30802: a
// copy could drift, and a mismatched stage drops or garbles the draw without an error).
constexpr const char* TripleGsMain=R"HLSL(
struct Panel{Varying v;uint vp:SV_ViewportArrayIndex;};
cbuffer Triple:register(b0){row_major float4x4 C[3];float4 fix;};
[maxvertexcount(9)]
void gs_main(triangle Varying v[3],inout TriangleStream<Panel> s){
 [unroll] for(uint i=0;i<3;++i){
  [unroll] for(uint k=0;k<3;++k){
   float4 q=v[k].p;q.x-=fix.x*q.w;q.y+=fix.y*q.w;q=mul(q,C[i]);q.x+=fix.x*q.w;q.y-=fix.y*q.w;
   Panel o;o.v=v[k];o.v.p=q;o.vp=i;s.Append(o);}
  s.RestartStrip();}
}
)HLSL";
// The generated sources each declare their own Varying: the FFP one (vertex and pixel stage of the CPU path's draws) and
// the ps11 / ps14 pixel programs must agree with the vertex programs' (nene 33931), else a loud error.
std::string triple_gs_source(){
    const auto varying=[](const std::string& s){const auto at=s.find("struct Varying{"),end=at==std::string::npos?at:s.find("};",at);
        return end==std::string::npos?std::string{}:s.substr(at,end+2-at);};
    const auto native=varying(hlsl::native_vs);
    if(native.empty()||native!=varying(hlsl::ffp)||native!=varying(hlsl::ps11)||native!=varying(hlsl::ps14))
        throw std::runtime_error("three views: the generated shader stages disagree on their Varying");
    return native+TripleGsMain;
}
ComPtr<ID3DBlob> compile(const std::string& source,const char* entry,const char* target){
    ComPtr<ID3DBlob> code,errors;
    const HRESULT hr=D3DCompile(source.data(),source.size(),"pc_shader",nullptr,nullptr,entry,target,CompileFlags,0,&code,&errors);
    if(FAILED(hr))throw std::runtime_error(std::string("HLSL compile: ")+(errors?static_cast<const char*>(errors->GetBufferPointer()):"unknown error"));
    return code;
}
// D3D9 and D3D11 share the numbering of comparison functions, stencil
// operations, blend factors 1..11, blend operations and address modes.
D3D11_COMPARISON_FUNC comparison(unsigned f){return D3D11_COMPARISON_FUNC(std::clamp(f,1u,8u));}
D3D11_STENCIL_OP stencil_op(unsigned v){return D3D11_STENCIL_OP(v>=1&&v<=8?v:1u);}
D3D11_BLEND blend_factor(unsigned v){return v==0?D3D11_BLEND_ONE:D3D11_BLEND(std::min(v,11u));}
// The alpha channel only takes alpha factors; colour factors read the alpha.
D3D11_BLEND alpha_factor(unsigned v){
    switch(blend_factor(v)){case D3D11_BLEND_SRC_COLOR:return D3D11_BLEND_SRC_ALPHA;case D3D11_BLEND_INV_SRC_COLOR:return D3D11_BLEND_INV_SRC_ALPHA;
    case D3D11_BLEND_DEST_COLOR:return D3D11_BLEND_DEST_ALPHA;case D3D11_BLEND_INV_DEST_COLOR:return D3D11_BLEND_INV_DEST_ALPHA;default:return blend_factor(v);}
}
D3D11_BLEND_OP blend_op(unsigned v){return D3D11_BLEND_OP(v>=1&&v<=5?v:1u);}
D3D11_TEXTURE_ADDRESS_MODE address(unsigned v){return D3D11_TEXTURE_ADDRESS_MODE(v>=1&&v<=5?v:1u);}
template<class T> std::string key(const T& d){return std::string(reinterpret_cast<const char*>(&d),sizeof d);}
void colour(std::uint32_t c,float* out){out[0]=float((c>>16)&255)/255;out[1]=float((c>>8)&255)/255;out[2]=float(c&255)/255;out[3]=float(c>>24)/255;}
// Compiled pixel programs on disk (home()/shadercache; a latched process's home is its run folder): FNV-1a 64 of the
// source, target and flags. A missing, short or unreadable file is compiled again; a failed write only costs time.
std::string shader_cache_path(const std::string& source,const char* target){
    std::uint64_t h=1469598103934665603ull;
    auto mix=[&h](const char* p,std::size_t n){for(std::size_t i=0;i<n;++i){h^=std::uint8_t(p[i]);h*=1099511628211ull;}};
    // The compile flags and the compiler version are part of the key: a change compiles again rather than
    // loading code built another way.
    const std::uint32_t build[2]{CompileFlags,std::uint32_t(D3D_COMPILER_VERSION)};
    mix(source.data(),source.size());mix(target,std::strlen(target));mix(reinterpret_cast<const char*>(build),sizeof build);
    char name[40];std::snprintf(name,sizeof name,"/ps-%016llx.cso",(unsigned long long)h);
    return outrun::runtime::home()+"/shadercache"+name;
}
std::vector<std::uint8_t> read_shader_cache(const std::string& path){
    std::vector<std::uint8_t> bytes;
    if(std::FILE* f=std::fopen(path.c_str(),"rb")){
        std::uint8_t buffer[4096];std::size_t n;
        while((n=std::fread(buffer,1,sizeof buffer,f))>0)bytes.insert(bytes.end(),buffer,buffer+n);
        std::fclose(f);
    }
    return bytes.size()<16?std::vector<std::uint8_t>{}:bytes;
}
void write_shader_cache(const std::string& path,const void* data,std::size_t size){
    std::error_code e;std::filesystem::create_directories(std::filesystem::u8path(outrun::runtime::home()+"/shadercache"),e);
    const std::string temp=path+".tmp";
    std::FILE* f=std::fopen(temp.c_str(),"wb");if(!f)return;
    const bool ok=std::fwrite(data,1,size,f)==size;
    if(std::fclose(f)!=0||!ok){std::remove(temp.c_str());return;}
    std::filesystem::rename(std::filesystem::u8path(temp),std::filesystem::u8path(path),e);
    if(e)std::remove(temp.c_str());
}
}
D3D11D3D9Device::D3D11D3D9Device(ID3D11Device* device,ID3D11DeviceContext* context,unsigned w,unsigned h)
    :PcSoftD3D9Device(w,h),device_(device),context_(context){
    const auto vs=compile(hlsl::ffp,"vs_main","vs_4_1");
    check(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex_),"CreateVertexShader");
    const auto ps=compile(hlsl::ffp,"ps_main","ps_4_1");
    check(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&ffp_),"CreatePixelShader");
    D3D11_INPUT_ELEMENT_DESC elements[12]{};
    const char* names[3]{"POSITION","COLOR","TEXCOORD"};
    for(unsigned a=0;a<12;++a){auto& e=elements[a];e.SemanticName=a==0?names[0]:a<3?names[1]:names[2];
        e.SemanticIndex=a==0?0:a<3?a-1:a-3;e.Format=a==11?DXGI_FORMAT_R32_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT;
        e.AlignedByteOffset=UINT(a*4*sizeof(float));e.InputSlotClass=D3D11_INPUT_PER_VERTEX_DATA;}
    static_assert(offsetof(Vertex,fog)==11*4*sizeof(float),"vertex layout");
    check(device_->CreateInputLayout(elements,12,vs->GetBufferPointer(),vs->GetBufferSize(),&layout_),"CreateInputLayout");
    const UINT sizes[3]{8*16,512,48};
    for(unsigned k=0;k<3;++k){D3D11_BUFFER_DESC d{};d.ByteWidth=sizes[k];d.Usage=D3D11_USAGE_DYNAMIC;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        check(device_->CreateBuffer(&d,nullptr,&ubo_[k]),"CreateBuffer(constants)");}
    {   D3D11_BUFFER_DESC d{};d.ByteWidth=257*16;d.Usage=D3D11_USAGE_DYNAMIC;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        check(device_->CreateBuffer(&d,nullptr,&vs_constants_),"CreateBuffer(vertex constants)");}
    native_enabled_=std::getenv("OR2_D3D11_CPU_VERTEX")==nullptr;
    std::printf("D3D11 vertex path: %s\n",native_enabled_?"GPU (CPU for draws it cannot take)":"CPU (OR2_D3D11_CPU_VERTEX)");
    allocate_back(w,h);
    {   const auto fvs=compile(hlsl::fxaa,"vs_main","vs_4_1"),fps=compile(hlsl::fxaa,"ps_main","ps_4_1");
        check(device_->CreateVertexShader(fvs->GetBufferPointer(),fvs->GetBufferSize(),nullptr,&fxaa_vertex_),"CreateVertexShader(FXAA)");
        check(device_->CreatePixelShader(fps->GetBufferPointer(),fps->GetBufferSize(),nullptr,&fxaa_pixel_),"CreatePixelShader(FXAA)");
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
        check(device_->CreateSamplerState(&sd,&fxaa_sampler_),"CreateSamplerState(FXAA)");
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        check(device_->CreateRasterizerState(&rd,&fxaa_rasterizer_),"CreateRasterizerState(FXAA)");
        D3D11_DEPTH_STENCIL_DESC dd{};check(device_->CreateDepthStencilState(&dd,&fxaa_depth_),"CreateDepthStencilState(FXAA)");
        D3D11_BLEND_DESC bd{};bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;check(device_->CreateBlendState(&bd,&fxaa_blend_),"CreateBlendState(FXAA)");
    }
    D3D11_TEXTURE2D_DESC t{};t.MipLevels=1;t.ArraySize=1;t.SampleDesc.Count=1;t.Usage=D3D11_USAGE_DEFAULT;
    const std::uint32_t black[6]{};D3D11_SUBRESOURCE_DATA init[6];for(auto& i:init){i.pSysMem=black;i.SysMemPitch=4;i.SysMemSlicePitch=4;}
    t.Width=t.Height=1;t.Format=DXGI_FORMAT_R8G8B8A8_UNORM;t.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    check(device_->CreateTexture2D(&t,init,&dummy2d_),"CreateTexture2D(dummy)");
    check(device_->CreateShaderResourceView(dummy2d_.Get(),nullptr,&dummy2d_srv_),"CreateShaderResourceView(dummy)");
    t.ArraySize=6;t.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
    check(device_->CreateTexture2D(&t,init,&dummycube_),"CreateTexture2D(dummy cube)");
    check(device_->CreateShaderResourceView(dummycube_.Get(),nullptr,&dummycube_srv_),"CreateShaderResourceView(dummy cube)");
    D3D11_QUERY_DESC q{};q.Query=D3D11_QUERY_OCCLUSION;for(auto& query:queries_)check(device_->CreateQuery(&q,&query),"CreateQuery");
}
D3D11D3D9Device::~D3D11D3D9Device(){
    if(query_live_){context_->End(queries_[query_index_].Get());query_pending_[query_index_]=true;}
    collect_queries(false);   // what the GPU has finished; the rest of the diagnostic count is not waited for
    std::printf("shader programs: %u compiled, %u from the cache, %.0f ms in total, slowest %.0f ms\n",
                shader_stats_.compiled,shader_stats_.loaded,shader_stats_.ms,shader_stats_.max_ms);
    std::printf("DrawPrimitiveUP not drawn (unsupported primitive, compatibility fallback): points %llu, lines %llu, line strips %llu\n",
                (unsigned long long)skipped_up[1],(unsigned long long)skipped_up[2],(unsigned long long)skipped_up[3]);
    std::printf("point sprites: %llu drawn, %llu not drawn by D3D9's rules (depth or viewport)\n",
                (unsigned long long)point_sprites_drawn,(unsigned long long)point_sprites_rejected);
    if(platform::g_pc_triple_corrections)
        std::printf("triple views: %llu draws into three viewports over %llu frames; single on the centre: %llu pretransformed, %llu custom "
                    "projection, %llu not triangles, %llu other targets (not the back buffer)\n",(unsigned long long)triple_draws,
                    (unsigned long long)triple_frames,(unsigned long long)triple_single_rhw,(unsigned long long)triple_single_custom,
                    (unsigned long long)triple_single_topology,(unsigned long long)triple_single_target);
    std::printf("vertex path: %llu GPU draws, %llu CPU draws; vertex programs %u compiled, %u from the cache, %u failed (%.0f ms); %llu buffer uploads (%llu bytes)\n",
                (unsigned long long)native_stats_.draws,(unsigned long long)native_stats_.cpu_draws,native_stats_.compiled,native_stats_.loaded,native_stats_.failed,native_stats_.ms,
                (unsigned long long)native_stats_.uploads,(unsigned long long)native_stats_.upload_bytes);
    for(const auto& r:native_stats_.refused)std::printf("  CPU vertex path: %s (%llu draws)\n",r.first.c_str(),(unsigned long long)r.second);
}
void D3D11D3D9Device::allocate_back(unsigned w,unsigned h){
    if(!w||!h||w>16384||h>16384)throw std::runtime_error("invalid GPU back buffer size");
    bound_=Bound{};
    back_srv_.Reset();back_rtv_.Reset();back_dsv_.Reset();back_colour_tex_.Reset();back_depth_tex_.Reset();
    msaa_rtv_.Reset();msaa_dsv_.Reset();msaa_colour_tex_.Reset();msaa_depth_tex_.Reset();msaa_samples_=0;
    fxaa_copy_srv_.Reset();fxaa_copy_tex_.Reset();
    D3D11_TEXTURE2D_DESC t{};t.Width=w;t.Height=h;t.MipLevels=1;t.ArraySize=1;t.Format=DXGI_FORMAT_R8G8B8A8_UNORM;t.SampleDesc.Count=1;
    t.Usage=D3D11_USAGE_DEFAULT;t.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    check(device_->CreateTexture2D(&t,nullptr,&back_colour_tex_),"CreateTexture2D(back)");
    check(device_->CreateRenderTargetView(back_colour_tex_.Get(),nullptr,&back_rtv_),"CreateRenderTargetView(back)");
    check(device_->CreateShaderResourceView(back_colour_tex_.Get(),nullptr,&back_srv_),"CreateShaderResourceView(back)");
    t.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;t.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    check(device_->CreateTexture2D(&t,nullptr,&back_depth_tex_),"CreateTexture2D(depth)");
    check(device_->CreateDepthStencilView(back_depth_tex_.Get(),nullptr,&back_dsv_),"CreateDepthStencilView(back)");
    back_w_=w;back_h_=h;
}
void D3D11D3D9Device::resize_back_buffer(unsigned w,unsigned h){if(w!=back_w_||h!=back_h_)allocate_back(w,h);}
void D3D11D3D9Device::ensure_msaa(){
    if(msaa_samples_==msaa_&&msaa_rtv_)return;
    UINT quality=0;check(device_->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM,msaa_,&quality),"CheckMultisampleQualityLevels");
    if(!quality)throw std::runtime_error("GPU lacks this MSAA sample count");
    msaa_rtv_.Reset();msaa_dsv_.Reset();msaa_colour_tex_.Reset();msaa_depth_tex_.Reset();
    D3D11_TEXTURE2D_DESC t{};t.Width=back_w_;t.Height=back_h_;t.MipLevels=1;t.ArraySize=1;t.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    t.SampleDesc.Count=msaa_;t.Usage=D3D11_USAGE_DEFAULT;t.BindFlags=D3D11_BIND_RENDER_TARGET;
    check(device_->CreateTexture2D(&t,nullptr,&msaa_colour_tex_),"CreateTexture2D(MSAA)");
    check(device_->CreateRenderTargetView(msaa_colour_tex_.Get(),nullptr,&msaa_rtv_),"CreateRenderTargetView(MSAA)");
    t.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;t.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    check(device_->CreateTexture2D(&t,nullptr,&msaa_depth_tex_),"CreateTexture2D(MSAA depth)");
    check(device_->CreateDepthStencilView(msaa_depth_tex_.Get(),nullptr,&msaa_dsv_),"CreateDepthStencilView(MSAA)");
    msaa_samples_=msaa_;
}
void D3D11D3D9Device::finish_scene(){
    if(scene_finished_||count_only||colour_target_!=BackBuffer)return;
    if(msaa_live_){   // resolve into the back buffer, whose depth/stencil is cleared for the 2D layer
        msaa_live_=false;scene_finished_=true;
        context_->ResolveSubresource(back_colour_tex_.Get(),0,msaa_colour_tex_.Get(),0,DXGI_FORMAT_R8G8B8A8_UNORM);
        context_->ClearDepthStencilView(back_dsv_.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1.f,0);
        ++msaa_resolves;return;
    }
    if(!fxaa_)return;
    scene_finished_=true;
    if(!fxaa_copy_tex_){
        D3D11_TEXTURE2D_DESC t{};back_colour_tex_->GetDesc(&t);t.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        check(device_->CreateTexture2D(&t,nullptr,&fxaa_copy_tex_),"CreateTexture2D(FXAA)");
        check(device_->CreateShaderResourceView(fxaa_copy_tex_.Get(),nullptr,&fxaa_copy_srv_),"CreateShaderResourceView(FXAA)");
    }
    triple_unbind();
    ID3D11ShaderResourceView* none[12]{};context_->PSSetShaderResources(0,12,none);
    context_->CopyResource(fxaa_copy_tex_.Get(),back_colour_tex_.Get());
    ID3D11RenderTargetView* rtv=back_rtv_.Get();context_->OMSetRenderTargets(1,&rtv,nullptr);
    const D3D11_VIEWPORT vp{0,0,float(back_w_),float(back_h_),0,1};context_->RSSetViewports(1,&vp);
    context_->RSSetState(fxaa_rasterizer_.Get());context_->OMSetDepthStencilState(fxaa_depth_.Get(),0);context_->OMSetBlendState(fxaa_blend_.Get(),nullptr,0xffffffffu);
    context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(fxaa_vertex_.Get(),nullptr,0);context_->PSSetShader(fxaa_pixel_.Get(),nullptr,0);
    ID3D11ShaderResourceView* srv=fxaa_copy_srv_.Get();context_->PSSetShaderResources(0,1,&srv);
    ID3D11SamplerState* sampler=fxaa_sampler_.Get();context_->PSSetSamplers(0,1,&sampler);
    context_->Draw(3,0);bound_=Bound{};
    ++fxaa_passes;
}
void D3D11D3D9Device::begin_frame(){
    scene_finished_=false;msaa_live_=msaa_>1&&!count_only;
    if(msaa_live_)ensure_msaa();
    bound_=Bound{};
    if(platform::g_pc_triple_corrections){context_->GSSetShader(nullptr,nullptr,0);triple_gs_bound_=false;}
    if(!count_only){
        // A slot three frames old that the GPU has not answered yet: its sample (diagnostics only) is dropped and the
        // query reissued, never waited for; OR2_XBOX_DRAW_STATS waits, for its per-frame log.
        static const bool stats=std::getenv("OR2_XBOX_DRAW_STATS")!=nullptr;
        if(query_pending_[query_index_]){collect_queries(stats);query_pending_[query_index_]=false;}
        context_->Begin(queries_[query_index_].Get());query_live_=true;}
}
void D3D11D3D9Device::collect_queries(bool wait_all){
    for(unsigned k=0;k<3;++k)if(query_pending_[k]){
        UINT64 passed=0;HRESULT hr;
        while((hr=context_->GetData(queries_[k].Get(),&passed,sizeof passed,wait_all?0:D3D11_ASYNC_GETDATA_DONOTFLUSH))==S_FALSE&&wait_all){}
        if(hr==S_OK){pixels_shaded+=passed;query_pending_[k]=false;}
    }
}
void D3D11D3D9Device::end_frame(){
    triple_unbind();if(triple_frame_draws_){++triple_frames;triple_frame_draws_=0;}
    if(msaa_live_){const auto target=colour_target_;colour_target_=BackBuffer;finish_scene();colour_target_=target;}   // no 2D layer this frame
    if(!query_live_)return;context_->End(queries_[query_index_].Get());query_live_=false;
    query_pending_[query_index_]=true;query_index_=(query_index_+1)%3;
    // Development: OR2_XBOX_DRAW_STATS=1 logs the draws of every 60th frame (and waits for each frame's count).
    static const bool stats=std::getenv("OR2_XBOX_DRAW_STATS")!=nullptr;static unsigned frame=0;
    const auto before=pixels_shaded;collect_queries(stats);const UINT64 passed=pixels_shaded-before;
    if(stats&&(frame++%60u)==0u)std::fprintf(stdout,"D3D11 frame %u: draws=%u vertices=%u pixels=%llu viewport=%.0fx%.0f\n",
        frame,frame_draws_,frame_vertices_,(unsigned long long)passed,soft_viewport_.w,soft_viewport_.h);
    frame_draws_=frame_vertices_=0;
}
D3D11D3D9Device::Texture* D3D11D3D9Device::texture(std::uint32_t handle){
    auto* o=object(handle);if(!o||o->kind!=KTexture)return nullptr;
    if(auto it=textures_.find(handle);it!=textures_.end())return &it->second;
    Texture t{};t.cube=o->texture.cube;t.levels=std::max(1u,o->texture.levels);
    const auto& top=o->texture.faces[0][0];
    D3D11_TEXTURE2D_DESC d{};d.Width=top.width;d.Height=top.height;d.MipLevels=t.levels;d.ArraySize=t.cube?6:1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;d.MiscFlags=t.cube?D3D11_RESOURCE_MISC_TEXTURECUBE:0;
    check(device_->CreateTexture2D(&d,nullptr,&t.texture),"CreateTexture2D");
    for(unsigned face=0;face<o->rgba.size();++face)for(unsigned level=0;level<o->rgba[face].size()&&level<t.levels;++level){
        const auto& rgba=o->rgba[face][level];if(rgba.empty())continue;
        context_->UpdateSubresource(t.texture.Get(),D3D11CalcSubresource(level,face,t.levels),nullptr,rgba.data(),o->texture.faces[face][level].width*4,0);
    }
    check(device_->CreateShaderResourceView(t.texture.Get(),nullptr,&t.srv),"CreateShaderResourceView");
    return &(textures_[handle]=std::move(t));
}
ID3D11RenderTargetView* D3D11D3D9Device::colour_view(){
    if(colour_target_==BackBuffer)return msaa_live_?msaa_rtv_.Get():back_rtv_.Get();   // the multisampled scene (port enhancement)
    auto* s=object(colour_target_);if(!s||!s->parent)throw std::runtime_error("invalid GPU colour surface");
    auto* t=texture(s->parent);if(!t)throw std::runtime_error("GPU surface has no parent");
    const std::uint64_t k=std::uint64_t(s->parent)<<32|std::uint64_t(s->face)<<16|s->level;
    if(auto it=colour_views_.find(k);it!=colour_views_.end())return it->second.Get();
    D3D11_RENDER_TARGET_VIEW_DESC d{};d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    if(t->cube){d.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;d.Texture2DArray.MipSlice=s->level;d.Texture2DArray.FirstArraySlice=s->face;d.Texture2DArray.ArraySize=1;}
    else{d.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;d.Texture2D.MipSlice=s->level;}
    ComPtr<ID3D11RenderTargetView> view;check(device_->CreateRenderTargetView(t->texture.Get(),&d,&view),"CreateRenderTargetView");
    return (colour_views_[k]=view).Get();
}
ID3D11DepthStencilView* D3D11D3D9Device::depth_view(std::uint32_t handle){
    if(handle==DepthBuffer)return colour_target_==BackBuffer&&msaa_live_?msaa_dsv_.Get():back_dsv_.Get();if(!handle)return nullptr;
    if(auto it=depth_views_.find(handle);it!=depth_views_.end())return it->second.Get();
    auto* o=object(handle);if(!o||o->kind!=KSurface||o->parent)throw std::runtime_error("invalid GPU depth surface");
    D3D11_TEXTURE2D_DESC d{};d.Width=o->width;d.Height=o->height;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
    d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> tex;check(device_->CreateTexture2D(&d,nullptr,&tex),"CreateTexture2D(depth surface)");
    ComPtr<ID3D11DepthStencilView> view;check(device_->CreateDepthStencilView(tex.Get(),nullptr,&view),"CreateDepthStencilView");
    return (depth_views_[handle]=view).Get();
}
void D3D11D3D9Device::bind_target(){
    if(colour_target_==BackBuffer&&msaa_live_&&depth_target_&&depth_target_!=DepthBuffer)throw std::runtime_error("MSAA scene with a separate depth surface");
    ID3D11RenderTargetView* rtv=colour_view();ID3D11DepthStencilView* dsv=depth_view(depth_target_);
    if(bound_.target&&rtv==bound_.rtv&&dsv==bound_.dsv)return;
    // A texture bound for sampling cannot be the target at the same time.
    ID3D11ShaderResourceView* none[12]{};context_->PSSetShaderResources(0,12,none);
    context_->OMSetRenderTargets(1,&rtv,dsv);
    bound_.target=true;bound_.rtv=rtv;bound_.dsv=dsv;bound_.textures_set=false;
}
void D3D11D3D9Device::release(std::uint32_t handle){
    PcSoftD3D9Device::release(handle);if(object(handle))return;
    textures_.erase(handle);depth_views_.erase(handle);shader_programs_.erase(handle);gpu_buffers_.erase(handle);index_ranges_.erase(handle);
    for(auto it=colour_views_.begin();it!=colour_views_.end();)it=std::uint32_t(it->first>>32)==handle?colour_views_.erase(it):std::next(it);
}
void D3D11D3D9Device::unlock_rect(std::uint32_t handle){
    auto* s=object(handle);const auto parent=s?s->parent:0;PcSoftD3D9Device::unlock_rect(handle);
    if(!parent||!textures_.count(parent))return;
    s=object(handle);auto* p=object(parent);auto& t=textures_.at(parent);
    context_->UpdateSubresource(t.texture.Get(),D3D11CalcSubresource(s->level,s->face,t.levels),nullptr,p->rgba[s->face][s->level].data(),s->width*4,0);
}
void D3D11D3D9Device::clear(std::uint32_t flags,std::uint32_t c,float z,std::uint32_t stencil){
    if(count_only)return;bind_target();
    if(flags&1){float rgba[4];colour(c,rgba);context_->ClearRenderTargetView(colour_view(),rgba);}
    if(auto* dsv=depth_view(depth_target_);dsv&&(flags&6))
        context_->ClearDepthStencilView(dsv,(flags&2?D3D11_CLEAR_DEPTH:0)|(flags&4?D3D11_CLEAR_STENCIL:0),z,UINT8(stencil));
}
void D3D11D3D9Device::raster(const Vertex* vertices,unsigned count){
    if(count<3)return;
    batch_rhw_=pretransformed_;
    auto push=[&](const Vertex& source){auto v=source;
        if(pretransformed_){const float w=1.f/v.pos[3];v.pos[0]=(2*(v.pos[0]+.5f-soft_viewport_.x)/soft_viewport_.w-1)*w;
            v.pos[1]=(1-2*(v.pos[1]+.5f-soft_viewport_.y)/soft_viewport_.h)*w;v.pos[2]=v.pos[2]*w;v.pos[3]=w;}
        else{v.pos[0]+=v.pos[3]/soft_viewport_.w;v.pos[1]-=v.pos[3]/soft_viewport_.h;}
        batch_.push_back(v);};
    for(unsigned k=1;k+1<count;++k){push(vertices[0]);push(vertices[k]);push(vertices[k+1]);++triangles_drawn;}
}
std::uint32_t D3D11D3D9Device::draw_indexed_primitive(std::uint32_t type,std::int32_t base,std::uint32_t min,std::uint32_t vertices,std::uint32_t start,std::uint32_t count){
    if(!count_only&&draw_native(type,base,start,count)){note_dip_state(count);return 0;}
    ++native_stats_.cpu_draws;
    batch_.clear();const auto result=PcSoftD3D9Device::draw_indexed_primitive(type,base,min,vertices,start,count);flush();return result;
}
std::uint8_t* D3D11D3D9Device::lock(std::uint32_t h,std::uint32_t offset,std::uint32_t size,std::uint32_t flags){
    auto* p=PcSoftD3D9Device::lock(h,offset,size,flags);
    if(p){   // D3D9: size 0 locks to the end. The GPU copy takes the range at the next draw; index bounds are rescanned.
        index_ranges_.erase(h);
        if(auto it=gpu_buffers_.find(h);it!=gpu_buffers_.end()){auto& g=it->second;const std::size_t end=size?std::size_t(offset)+size:object(h)->bytes.size();
            g.dirty_lo=std::min<std::size_t>(g.dirty_lo,offset);g.dirty_hi=std::max(g.dirty_hi,end);}
    }
    return p;
}
ID3D11Buffer* D3D11D3D9Device::gpu_buffer(std::uint32_t h){
    const auto& bytes=object(h)->bytes;auto& g=gpu_buffers_[h];
    if(!g.buffer||g.bytes!=bytes.size()){
        D3D11_BUFFER_DESC d{};d.ByteWidth=UINT((bytes.size()+15)&~std::size_t(15));d.Usage=D3D11_USAGE_DEFAULT;
        d.BindFlags=D3D11_BIND_VERTEX_BUFFER|D3D11_BIND_INDEX_BUFFER;
        std::vector<std::uint8_t> initial(d.ByteWidth);std::memcpy(initial.data(),bytes.data(),bytes.size());
        D3D11_SUBRESOURCE_DATA s{initial.data(),0,0};g.buffer.Reset();
        check(device_->CreateBuffer(&d,&s,&g.buffer),"CreateBuffer(D3D9 buffer)");
        g.bytes=bytes.size();g.dirty_lo=~std::size_t(0);g.dirty_hi=0;++native_stats_.uploads;native_stats_.upload_bytes+=bytes.size();
    }else if(g.dirty_hi>g.dirty_lo){
        const std::size_t lo=g.dirty_lo,hi=std::min(g.dirty_hi,bytes.size());
        if(hi>lo){const D3D11_BOX box{UINT(lo),0,0,UINT(hi),1,1};context_->UpdateSubresource(g.buffer.Get(),0,&box,bytes.data()+lo,0,0);
            ++native_stats_.uploads;native_stats_.upload_bytes+=hi-lo;}
        g.dirty_lo=~std::size_t(0);g.dirty_hi=0;
    }
    return g.buffer.Get();
}
D3D11D3D9Device::NativeProgram* D3D11D3D9Device::native_program(const std::string& key,const platform::PcShaderProgram& program,
                                                              unsigned provided,unsigned colours,unsigned bytes4){
    if(auto it=native_programs_.find(key);it!=native_programs_.end())return &it->second;
    // The program: its declared input registers (D3DCOLOR as RGBA, UBYTE4 as 0..255, the others (0,0,0,1)) and
    // its blob list as literal run() calls, then the D3D9 pixel-centre fix as in raster() (x + w/vw, y - w/vh).
    std::string source=hlsl::native_vs;source+="struct NativeIn{";
    for(unsigned r=0;r<16;++r)if(provided&(1u<<r))source+="float4 v"+std::to_string(r)+":V"+std::to_string(r)+";";
    if(!provided)source+="uint vertex:SV_VertexID;";   // a program without declared inputs: no input layout element
    source+="};\n";
    source+="Varying vs_main(NativeIn i){\n [unroll] for(int k=0;k<12;++k)R[k]=mk4(0.0);\n [unroll] for(int j=0;j<16;++j)V[j]=float4(0,0,0,1);\n";
    for(unsigned r=0;r<16;++r)if(provided&(1u<<r)){const auto n=std::to_string(r);
        source+=" V["+n+"]="+(colours&(1u<<r)?"i.v"+n+".zyxw":bytes4&(1u<<r)?"round(i.v"+n+"*255.0)":"i.v"+n)+";\n";}
    source+=" oPos=mk4(0.0);oFog=mk4(1.0);oPts=mk4(0.0);oD[0]=mk4(0.0);oD[1]=mk4(0.0);\n [unroll] for(int t=0;t<8;++t)oT[t]=mk4(0.0);\n";
    for(const auto blob:program.blobs)source+=" run("+std::to_string(blob)+");\n";
    source+=" Varying o;o.p=float4(oPos.x+clip_fix.x*oPos.w,oPos.y-clip_fix.y*oPos.w,oPos.z,oPos.w);\n"
            " o.d0=saturate(oD[0]);o.d1=saturate(oD[1]);o.t0=oT[0];o.t1=oT[1];o.t2=oT[2];o.t3=oT[3];o.t4=oT[4];o.t5=oT[5];o.t6=oT[6];o.t7=oT[7];\n"
            " o.fog=saturate(oFog.x);return o;}\n";
    const auto started=std::chrono::steady_clock::now();
    NativeProgram p;const std::string path=shader_cache_path(source,"vs_4_1");
    p.code=read_shader_cache(path);
    const bool loaded=!p.code.empty()&&SUCCEEDED(device_->CreateVertexShader(p.code.data(),p.code.size(),nullptr,&p.shader));
    if(!loaded){
        // A program that does not compile stays on the CPU path (a null shader), logged once, never a frame failure.
        try{
            const auto code=compile(source,"vs_main","vs_4_1");
            const auto* b=static_cast<const std::uint8_t*>(code->GetBufferPointer());p.code.assign(b,b+code->GetBufferSize());
            check(device_->CreateVertexShader(p.code.data(),p.code.size(),nullptr,&p.shader),"CreateVertexShader(native)");
            write_shader_cache(path,p.code.data(),p.code.size());
        }catch(const std::exception& e){
            std::printf("shader: vertex program %s stays on the CPU path: %.400s\n",key.c_str(),e.what());
            p=NativeProgram{};
        }
    }
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    ++(loaded?native_stats_.loaded:p.shader?native_stats_.compiled:native_stats_.failed);native_stats_.ms+=ms;
    std::printf("shader: vertex program %zu (%zu blobs) %s in %.0f ms\n",native_programs_.size()+1,program.blobs.size(),loaded?"loaded":"compiled",ms);
    return &(native_programs_[key]=std::move(p));
}
bool D3D11D3D9Device::draw_native(std::uint32_t type,std::int32_t base,std::uint32_t start,std::uint32_t count){
    if(!native_enabled_)return false;
    auto refuse=[this](const char* why){++native_stats_.refused[why];return false;};
    auto* ib=object(indices);auto* decl=object(declaration);auto* vs=object(vertex_shader);
    const std::size_t total=type==5?std::size_t(count)+2:type==4?std::size_t(count)*3:0;
    if(!ib||ib->kind!=KBuffer||!decl||decl->kind!=KDecl||!vs||vs->kind!=KVs)return refuse("no index buffer, declaration or vertex shader");
    if(!total||(std::size_t(start)+total)*2>ib->bytes.size())return refuse("primitive type or index range");
    if(vs->program.blobs.size()>32)return refuse("vertex program over 32 blobs");
    if(!targets_consistent())return refuse("render target and depth surface sizes differ");
    // The indices' bounds, as the CPU path would read the vertices (cached until the index buffer is locked).
    auto& ranges=index_ranges_[indices];const std::uint64_t range_key=(std::uint64_t(start)<<32)|total;
    auto range=ranges.find(range_key);
    if(range==ranges.end()){std::uint32_t low=0xffff,high=0;
        for(std::size_t k=start;k<std::size_t(start)+total;++k){std::uint16_t i;std::memcpy(&i,ib->bytes.data()+k*2,2);low=std::min<std::uint32_t>(low,i);high=std::max<std::uint32_t>(high,i);}
        if(ranges.size()>=256)ranges.clear();
        range=ranges.emplace(range_key,low|(high<<16)).first;}
    const std::int64_t low=range->second&0xffffu,high=range->second>>16;
    if(std::int64_t(base)+low<0)return refuse("negative base vertex");
    // D3D9 draws index 0xFFFF like any other; D3D11 takes it as a strip cut (the triangle is dropped, lists too).
    if(high==0xffff)return refuse("index 0xFFFF (a D3D11 strip cut)");
    // The declaration's elements by the program's input registers (a later element wins, as shade_vertex).
    int element_of[16];std::fill(std::begin(element_of),std::end(element_of),-1);
    for(std::size_t k=0;k<decl->elements.size();++k){const auto& e=decl->elements[k];
        if(e.stream==platform::PcDeclEndStream)break;
        if(e.type>5)return refuse("declaration type");
        if(e.stream>=streams.size())return refuse("declaration stream");
        const auto& st=streams[e.stream];auto* b=object(st.buffer);if(!b||b->kind!=KBuffer)return refuse("stream without a buffer");
        const std::uint64_t need=e.type<=3?4u*(e.type+1u):4u;
        if(std::uint64_t(st.offset)+std::uint64_t(std::int64_t(base)+high)*st.stride+e.offset+need>b->bytes.size())return refuse("vertex outside its stream");
        for(const auto& in:vs->program.inputs)if(in.usage==e.usage&&in.usage_index==e.usage_index){
            if(in.reg>=16)return refuse("input register");element_of[in.reg]=int(k);}
    }
    static constexpr DXGI_FORMAT formats[6]{DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R32G32_FLOAT,DXGI_FORMAT_R32G32B32_FLOAT,
        DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM};
    unsigned provided=0,colours=0,bytes4=0,slots=0;D3D11_INPUT_ELEMENT_DESC elements[16];UINT n=0;
    std::string key;for(const auto blob:vs->program.blobs)key+=std::to_string(blob)+",";
    for(unsigned r=0;r<16;++r)if(element_of[r]>=0){const auto& e=decl->elements[std::size_t(element_of[r])];
        provided|=1u<<r;if(e.type==4)colours|=1u<<r;if(e.type==5)bytes4|=1u<<r;slots|=1u<<e.stream;
        elements[n++]={"V",r,formats[e.type],e.stream,e.offset,D3D11_INPUT_PER_VERTEX_DATA,0};}
    key+="|"+std::to_string(provided)+"|"+std::to_string(colours)+"|"+std::to_string(bytes4);
    auto* program=native_program(key,vs->program,provided,colours,bytes4);
    if(!program->shader)return refuse("vertex program did not compile");
    std::string layout_key=key;for(UINT k=0;k<n;++k)layout_key+=";"+std::to_string(elements[k].SemanticIndex)+":"+std::to_string(elements[k].Format)+":"+
        std::to_string(elements[k].InputSlot)+":"+std::to_string(elements[k].AlignedByteOffset);
    auto& layout=native_layouts_[layout_key];   // a refused layout stays null: its draws keep the CPU path
    if(!layout&&!native_refused_layouts_.count(layout_key)&&FAILED(device_->CreateInputLayout(n?elements:nullptr,n,program->code.data(),program->code.size(),&layout))){
        std::printf("shader: input layout %s refused; its draws stay on the CPU path\n",layout_key.c_str());
        native_refused_layouts_.insert(layout_key);layout.Reset();}
    if(!layout)return refuse("input layout refused");
    // The pixel side, then the vertex side: constants, streams, indices, program.
    batch_rhw_=false;pretransformed_=false;
    pixel_pipeline();
    if(vs_shadow_.size()!=257*4)vs_shadow_.assign(257*4,std::numeric_limits<float>::quiet_NaN());
    float constants[257*4];std::memcpy(constants,vs_constants.data(),256*16);
    constants[1024]=1.f/soft_viewport_.w;constants[1025]=1.f/soft_viewport_.h;constants[1026]=0.f;constants[1027]=0.f;
    if(std::memcmp(constants,vs_shadow_.data(),sizeof constants)!=0){
        D3D11_MAPPED_SUBRESOURCE m{};check(context_->Map(vs_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&m),"Map(vertex constants)");
        std::memcpy(m.pData,constants,sizeof constants);context_->Unmap(vs_constants_.Get(),0);std::memcpy(vs_shadow_.data(),constants,sizeof constants);}
    for(unsigned s=0;s<streams.size()&&s<16;++s)if(slots&(1u<<s)){
        ID3D11Buffer* b=gpu_buffer(streams[s].buffer);const UINT stride=streams[s].stride,offset=streams[s].offset;
        if(bound_.vb[s]!=b||bound_.vb_stride[s]!=stride||bound_.vb_offset[s]!=offset){
            context_->IASetVertexBuffers(s,1,&b,&stride,&offset);bound_.vb[s]=b;bound_.vb_stride[s]=stride;bound_.vb_offset[s]=offset;}}
    if(ID3D11Buffer* b=gpu_buffer(indices);bound_.ib!=b){context_->IASetIndexBuffer(b,DXGI_FORMAT_R16_UINT,0);bound_.ib=b;}
    if(bound_.layout!=layout.Get()){context_->IASetInputLayout(layout.Get());bound_.layout=layout.Get();}
    const auto topology=type==5?D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    if(bound_.topology!=topology){context_->IASetPrimitiveTopology(topology);bound_.topology=topology;}
    if(!bound_.vs_constants){ID3D11Buffer* vsb=vs_constants_.Get();context_->VSSetConstantBuffers(0,1,&vsb);bound_.vs_constants=true;}
    if(bound_.vs!=program->shader.Get()){context_->VSSetShader(program->shader.Get(),nullptr,0);bound_.vs=program->shader.Get();}
    if(platform::g_pc_triple_corrections)triple_bind(false,colour_target_==BackBuffer&&triple_native_standard(program));
    context_->DrawIndexed(UINT(total),start,base);
    ++frame_draws_;frame_vertices_+=unsigned(total);++native_stats_.draws;triangles_drawn+=count;
    return true;
}
void D3D11D3D9Device::draw_primitive_up(std::uint32_t type,std::uint32_t count,const void* data,std::uint32_t stride){
    batch_.clear();PcSoftD3D9Device::draw_primitive_up(type,count,data,stride);flush();
}
void D3D11D3D9Device::uniform(unsigned slot,const void* data,std::size_t bytes){
    auto& shadow=uniform_shadow_[slot];   // the buffer keeps its contents: rewrite it only when they change
    if(shadow.size()==bytes&&std::memcmp(shadow.data(),data,bytes)==0)return;
    D3D11_MAPPED_SUBRESOURCE m{};check(context_->Map(ubo_[slot].Get(),0,D3D11_MAP_WRITE_DISCARD,0,&m),"Map(constants)");
    std::memcpy(m.pData,data,bytes);context_->Unmap(ubo_[slot].Get(),0);
    shadow.assign(static_cast<const std::uint8_t*>(data),static_cast<const std::uint8_t*>(data)+bytes);
}
// [Graphics] AnisotropicFiltering (the Tweaks key; 0 or 1 = the game's own filtering, 0 the default; 2..16 = that
// anisotropy level): the samplers the
// game filters linearly when minifying take anisotropic filtering at that level; point-filtered ones are left alone.
namespace { unsigned g_anisotropy=0;bool g_transparency_supersampling=false; }
void set_anisotropic_filtering(unsigned level){g_anisotropy=level<=16u?level:0u;}
// [Graphics] TransparencySupersampling (the Tweaks key; off by default): alpha-to-coverage for the alpha-tested,
// unblended draws of the multisampled scene (barriers, foliage, cloth at the track edge). The game's alpha test still
// discards below its reference, so nothing below it appears; above it, the texel's alpha becomes partial MSAA
// coverage, which smooths the cut edge. Needs MSAA (options.ini antialiasing=msaa2/4); without it, no effect.
void set_transparency_supersampling(bool on){g_transparency_supersampling=on;}
void D3D11D3D9Device::apply_state(){
    // The logical PC target stretched to the back buffer's pixels; with the UI
    // rectangle, pretransformed (2D) draws keep a centred 4:3 area.
    const bool back=colour_target_==BackBuffer;
    double sx=back?double(back_w_)/back_width_:1,sy=back?double(back_h_)/back_height_:1,ox=0;
    if(back&&ui_rect_&&batch_rhw_){const double w=double(back_h_)*4/3;ox=std::floor((back_w_-w)/2);sx=w/back_width_;}
    const float vx=float(ox+soft_viewport_.x*sx),vy=float(soft_viewport_.y*sy),vw=float(soft_viewport_.w*sx),vh=float(soft_viewport_.h*sy);
    const D3D11_VIEWPORT vp{vx,vy,vw,vh,soft_viewport_.min_z,soft_viewport_.max_z};
    if(!bound_.viewport_set||std::memcmp(&vp,&bound_.viewport,sizeof vp)!=0){context_->RSSetViewports(1,&vp);bound_.viewport=vp;bound_.viewport_set=true;}
    const D3D11_RECT scissor{LONG(std::lround(vx)),LONG(std::lround(vy)),LONG(std::lround(vx+vw)),LONG(std::lround(vy+vh))};
    if(!bound_.scissor_set||std::memcmp(&scissor,&bound_.scissor,sizeof scissor)!=0){context_->RSSetScissorRects(1,&scissor);bound_.scissor=scissor;bound_.scissor_set=true;}
    D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.FrontCounterClockwise=FALSE;r.DepthClipEnable=TRUE;r.ScissorEnable=TRUE;
    r.MultisampleEnable=back&&msaa_live_?TRUE:FALSE;
    r.CullMode=render[RS_CULLMODE]==CULL_NONE?D3D11_CULL_NONE:render[RS_CULLMODE]==CULL_CCW?D3D11_CULL_BACK:D3D11_CULL_FRONT;
    const float bias=f(render[RS_DEPTHBIAS]),slope=f(render[RS_SLOPESCALEDEPTHBIAS]);r.DepthBias=INT(bias*16777215.f);r.SlopeScaledDepthBias=slope;
    if(!last_rasterizer_state_||std::memcmp(&r,&last_rasterizer_,sizeof r)!=0){
        auto& rs=rasterizer_states_[key(r)];if(!rs)check(device_->CreateRasterizerState(&r,&rs),"CreateRasterizerState");
        last_rasterizer_=r;last_rasterizer_state_=rs.Get();}
    if(bound_.rasterizer!=last_rasterizer_state_){context_->RSSetState(last_rasterizer_state_);bound_.rasterizer=last_rasterizer_state_;}
    D3D11_DEPTH_STENCIL_DESC d{};d.DepthEnable=render[RS_ZENABLE]!=0;d.DepthWriteMask=render[RS_ZWRITEENABLE]?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;
    d.DepthFunc=comparison(render[RS_ZFUNC]);d.StencilEnable=render[52]!=0;d.StencilReadMask=UINT8(render[58]);d.StencilWriteMask=UINT8(render[59]);
    d.FrontFace={stencil_op(render[53]),stencil_op(render[54]),stencil_op(render[55]),comparison(render[56])};d.BackFace=d.FrontFace;
    if(!last_depth_state_||std::memcmp(&d,&last_depth_,sizeof d)!=0){
        auto& ds=depth_states_[key(d)];if(!ds)check(device_->CreateDepthStencilState(&d,&ds),"CreateDepthStencilState");
        last_depth_=d;last_depth_state_=ds.Get();}
    if(bound_.depth!=last_depth_state_||bound_.stencil_ref!=(render[57]&255)){
        context_->OMSetDepthStencilState(last_depth_state_,render[57]&255);bound_.depth=last_depth_state_;bound_.stencil_ref=render[57]&255;}
    D3D11_BLEND_DESC b{};auto& t=b.RenderTarget[0];const bool separate=render[RS_SEPARATEALPHABLENDENABLE]!=0;
    t.BlendEnable=render[RS_ALPHABLENDENABLE]!=0;t.SrcBlend=blend_factor(render[RS_SRCBLEND]);t.DestBlend=blend_factor(render[RS_DESTBLEND]);t.BlendOp=blend_op(render[RS_BLENDOP]);
    t.SrcBlendAlpha=alpha_factor(render[separate?RS_SRCBLENDALPHA:RS_SRCBLEND]);t.DestBlendAlpha=alpha_factor(render[separate?RS_DESTBLENDALPHA:RS_DESTBLEND]);
    t.BlendOpAlpha=blend_op(render[separate?RS_BLENDOPALPHA:RS_BLENDOP]);t.RenderTargetWriteMask=UINT8(render[RS_COLORWRITEENABLE]&15);
    b.AlphaToCoverageEnable=g_transparency_supersampling&&back&&msaa_live_&&render[RS_ALPHATESTENABLE]!=0&&!t.BlendEnable?TRUE:FALSE;
    if(!last_blend_state_||std::memcmp(&b,&last_blend_,sizeof b)!=0){
        auto& bs=blend_states_[key(b)];if(!bs)check(device_->CreateBlendState(&b,&bs),"CreateBlendState");
        last_blend_=b;last_blend_state_=bs.Get();}
    if(bound_.blend!=last_blend_state_){context_->OMSetBlendState(last_blend_state_,nullptr,0xffffffffu);bound_.blend=last_blend_state_;}
}
void D3D11D3D9Device::bind_textures(){
    ID3D11ShaderResourceView* views[12];ID3D11SamplerState* samplers[6];
    for(unsigned s=0;s<6;++s){auto* o=object(textures[s]);auto* t=texture(textures[s]);
        views[s]=t&&!t->cube?t->srv.Get():dummy2d_srv_.Get();views[6+s]=t&&t->cube?t->srv.Get():dummycube_srv_.Get();
        const auto& p=sampler[s];const bool linear=p[SAMP_MINFILTER]!=1,mag=p[SAMP_MAGFILTER]!=1;const auto mip=p[SAMP_MIPFILTER];
        D3D11_SAMPLER_DESC d{};
        d.Filter=D3D11_FILTER((linear?D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT:0)|(mag?D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT:0)|(mip==2?D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR:0));
        // D3D11's anisotropic filter covers min, mag and mip together; D3D9 (Tweaks) sets only MINFILTER. So only
        // samplers already linear in both min and mag take it: a point-magnified texture (HUD, 2D) stays sharp (nene 27514).
        if(g_anisotropy>1u&&linear&&mag){d.Filter=D3D11_FILTER_ANISOTROPIC;d.MaxAnisotropy=g_anisotropy;}
        d.AddressU=address(p[SAMP_ADDRESSU]);d.AddressV=address(p[SAMP_ADDRESSV]);d.AddressW=address(p[SAMP_ADDRESSW]);
        // D3D9 takes any LOD bias; D3D11 refuses one outside -16..15.99 (or NaN) with E_INVALIDARG.
        const float bias=f(p[SAMP_MIPMAPLODBIAS]);
        d.MipLODBias=std::isfinite(bias)?std::clamp(bias,D3D11_MIP_LOD_BIAS_MIN,D3D11_MIP_LOD_BIAS_MAX):0.f;
        d.ComparisonFunc=D3D11_COMPARISON_NEVER;colour(p[SAMP_BORDERCOLOR],d.BorderColor);
        d.MinLOD=o&&o->kind==KTexture?float(std::min(p[SAMP_MAXMIPLEVEL],std::max(1u,o->texture.levels)-1)):0.f;d.MaxLOD=mip==0?d.MinLOD:FLT_MAX;
        if(last_sampler_state_[s]&&std::memcmp(&d,&last_sampler_[s],sizeof d)==0){samplers[s]=last_sampler_state_[s];continue;}
        auto& ss=sampler_states_[key(d)];
        if(!ss){
            const HRESULT hr=device_->CreateSamplerState(&d,&ss);
            if(FAILED(hr)){char t[256];std::snprintf(t,sizeof t,"CreateSamplerState (stage %u: filter %x address %u/%u/%u bias %08x lod %g..%g; D3D9 min/mag/mip %u/%u/%u)",
                s,unsigned(d.Filter),unsigned(d.AddressU),unsigned(d.AddressV),unsigned(d.AddressW),p[SAMP_MIPMAPLODBIAS],d.MinLOD,d.MaxLOD,
                p[SAMP_MINFILTER],p[SAMP_MAGFILTER],unsigned(mip));check(hr,t);}
        }
        samplers[s]=ss.Get();last_sampler_[s]=d;last_sampler_state_[s]=ss.Get();
    }
    if(!bound_.textures_set||std::memcmp(views,bound_.views,sizeof views)!=0){context_->PSSetShaderResources(0,12,views);std::memcpy(bound_.views,views,sizeof views);}
    if(!bound_.textures_set||std::memcmp(samplers,bound_.samplers,sizeof samplers)!=0){context_->PSSetSamplers(0,6,samplers);std::memcpy(bound_.samplers,samplers,sizeof samplers);}
    bound_.textures_set=true;
}
ID3D11PixelShader* D3D11D3D9Device::pixel_program(unsigned kind,const platform::PcShaderProgram* program){
    auto found=shader_programs_.find(pixel_shader);if(found!=shader_programs_.end())return found->second.Get();
    // Specialize the recovered instruction sequence, leaving material,
    // constants and textures dynamic (as the PS5 GL device does).
    if(program->blobs.size()>16)throw std::runtime_error("PS program exceeds 16 blobs");
    std::string source=kind==1?hlsl::ps11:hlsl::ps14;
    const std::string declaration="int4 program[4];int4 info;";
    const auto at=source.find(declaration);if(at==std::string::npos)throw std::runtime_error("PS constant layout mismatch");
    source.replace(at,declaration.size(),"int4 unused_program[4];int4 unused_info;");
    const auto insert=source.find('\n',source.find("cbuffer PsProgram"))+1;
    std::string fixed="static const int4 program[4]={";
    for(unsigned row=0;row<4;++row){if(row)fixed+=",";fixed+="int4(";
        for(unsigned col=0;col<4;++col){if(col)fixed+=",";const auto k=row*4+col;fixed+=k<program->blobs.size()?std::to_string(program->blobs[k]):"-1";}fixed+=")";}
    fixed+="};static const int4 info=int4("+std::to_string(program->blobs.size())+",0,0,0);\n";
    source.insert(insert,fixed);
    // The specialized interpreter at optimization level 3 takes long to compile, and a race meets dozens of programs
    // the first time: the code is kept in home()/shadercache under a hash of its source, so later runs load it.
    const auto started=std::chrono::steady_clock::now();
    ComPtr<ID3D11PixelShader> shader;
    const std::string path=shader_cache_path(source,"ps_4_1");
    const auto cached=read_shader_cache(path);
    const bool loaded=!cached.empty()&&SUCCEEDED(device_->CreatePixelShader(cached.data(),cached.size(),nullptr,&shader));
    if(!loaded){
        const auto code=compile(source,"ps_main","ps_4_1");
        check(device_->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader),"CreatePixelShader");
        write_shader_cache(path,code->GetBufferPointer(),code->GetBufferSize());
    }
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    ++(loaded?shader_stats_.loaded:shader_stats_.compiled);shader_stats_.ms+=ms;shader_stats_.max_ms=std::max(shader_stats_.max_ms,ms);
    std::printf("shader: program %zu %s in %.0f ms\n",shader_programs_.size()+1,loaded?"loaded":"compiled",ms);
    return (shader_programs_[pixel_shader]=shader).Get();
}
// The three-view spike: just before a draw (after pixel_pipeline's single viewport). A back-buffer draw that is not
// pretransformed and whose projection is a standard perspective goes to three viewports (the back buffer's thirds)
// through the geometry shader; any other back-buffer draw stays single on the centre third, where the game's 16:9
// camera put it. Nothing changes without g_pc_triple_corrections.
bool D3D11D3D9Device::triple_bind(bool rhw,bool standard){
    const auto corrections=platform::g_pc_triple_corrections;
    if(!corrections)return false;
    if(colour_target_!=BackBuffer){++triple_single_target;triple_unbind();return false;}
    if(rhw){++triple_single_rhw;triple_unbind();return false;}   // apply_state's ui_rect already keeps it in the centre
    const float fx=1.f/soft_viewport_.w,fy=1.f/soft_viewport_.h;
    if(standard&&(!triple_cached_||std::memcmp(triple_proj_.data(),transform_proj.data(),64)!=0||fx!=triple_fix_[0]||fy!=triple_fix_[1])){
        triple_proj_=transform_proj;triple_fix_[0]=fx;triple_fix_[1]=fy;triple_cached_=true;
        float c[52]{};triple_ok_=corrections(transform_proj.data(),c);
        if(triple_ok_){c[48]=fx;c[49]=fy;
            if(!triple_cb_){D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof c;b.Usage=D3D11_USAGE_DYNAMIC;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
                b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;check(device_->CreateBuffer(&b,nullptr,&triple_cb_),"CreateBuffer(three views)");}
            D3D11_MAPPED_SUBRESOURCE m{};check(context_->Map(triple_cb_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&m),"Map(three views)");
            std::memcpy(m.pData,c,sizeof c);context_->Unmap(triple_cb_.Get(),0);}
    }
    const double third=double(back_w_)/3.0,sx=third/back_width_,sy=double(back_h_)/back_height_;
    const auto viewport=[&](unsigned i){return D3D11_VIEWPORT{float(i*third+soft_viewport_.x*sx),float(soft_viewport_.y*sy),
        float(soft_viewport_.w*sx),float(soft_viewport_.h*sy),soft_viewport_.min_z,soft_viewport_.max_z};};
    const auto scissor=[](const D3D11_VIEWPORT& v){return D3D11_RECT{LONG(std::lround(v.TopLeftX)),LONG(std::lround(v.TopLeftY)),
        LONG(std::lround(v.TopLeftX+v.Width)),LONG(std::lround(v.TopLeftY+v.Height))};};
    bound_.viewport_set=bound_.scissor_set=false;   // apply_state sets its single viewport again for the next draw
    // The geometry shader takes triangles (nene 30802): a list or a strip, which is every draw that reaches here today
    // (draw_native takes only those, flush draws a list). Anything else would not be drawn with it bound.
    const bool triangles=bound_.topology==D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST||bound_.topology==D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    if(!triangles||!standard||!triple_ok_){   // a custom projection (or not triangles): the centre third only
        ++(triangles?triple_single_custom:triple_single_topology);triple_unbind();
        const auto v=viewport(1);const auto r=scissor(v);context_->RSSetViewports(1,&v);context_->RSSetScissorRects(1,&r);
        return false;
    }
    if(!triple_gs_){const auto code=compile(triple_gs_source(),"gs_main","gs_4_1");
        check(device_->CreateGeometryShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&triple_gs_),"CreateGeometryShader(three views)");}
    if(!triple_gs_bound_){context_->GSSetShader(triple_gs_.Get(),nullptr,0);ID3D11Buffer* b=triple_cb_.Get();context_->GSSetConstantBuffers(0,1,&b);
        triple_gs_bound_=true;}
    const D3D11_VIEWPORT v[3]{viewport(0),viewport(1),viewport(2)};const D3D11_RECT r[3]{scissor(v[0]),scissor(v[1]),scissor(v[2])};
    context_->RSSetViewports(3,v);context_->RSSetScissorRects(3,r);
    if(!triple_logged_){triple_logged_=true;
        std::printf("triple views: back buffer %ux%u, viewports x %.0f / %.0f / %.0f, width %.0f each, height %.0f\n",back_w_,back_h_,
                    double(v[0].TopLeftX),double(v[1].TopLeftX),double(v[2].TopLeftX),double(v[0].Width),double(v[0].Height));}
    ++triple_draws;++triple_frame_draws_;
    return true;
}
void D3D11D3D9Device::triple_unbind(){if(triple_gs_bound_){context_->GSSetShader(nullptr,nullptr,0);triple_gs_bound_=false;}}
// A vertex program's clip position is the standard one when its constants hold the current world x view x projection
// (as m4x4 reads it: four registers, the matrix transposed). The register is found once per program, then checked.
// Per draw that is one comparison of the transforms and, for a program without the matrix, a scan that stops at each
// register's first element (nene 30802); the product and its tolerance are computed again only when a transform changes.
bool D3D11D3D9Device::triple_native_standard(const void* program){
    if(!triple_xf_valid_||std::memcmp(triple_xf_.data(),transform_world.data(),64)!=0||std::memcmp(triple_xf_.data()+16,transform_view.data(),64)!=0||
       std::memcmp(triple_xf_.data()+32,transform_proj.data(),64)!=0){
        std::memcpy(triple_xf_.data(),transform_world.data(),64);std::memcpy(triple_xf_.data()+16,transform_view.data(),64);
        std::memcpy(triple_xf_.data()+32,transform_proj.data(),64);triple_xf_valid_=true;
        triple_wvp_=transform_wvp();double scale=0.;for(const float v:triple_wvp_)scale=std::max(scale,double(std::fabs(v)));
        triple_wvp_tolerance_=scale>0.&&std::isfinite(scale)?1e-4*scale:-1.;}
    if(!(triple_wvp_tolerance_>0.))return false;
    const auto& wvp=triple_wvp_;const double tolerance=triple_wvp_tolerance_;
    const auto at=[&](int base){   // !(<=): a NaN register is not a match
        for(int i=0;i<4;++i)for(int j=0;j<4;++j)
            if(!(std::fabs(double(vs_constants[std::size_t(base+i)][std::size_t(j)])-double(wvp[std::size_t(j*4+i)]))<=tolerance))return false;
        return true;};
    auto it=triple_wvp_base_.find(program);
    if(it!=triple_wvp_base_.end()&&it->second>=0&&at(it->second))return true;
    for(int base=0;base+4<=int(vs_constants.size());++base)if(at(base)){triple_wvp_base_[program]=base;return true;}
    if(it==triple_wvp_base_.end())triple_wvp_base_[program]=-1;
    return false;
}
void D3D11D3D9Device::flush(){
    if(batch_.empty()||count_only)return;
    pixel_pipeline();
    const std::size_t bytes=batch_.size()*sizeof(Vertex);
    if(bytes>vbo_bytes_){vbo_bytes_=std::max<std::size_t>(bytes,std::size_t(1)<<20);vbo_.Reset();
        D3D11_BUFFER_DESC d{};d.ByteWidth=UINT(vbo_bytes_);d.Usage=D3D11_USAGE_DYNAMIC;d.BindFlags=D3D11_BIND_VERTEX_BUFFER;d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        check(device_->CreateBuffer(&d,nullptr,&vbo_),"CreateBuffer(vertices)");}
    D3D11_MAPPED_SUBRESOURCE m{};check(context_->Map(vbo_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&m),"Map(vertices)");std::memcpy(m.pData,batch_.data(),bytes);context_->Unmap(vbo_.Get(),0);
    const UINT stride=sizeof(Vertex),offset=0;ID3D11Buffer* vb=vbo_.Get();
    if(bound_.vb[0]!=vb||bound_.vb_stride[0]!=stride||bound_.vb_offset[0]!=offset){
        context_->IASetVertexBuffers(0,1,&vb,&stride,&offset);bound_.vb[0]=vb;bound_.vb_stride[0]=stride;bound_.vb_offset[0]=offset;}
    if(bound_.layout!=layout_.Get()){context_->IASetInputLayout(layout_.Get());bound_.layout=layout_.Get();}
    if(bound_.topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST){context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);bound_.topology=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;}
    if(bound_.vs!=vertex_.Get()){context_->VSSetShader(vertex_.Get(),nullptr,0);bound_.vs=vertex_.Get();}
    triple_bind(batch_rhw_,true);   // the CPU's clip positions: world x view x projection, the standard form
    context_->Draw(UINT(batch_.size()),0);++frame_draws_;frame_vertices_+=unsigned(batch_.size());batch_.clear();batch_rhw_=false;
}
// The pixel side of a draw: target, states, textures, pixel program and its constants.
void D3D11D3D9Device::pixel_pipeline(){
    bind_target();apply_state();bind_textures();
    auto* ps=object(pixel_shader);const unsigned kind=ps&&ps->kind==4?(ps->program.version==0xffff0101u?1:2):0;
    ID3D11PixelShader* pixel=kind?pixel_program(kind,&ps->program):ffp_.Get();
    if(bound_.ps!=pixel){context_->PSSetShader(pixel,nullptr,0);bound_.ps=pixel;}
    uniform(0,ps_constants.data(),8*4*sizeof(float));
    if(kind){struct {std::int32_t program[16],info[4],kinds[8];float bump[8][4],lum[2][4];} p{};
        std::fill(std::begin(p.program),std::end(p.program),-1);
        for(unsigned k=0;k<ps->program.blobs.size();++k)p.program[k]=int(ps->program.blobs[k]);p.info[0]=int(ps->program.blobs.size());
        for(unsigned s=0;s<8;++s){auto* t=object(textures[s]);p.kinds[s]=t&&t->kind==KTexture&&t->texture.cube?1:0;for(unsigned k=0;k<4;++k)p.bump[s][k]=f(stage[s][7+k]);}
        for(unsigned s=0;s<4;++s){p.lum[s>>1][(s&1)*2]=f(stage[s][22]);p.lum[s>>1][(s&1)*2+1]=f(stage[s][23]);}uniform(1,&p,sizeof p);
    }else{struct {std::int32_t op[4][4],arg[4][4],misc[4][4];float konst[4][4],tfactor[4];std::int32_t flags[4];} p{};
        for(unsigned s=0;s<4;++s){const auto& t=stage[s];p.op[s][0]=int(t[1]);p.op[s][1]=int(t[2]);p.op[s][2]=int(t[3]);p.op[s][3]=int(t[4]);
            p.arg[s][0]=int(t[5]);p.arg[s][1]=int(t[6]);p.arg[s][2]=int(t[26]);p.arg[s][3]=int(t[27]);auto* tx=object(textures[s]);
            p.misc[s][0]=int(t[28]);p.misc[s][1]=int(t[11]&3);p.misc[s][2]=tx&&tx->kind==KTexture&&!tx->texture.cube;p.misc[s][3]=tx&&tx->kind==KTexture&&tx->texture.cube;
            colour(t[32],p.konst[s]);}
        colour(render[RS_TEXTUREFACTOR],p.tfactor);p.flags[0]=render[RS_SPECULARENABLE]!=0;uniform(1,&p,sizeof p);}
    float fixed[12]{};colour(render[RS_FOGCOLOR],fixed);fixed[3]=f(render[RS_FOGEND]);
    if(render[RS_FOGENABLE])fixed[4]=render[RS_FOGTABLEMODE]?float(render[RS_FOGTABLEMODE]):4.f;
    fixed[5]=f(render[RS_FOGSTART]);fixed[6]=f(render[RS_FOGDENSITY]);fixed[7]=1;fixed[8]=render[RS_ALPHATESTENABLE]?1.f:0.f;fixed[9]=float(render[RS_ALPHAREF]&255);fixed[10]=float(render[RS_ALPHAFUNC]);uniform(2,fixed,sizeof fixed);
    if(!bound_.ps_constants){ID3D11Buffer* constants[3]{ubo_[0].Get(),ubo_[1].Get(),ubo_[2].Get()};context_->PSSetConstantBuffers(0,3,constants);bound_.ps_constants=true;}
}
}
