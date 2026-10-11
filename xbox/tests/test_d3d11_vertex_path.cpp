// The D3D11 device's GPU vertex path against its CPU path (OR2_D3D11_CPU_VERTEX), headless on WARP, after
// ps5/tests/test_vulkan.cpp indexed_cases: recovered and linked vertex programs, two streams with offsets,
// D3DCOLOR / UBYTE4 / FLOAT2 inputs, a base vertex, lists and strips, depth clipping, a partly locked stream
// (the GPU copy takes only that range), changed constants and a stream rewritten between two draws.
#include "d3d11_d3d9.hpp"
#include "platform/pc_vertex_shader_setup.hpp"
#include "system/exe_image.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace outrun::platform;
using namespace outrun::platform::d3d9;
using outrun::xbox_runtime::D3D11D3D9Device;
using Microsoft::WRL::ComPtr;

static int failures=0,checks=0;
static void check(bool ok,const char* what){++checks;if(!ok){++failures;std::printf("FAIL %s\n",what);}}

static std::vector<std::uint8_t> readback(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11ShaderResourceView* view){
    ComPtr<ID3D11Resource> resource;view->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;resource.As(&texture);
    D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;if(FAILED(device->CreateTexture2D(&d,nullptr,&staging)))return {};
    context->CopyResource(staging.Get(),texture.Get());
    D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m)))return {};
    std::vector<std::uint8_t> out(std::size_t(d.Width)*d.Height*4);
    for(unsigned y=0;y<d.Height;++y)std::memcpy(out.data()+std::size_t(y)*d.Width*4,static_cast<const std::uint8_t*>(m.pData)+std::size_t(y)*m.RowPitch,d.Width*4);
    context->Unmap(staging.Get(),0);return out;
}
// Pixels whose 3x3 neighbourhood is uniform in the CPU image must match within one level; edge pixels may differ
// (CPU-transformed and GPU-transformed vertices round subpixel coverage differently).
// Each device has its own D3D11 device and context, as in the game: a device caches what it has bound on its
// context, so two of them must not share one.
struct Gpu {ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;};
static Gpu g_gpu,g_cpu;
static void compare(D3D11D3D9Device& gpu,D3D11D3D9Device& cpu,const char* what,bool expect_drawn=true){
    const auto a=readback(g_gpu.device.Get(),g_gpu.context.Get(),gpu.back_texture()),b=readback(g_cpu.device.Get(),g_cpu.context.Get(),cpu.back_texture());
    check(a.size()==64u*48u*4u&&b.size()==a.size(),"read back both back buffers");if(a.size()!=b.size()||a.empty())return;
    unsigned stable=0,bad=0,drawn=0;
    for(unsigned y=1;y<47;++y)for(unsigned x=1;x<63;++x){
        const auto at=(y*64+x)*4;bool interior=true;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(std::memcmp(b.data()+at,b.data()+((int(y)+dy)*64+int(x)+dx)*4,4))interior=false;
        if(b[at]|b[at+1]|b[at+2])++drawn;
        if(!interior)continue;++stable;
        for(unsigned c=0;c<4;++c)if(std::abs(int(a[at+c])-int(b[at+c]))>1){++bad;break;}
    }
    if(bad)std::printf("  %s: %u of %u stable pixels differ\n",what,bad,stable);
    check(bad==0,what);check(stable>500,"the comparison covers enough stable pixels");
    if(expect_drawn)check(drawn>100,"the draw produced visible pixels");
}

int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    // The recovered vertex programs are read from the player's verified EXE image (OR2_EXE: its OR2006C2C.cache).
    if(!exe_image_autoload()){std::printf("SKIP: set OR2_EXE to the game's OR2006C2C.cache (the vertex programs come from it)\n");return 77;}
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_10_1};
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,1,D3D11_SDK_VERSION,&g_gpu.device,nullptr,&g_gpu.context))||
       FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,1,D3D11_SDK_VERSION,&g_cpu.device,nullptr,&g_cpu.context))){
        std::printf("FAIL no WARP D3D11 device at feature level 10.1\n");return 1;}
    _putenv("OR2_D3D11_CPU_VERTEX=");D3D11D3D9Device gpu(g_gpu.device.Get(),g_gpu.context.Get(),64,48);
    _putenv("OR2_D3D11_CPU_VERTEX=1");D3D11D3D9Device cpu(g_cpu.device.Get(),g_cpu.context.Get(),64,48);
    _putenv("OR2_D3D11_CPU_VERTEX=");
    D3D11D3D9Device* devices[]{&gpu,&cpu};
    struct Position {float x,y,z,w;unsigned bytes;float uv[2];};
    std::vector<Position> positions(65538);
    // Vertex 0 is a real vertex here: one with w = 0 is clipped homogeneously by the GPU (as D3D9 hardware) but
    // dropped with its triangle by the CPU path, which a pixel comparison cannot reconcile.
    positions[0]={-.83f,.05f,.5f,1,0x04030201,{.3f,.5f}};
    positions[1]={-.91f,-.87f,.5f,1,0x04030201,{.1f,.2f}};
    positions[2]={ .87f,-.87f,.5f,1,0x04030201,{.9f,.2f}};
    positions[3]={-.91f, .89f,.5f,1,0x04030201,{.1f,.8f}};
    positions[4]={ .87f, .89f,.5f,1,0x04030201,{.9f,.8f}};
    positions[65536]=positions[4];
    std::vector<unsigned> colours(positions.size(),0xff336699);
    const std::uint16_t indices[]{123,456,0,1,2,2,1,3,0,1,2,3,0,1,65535};
    unsigned vb[2]{},colour[2]{},ib[2]{};
    const PcVertexElement elements[]{{0,0,3,0,0,0},{1,0,4,0,10,0},{0,16,5,0,2,0},{0,20,1,0,5,0},{PcDeclEndStream,0,17,0,0,0}};
    for(unsigned i=0;i<2;++i){auto& d=*devices[i];d.reset_state();d.set_fvf(0);
        d.set_render_state(RS_CULLMODE,CULL_NONE);d.set_texture_stage_state(0,1,2);d.set_texture_stage_state(0,2,0);
        d.set_texture_stage_state(0,4,2);d.set_texture_stage_state(0,5,0);
        d.set_render_state(RS_TEXTUREFACTOR,0xff336699);d.set_texture_stage_state(0,2,3);d.set_texture_stage_state(0,5,3);
        vb[i]=d.create_vertex_buffer(unsigned(positions.size()*sizeof(Position)+8),0,0,0);
        std::memcpy(d.lock(vb[i],8,unsigned(positions.size()*sizeof(Position)),0),positions.data(),positions.size()*sizeof(Position));d.unlock(vb[i]);
        colour[i]=d.create_vertex_buffer(unsigned(colours.size()*4+4),0,0,0);
        std::memcpy(d.lock(colour[i],4,unsigned(colours.size()*4),0),colours.data(),colours.size()*4);d.unlock(colour[i]);
        ib[i]=d.create_index_buffer(sizeof indices,0,0,0);std::memcpy(d.lock(ib[i],0,sizeof indices,0),indices,sizeof indices);d.unlock(ib[i]);
        d.set_stream_source(0,vb[i],8,sizeof(Position));d.set_stream_source(1,colour[i],4,4);d.set_indices(ib[i]);
        d.set_vertex_declaration(d.create_vertex_declaration(elements));
        for(unsigned row=0;row<4;++row){float c[4]{};c[row]=1;d.set_vertex_shader_constant_f(64+row,c,1);}
        const float one[]{1,1,1,1};d.set_vertex_shader_constant_f(0,one,1);
    }
    auto draw=[&](unsigned type,int base,unsigned start,unsigned count,const char* what,bool expect_drawn=true){
        for(auto* d:devices){d->begin_frame();d->clear(7,0xff000000,1,0);check(d->draw_indexed_primitive(type,base,0,4,start,count)==0,"indexed draw succeeds");}
        compare(gpu,cpu,what,expect_drawn);
    };
    const auto native_before=gpu.native_draws();
    for(unsigned config:{8u,8u|(1u<<9),0xffffffffu}){
        const auto tokens=config==0xffffffffu?pc_shader_tokens(0x621e10):link_vertex_shader_40e140(config);
        for(auto* d:devices){d->set_texture_stage_state(0,2,config==0xffffffffu?0:3);d->set_texture_stage_state(0,5,config==0xffffffffu?0:3);
            const float one[]{1,1,1,1};d->set_vertex_shader_constant_f(8,one,1);
            const auto shader=d->create_vertex_shader(tokens.data());check(shader!=0,"recovered vertex program created");d->set_vertex_shader(shader);}
        draw(4,1,2,2,"list, base vertex 1");draw(5,1,8,2,"strip, base vertex 1");draw(4,1,12,1,"list from index 12");draw(4,0,2,2,"list, base vertex 0");
        for(auto* d:devices){const float shift[]{1,0,0,.15f};d->set_vertex_shader_constant_f(64,shift,1);}
        draw(4,1,2,2,"changed constants");
        for(auto* d:devices){const float reset[]{1,0,0,0};d->set_vertex_shader_constant_f(64,reset,1);}
        for(float z:{-.6f,1.6f,.5f}){   // one vertex in front of / behind the depth range: clipping
            positions[1].z=z;
            for(auto i=0;i<2;++i){std::memcpy(devices[i]->lock(vb[i],8+sizeof(Position),sizeof(Position),0),&positions[1],sizeof(Position));devices[i]->unlock(vb[i]);}
            draw(4,1,2,2,"partly locked stream, depth clipping (list)");draw(5,1,8,2,"partly locked stream, depth clipping (strip)");
        }
        if(config==0xffffffffu){   // a stream rewritten between two draws of one frame: each draw sees its own contents
            for(auto* d:devices){d->begin_frame();d->clear(7,0xff000000,1,0);}
            for(unsigned half=0;half<2;++half){
                std::fill(colours.begin(),colours.end(),half?0xff00ff00:0xffff0000);
                for(unsigned i=0;i<2;++i){auto& d=*devices[i];d.set_viewport(half*32,0,32,48,0,1);
                    std::memcpy(d.lock(colour[i],4,unsigned(colours.size()*4),0),colours.data(),colours.size()*4);d.unlock(colour[i]);
                    check(d.draw_indexed_primitive(5,1,0,4,8,2)==0,"mutable stream draw succeeds");}
            }
            compare(gpu,cpu,"stream rewritten between two draws");
            for(auto* d:devices)d->set_viewport(0,0,64,48,0,1);
        }
    }
    // One frame of several draws: the device skips calls that set what is already bound, so state must carry over
    // correctly between draws. Quadrant viewports, list and strip, changed constants, alpha blending on and off,
    // and a CPU-path draw (index 0xFFFF) between GPU-path draws.
    for(auto* d:devices){d->begin_frame();d->clear(7,0xff000000,1,0);}
    struct Step {unsigned x,y,type,start,count;int base;float shift;bool blend;};
    const Step steps[]{{0,0,4,2,2,1,0.f,false},{32,0,5,8,2,1,.1f,false},{0,24,4,12,1,1,0.f,true},{32,24,4,2,2,0,-.1f,true},{0,0,5,8,2,1,.05f,false}};
    for(const auto& st:steps)for(auto* d:devices){
        d->set_viewport(st.x,st.y,32,24,0,1);
        const float shift[]{1,0,0,st.shift};d->set_vertex_shader_constant_f(64,shift,1);
        d->set_render_state(RS_ALPHABLENDENABLE,st.blend?1u:0u);d->set_render_state(RS_SRCBLEND,5);d->set_render_state(RS_DESTBLEND,6);
        check(d->draw_indexed_primitive(st.type,st.base,0,4,st.start,st.count)==0,"multi-draw frame draw succeeds");
    }
    compare(gpu,cpu,"several draws in one frame (bound-state cache)");
    for(auto* d:devices){d->set_viewport(0,0,64,48,0,1);d->set_render_state(RS_ALPHABLENDENABLE,0);const float reset[]{1,0,0,0};d->set_vertex_shader_constant_f(64,reset,1);}
    check(gpu.native_draws()>native_before+20,"the GPU device drew through the GPU vertex path");
    check(cpu.native_draws()==0,"OR2_D3D11_CPU_VERTEX keeps every draw on the CPU path");
    std::printf("%s: %d checks, %d failures (GPU vertex draws %llu)\n",failures?"FAIL":"PASS",checks,failures,(unsigned long long)gpu.native_draws());
    return failures?1:0;
}
