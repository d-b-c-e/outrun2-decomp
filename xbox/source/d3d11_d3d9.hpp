#pragma once
#include "platform/pc_soft_d3d9.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace outrun::xbox_runtime {
using Microsoft::WRL::ComPtr;
// Direct3D 11 back end of the D3D9 device, the same split as the PS5 GL
// device (ps5/source/gl_d3d9.cpp): the recovered vertex programs and the D3D9
// resource/state ownership stay in PcSoftD3D9Device; raster, pixel shaders,
// depth/stencil, blend, render targets and reflections use D3D11 (feature
// level 10.1, the Xbox One UWP limit). The pixel shaders are the GLSL ones
// translated by xbox/tools/generate_hlsl_shaders.py.
class D3D11D3D9Device final:public platform::PcSoftD3D9Device {
public:
    D3D11D3D9Device(ID3D11Device* device,ID3D11DeviceContext* context,unsigned width,unsigned height);
    ~D3D11D3D9Device()override;
    std::uint32_t draw_indexed_primitive(std::uint32_t,std::int32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t)override;
    void draw_primitive_up(std::uint32_t,std::uint32_t,const void*,std::uint32_t)override;
    void clear(std::uint32_t,std::uint32_t,float,std::uint32_t)override;
    void unlock_rect(std::uint32_t)override;
    void release(std::uint32_t)override;
    std::uint8_t* lock(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t)override;
    void begin_frame();void end_frame();
    // The frame the game drew (RGBA8), for the presenter.
    ID3D11ShaderResourceView* back_texture()const{return back_srv_.Get();}
    std::uint64_t gpu_nanoseconds()const{return 0;}   // no GPU timer yet (overlay shows 0)
    // Port enhancements (Options > Settings), as the macOS Metal and PS5 GL
    // devices: the back buffer at any pixel size (the logical PC screen is
    // stretched to it), the 2D layer kept in the centred 4:3 rectangle of a
    // wider back buffer (pretransformed draws), and the 3D scene's
    // antialiasing: MSAA (drawn multisampled, resolved when the 2D layer
    // starts) or FXAA over it then (finish_scene, PC 42D710; end_frame when no
    // 2D layer came). The setters apply between frames.
    void resize_back_buffer(unsigned width,unsigned height);
    void set_ui_rect(bool on){ui_rect_=on;}
    void set_fxaa(bool on){fxaa_=on;}
    void set_msaa(unsigned samples){msaa_=samples==2||samples==4?samples:1;}
    void finish_scene();
    std::uint32_t fxaa_passes{},msaa_resolves{};
    std::uint64_t native_draws()const{return native_stats_.draws;}   // draws taken by the GPU vertex path
private:
    struct Texture { ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> srv;bool cube{};unsigned levels{}; };
    void raster(const Vertex*,unsigned)override;
    void flush();void pixel_pipeline();void bind_target();void apply_state();void bind_textures();
    // The GPU vertex path (the PS5 native path's design): DrawIndexedPrimitive with a vertex shader runs the game's
    // vertex program on the GPU (hlsl::native_vs specialized per blob list and input formats) from GPU copies of
    // its vertex and index buffers, updated over the byte range locked since the last draw. Anything else, and
    // every draw with OR2_D3D11_CPU_VERTEX set, keeps the CPU path (PcSoftD3D9Device::draw_indexed_primitive).
    struct GpuBuffer { ComPtr<ID3D11Buffer> buffer;std::size_t bytes{},dirty_lo{~std::size_t(0)},dirty_hi{}; };
    struct NativeProgram { ComPtr<ID3D11VertexShader> shader;std::vector<std::uint8_t> code; };
    bool draw_native(std::uint32_t type,std::int32_t base,std::uint32_t start,std::uint32_t count);
    ID3D11Buffer* gpu_buffer(std::uint32_t handle);
    NativeProgram* native_program(const std::string& key,const platform::PcShaderProgram& program,unsigned provided,unsigned colours,unsigned bytes4);
    std::map<std::uint32_t,GpuBuffer> gpu_buffers_;                       // by D3D9 buffer handle
    std::map<std::uint32_t,std::map<std::uint64_t,std::uint32_t>> index_ranges_;   // index buffer -> (start, count) -> low | high << 16
    std::map<std::string,NativeProgram> native_programs_;                 // by blob list and input formats
    std::map<std::string,ComPtr<ID3D11InputLayout>> native_layouts_;
    std::set<std::string> native_refused_layouts_;
    ComPtr<ID3D11Buffer> vs_constants_;std::vector<float> vs_shadow_;      // C[256] and the pixel-centre fix (VS b0)
    bool native_enabled_{true};
    struct { std::uint64_t draws{},cpu_draws{},uploads{},upload_bytes{}; unsigned compiled{},loaded{}; double ms{};
             std::map<std::string,std::uint64_t> refused; } native_stats_;
    Texture* texture(std::uint32_t);
    ID3D11RenderTargetView* colour_view();
    ID3D11DepthStencilView* depth_view(std::uint32_t);
    void uniform(unsigned slot,const void*,std::size_t);
    ID3D11PixelShader* pixel_program(unsigned kind,const platform::PcShaderProgram*);
    ID3D11Device* device_;ID3D11DeviceContext* context_;
    std::vector<Vertex> batch_;
    std::map<std::uint32_t,Texture> textures_;
    std::map<std::uint64_t,ComPtr<ID3D11RenderTargetView>> colour_views_;   // texture<<32 | face<<16 | level
    std::map<std::uint32_t,ComPtr<ID3D11DepthStencilView>> depth_views_;
    ComPtr<ID3D11Texture2D> back_colour_tex_,back_depth_tex_,dummy2d_,dummycube_;
    ComPtr<ID3D11ShaderResourceView> back_srv_,dummy2d_srv_,dummycube_srv_;
    ComPtr<ID3D11RenderTargetView> back_rtv_;ComPtr<ID3D11DepthStencilView> back_dsv_;
    ComPtr<ID3D11VertexShader> vertex_;ComPtr<ID3D11InputLayout> layout_;ComPtr<ID3D11PixelShader> ffp_;
    ComPtr<ID3D11Buffer> vbo_,ubo_[3];std::size_t vbo_bytes_{};
    // The occlusion queries of the diagnostic pixel count: rotated and collected when ready, never waited for
    // (except with OR2_XBOX_DRAW_STATS, whose log prints each frame's count).
    ComPtr<ID3D11Query> queries_[3];unsigned query_index_{};bool query_pending_[3]{},query_live_{};
    void collect_queries(bool wait_all);
    // What this frame has bound (see begin_frame): a call that would set the same is skipped.
    struct Bound {
        bool valid{},target{},ps_constants{},vs_constants{};
        ID3D11RenderTargetView* rtv{};ID3D11DepthStencilView* dsv{};
        D3D11_VIEWPORT viewport{};D3D11_RECT scissor{};bool viewport_set{},scissor_set{};
        ID3D11RasterizerState* rasterizer{};ID3D11DepthStencilState* depth{};UINT stencil_ref{};ID3D11BlendState* blend{};
        bool textures_set{};ID3D11ShaderResourceView* views[12]{};ID3D11SamplerState* samplers[6]{};
        ID3D11PixelShader* ps{};ID3D11VertexShader* vs{};ID3D11InputLayout* layout{};D3D11_PRIMITIVE_TOPOLOGY topology{};
        ID3D11Buffer* vb[16]{};UINT vb_stride[16]{},vb_offset[16]{};ID3D11Buffer* ib{};
    } bound_;
    std::vector<std::uint8_t> uniform_shadow_[3];
    unsigned frame_draws_{},frame_vertices_{};
    std::map<std::uint32_t,ComPtr<ID3D11PixelShader>> shader_programs_;
    struct {unsigned compiled{},loaded{};double ms{},max_ms{};} shader_stats_;   // pixel programs: compiled vs from the disk cache
    std::map<std::string,ComPtr<ID3D11RasterizerState>> rasterizer_states_;
    std::map<std::string,ComPtr<ID3D11DepthStencilState>> depth_states_;
    std::map<std::string,ComPtr<ID3D11BlendState>> blend_states_;
    std::map<std::string,ComPtr<ID3D11SamplerState>> sampler_states_;
    // The last description of each state and its object (owned by the maps above): consecutive draws mostly repeat
    // them, and a map lookup builds a key string. A null object means nothing cached yet.
    D3D11_RASTERIZER_DESC last_rasterizer_{};ID3D11RasterizerState* last_rasterizer_state_{};
    D3D11_DEPTH_STENCIL_DESC last_depth_{};ID3D11DepthStencilState* last_depth_state_{};
    D3D11_BLEND_DESC last_blend_{};ID3D11BlendState* last_blend_state_{};
    D3D11_SAMPLER_DESC last_sampler_[6]{};ID3D11SamplerState* last_sampler_state_[6]{};
    void allocate_back(unsigned width,unsigned height);
    void ensure_msaa();
    unsigned back_w_{},back_h_{},msaa_{1},msaa_samples_{};
    ComPtr<ID3D11Texture2D> msaa_colour_tex_,msaa_depth_tex_,fxaa_copy_tex_;
    ComPtr<ID3D11RenderTargetView> msaa_rtv_;ComPtr<ID3D11DepthStencilView> msaa_dsv_;ComPtr<ID3D11ShaderResourceView> fxaa_copy_srv_;
    ComPtr<ID3D11VertexShader> fxaa_vertex_;ComPtr<ID3D11PixelShader> fxaa_pixel_;ComPtr<ID3D11SamplerState> fxaa_sampler_;
    ComPtr<ID3D11RasterizerState> fxaa_rasterizer_;ComPtr<ID3D11DepthStencilState> fxaa_depth_;ComPtr<ID3D11BlendState> fxaa_blend_;
    bool msaa_live_{},ui_rect_{},fxaa_{},scene_finished_{},batch_rhw_{};
};
}
