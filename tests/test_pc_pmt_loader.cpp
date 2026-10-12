// Host checks of the native PMT loader and material shader setup on a
// synthetic PMT. The retail-data comparison against the original code is the
// pmt_loader_probe oracle.
#include "platform/pc_pmt_loader.hpp"
#include "platform/pc_dds.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
using namespace outrun::platform;
using outrun::driving::Bytes;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"pmt loader line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
struct Device final:PcD3D9Device{
    std::vector<std::vector<std::uint8_t>> buffers;std::vector<std::uint32_t> kinds;std::vector<std::uint32_t> calls;
    std::vector<PcTextureFileRequest> textures;std::vector<std::vector<std::uint8_t>> texture_bytes;
    std::uint32_t refuse_size{};   // a texture request of this size fails (returns 0)
    std::uint32_t add(std::uint32_t kind){kinds.push_back(kind);buffers.emplace_back();return std::uint32_t(kinds.size());}
    std::uint32_t create_vertex_buffer(std::uint32_t n,std::uint32_t,std::uint32_t,std::uint32_t)override{
        calls.push_back(26);const auto h=add(26);buffers[h-1].resize(n);return h;}
    std::uint32_t create_index_buffer(std::uint32_t n,std::uint32_t,std::uint32_t f,std::uint32_t)override{
        calls.push_back(27);CHECK(f==0x65);const auto h=add(27);buffers[h-1].resize(n);return h;}
    std::uint8_t* lock(std::uint32_t h,std::uint32_t o,std::uint32_t,std::uint32_t)override{calls.push_back(0x100b);return buffers.at(h-1).data()+o;}
    void unlock(std::uint32_t)override{calls.push_back(0x100c);}
    std::uint32_t create_vertex_declaration(const PcVertexElement*)override{calls.push_back(86);return add(86);}
    std::uint32_t create_vertex_shader(const std::uint32_t* t)override{calls.push_back(91);CHECK(t[0]==0xfffe0101u);return add(91);}
    void set_vertex_shader_constant_f(std::uint32_t,const float*,std::uint32_t)override{calls.push_back(94);}
    std::uint32_t create_texture_from_file(const PcTextureFileRequest& q)override{
        calls.push_back(0x5001);textures.push_back(q);texture_bytes.emplace_back(q.data,q.data+q.size);
        return q.size==refuse_size?0u:add(0x5001);}
    void release(std::uint32_t)override{calls.push_back(2);}
    std::uint32_t get_render_state(std::uint32_t)override{return 0;}
    void set_render_state(std::uint32_t,std::uint32_t)override{}
    std::uint32_t get_texture(std::uint32_t)override{return 0;}
    void set_texture(std::uint32_t,std::uint32_t)override{}
    std::uint32_t get_texture_stage_state(std::uint32_t,std::uint32_t)override{return 0;}
    void set_texture_stage_state(std::uint32_t,std::uint32_t,std::uint32_t)override{}
    std::uint32_t get_sampler_state(std::uint32_t,std::uint32_t)override{return 0;}
    void set_sampler_state(std::uint32_t,std::uint32_t,std::uint32_t)override{}
    std::uint32_t draw_indexed_primitive(std::uint32_t,std::int32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t)override{return 0;}
    void set_vertex_declaration(std::uint32_t)override{}
    void set_vertex_shader(std::uint32_t)override{}
    void set_stream_source(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t)override{}
    void set_indices(std::uint32_t)override{}
    std::uint32_t create_pixel_shader(const std::uint32_t*)override{return add(106);}
    void set_pixel_shader(std::uint32_t)override{}
    void set_pixel_shader_constant_f(std::uint32_t,const float*,std::uint32_t)override{}
    std::uint32_t get_declaration(std::uint32_t,PcVertexElement*)override{return 0;}
};
// Synthetic PMT: one object, one group (1 VB, 1 IB), one material, one texture (a 4x8 DDS header at video+0x20).
void synthetic_pmt(PcPmtResources& r,std::vector<std::uint8_t>& video){
    r.system.assign(0x1b0,0);Bytes s(r.system.data(),r.system.size());
    video.assign(0xa0,0);for(unsigned k=0;k<12;++k)video[k]=std::uint8_t(0x40+k);
    std::memcpy(video.data()+0x20,"DDS ",4);video[0x20+12]=8;video[0x20+16]=4;   // dwHeight, dwWidth
    r.video=video.data();r.video_size=video.size();
    s.put32(4,1);s.put32(8,1);
    const std::uint32_t rec=0x18;
    s.put32(rec+0x04,0x100);s.put32(rec+0x08,0xe0);s.put32(rec+0x0c,0x130);s.put32(rec+0x18,0x80);
    s.put32(rec+0x30,0xb0);s.put32(rec+0x34,0x150);
    s.put32(0x80+0x24,1);s.put32(0x80+0x2c,1);
    s.put32(0xb0,1);s.put32(0xb0+0x18,6);s.put32(0xb0+0x1c,12);s.put32(0xb0+0x20,0x2);s.put32(0xb0+0x28,0);
    s.put32(0xe0,0xf0);s.put32(0xf0+4,0);
    s.put32(0x100,0x110);s.put32(0x110+4,0x120);for(unsigned k=0;k<6;++k)s.put8(0x120+k,std::uint8_t(k+1));
    for(unsigned l=0;l<3;++l)s.puti(0x150+8+l*0x14+0x10,-1);
    s.put32(0x68+4,0x20);s.put32(0x68+0x10,0);
}
// Objects then textures of a synthetic PMT on its own device.
void load_synthetic(PcPmtResources& r,Device& dev){
    PcShaderCache cache{};PcShaderGlobals globals{};PcPmtLoadCursor cur{};
    pmt_objects_begin_42e490(r,cur);CHECK(pmt_objects_step_42e4f0(r,cur,dev,cache,globals));
    pmt_textures_begin_42e850(r,cur);CHECK(pmt_textures_step_42e8c0(r,cur,dev));
}
}
int main(){
    PcVertexDeclaration d{};
    CHECK(d3dx_declarator_from_fvf(0x152,d)); // XYZ | NORMAL | DIFFUSE | TEX1
    CHECK(d[0].type==2&&d[0].usage==0&&d[0].offset==0);
    CHECK(d[1].type==2&&d[1].usage==3&&d[1].offset==12);
    CHECK(d[2].type==4&&d[2].usage==10&&d[2].offset==24);
    CHECK(d[3].type==1&&d[3].usage==5&&d[3].offset==28&&d[3].usage_index==0);
    CHECK(d[4].stream==PcDeclEndStream&&d[4].type==17);
    CHECK(!d3dx_declarator_from_fvf(0x1,d));
    type2_declaration_4101c0(0x152,d);
    CHECK(d[4].stream==1&&d[4].offset==0&&d[4].type==2&&d[4].usage==0&&d[4].usage_index==1);
    CHECK(d[5].stream==1&&d[5].offset==12&&d[5].usage==3&&d[5].usage_index==1);
    CHECK(d[6].stream==1&&d[6].offset==24&&d[6].type==4&&d[6].usage==10);
    CHECK(d[7].stream==1&&d[7].offset==28&&d[7].type==1&&d[7].usage==5&&d[7].usage_index==4);
    CHECK(d[8].stream==PcDeclEndStream&&d[8].type==0x11);
    const auto shader=link_vertex_shader_40e140(0);
    CHECK(shader.front()==0xfffe0101u&&shader.back()==0xffffu);
    // Shader cache: identical requests share one shader; key3 separates entries.
    {
        Device dev;PcShaderCache cache{};std::uint32_t decl=0;
        CHECK(d3dx_declarator_from_fvf(0x112,d));
        const auto a=shader_cache_40f4e0(cache,dev,1,d.data(),shader.data(),0,decl);
        const auto b=shader_cache_40f4e0(cache,dev,1,d.data(),shader.data(),0,decl);
        const auto c=shader_cache_40f4e0(cache,dev,1,d.data(),shader.data(),5,decl);
        CHECK(a==b&&a!=c&&cache.count==2&&cache.high_water==2&&cache.entries[0].references==2);
        shader_cache_release_40f650(cache,dev);
        CHECK(cache.entries[0].shader==0&&cache.entries[1].declaration==0);
    }
    PcPmtResources r{};std::vector<std::uint8_t> video;synthetic_pmt(r,video);Bytes s(r.system.data(),r.system.size());
    Device dev;PcShaderCache cache{};PcShaderGlobals globals{};PcPmtLoadCursor cur{};
    pmt_objects_begin_42e490(r,cur);
    CHECK(s.u32(0x14)==0x18&&r.object_count_0c==1);
    CHECK(pmt_objects_step_42e4f0(r,cur,dev,cache,globals));
    CHECK(cur.object_95b230==1&&cur.offset_95b234==0x54);
    pmt_textures_begin_42e850(r,cur);
    CHECK(r.texture_table_24==0x54&&s.u32(0x54)==0x58&&r.texture_records_04==0x68);
    CHECK(pmt_textures_step_42e8c0(r,cur,dev));
    const std::vector<std::uint32_t> expected{26,0x100b,0x100c,27,0x100b,0x100c,86,91,0x5001};
    CHECK(dev.calls==expected);
    CHECK(s.u32(0xe0)==1&&s.u32(0x100)==2);
    CHECK(dev.buffers[0][0]==0x40&&dev.buffers[0][11]==0x4b&&dev.buffers[1][0]==1&&dev.buffers[1][5]==6);
    CHECK(s.u32(0x130)==4&&s.u32(0x130+4)==3&&s.u32(0x130+8)==1);
    CHECK(s.u32(0x58)==5);
    CHECK(dev.textures[0].data==video.data()+0x20&&dev.textures[0].size==0x80&&!dev.textures[0].cube&&dev.textures[0].mip_levels==4);
    // Texture replacement (the Tweaks pack layout). XXH32 reference vectors (python-xxhash 3.x), then the synthetic
    // texture swapped for <pack>/0_<XXH32>_4x8.dds, matched case-insensitively, created with the game's own request.
    const auto bytes=[](const char* text){return reinterpret_cast<const std::uint8_t*>(text);};
    CHECK(pmt_xxh32(bytes(""),0)==0x02cc5d05u&&pmt_xxh32(bytes("abc"),3)==0x32d153ffu);
    CHECK(pmt_xxh32(bytes("Nobody inspects the spammish repetition"),39)==0xe2293b2fu);   // the 16-byte stripe path
    CHECK(pmt_xxh32(bytes("abc"),3,0x9e3779b1u)==0xa1ae7709u);
    {
        namespace fs=std::filesystem;
        const auto dir=fs::temp_directory_path()/"or2_texture_replacement_test";
        fs::remove_all(dir);
        // A 4x4 DXT1 DDS, one level (0x88 bytes); `width` changes the header only.
        const auto dxt1=[](std::uint32_t width,std::uint32_t fourcc){
            std::vector<std::uint8_t> f(0x88,0x5a);std::memset(f.data(),0,0x80);std::memcpy(f.data(),"DDS ",4);
            auto put=[&](std::size_t o,std::uint32_t v){std::memcpy(f.data()+o,&v,4);};
            put(4,124);put(12,4);put(16,width);put(28,1);put(76,32);put(80,4);put(84,fourcc);return f;};
        const auto dds=dxt1(4,0x31545844u),truncated=dxt1(64,0x31545844u),dxt2=dxt1(4,0x32545844u);
        {PcTextureFileRequest q{};q.data=dds.data();q.size=std::uint32_t(dds.size());q.mip_levels=4;q.mip_filter=3;
         PcDdsTexture tex;std::string error;CHECK(pc_d3dx_texture_from_file(q,tex,error));}   // the device's reader takes it
        PcPmtResources a{};std::vector<std::uint8_t> av;synthetic_pmt(a,av);
        char name[64];std::snprintf(name,sizeof name,"0_%X_4x8.DDS",pmt_xxh32(av.data()+0x20,0x80));
        const auto write=[&](const char* pack,const std::vector<std::uint8_t>& f){
            fs::create_directories(dir/pack);std::ofstream o(dir/pack/name,std::ios::binary);
            o.write(reinterpret_cast<const char*>(f.data()),std::streamsize(f.size()));};
        write("cs_TEST_pmt",dds);write("cs_SHORT_pmt",truncated);write("cs_DXT2_pmt",dxt2);
        const auto bank=[&](const char* pack,PcPmtResources& r,std::vector<std::uint8_t>& v){synthetic_pmt(r,v);r.pack_name=pack;};
        // Off: not consulted, even with a matching file.
        a.pack_name="cs_TEST_pmt";pmt_texture_replacement_configure(false,dir.string());
        {Device d;load_synthetic(a,d);CHECK(d.textures[0].data==av.data()+0x20&&pmt_texture_replacement_stats().looked==0);}
        pmt_texture_replacement_configure(true,dir.string());
        CHECK(pmt_texture_replacement_stats().files==3);
        {PcPmtResources r{};std::vector<std::uint8_t> v;bank("cs_TEST_pmt",r,v);Device d;load_synthetic(r,d);
         CHECK(d.textures.size()==1&&d.texture_bytes[0]==dds&&d.textures[0].mip_levels==4&&d.textures[0].mip_filter==3);}
        // Another bank: no folder of its own and no loose file, so it keeps its texture.
        {PcPmtResources r{};std::vector<std::uint8_t> v;bank("cs_OTHER_pmt",r,v);Device d;load_synthetic(r,d);
         CHECK(d.textures.size()==1&&d.textures[0].data==v.data()+0x20);}
        // A file shorter than its levels, and an unsupported FourCC: never offered to the device; stock kept.
        for(const char* pack:{"cs_SHORT_pmt","cs_DXT2_pmt"}){
            PcPmtResources r{};std::vector<std::uint8_t> v;bank(pack,r,v);Device d;load_synthetic(r,d);
            CHECK(d.textures.size()==1&&d.textures[0].data==v.data()+0x20);}
        // The device refuses the replacement: the stock texture is created in its place.
        {PcPmtResources r{};std::vector<std::uint8_t> v;bank("cs_TEST_pmt",r,v);Device d;d.refuse_size=0x88;load_synthetic(r,d);
         CHECK(d.textures.size()==2&&d.texture_bytes[0]==dds&&d.textures[1].data==v.data()+0x20);
         CHECK(Bytes(r.system.data(),r.system.size()).u32(0x58)!=0);}
        const auto& st=pmt_texture_replacement_stats();
        CHECK(st.looked==5&&st.replaced==1&&st.failed==3);
        pmt_texture_replacement_configure(false,{});fs::remove_all(dir);
    }
    std::printf("pc pmt loader: %u checks\n",checks);
    return 0;
}
