// The D3D11 device's GPU vertex path against its CPU path (OR2_D3D11_CPU_VERTEX), headless on WARP, after
// ps5/tests/test_vulkan.cpp indexed_cases: recovered and linked vertex programs, two streams with offsets,
// D3DCOLOR / UBYTE4 / FLOAT2 inputs, a base vertex, lists and strips, depth clipping, a partly locked stream
// (the GPU copy takes only that range), changed constants and a stream rewritten between two draws. Then the
// background shader compiles from a cold disk cache against the synchronous device (see the last section).
#include "d3d11_d3d9.hpp"
#include "platform/pc_vertex_shader_setup.hpp"
#include "runtime/game_host.hpp"
#include "system/exe_image.hpp"
#include <cmath>
#include <filesystem>
#include <string>
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
static std::vector<std::uint8_t> image(Gpu& g,D3D11D3D9Device& d){return readback(g.device.Get(),g.context.Get(),d.back_texture());}
// a against the reference b on b's stable pixels (see above).
static void compare_images(const std::vector<std::uint8_t>& a,const std::vector<std::uint8_t>& b,const char* what,bool expect_drawn=true){
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
static void compare(D3D11D3D9Device& gpu,D3D11D3D9Device& cpu,const char* what,bool expect_drawn=true){
    compare_images(image(g_gpu,gpu),image(g_cpu,cpu),what,expect_drawn);
}
// Every byte equal; returns whether the reference drew something (some programs leave the frame black here).
static bool compare_exact(const std::vector<std::uint8_t>& a,const std::vector<std::uint8_t>& b,const char* what){
    check(a.size()==64u*48u*4u&&b.size()==a.size(),"read back both back buffers");if(a.size()!=b.size()||a.empty())return false;
    unsigned bad=0,drawn=0;
    for(std::size_t at=0;at<a.size();at+=4){if(std::memcmp(a.data()+at,b.data()+at,4))++bad;if(b[at]|b[at+1]|b[at+2])++drawn;}
    if(bad)std::printf("  %s: %u pixels differ\n",what,bad);
    check(bad==0,what);return drawn>100;
}

int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    // The recovered vertex programs are read from the player's verified EXE image (OR2_EXE: its OR2006C2C.cache).
    if(!exe_image_autoload()){std::printf("SKIP: set OR2_EXE to the game's OR2006C2C.cache (the vertex programs come from it)\n");return 77;}
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_10_1};
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,1,D3D11_SDK_VERSION,&g_gpu.device,nullptr,&g_gpu.context))||
       FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,1,D3D11_SDK_VERSION,&g_cpu.device,nullptr,&g_cpu.context))){
        std::printf("FAIL no WARP D3D11 device at feature level 10.1\n");return 1;}
    // These two compile in the draw (OR2_D3D11_SYNC_COMPILE), as the device did before the background compiler.
    _putenv("OR2_D3D11_SYNC_COMPILE=1");
    _putenv("OR2_D3D11_CPU_VERTEX=");D3D11D3D9Device gpu(g_gpu.device.Get(),g_gpu.context.Get(),64,48);
    _putenv("OR2_D3D11_CPU_VERTEX=1");D3D11D3D9Device cpu(g_cpu.device.Get(),g_cpu.context.Get(),64,48);
    _putenv("OR2_D3D11_CPU_VERTEX=");_putenv("OR2_D3D11_SYNC_COMPILE=");
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
    // Background compiles from a cold disk cache (romi 24424, nene 24439). The synchronous device draws each frame
    // first, in its own cold cache folder; the background device then draws the same frames in another one, with no
    // compile of the first device running (home() is read by the cache writer). Frame 1: a new vertex program, so the
    // CPU vertex path draws while it compiles (stable pixels match). After the compiler drains, the GPU vertex path.
    // Frame 2: new pixel programs over the compiled vertex program, so the generic interpreter draws while they
    // compile: exactly the specialized program's pixels, since only the pixel program differs and specialization
    // only folds constants. After draining, the specialized programs, exactly again.
    {
        auto& home=outrun::runtime::home();const std::string saved=home;
        const std::string base=(std::filesystem::temp_directory_path()/("or2-async-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()))).string();
        const std::string cold[2]{base+"-sync",base+"-background"};
        Gpu g[2];
        for(auto& x:g)if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,1,D3D11_SDK_VERSION,&x.device,nullptr,&x.context))){std::printf("FAIL no WARP device\n");return 1;}
        // ps.1.1 (the fixed pixel shaders, 40AC60, and 95AF9C) and ps.1.4 (from the 61EDE8.. table).
        const std::uint32_t pixel_programs[]{0x623cf0,0x623d40,0x623da8,0x623bb0,0x623c88,0x623830,0x61ee0c,0x61ee4c,0x61f218};
        constexpr unsigned PixelCount=sizeof pixel_programs/sizeof pixel_programs[0];
        std::vector<std::uint8_t> sync_images[2+PixelCount];
        for(unsigned which=0;which<2;++which){
            home=cold[which];
            _putenv(which==0?"OR2_D3D11_SYNC_COMPILE=1":"OR2_D3D11_SYNC_COMPILE=");
            D3D11D3D9Device d(g[which].device.Get(),g[which].context.Get(),64,48);_putenv("OR2_D3D11_SYNC_COMPILE=");
            check(d.background_compiles()==(which==1),which?"the background compiler is on by default":"OR2_D3D11_SYNC_COMPILE compiles in the draw");
            d.reset_state();d.set_fvf(0);d.set_render_state(RS_CULLMODE,CULL_NONE);
            d.set_texture_stage_state(0,1,2);d.set_texture_stage_state(0,2,3);d.set_texture_stage_state(0,4,2);d.set_texture_stage_state(0,5,3);
            d.set_render_state(RS_TEXTUREFACTOR,0xff336699);
            const auto v=d.create_vertex_buffer(unsigned(positions.size()*sizeof(Position)+8),0,0,0);
            std::memcpy(d.lock(v,8,unsigned(positions.size()*sizeof(Position)),0),positions.data(),positions.size()*sizeof(Position));d.unlock(v);
            std::fill(colours.begin(),colours.end(),0xff336699);
            const auto c=d.create_vertex_buffer(unsigned(colours.size()*4+4),0,0,0);
            std::memcpy(d.lock(c,4,unsigned(colours.size()*4),0),colours.data(),colours.size()*4);d.unlock(c);
            const auto i=d.create_index_buffer(sizeof indices,0,0,0);std::memcpy(d.lock(i,0,sizeof indices,0),indices,sizeof indices);d.unlock(i);
            d.set_stream_source(0,v,8,sizeof(Position));d.set_stream_source(1,c,4,4);d.set_indices(i);
            d.set_vertex_declaration(d.create_vertex_declaration(elements));
            for(unsigned row=0;row<4;++row){float k[4]{};k[row]=1;d.set_vertex_shader_constant_f(64+row,k,1);}
            const float one[]{1,1,1,1};d.set_vertex_shader_constant_f(0,one,1);d.set_vertex_shader_constant_f(8,one,1);
            // A 4x4 texture on stages 0..3 (A8R8G8B8, a colour ramp), so the pixel programs have something to sample.
            const auto t=d.create_texture(4,4,1,0,21,0);const auto surface=d.get_surface_level(t,0);std::uint32_t pitch=0;
            if(auto* texels=d.lock_rect(surface,pitch)){for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){
                const std::uint32_t argb=0xff000000u|((40u+x*60u)<<16)|((40u+y*60u)<<8)|(200u-x*20u-y*20u);std::memcpy(texels+y*pitch+x*4,&argb,4);}
                d.unlock_rect(surface);}
            for(unsigned s=0;s<4;++s)d.set_texture(s,t);
            const float half[]{.5f,.5f,.5f,.5f};for(unsigned r=0;r<8;++r)d.set_pixel_shader_constant_f(r,half,1);
            const auto tokens=link_vertex_shader_40e140(8);d.set_vertex_shader(d.create_vertex_shader(tokens.data()));
            auto draw_frame=[&]{d.begin_frame();d.clear(7,0xff000000,1,0);check(d.draw_indexed_primitive(4,1,0,4,2,2)==0,"background-compile draw succeeds");return image(g[which],d);};
            if(which==0){
                sync_images[0]=draw_frame();
                for(unsigned k=0;k<PixelCount;++k){const auto pt=pc_shader_tokens(pixel_programs[k]);const auto ps=d.create_pixel_shader(pt.data());
                    check(ps!=0,"recovered pixel program created");d.set_pixel_shader(ps);sync_images[2+k]=draw_frame();}
                continue;
            }
            const auto before=d.native_draws();
            compare_images(draw_frame(),sync_images[0],"frame 1 while the vertex program compiles: the CPU vertex path");
            check(d.vertex_fallback_draws()==1&&d.native_draws()==before,"the first draw of a new vertex program takes the CPU path");
            d.wait_for_compiles();
            compare_images(draw_frame(),sync_images[0],"frame 1 after the compile: the GPU vertex path");
            check(d.native_draws()==before+1&&d.vertex_fallback_draws()==1,"the compiled vertex program takes the GPU path");
            unsigned visible=0;
            for(unsigned k=0;k<PixelCount;++k){
                const auto pt=pc_shader_tokens(pixel_programs[k]);d.set_pixel_shader(d.create_pixel_shader(pt.data()));
                const auto generic_before=d.pixel_fallback_draws();
                const auto first=draw_frame();
                check(d.pixel_fallback_draws()==generic_before+1,"the first draw of a new pixel program uses the generic interpreter");
                if(compare_exact(first,sync_images[2+k],"the generic interpreter draws exactly what the specialized program draws"))++visible;
                d.wait_for_compiles();
                const auto later=draw_frame();
                check(d.pixel_fallback_draws()==generic_before+1,"the compiled pixel program replaces the generic interpreter");
                compare_exact(later,sync_images[2+k],"the specialized program after the background compile");
            }
            check(d.native_draws()==before+1+2*PixelCount,"every later draw took the GPU vertex path");
            check(visible>=2,"at least two of the pixel programs draw visible pixels (the comparison is not of black frames)");
        }
        home=saved;
        // Only this run's own fresh folders.
        for(const auto& f:cold){std::error_code e;if(std::filesystem::path(f).filename().string().rfind("or2-async-",0)==0)std::filesystem::remove_all(f,e);}
    }
    std::printf("%s: %d checks, %d failures (GPU vertex draws %llu)\n",failures?"FAIL":"PASS",checks,failures,(unsigned long long)gpu.native_draws());
    return failures?1:0;
}
