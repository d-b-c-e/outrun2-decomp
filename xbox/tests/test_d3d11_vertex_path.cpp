// The D3D11 device's GPU vertex path against its CPU path (OR2_D3D11_CPU_VERTEX), headless on WARP, after
// ps5/tests/test_vulkan.cpp indexed_cases: recovered and linked vertex programs, two streams with offsets,
// D3DCOLOR / UBYTE4 / FLOAT2 inputs, a base vertex, lists and strips, depth clipping, a partly locked stream
// (the GPU copy takes only that range), changed constants and a stream rewritten between two draws. Then the three-view
// spike's replication into the back buffer's thirds through both paths (TRIPLE-SCREENS.md step 4).
#include "d3d11_d3d9.hpp"
#include "platform/pc_screen.hpp"
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
// An uncompressed A8R8G8B8 DDS (B, G, R, A bytes), every level and face with its own texels.
static std::vector<std::uint8_t> test_dds(std::uint32_t w,std::uint32_t h,std::uint32_t levels,bool cube){
    std::vector<std::uint8_t> f(0x80,0);std::memcpy(f.data(),"DDS ",4);
    const auto put=[&](std::size_t at,std::uint32_t v){std::memcpy(f.data()+at,&v,4);};
    put(4,124);put(12,h);put(16,w);put(28,levels);put(76,32);put(80,0x41);put(88,32);
    put(92,0xff0000u);put(96,0xff00u);put(100,0xffu);put(104,0xff000000u);put(112,cube?0xfe00u:0u);
    for(unsigned face=0;face<(cube?6u:1u);++face){std::uint32_t lw=w,lh=h;
        for(std::uint32_t l=0;l<levels;++l){
            for(std::uint32_t k=0;k<lw*lh;++k){f.push_back(std::uint8_t(40*l+k*7));f.push_back(std::uint8_t(face*30+k*11));
                f.push_back(std::uint8_t(200-k*13));f.push_back(255);}
            lw=lw>1?lw/2:1;lh=lh>1?lh/2:1;}}
    return f;
}
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
// One third of a three-view image (192x48) against the single-view reference of the same vertex path (64x48), moved
// right by shift pixels: the reference's stable pixels within one level, and its coverage exactly, edges included (a
// half-pixel error, such as a pixel-centre fix not re-applied, moves an edge); or, blank, nothing drawn in that third.
static int g_triple_mode=0;   // the test's corrections: 0 identities, 1 panel 0 moved, 2 refused
static void compare_third(const std::vector<std::uint8_t>& image,unsigned panel,const std::vector<std::uint8_t>& ref,int shift,bool blank,const char* what){
    check(image.size()==192u*48u*4u&&ref.size()==64u*48u*4u,"read back the three-view image and its reference");
    if(image.size()!=192u*48u*4u||ref.size()!=64u*48u*4u)return;
    unsigned stable=0,bad=0,drawn=0,coverage=0;
    for(unsigned y=0;y<48;++y)for(unsigned x=0;x<64;++x){const int rx=int(x)-shift;if(rx<0||rx>63)continue;
        const auto at=(y*192+panel*64+x)*4,r=(y*64+unsigned(rx))*4;
        if(bool(image[at]|image[at+1]|image[at+2])!=bool(ref[r]|ref[r+1]|ref[r+2]))++coverage;}
    for(unsigned y=1;y<47;++y)for(unsigned x=1;x<63;++x){
        const auto at=(y*192+panel*64+x)*4;
        if(image[at]|image[at+1]|image[at+2])++drawn;
        const int rx=int(x)-shift;if(rx<1||rx>62)continue;
        const auto r=(y*64+unsigned(rx))*4;bool interior=true;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(std::memcmp(ref.data()+r,ref.data()+((int(y)+dy)*64+rx+dx)*4,4))interior=false;
        if(!interior)continue;++stable;
        for(unsigned c=0;c<4;++c)if(std::abs(int(image[at+c])-int(ref[r+c]))>1){++bad;break;}
    }
    if(blank){if(drawn)std::printf("  %s: panel %u has %u drawn pixels\n",what,panel,drawn);check(drawn==0,what);return;}
    if(bad||coverage)std::printf("  %s: panel %u, %u of %u stable pixels differ, coverage differs at %u pixels\n",what,panel,bad,stable,coverage);
    check(bad==0&&coverage==0,what);check(stable>300,"the three-view comparison covers enough stable pixels");check(drawn>100,"the three-view draw produced visible pixels");
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
    const auto prepare=[&](D3D11D3D9Device& d,unsigned& vbh,unsigned& colourh,unsigned& ibh){d.reset_state();d.set_fvf(0);
        d.set_render_state(RS_CULLMODE,CULL_NONE);d.set_texture_stage_state(0,1,2);d.set_texture_stage_state(0,2,0);
        d.set_texture_stage_state(0,4,2);d.set_texture_stage_state(0,5,0);
        d.set_render_state(RS_TEXTUREFACTOR,0xff336699);d.set_texture_stage_state(0,2,3);d.set_texture_stage_state(0,5,3);
        vbh=d.create_vertex_buffer(unsigned(positions.size()*sizeof(Position)+8),0,0,0);
        std::memcpy(d.lock(vbh,8,unsigned(positions.size()*sizeof(Position)),0),positions.data(),positions.size()*sizeof(Position));d.unlock(vbh);
        colourh=d.create_vertex_buffer(unsigned(colours.size()*4+4),0,0,0);
        std::memcpy(d.lock(colourh,4,unsigned(colours.size()*4),0),colours.data(),colours.size()*4);d.unlock(colourh);
        ibh=d.create_index_buffer(sizeof indices,0,0,0);std::memcpy(d.lock(ibh,0,sizeof indices,0),indices,sizeof indices);d.unlock(ibh);
        d.set_stream_source(0,vbh,8,sizeof(Position));d.set_stream_source(1,colourh,4,4);d.set_indices(ibh);
        d.set_vertex_declaration(d.create_vertex_declaration(elements));
        for(unsigned row=0;row<4;++row){float c[4]{};c[row]=1;d.set_vertex_shader_constant_f(64+row,c,1);}
        const float one[]{1,1,1,1};d.set_vertex_shader_constant_f(0,one,1);
    };
    for(unsigned i=0;i<2;++i)prepare(*devices[i],vb[i],colour[i],ib[i]);
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
    // Three views (TRIPLE-SCREENS.md step 4, nene 30802): a back buffer three logical screens wide, each draw replicated
    // by the geometry shader into the thirds with its panel's clip correction (pc_screen.hpp g_pc_triple_corrections).
    // Through both vertex paths, so the geometry shader's input matches the FFP and the generated native vertex outputs
    // and its output feeds the pixel program: a mismatch raises no error, the draw just disappears (a black third).
    // - identity corrections: every third is the single-view image (a list, then a strip);
    // - panel 0 moved by +0.5 in NDC x: its third is the image 16 pixels to the right, the others unchanged;
    // - corrections refused (a projection the rig cannot take): the centre third only, the sides black;
    // - the world moved but the program's constants unchanged: they no longer hold world x view x projection, but still
    //   an affine matrix x the projection, so the GPU path replicates (counted as affine);
    // - a projective projection the constants do not factor through (identity x P^-1 is not affine): the GPU path keeps
    //   the draw single on the centre; the CPU path, whose clip positions are its own, replicates.
    // The device caches the corrections per projection, so each case sets its own, with the view its inverse: world x
    // view x projection stays the identity the program's constants hold, which the GPU path checks before replicating.
    {
        struct Case {unsigned type,start,count;int mode;float scale;bool moved,projective,gpu_single;const char* what;};
        const Case cases[]{{4,2,2,0,1.f,false,true,true,"three views, no matrix of a projection (GPU single, CPU replicated)"},
                           {4,2,2,0,1.f,false,false,false,"three views, identity corrections, list"},{5,8,2,0,2.f,false,false,false,"three views, identity corrections, strip"},
                           {4,2,2,1,4.f,false,false,false,"three views, panel 0 moved +0.5 NDC"},{4,2,2,2,8.f,false,false,false,"three views, corrections refused"},
                           {4,2,2,0,16.f,true,false,false,"three views, affine match (world moved, constants kept)"}};
        constexpr unsigned n=sizeof cases/sizeof cases[0];
        std::vector<std::uint8_t> refs[n][2];   // the single-view images, before any corrections are set
        for(unsigned k=0;k<n;++k){const auto& c=cases[k];
            for(auto* d:devices){d->begin_frame();d->clear(7,0xff000000,1,0);check(d->draw_indexed_primitive(c.type,1,0,4,c.start,c.count)==0,"reference draw succeeds");}
            refs[k][0]=readback(g_gpu.device.Get(),g_gpu.context.Get(),gpu.back_texture());refs[k][1]=readback(g_cpu.device.Get(),g_cpu.context.Get(),cpu.back_texture());}
        Gpu tg,tc;
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,1,D3D11_SDK_VERSION,&tg.device,nullptr,&tg.context))||
           FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,1,D3D11_SDK_VERSION,&tc.device,nullptr,&tc.context))){
            std::printf("FAIL no WARP D3D11 device for the three views\n");return 1;}
        _putenv("OR2_D3D11_CPU_VERTEX=");D3D11D3D9Device tgpu(tg.device.Get(),tg.context.Get(),64,48);
        _putenv("OR2_D3D11_CPU_VERTEX=1");D3D11D3D9Device tcpu(tc.device.Get(),tc.context.Get(),64,48);
        _putenv("OR2_D3D11_CPU_VERTEX=");
        D3D11D3D9Device* triples[]{&tgpu,&tcpu};const Gpu* contexts[]{&tg,&tc};unsigned tvb[2]{},tcolour[2]{},tib[2]{};
        for(unsigned i=0;i<2;++i){auto& d=*triples[i];d.resize_back_buffer(192,48);prepare(d,tvb[i],tcolour[i],tib[i]);   // as the devices are now
            d.set_texture_stage_state(0,2,0);d.set_texture_stage_state(0,5,0);const float one[]{1,1,1,1};d.set_vertex_shader_constant_f(8,one,1);
            const auto shader=d.create_vertex_shader(pc_shader_tokens(0x621e10).data());check(shader!=0,"three-view vertex program created");d.set_vertex_shader(shader);}
        g_pc_triple_corrections=[](const float*,float* c){
            if(g_triple_mode==2)return false;
            for(unsigned i=0;i<52;++i)c[i]=i<48&&i%16%5==0?1.f:0.f;   // three identities
            if(g_triple_mode==1)c[12]=.5f;                               // panel 0: x' = x + 0.5 w (row vectors)
            return true;};
        for(unsigned k=0;k<n;++k){const auto& c=cases[k];g_triple_mode=c.mode;
            const float scaled[16]{c.scale,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1},perspective[16]{1,0,0,0, 0,1,0,0, 0,0,-2,-1, 0,0,-1,0};
            const float view[16]{c.projective?1.f:1.f/c.scale,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            const float world[16]{1,0,0,0, 0,1,0,0, 0,0,1,0, c.moved?5.f:0.f,0,0,1};
            for(unsigned i=0;i<2;++i){auto& d=*triples[i];d.set_transform(0x100,world);d.set_transform(2,view);d.set_transform(3,c.projective?perspective:scaled);
                d.begin_frame();d.clear(7,0xff000000,1,0);check(d.draw_indexed_primitive(c.type,1,0,4,c.start,c.count)==0,"three-view draw succeeds");d.end_frame();
                const auto image=readback(contexts[i]->device.Get(),contexts[i]->context.Get(),d.back_texture());
                const bool single=c.mode==2||(c.gpu_single&&i==0);
                for(unsigned panel=0;panel<3;++panel)compare_third(image,panel,refs[k][i],c.mode==1&&panel==0?16:0,single&&panel!=1,c.what);}
        }
        // Counts only (these perspective constants place the quad off screen). Constants built with the current projection
        // replicate; constants whose depth row is scaled (the race sky's, F11 / F12 dumps: w scale 0.9989 .. 0.99989)
        // replicate, as clip x, y and w do not depend on it; constants with another far term (a projective term in
        // K x P^-1) stay single.
        {   const float pa[16]{1,0,0,0, 0,1,0,0, 0,0,-2,-1, 0,0,-1,0},pb[16]{2,0,0,0, 0,2,0,0, 0,0,-2,-1, 0,0,-1,0},
                        pa_scaled[16]{1,0,0,0, 0,1,0,0, 0,0,-2,-1, 0,0,-0.99f,0},pa_far[16]{1,0,0,0, 0,1,0,0, 0,0,-2.1f,-1, 0,0,-1,0},
                        identity[16]{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            auto& d=tgpu;g_triple_mode=0;
            const auto constants=[&](const float* k){for(unsigned i=0;i<4;++i){const float c[4]{k[i],k[4+i],k[8+i],k[12+i]};d.set_vertex_shader_constant_f(64+i,c,1);}};
            const auto frame=[&](const float* current,const float* k){d.set_transform(0x100,identity);d.set_transform(2,identity);d.set_transform(3,current);
                constants(k);d.begin_frame();d.clear(7,0xff000000,1,0);check(d.draw_indexed_primitive(4,1,0,4,2,2)==0,"counts-only draw succeeds");d.end_frame();};
            const auto draws0=d.triple_draws,unmatched0=d.triple_single_unmatched;
            frame(pa,pa);check(d.triple_draws==draws0+1,"constants with the current projection: replicated");
            frame(pb,pb);check(d.triple_draws==draws0+2,"another projection, current and in the constants: replicated");
            frame(pa,pa_scaled);check(d.triple_draws==draws0+3&&d.triple_single_unmatched==unmatched0,"a depth row scaled 0.99: replicated");
            frame(pa,pa_far);check(d.triple_draws==draws0+3&&d.triple_single_unmatched==unmatched0+1,"another far term: single on the centre");
            for(unsigned row=0;row<4;++row){float c[4]{};c[row]=1;d.set_vertex_shader_constant_f(64+row,c,1);}
        }
        g_pc_triple_corrections=nullptr;
        check(tgpu.native_draws()>=n,"the three-view GPU device drew through the GPU vertex path");
        check(tgpu.triple_draws==7&&tcpu.triple_draws==5,"draws replicated: 7 on the GPU path (4 + 3 counts-only frames), 5 on the CPU path");
        check(tgpu.triple_draws_affine==2&&tcpu.triple_draws_affine==0,"two GPU draws replicated through an affine match");
        check(tgpu.triple_single_refused==1&&tcpu.triple_single_refused==1,"refused corrections keep the draw single");
        check(tgpu.triple_single_unmatched==2&&tcpu.triple_single_unmatched==0,"programs without the projection keep the draw single");
        check(tgpu.triple_single_topology==0&&tcpu.triple_single_topology==0&&tgpu.triple_frames==7&&tcpu.triple_frames==5,"three-view counts");
        std::printf("three views: GPU path %llu replicated, CPU path %llu replicated\n",(unsigned long long)tgpu.triple_draws,(unsigned long long)tcpu.triple_draws);
    }
    // UIScalingMode's 2D rectangle (display_fit.hpp ui_scaling_rect; windows/docs/UI-SCALING.md steps 1 / 2a) on a back
    // buffer wider than 4:3: logical 64 x 48 on 128 x 48, so the 4:3 rectangle is 64 px wide from x 32. A pretransformed
    // quad at logical x 0..8 lands at 32..40 with or without spacing; one at -8..0 is clipped by the plain rectangle
    // (mode 2, as before) and drawn at 24..32 with a spacing of 8 (mode 1: same scale, wider mapping); one at -24..-16
    // stays clipped even then (outside the extension, as a span's other panels are).
    {   Gpu ug{};const D3D_FEATURE_LEVEL ui_levels[]{D3D_FEATURE_LEVEL_10_1};
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,ui_levels,1,D3D11_SDK_VERSION,&ug.device,nullptr,&ug.context))){
            std::printf("FAIL no WARP D3D11 device for the UI rectangle\n");return 1;}
        D3D11D3D9Device u(ug.device.Get(),ug.context.Get(),64,48);u.resize_back_buffer(128,48);
        u.reset_state();u.set_render_state(RS_CULLMODE,CULL_NONE);u.set_render_state(RS_ZENABLE,0);u.set_viewport(0,0,64,48,0.f,1.f);
        u.set_texture_stage_state(0,1,2);u.set_texture_stage_state(0,2,0);u.set_texture_stage_state(0,4,2);u.set_texture_stage_state(0,5,0);
        u.set_fvf(0x44);   // XYZRHW | DIFFUSE
        struct V {float x,y,z,rhw;std::uint32_t c;};
        const auto quad=[&](float x0,float x1,std::uint32_t c){
            const V v[4]{{x0,8,0,1,c},{x1,8,0,1,c},{x0,40,0,1,c},{x1,40,0,1,c}};u.draw_primitive_up(5,2,v,sizeof(V));};
        const auto frame=[&](double spacing){
            u.set_ui_rect(true,4./3.,spacing);u.begin_frame();u.clear(1,0xff000000,1,0);
            quad(0,8,0xff00ff00);quad(-8,0,0xffff0000);quad(-24,-16,0xff0000ff);u.end_frame();
            return readback(ug.device.Get(),ug.context.Get(),u.back_texture());};
        const auto at=[](const std::vector<std::uint8_t>& im,unsigned x,unsigned y,unsigned ch){
            return im.size()>=128u*48u*4u?unsigned(im[(y*128u+x)*4u+ch]):0u;};
        const auto plain=frame(0.),spaced=frame(8.);
        check(at(plain,36,24,1)>200&&at(plain,36,24,0)<50,"mode 2: the in-range quad at the 4:3 rectangle's x 32..40");
        check(at(plain,28,24,0)<50&&at(plain,12,24,2)<50,"mode 2: quads left of the 4:3 rectangle are clipped (unchanged)");
        bool same=true;
        for(unsigned x=32;x<44;++x)for(unsigned ch=0;ch<3;++ch)same=same&&(at(plain,x,24,ch)==at(spaced,x,24,ch));
        check(same,"spacing leaves the in-range quad's pixels as they were");
        check(at(spaced,28,24,0)>200&&at(spaced,28,24,1)<50,"mode 1: the quad at -8..0 is drawn at 24..32 (same scale)");
        check(at(spaced,12,24,2)<50&&at(spaced,20,24,2)<50,"mode 1: the quad beyond the extension stays clipped");
        // A game sub-viewport keeps its own clipping: full width but rows 4..44 only (sana 39535), so no extension.
        u.set_viewport(0,4,64,40,0.f,1.f);
        const auto partial=frame(8.);
        check(at(partial,28,24,0)<50,"mode 1 under a full-width, partial-height viewport: no extension (quad left of the 4:3 clipped)");
        check(at(partial,36,24,1)>200,"mode 1 under a partial-height viewport: the in-range quad still draws");
        u.set_viewport(0,0,64,48,0.f,1.f);
        // World-anchored 2D (pc_screen.hpp g_pc_world_2d, 418CF0's particles): the rectangle's mapping out to the back
        // buffer's edges, so the quads at -8..0 and -24..-16 land at 24..32 and 8..16; the HUD's clipping returns after.
        g_pc_world_2d=true;
        const auto world=frame(0.);
        g_pc_world_2d=false;
        check(at(world,28,24,0)>200&&at(world,12,24,2)>200,"world-anchored 2D: quads left of the rectangle draw at its scale");
        bool kept=true;
        for(unsigned x=32;x<44;++x)for(unsigned ch=0;ch<3;++ch)kept=kept&&(at(plain,x,24,ch)==at(world,x,24,ch));
        check(kept,"world-anchored 2D leaves the in-range quad's pixels as they were");
        const auto after=frame(0.);
        check(at(after,28,24,0)<50&&at(after,12,24,2)<50,"after the world-anchored draw the rectangle clips again");
    }
    // Texture CPU-copy lifetime (sana 44416): the D3D11 device uploads a file texture on its first draw and then releases
    // its RGBA copy (unless OR2_KEEP_TEXTURE_RGBA=1). The drawn texels are exact (point sampling, 2x magnification), and
    // later frames draw them again from the GPU copy alone. A created texture keeps its copy.
    {   Gpu tg{};const D3D_FEATURE_LEVEL tl[]{D3D_FEATURE_LEVEL_10_1};
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,tl,1,D3D11_SDK_VERSION,&tg.device,nullptr,&tg.context))){
            std::printf("FAIL no WARP D3D11 device for the texture lifetime\n");return 1;}
        D3D11D3D9Device u(tg.device.Get(),tg.context.Get(),64,48);
        const auto file=test_dds(4,4,3,false);
        PcTextureFileRequest q{};q.data=file.data();q.size=std::uint32_t(file.size());
        q.width=q.height=0xffffffffu;q.mip_levels=0;q.pool=1;q.filter=1;q.mip_filter=1;
        const auto tex=u.create_texture_from_file(q);
        const char* keep_env=std::getenv("OR2_KEEP_TEXTURE_RGBA");const bool keep=keep_env&&std::strcmp(keep_env,"1")==0;   // as the runtime
        check(tex!=0&&u.texture_rgba_bytes(tex)==84u,"D3D11: file texture created with its RGBA copy");
        u.reset_state();u.set_render_state(RS_CULLMODE,CULL_NONE);u.set_render_state(RS_ZENABLE,0);u.set_viewport(0,0,64,48,0.f,1.f);
        u.set_texture_stage_state(0,1,2);u.set_texture_stage_state(0,2,2);u.set_texture_stage_state(0,4,2);u.set_texture_stage_state(0,5,2);
        u.set_sampler_state(0,SAMP_MAGFILTER,1);u.set_sampler_state(0,SAMP_MINFILTER,1);u.set_sampler_state(0,SAMP_MIPFILTER,0);
        u.set_fvf(0x104);   // XYZRHW | TEX1
        struct V {float x,y,z,rhw,u,v;};
        const auto frame=[&]{u.begin_frame();u.clear(1,0xff000000,1,0);u.set_texture(0,tex);
            const V v[4]{{8,8,0,1,0,0},{16,8,0,1,1,0},{8,16,0,1,0,1},{16,16,0,1,1,1}};u.draw_primitive_up(5,2,v,sizeof(V));
            u.set_texture(0,0);u.end_frame();return readback(tg.device.Get(),tg.context.Get(),u.back_texture());};
        const auto first=frame();
        bool exact=first.size()==64u*48u*4u;
        for(unsigned ty=0;ty<4&&exact;++ty)for(unsigned tx=0;tx<4&&exact;++tx){
            const unsigned k=ty*4+tx,at=((9+ty*2)*64+(9+tx*2))*4;   // texel (tx, ty) at 2x: its block's second pixel
            exact=first[at]==std::uint8_t(200-k*13)&&first[at+1]==std::uint8_t(k*11)&&first[at+2]==std::uint8_t(k*7);}
        check(exact,"D3D11: the file texture's texels are drawn exactly");
        if(keep)check(u.texture_rgba_bytes(tex)==84u&&u.rgba_released_textures==0u,"OR2_KEEP_TEXTURE_RGBA: the copy is kept");
        else check(u.texture_rgba_bytes(tex)==0u&&u.rgba_released_textures==1u&&u.rgba_released_bytes==84u,"D3D11: the RGBA copy released after upload");
        check(frame()==first,"D3D11: later frames draw the same pixels from the GPU copy");
        check(u.rgba_levels_decoded_again==0u,"D3D11: nothing decoded again (no CPU reader)");
        const auto made=u.create_texture(4,4,1,1,21,0);
        u.set_texture(0,made);u.set_texture(0,0);
        check(!u.release_rgba(made)&&u.texture_rgba_bytes(made)==64u,"D3D11: a created texture keeps its copy");
        u.release(tex);u.release(made);
        check(u.errors.empty(),"D3D11: no errors in the texture lifetime fixture");
    }
    std::printf("%s: %d checks, %d failures (GPU vertex draws %llu)\n",failures?"FAIL":"PASS",checks,failures,(unsigned long long)gpu.native_draws());
    return failures?1:0;
}
