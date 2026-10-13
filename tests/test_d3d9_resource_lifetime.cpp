#include "platform/pc_soft_d3d9.hpp"
#include "platform/pc_scene_renderer.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using outrun::platform::PcSoftD3D9Device;
static unsigned checks=0;
static void check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
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
int main(){
    PcSoftD3D9Device d(64,48);
    for(unsigned cycle=0;cycle<100;++cycle){
        auto b=d.create_vertex_buffer(128,0,0,0);
        d.set_stream_source(0,b,0,16);d.release(b);
        check(d.buffer_size(b)==128,"bound buffer retains ownership");
        d.set_stream_source(0,0,0,0);
        check(d.lock(b,0,4,0)==nullptr,"released buffer cannot be locked");
        auto t=d.create_texture(32,32,1,1,21,0);
        d.set_texture(0,t);d.release(t);
        auto saved=d.get_texture(0);d.set_texture(0,0);
        check(d.live_objects()==1,"GetTexture retains texture after unbind");
        d.release(saved);check(d.live_objects()==0,"texture released after GetTexture reference");
        t=d.create_cube_texture(16,1,1,21,0);
        auto s=d.get_cube_map_surface(t,0,0),again=d.get_cube_map_surface(t,0,0);
        check(s==again,"surface identity stable");
        d.release(t);d.release(again);
        const auto back=d.get_render_target(0);
        d.set_render_target(0,s);d.release(s);
        d.clear(1u,0xff112233u,1.f,0);
        check(d.rgba().size()==16u*16*4&&d.rgba()[0]==0x11,"surface and parent retained while bound");
        d.set_render_target(0,back);d.release(back);
        check(d.live_objects()==0,"restoring target frees surface and parent texture");
        auto depth=d.create_depth_stencil_surface(64,48,21,0,0,0);
        d.set_depth_stencil_surface(depth);d.release(depth);
        saved=d.get_depth_stencil_surface();d.set_depth_stencil_surface(PcSoftD3D9Device::DepthBuffer);
        check(d.live_objects()==1,"GetDepthStencilSurface retains ownership");
        d.release(saved);check(d.live_objects()==0,"depth surface freed");
    }
    auto pending=d.create_texture(8,8,1,1,21,0);
    {
        outrun::platform::PcSceneRenderer scene(d);
        check(scene.texture_swap_406630(0x1230000,pending),"deferred swap owns reference");
        check(scene.texture_swap_406630(0x1230000,pending),"same deferred swap preserves reference");
        d.release(pending);unsigned width=0,height=0;
        check(d.texture_level_size(pending,0,width,height),"deferred swap retains producer texture");
    }
    unsigned width=0,height=0;
    check(!d.texture_level_size(pending,0,width,height),"scene shutdown releases unapplied swap");
    // Texture CPU-copy lifetime (pc_soft_d3d9.hpp release_rgba / rgba_level; sana 44416). A file texture's RGBA levels
    // are released with its DDS levels and [face][level] shape kept. A CPU reader (the soft sampler) decodes them again
    // and draws the same pixels, repeatedly. Created textures are never released, and a file texture has no D3DFORMAT, so
    // it cannot be locked or become a target: the written-retains rule is defensive there.
    {
        using namespace outrun::platform::d3d9;
        const auto live_before=d.live_objects();
        const auto file=test_dds(4,4,3,false),cubef=test_dds(4,4,3,true);
        outrun::platform::PcTextureFileRequest q{};q.data=file.data();q.size=std::uint32_t(file.size());
        q.width=q.height=0xffffffffu;q.mip_levels=0;q.pool=1;q.filter=1;q.mip_filter=1;
        const auto tex=d.create_texture_from_file(q);
        check(tex!=0,"file texture created");
        check(d.texture_rgba_bytes(tex)==(16u+4u+1u)*4u,"file texture holds RGBA for its three levels");
        d.reset_state();d.set_render_state(RS_CULLMODE,CULL_NONE);d.set_render_state(RS_ZENABLE,0);
        d.set_texture_stage_state(0,1,2);d.set_texture_stage_state(0,2,2);d.set_texture_stage_state(0,4,2);d.set_texture_stage_state(0,5,2);
        d.set_sampler_state(0,SAMP_MAGFILTER,1);d.set_sampler_state(0,SAMP_MINFILTER,1);d.set_fvf(0x104);   // XYZRHW | TEX1
        struct V {float x,y,z,rhw,u,v;};
        const auto draw=[&]{d.clear(1u,0xff000000u,1.f,0);d.set_texture(0,tex);
            const V v[4]{{8,8,0,1,0,0},{16,8,0,1,1,0},{8,16,0,1,0,1},{16,16,0,1,1,1}};d.draw_primitive_up(5,2,v,sizeof(V));d.set_texture(0,0);
            return d.rgba();};
        const auto before=draw();
        const std::size_t at=(9u*64u+9u)*4u;   // inside texel (0, 0) at 2x: R 200, G 0, B 0
        check(before.size()==64u*48u*4u&&before[at]==200u&&before[at+1]==0u&&before[at+2]==0u,"the textured quad draws texel (0, 0)");
        check(d.release_rgba(tex),"a file texture releases its RGBA");
        check(d.texture_rgba_bytes(tex)==0u,"released: no RGBA resident");
        const auto shape=d.texture_rgba_shape(tex);
        check(shape[0]==1u&&shape[1]==3u,"released: the [face][level] shape is kept");
        unsigned lw=0,lh=0;
        check(d.texture_level_size(tex,0,lw,lh)&&lw==4u&&lh==4u&&d.texture_level_size(tex,2,lw,lh)&&lw==1u&&lh==1u,"released: level sizes kept");
        const auto decoded=d.rgba_levels_decoded_again;
        check(draw()==before,"the CPU sampler draws the same pixels after a release (levels decoded again)");
        check(d.rgba_levels_decoded_again==decoded+3u&&d.texture_rgba_bytes(tex)==(16u+4u+1u)*4u,"all three levels decoded again");
        check(draw()==before&&d.rgba_levels_decoded_again==decoded+3u,"a repeated read decodes nothing more");
        check(d.release_rgba(tex)&&draw()==before,"released again and decoded again: the same pixels");
        check(d.get_surface_level(tex,0)==0u,"a file texture cannot be locked (no D3DFORMAT)");
        const auto made=d.create_texture(4,4,1,1,21,0);
        check(!d.release_rgba(made),"a created texture is never released");
        q.data=cubef.data();q.size=std::uint32_t(cubef.size());q.cube=true;
        const auto cube=d.create_texture_from_file(q);
        check(cube!=0&&d.texture_rgba_shape(cube)[0]==6u&&d.texture_rgba_shape(cube)[1]==3u,"file cube texture: 6 faces x 3 levels");
        check(d.release_rgba(cube)&&d.texture_rgba_bytes(cube)==0u,"file cube texture released");
        check(d.texture_rgba_shape(cube)[0]==6u&&d.texture_rgba_shape(cube)[1]==3u,"cube: faces and levels kept");
        d.release(tex);d.release(made);d.release(cube);
        check(!d.release_rgba(tex),"a released handle releases nothing");
        check(d.live_objects()==live_before,"lifetime fixtures freed");
    }
    check(d.errors.empty(),"no D3D9 errors across repeated cycles");
    std::printf("D3D9 renderer resources: %u checks passed\n",checks);
}
