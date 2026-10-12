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
void set_anisotropic_filtering(unsigned level);   // [Graphics] AnisotropicFiltering: 0/1 the game's filtering, 2..16 the level
void set_transparency_supersampling(bool on);     // [Graphics] TransparencySupersampling: alpha-to-coverage under MSAA
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
    // aspect: the rectangle's width per unit of height (4:3 by default; UIScalingMode 0 on a span: its centre panel's).
    void set_ui_rect(bool on,double aspect=4./3.){ui_rect_=on;ui_aspect_=aspect>0.?aspect:4./3.;}
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
    // The three-view spike (pc_screen.hpp g_pc_triple_corrections): a geometry shader that emits each back-buffer
    // triangle into three viewports with the panel's clip correction; corrections cached per projection.
    ComPtr<ID3D11GeometryShader> triple_gs_;ComPtr<ID3D11Buffer> triple_cb_;
    std::array<float,16> triple_proj_{};float triple_fix_[2]{};bool triple_cached_{},triple_ok_{},triple_gs_bound_{},triple_logged_{};
    // Corrections already computed, by projection and pixel-centre fix (six, replaced in turn): draws that alternate
    // between projections reuse them instead of computing again (nene 35084).
    struct TripleCorrections { std::array<float,16> p{};float fix[2]{};bool used{},ok{};float c[52]{}; };
    std::array<TripleCorrections,6> triple_corrections_{};unsigned triple_corrections_next_{};
    // Per vertex program: the constant register of its position matrix (-1: none found) and whether that matrix is the
    // current world x view x projection or only some affine matrix x the current projection; the outcomes already
    // logged (each distinct one once, at most four, encoded base*2+affine or -1), how often the outcome changed, and the
    // constant and transform generations of its last failed scan (unchanged since: no rescan; nene 34449).
    struct TripleProgram { int base{-1};bool affine{};unsigned logged_n{};int logged[4]{};std::uint64_t changes{},failed_constants{~0ull},failed_xf{~0ull}; };
    std::map<const void*,TripleProgram> triple_programs_;
    // The world x view x projection those registers are compared with, its tolerance, and column 3 of the inverse
    // projection (the affine test): computed again only when a transform changes (world, view, projection as last seen).
    std::array<float,48> triple_xf_{};std::array<float,16> triple_wvp_{};double triple_wvp_tolerance_{};bool triple_xf_valid_{};
    std::array<double,4> triple_pinv_col3_{};bool triple_pinv_ok_{};
    std::uint64_t triple_xf_generation_{},triple_constants_generation_{};   // a transform change; a vertex constants upload
    std::vector<std::array<float,16>> triple_refused_logged_;   // refused projections already logged (at most 4)
    unsigned triple_frame_draws_{};
    // triple_native_standard: 0 no matrix of the current projection, 1 world x view x projection, 2 an affine matrix x it.
    bool triple_bind(bool rhw,bool standard,bool affine=false);void triple_unbind();int triple_native_standard(const void* program,const std::string& key);
public:
    // Three-view counts (runtime.log at exit): draws into three viewports (of them, through a vertex program whose
    // matrix is an affine one x the projection, not the current world x view x projection); draws kept single on the
    // centre panel because pretransformed (rhw: HUD, menus, screen-space particles), a vertex program with no matrix of
    // the current projection in its constants, a projection the rig refuses, not triangles (the geometry shader takes
    // only those), or not drawn into the back buffer; frames with at least one three-viewport draw.
    std::uint64_t triple_draws{},triple_draws_affine{},triple_single_rhw{},triple_single_unmatched{},triple_single_refused{},
        triple_single_topology{},triple_single_target{},triple_frames{};
private:
    bool native_enabled_{true};
    struct { std::uint64_t draws{},cpu_draws{},uploads{},upload_bytes{}; unsigned compiled{},loaded{},failed{}; double ms{};
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
    double ui_aspect_{4./3.};
};
}
