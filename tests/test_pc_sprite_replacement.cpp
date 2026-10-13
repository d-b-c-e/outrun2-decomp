// UITextureReplacement on the 2D sprite renderer (pc_sprite_2d / pc_pmt_loader): a synthetic XST bank loaded without
// and with a pack in the OutRun2006Tweaks layout. The replacement is created at its own size, and the ID3DXSprite
// image draw (42A0A0), the 42C0F0 quad and the SPRANI footage strip (428BD0 -> 42A3A0) address the same stock texels at
// the same screen size as the stock texture. Optional: `test_pc_sprite_replacement <game folder> <pack load folder>`
// loads every XST bank of the game through the pack (read only) and reports, per bank, what the pack replaces.
#include "platform/pc_sprite_2d.hpp"
#include "platform/pc_pmt_loader.hpp"
#include "platform/pc_dds.hpp"
#include "platform/pc_render_flush.hpp"
#include "platform/retail_asset_store.hpp"
#include "platform/sprite_2d_runtime.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>
using namespace outrun;
using namespace outrun::platform;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"sprite replacement line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
void put32(std::vector<std::uint8_t>& v,std::size_t o,std::uint32_t x){std::memcpy(v.data()+o,&x,4);}
void putf(std::vector<std::uint8_t>& v,std::size_t o,float x){std::memcpy(v.data()+o,&x,4);}
std::uint32_t get32(const std::uint8_t* p){std::uint32_t v;std::memcpy(&v,p,4);return v;}
float getf(const std::uint8_t* p){float v;std::memcpy(&v,p,4);return v;}
// One-level 32-bit DDS. Texel i has the colour (seed+i, seed+2i, seed+3i, 255-i); `abgr` stores it as A8B8G8R8
// (bytes R,G,B,A), else as A8R8G8B8 (bytes B,G,R,A).
std::vector<std::uint8_t> dds32(std::uint32_t w,std::uint32_t h,std::uint8_t seed,bool abgr){
    std::vector<std::uint8_t> f(0x80+std::size_t(w)*h*4,0);
    std::memcpy(f.data(),"DDS ",4);put32(f,4,124);put32(f,8,0x100f);put32(f,12,h);put32(f,16,w);put32(f,28,1);
    put32(f,76,32);put32(f,80,0x41);put32(f,88,32);
    put32(f,92,abgr?0xffu:0xff0000u);put32(f,96,0xff00u);put32(f,100,abgr?0xff0000u:0xffu);put32(f,104,0xff000000u);
    for(std::uint32_t i=0;i<w*h;++i){
        const std::uint8_t r=std::uint8_t(seed+i),g=std::uint8_t(seed+2*i),b=std::uint8_t(seed+3*i),a=std::uint8_t(255-i);
        auto* p=f.data()+0x80+std::size_t(i)*4;
        p[0]=abgr?r:b;p[1]=g;p[2]=abgr?b:r;p[3]=a;
    }
    return f;
}
// XST (pc_sprite_bank_load_xst): {meta size, pixel size}, a 0x100-byte meta section (image table at 0x20, entry table
// at 0x80), the DDS files. An empty texture is an entry without a texture (type 0). Images: {texture, l, t, r, b}.
std::vector<std::uint8_t> make_xst(const std::vector<std::vector<std::uint8_t>>& textures,const std::vector<std::array<std::uint16_t,5>>& images){
    constexpr std::uint32_t Meta=0x100;
    std::vector<std::uint8_t> meta(Meta,0),pixels;
    put32(meta,4,0x74);put32(meta,8,std::uint32_t(textures.size()));put32(meta,0x18,std::uint32_t(images.size()));put32(meta,0x1c,0x20);
    for(std::size_t k=0;k<images.size();++k){
        const auto at=0x20+k*0x1c;put32(meta,at,images[k][0]);
        for(unsigned j=0;j<4;++j){meta[at+0x14+j*2]=std::uint8_t(images[k][j+1]);meta[at+0x15+j*2]=std::uint8_t(images[k][j+1]>>8);}
    }
    for(std::size_t k=0;k<textures.size();++k){
        const auto at=0x80+k*0x14;
        if(textures[k].empty())continue;
        put32(meta,at+4,std::uint32_t(pixels.size()));meta[at+0xd]=1;put32(meta,at+0x10,std::uint32_t(textures[k].size()));
        pixels.insert(pixels.end(),textures[k].begin(),textures[k].end());
    }
    std::vector<std::uint8_t> d(8);put32(d,0,Meta);put32(d,4,std::uint32_t(pixels.size()));
    d.insert(d.end(),meta.begin(),meta.end());d.insert(d.end(),pixels.begin(),pixels.end());
    return d;
}
struct Device final:PcD3D9Device{
    struct Tex { std::uint32_t w,h; std::vector<std::uint8_t> bytes; };
    std::vector<Tex> textures;
    bool keep_bytes{true};
    std::array<std::uint32_t,0x400> render{};std::array<std::array<std::uint32_t,32>,16> stage{},sampler{};
    std::vector<std::array<float,16>> transforms;std::vector<std::array<std::int32_t,4>> rects;
    std::vector<std::vector<std::uint8_t>> strips;
    struct Sprite final:PcD3DXSprite{
        Device* d{};
        void set_transform(const float m[16])override{std::array<float,16> a{};std::memcpy(a.data(),m,64);d->transforms.push_back(a);}
        void begin(std::uint32_t)override{}
        void draw(std::uint32_t,const std::int32_t* r,const float*,const float*,std::uint32_t)override{d->rects.push_back({r[0],r[1],r[2],r[3]});}
        void end()override{}
    } sprite_obj;
    Device(){sprite_obj.d=this;}
    std::uint32_t create_texture_from_file(const PcTextureFileRequest& q)override{
        textures.push_back({get32(q.data+16),get32(q.data+12),keep_bytes?std::vector<std::uint8_t>(q.data,q.data+q.size):std::vector<std::uint8_t>{}});
        return std::uint32_t(textures.size());}
    bool texture_level_size(std::uint32_t t,std::uint32_t,std::uint32_t& w,std::uint32_t& h)override{
        if(!t||t>textures.size())return false;w=textures[t-1].w;h=textures[t-1].h;return true;}
    PcD3DXSprite* sprite()override{return &sprite_obj;}
    void draw_primitive_up(std::uint32_t,std::uint32_t count,const void* data,std::uint32_t stride)override{
        const auto* p=static_cast<const std::uint8_t*>(data);strips.emplace_back(p,p+std::size_t(count+2)*stride);}
    std::uint32_t create_vertex_buffer(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t)override{return 0;}
    std::uint32_t create_index_buffer(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t)override{return 0;}
    std::uint8_t* lock(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t)override{return nullptr;}
    void unlock(std::uint32_t)override{}
    std::uint32_t create_vertex_declaration(const PcVertexElement*)override{return 0;}
    std::uint32_t create_vertex_shader(const std::uint32_t*)override{return 0;}
    void set_vertex_shader_constant_f(std::uint32_t,const float*,std::uint32_t)override{}
    void release(std::uint32_t)override{}
    std::uint32_t get_render_state(std::uint32_t s)override{return render[s&0x3ffu];}
    void set_render_state(std::uint32_t s,std::uint32_t v)override{render[s&0x3ffu]=v;}
    std::uint32_t get_texture(std::uint32_t)override{return 0;}
    void set_texture(std::uint32_t,std::uint32_t)override{}
    std::uint32_t get_texture_stage_state(std::uint32_t s,std::uint32_t t)override{return stage[s&15u][t&31u];}
    void set_texture_stage_state(std::uint32_t s,std::uint32_t t,std::uint32_t v)override{stage[s&15u][t&31u]=v;}
    std::uint32_t get_sampler_state(std::uint32_t s,std::uint32_t t)override{return sampler[s&15u][t&31u];}
    void set_sampler_state(std::uint32_t s,std::uint32_t t,std::uint32_t v)override{sampler[s&15u][t&31u]=v;}
    std::uint32_t draw_indexed_primitive(std::uint32_t,std::int32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t)override{return 0;}
    void set_vertex_declaration(std::uint32_t)override{}
    void set_vertex_shader(std::uint32_t)override{}
    void set_stream_source(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t)override{}
    void set_indices(std::uint32_t)override{}
    std::uint32_t create_pixel_shader(const std::uint32_t*)override{return 0;}
    void set_pixel_shader(std::uint32_t)override{}
    void set_pixel_shader_constant_f(std::uint32_t,const float*,std::uint32_t)override{}
    std::uint32_t get_declaration(std::uint32_t,PcVertexElement*)override{return 0;}
};
constexpr std::uint32_t Bank=3;
// A one-layer SPRANI animation in bank 3's window: footage 4x2 on screen, its one frame showing texels (2,1)-(6,3) of
// texture 1 (the frame token).
void make_animation(PcSpriteBank& b){
    const auto base=PcSpraniAnimationBase(Bank);
    auto& a=b.animation;a.assign(0x200,0);
    putf(a,0x04,0.0f);putf(a,0x08,10.0f);put32(a,0x1c,1);put32(a,0x20,base+0x40);         // component: window, one layer
    a[0x40+2]=10;put32(a,0x40+4,0x10000000u);                                                // layer: frames 0..10, rate 4096/4096
    put32(a,0x40+8,base+0x100);putf(a,0x40+0x20,100.0f);putf(a,0x40+0x24,100.0f);putf(a,0x40+0x2c,100.0f);a[0x40+0x48]=3;
    put32(a,0x100,4);put32(a,0x104,2);put32(a,0x108,1);put32(a,0x10c,1);put32(a,0x110,base+0x140);   // footage
    put32(a,0x140,(Bank<<16)|1u);putf(a,0x144,2.0f);putf(a,0x148,1.0f);putf(a,0x14c,6.0f);putf(a,0x150,3.0f); // frame
}
struct Drawn { Device dev; PcSprite2dState s; };
// Load the bank, queue image 0 at (100, 50), image 1 at (200, 60), the footage at frame 1, a 42C0F0 quad of image 0
// (record flags 0x108: centred), and flush.
void draw_bank(Drawn& r,const std::vector<std::uint8_t>& xst){
    std::string error;
    CHECK(pc_sprite_bank_load_xst(r.s.banks[Bank],r.dev,xst,error,"spr_test_xst"));
    make_animation(r.s.banks[Bank]);
    r.s.screen_740c94=2.0f;r.s.screen_740c98=2.0f;r.s.sprite_95b218=true;
    pc_image_42d280(r.s,(Bank<<16)|0u,100,50,0,1.0f,0xffffffffu);
    pc_image_42d280(r.s,(Bank<<16)|1u,200,60,0,1.0f,0xffffffffu);
    std::array<std::uint8_t,0x48> quad{};pc_image_entry_42c2f0(r.s,quad.data(),(Bank<<16)|0u);
    {const float one=1.0f,x=300.0f,y=70.0f;std::memcpy(quad.data()+0x14,&one,4);std::memcpy(quad.data()+0x18,&one,4);
     std::memcpy(quad.data()+0x24,&x,4);std::memcpy(quad.data()+0x28,&y,4);
     const std::uint32_t col=0xffffffffu,fl=0x108u,tex=r.s.banks[Bank].textures[1];
     std::memcpy(quad.data()+0x2c,&col,4);std::memcpy(quad.data()+0x34,&fl,4);std::memcpy(quad.data()+0x38,&tex,4);}
    pc_image_record_42cfe0(r.s,quad,2.0f);
    std::vector<std::uint8_t> arena(16*64);driving::PcMatrixStack st{driving::Bytes(arena.data(),arena.size()),0,0,16};
    driving::pc_matrix_identity(st);
    pc_sprani_render_429460(r.s,r.dev,st,PcSpraniAnimationBase(Bank),1.0f,1.0f,3);
    PcRenderGlobals rg;
    PcFlushContext fc{r.dev,rg,[](std::uint32_t)->PcPmtResources*{return nullptr;},pc_colour_list_alt_408c80,1};
    pc_2d_flush_42d710(r.s,fc,r.dev.sprite(),0,Pc2dLayerCount,0);
    CHECK(r.s.missing==0);
}
bool near(float a,float b){return std::fabs(a-b)<=1e-6f*std::max(1.0f,std::fabs(b));}
// Retail check: every XST bank of the game through the pack (read only); prints the per-bank result.
int retail(const char* root,const char* pack){
    RetailAssetStore store;std::string error;
    CHECK(retail_asset_store_open(store,root,&error));
    texture_replacement_configure(false,true,pack);
    Device dev;dev.keep_bytes=false;
    std::uint32_t banks=0,textures=0;
    for(std::uint32_t bank=0;bank<PcSpriteBankCount;++bank){
        const char* path=native_sprite2d_xst_path(bank);if(!path)continue;
        PcSpriteBank b;std::vector<std::uint8_t> inflated;const std::vector<std::uint8_t>* bytes=nullptr;
        if(bank<0x20u)bytes=retail_asset_guest_path(store,path,true,&error);
        else if(retail_asset_read_relative_inflated(store,path,inflated,128u<<20,&error))bytes=&inflated;
        if(!bytes){std::printf("bank %02x %s: not read (%s)\n",bank,path,error.c_str());continue;}
        if(!pc_sprite_bank_load_xst(b,dev,*bytes,error,pc_sprite_xst_pack_name(path))){std::printf("bank %02x %s: %s\n",bank,path,error.c_str());continue;}
        ++banks;textures+=std::uint32_t(b.textures.size());
    }
    const auto& t=ui_texture_replacement_stats();
    std::printf("retail: %u XST banks, %u entries; UI pack: %u files indexed, %u looked up, %u replaced, %u kept stock\n",
        banks,textures,t.files,t.looked,t.replaced,t.failed);
    std::set<std::string> used;
    for(const auto& [name,b]:ui_texture_replacement_banks()){
        std::printf("  %-36s %s %3u of %3u replaced, %u kept stock, %.2f s\n",name.c_str(),b.folder?"folder   ":"no folder",b.replaced,b.looked,b.failed,b.seconds);
        if(b.folder){std::string l=name;for(auto& ch:l)ch=char(std::tolower(static_cast<unsigned char>(ch)));used.insert(l);}
    }
    std::error_code e;
    for(const auto& entry:std::filesystem::directory_iterator(std::filesystem::u8path(pack),e)){
        if(!entry.is_directory(e))continue;
        auto n=entry.path().filename().u8string();std::string l(n.begin(),n.end());
        for(auto& ch:l)ch=char(std::tolower(static_cast<unsigned char>(ch)));
        if(l.rfind("spr_",0)==0&&!used.count(l))std::printf("  pack folder no XST bank names: %s\n",l.c_str());
    }
    texture_replacement_configure(false,false,{});
    CHECK(t.replaced>0);
    std::printf("pc sprite replacement (retail): %u checks\n",checks);
    return 0;
}
}
int main(int argc,char** argv){
    if(argc>2)return retail(argv[1],argv[2]);
    namespace fs=std::filesystem;
    CHECK(pc_sprite_xst_pack_name("sprite/spr_etc_xst.sz")=="spr_etc_xst"&&pc_sprite_xst_pack_name("SPRITE\\spr_sprani_sumo_loading_Exst.sz")=="spr_sprani_sumo_loading_Exst");
    const auto a=dds32(8,4,0x10,false),b=dds32(8,4,0x40,false);
    // Entry 0 has no texture, so Tweaks' index of entry 1 is 0 and of entry 2 is 1. Images: 0 = texels (2,1)-(6,3) of
    // texture 1; 1 = the whole of texture 2.
    const auto xst=make_xst({{},a,b},{{1,2,1,6,3},{2,0,0,0,0}});
    const auto dir=fs::temp_directory_path()/"or2_ui_texture_replacement_test";
    fs::remove_all(dir);fs::create_directories(dir/"spr_TEST_xst");
    auto name=[&](const std::vector<std::uint8_t>& f,const char* prefix){
        char n[64];std::snprintf(n,sizeof n,"%s%X_8x4.dds",prefix,pmt_xxh32(f.data(),f.size()));return std::string(n);};
    auto write=[&](const std::string& n,const std::vector<std::uint8_t>& f){
        std::ofstream o(dir/"spr_TEST_xst"/n,std::ios::binary);o.write(reinterpret_cast<const char*>(f.data()),std::streamsize(f.size()));};
    const auto ra=dds32(16,8,0x77,true),rb=dds32(24,12,0x22,false),decoy=dds32(40,20,0x33,false);
    write(name(a,"0_"),ra);        // 2x, stored A8B8G8R8
    write(name(b,"1_"),rb);        // 3x, under Tweaks' index (1)
    write(name(b,"2_"),decoy);     // the entry position (2): must not be taken
    // Stock: UI replacement off.
    texture_replacement_configure(false,false,dir.string());
    Drawn stock;draw_bank(stock,xst);
    CHECK(stock.dev.textures.size()==2&&stock.dev.textures[0].bytes==a&&stock.dev.textures[1].bytes==b);
    CHECK(stock.s.banks[Bank].textures==(std::vector<std::uint32_t>{0,1,2}));
    CHECK(stock.s.banks[Bank].stock_sizes.size()==3&&stock.s.banks[Bank].stock_sizes[1][0]==0);
    CHECK(ui_texture_replacement_stats().looked==0);
    CHECK(stock.dev.rects.size()==2&&stock.dev.strips.size()==2);
    CHECK((stock.dev.rects[0]==std::array<std::int32_t,4>{2,1,5,3})&&(stock.dev.rects[1]==std::array<std::int32_t,4>{0,0,7,4}));
    // Replaced: UI replacement on, scene off.
    texture_replacement_configure(false,true,dir.string());
    CHECK(ui_texture_replacement_stats().files==3&&pmt_texture_replacement_stats().files==0);
    Drawn hd;draw_bank(hd,xst);
    CHECK(hd.dev.textures.size()==2&&hd.dev.textures[0].w==16&&hd.dev.textures[0].h==8&&hd.dev.textures[1].w==24&&hd.dev.textures[1].bytes==rb);
    {const auto& z=hd.s.banks[Bank].stock_sizes;CHECK(z.size()==3&&z[0][0]==0&&z[1][0]==8&&z[1][1]==4&&z[2][0]==8&&z[2][1]==4);}
    const auto& st=ui_texture_replacement_stats();
    CHECK(st.looked==2&&st.replaced==2&&st.failed==0&&pmt_texture_replacement_stats().looked==0);
    CHECK(ui_texture_replacement_banks().at("spr_test_xst").folder);
    // The A8B8G8R8 file reaches the device as A8R8G8B8 with the same colours.
    {   const auto& f=hd.dev.textures[0].bytes;
        CHECK(get32(f.data()+92)==0xff0000u&&get32(f.data()+100)==0xffu);
        PcTextureFileRequest q{};q.data=f.data();q.size=std::uint32_t(f.size());q.mip_levels=1;
        PcDdsTexture tex;std::string error;CHECK(pc_d3dx_texture_from_file(q,tex,error));
        const auto rgba=pc_dds_rgba(tex,0,0);
        for(std::uint32_t i:{0u,5u,127u})CHECK(rgba[i*4]==std::uint8_t(0x77+i)&&rgba[i*4+1]==std::uint8_t(0x77+2*i)&&rgba[i*4+2]==std::uint8_t(0x77+3*i)&&rgba[i*4+3]==std::uint8_t(255-i));}
    // Images: the source rectangle on the replacement's texels, the scale reduced by the same ratio, the position kept.
    CHECK(hd.dev.rects.size()==2);
    CHECK((hd.dev.rects[0]==std::array<std::int32_t,4>{4,2,10,6})&&(hd.dev.rects[1]==std::array<std::int32_t,4>{0,0,21,12}));
    for(unsigned k=0;k<2;++k){
        const float ratio=k==0?2.0f:3.0f;const auto& m0=stock.dev.transforms[k];const auto& m1=hd.dev.transforms[k];
        CHECK(near(m1[0]*ratio,m0[0])&&near(m1[5]*ratio,m0[5])&&m1[12]==m0[12]&&m1[13]==m0[13]);
    }
    // Strips (vertex stride 0x1C: x, y, z, rhw, colour, u, v): the 42C0F0 quad, then the footage. Positions equal; the
    // UVs equal apart from the half-texel offset footage adds, which follows the texture drawn (0.51 of its texel).
    CHECK(hd.dev.strips.size()==2);
    for(unsigned k=0;k<2;++k){
        const auto& s0=stock.dev.strips[k];const auto& s1=hd.dev.strips[k];CHECK(s0.size()==s1.size()&&s0.size()==4*0x1c);
        for(unsigned v=0;v<4;++v){
            const auto* p0=s0.data()+v*0x1c;const auto* p1=s1.data()+v*0x1c;
            CHECK(getf(p0)==getf(p1)&&getf(p0+4)==getf(p1+4));
            if(k==0){CHECK(getf(p0+0x14)==getf(p1+0x14)&&getf(p0+0x18)==getf(p1+0x18));continue;}
            const float du0=0.51f/8.0f,du1=0.51f/16.0f,dv0=0.51f/4.0f,dv1=0.51f/8.0f;
            const float u0=getf(p0+0x14),u1=getf(p1+0x14),v0=getf(p0+0x18),v1=getf(p1+0x18);
            const float su=u0>0.5f?-du0:du0,sv=v0>0.5f?-dv0:dv0;   // which corner: the offset's sign
            CHECK(near(u1-(su<0?-du1:du1),u0-su)&&near(v1-(sv<0?-dv1:dv1),v0-sv));
        }
    }
    // Footage corner 0 samples stock texel x0 = 2 of 8 (u 0.25) on both, plus its half texel.
    CHECK(near(getf(hd.dev.strips[1].data()+0x14),0.25f+0.51f/16.0f)&&near(getf(stock.dev.strips[1].data()+0x14),0.25f+0.51f/8.0f));
    // No usable file: the stock texture, counted. A truncated replacement, then UI off again.
    {   fs::remove_all(dir);fs::create_directories(dir/"spr_TEST_xst");
        auto t=ra;t.resize(0x90);write(name(a,""),t);
        texture_replacement_configure(false,true,dir.string());
        Drawn r;draw_bank(r,xst);
        CHECK(r.dev.textures[0].bytes==a&&r.dev.textures[1].bytes==b&&r.s.banks[Bank].stock_sizes[1][0]==0);
        CHECK((r.dev.rects[0]==std::array<std::int32_t,4>{2,1,5,3}));
        const auto& s=ui_texture_replacement_stats();CHECK(s.looked==2&&s.replaced==0&&s.failed==1);}
    texture_replacement_configure(false,false,{});fs::remove_all(dir);
    std::printf("pc sprite replacement: %u checks\n",checks);
    return 0;
}
