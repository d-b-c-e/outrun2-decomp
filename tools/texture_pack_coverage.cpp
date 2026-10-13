// Offline coverage of an OutRun2006Tweaks texture pack against the game's PMT (scene) banks, read only.
// Per bank with textures: what the replacement code replaces (pmt_textures_step_42e8c0 on a counting device, the real
// path), and for every texture it does not replace, why: no file anywhere, a file only in another bank's folder (a
// stage variant such as cs_CS_PALM_pmt vs cs_CS_PALM_T_pmt; Tweaks looks in the bank's own folder and the pack root
// only), or a file the device would refuse (cube request with a 2D dump, pixel format, shorter than its levels).
// Then the pack folders no bank names and the files no texture matches.
// Usage: texture_pack_coverage <game folder> <pack load folder>
#include "platform/pc_pmt_loader.hpp"
#include "platform/pc_scene_renderer.hpp"
#include "platform/retail_asset_store.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
using namespace outrun::platform;
namespace fs=std::filesystem;
namespace {
std::string lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
std::uint32_t u32(const std::uint8_t* p){std::uint32_t v;std::memcpy(&v,p,4);return v;}
// Creates nothing: every texture request gets a handle, so the replacement code's own counts are the result.
struct CountingDevice final:PcD3D9Device{
    std::uint32_t next{};
    std::uint32_t create_texture_from_file(const PcTextureFileRequest&)override{return ++next;}
    std::uint32_t create_vertex_buffer(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t)override{return ++next;}
    std::uint32_t create_index_buffer(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t)override{return ++next;}
    std::uint8_t* lock(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t)override{return nullptr;}
    void unlock(std::uint32_t)override{}
    std::uint32_t create_vertex_declaration(const PcVertexElement*)override{return ++next;}
    std::uint32_t create_vertex_shader(const std::uint32_t*)override{return ++next;}
    void set_vertex_shader_constant_f(std::uint32_t,const float*,std::uint32_t)override{}
    void release(std::uint32_t)override{}
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
    std::uint32_t create_pixel_shader(const std::uint32_t*)override{return ++next;}
    void set_pixel_shader(std::uint32_t)override{}
    void set_pixel_shader_constant_f(std::uint32_t,const float*,std::uint32_t)override{}
    std::uint32_t get_declaration(std::uint32_t,PcVertexElement*)override{return 0;}
};
struct PackFile { fs::path path; std::string folder; bool used{}; };
// Why the device would refuse a pack file for this request (empty: usable), from its header and size, as
// replacement_usable() decides, A8B8G8R8 counted usable (converted).
std::string refusal(const fs::path& p,bool cube){
    std::error_code e;const auto n=fs::file_size(p,e);
    std::uint8_t h[0x80]{};std::ifstream f(p,std::ios::binary);
    if(e||n<0x80||!f.read(reinterpret_cast<char*>(h),0x80))return "unreadable";
    if(std::memcmp(h,"DDS ",4)||u32(h+4)!=124u)return "not a DDS";
    const auto height=u32(h+12),width=u32(h+16),levels=std::max(1u,u32(h+28)),pf=u32(h+80),fourcc=u32(h+84),bits=u32(h+88);
    const auto r=u32(h+92),b=u32(h+100),caps2=u32(h+112);
    std::uint32_t block=0;
    if(pf&4u){if(fourcc==0x31545844u)block=8;else if(fourcc==0x33545844u||fourcc==0x35545844u)block=16;else return "format (FourCC)";}
    else if(!(bits==32u&&((r==0xff0000u&&b==0xffu)||(r==0xffu&&b==0xff0000u))))return "format ("+std::to_string(bits)+"-bit RGB)";
    if(cube&&!caps2)return "cube request, 2D file";
    if(!cube&&caps2)return "2D request, cube file";
    std::uint64_t need=0x80;
    for(unsigned face=0;face<(cube?6u:1u);++face)
        for(std::uint32_t l=0,w=width,hh=height;l<levels;++l,w=std::max(1u,w/2),hh=std::max(1u,hh/2))
            need+=block?std::uint64_t(std::max(1u,(w+3)/4))*std::max(1u,(hh+3)/4)*block:std::uint64_t(w)*hh*4u;
    if(need>n)return "shorter than its levels";
    return {};
}
}
int main(int argc,char** argv){
    if(argc<3){std::fprintf(stderr,"usage: texture_pack_coverage <game folder> <pack load folder>\n");return 2;}
    RetailAssetStore store;std::string error;
    if(!retail_asset_store_open(store,argv[1],&error)){std::fprintf(stderr,"game folder: %s\n",error.c_str());return 2;}
    const auto root=fs::u8path(argv[2]);
    // The pack: lower-case relative path -> file; "<hash>_<w>x<h>.dds" (index prefix dropped) -> files, for the
    // cross-folder search.
    std::map<std::string,PackFile> files;std::map<std::string,std::vector<std::string>> by_hash;std::set<std::string> folders;
    std::error_code e;
    for(fs::recursive_directory_iterator it(root,e),end;!e&&it!=end;it.increment(e)){
        if(!it->is_regular_file(e))continue;
        auto rel=fs::relative(it->path(),root,e).generic_u8string();const auto key=lower(std::string(rel.begin(),rel.end()));
        if(key.size()<4||key.substr(key.size()-4)!=".dds")continue;
        const auto slash=key.find('/');const auto folder=slash==std::string::npos?std::string():key.substr(0,slash);
        files[key]={it->path(),folder,false};folders.insert(folder);
        auto name=slash==std::string::npos?key:key.substr(slash+1);
        if(const auto u=name.find('_');u!=std::string::npos&&name.find('_',u+1)!=std::string::npos)name=name.substr(u+1);   // index prefix
        by_hash[name].push_back(key);
    }
    pmt_texture_replacement_configure(true,argv[2]);
    std::map<std::string,unsigned> totals;std::set<std::string> named_folders;
    for(std::uint32_t id=0;id<0x223u;++id){
        const auto pack=pc_bank_pack_name(id);
        if(pack.size()<5||lower(pack.substr(pack.size()-4))!="_pmt")continue;
        const auto* record=retail_asset_lookup(store,id,8u,&error);std::vector<std::uint8_t> pmt;
        if(!record||!retail_asset_inflate_sz(record->bytes,pmt)||pmt.size()<16){++totals["banks not read"];continue;}
        const auto system=u32(pmt.data()+8),video=u32(pmt.data()+12);
        if(std::uint64_t(system)+video+16u>pmt.size()){++totals["banks malformed"];continue;}
        PcPmtResources r;r.system.assign(pmt.begin()+16,pmt.begin()+16+system);r.video=pmt.data()+16+system;r.video_size=video;r.pack_name=pack;
        if(r.system.size()<12)continue;
        const auto objects=u32(r.system.data()+4),count=u32(r.system.data()+8);
        PcPmtLoadCursor c;c.offset_95b234=0x18u+objects*0x3cu;
        if(std::uint64_t(c.offset_95b234)+std::uint64_t(count)*4u+0x10u+std::uint64_t(count)*0x14u>r.system.size()){++totals["banks malformed"];continue;}
        if(!count)continue;
        pmt_textures_begin_42e850(r,c);
        const auto first=c.texture_record_957be0;
        // The real path, then the reasons.
        CountingDevice device;
        try{while(!pmt_textures_step_42e8c0(r,c,device)){}}catch(const std::exception& x){std::printf("%02x %s: %s\n",id,pack.c_str(),x.what());continue;}
        const auto stem=lower(pack);const bool has_folder=folders.count(stem)!=0;if(has_folder)named_folders.insert(stem);
        unsigned matched=0,nofile=0,elsewhere=0,other_index=0;std::map<std::string,unsigned> refused;std::set<std::string> other_folders;
        for(std::uint32_t k=0;k<count;++k){
            const auto* rec=r.system.data()+first+k*0x14u;
            const auto source=u32(rec+4),size=u32(rec+0x10)+0x80u;
            if(std::uint64_t(source)+size>r.video_size){++refused["record outside the video section"];continue;}
            const auto* data=r.video+source;const bool cube=u32(data+0x70)!=0u;
            char name[64];std::snprintf(name,sizeof name,"%x_%ux%u.dds",pmt_xxh32(data,size),u32(data+16),u32(data+12));
            const std::string idx=std::to_string(k)+"_"+name;
            const PackFile* hit=nullptr;std::string hit_key;
            for(const auto& key:{stem+"/"+idx,stem+"/"+std::string(name),idx,std::string(name)}){
                const auto it=files.find(key);if(it!=files.end()){hit=&it->second;hit_key=key;break;}}
            if(hit){
                files[hit_key].used=true;
                if(const auto why=refusal(hit->path,cube);!why.empty())++refused[why];else ++matched;
                continue;
            }
            if(const auto it=by_hash.find(name);it!=by_hash.end()){
                // In its own folder under another index prefix (Tweaks' index differs from the position), or a variant's.
                bool own=false;
                for(const auto& key:it->second){own|=files[key].folder==stem;if(files[key].folder!=stem)other_folders.insert(files[key].folder);files[key].used=true;}
                if(own)++other_index;else ++elsewhere;}
            else ++nofile;
        }
        PmtTextureReplacementBank b{};
        if(const auto it=pmt_texture_replacement_banks().find(pack);it!=pmt_texture_replacement_banks().end())b=it->second;
        std::printf("%03x %-34s %s %3u textures: replaced %3u (usable match %u), no file %u, own folder other index %u, only in other folders %u",
            id,pack.c_str(),has_folder?"folder   ":"no folder",count,b.replaced,matched,nofile,other_index,elsewhere);
        if(!other_folders.empty()){std::string s;for(const auto& f:other_folders)s+=(s.empty()?"":", ")+f;std::printf(" [%s]",s.c_str());}
        for(const auto& [why,n]:refused)std::printf(", refused %u (%s)",n,why.c_str());
        std::printf("\n");
        totals["textures"]+=count;totals["replaced"]+=b.replaced;totals["no file"]+=nofile;totals["only in other folders"]+=elsewhere;
        totals["own folder other index"]+=other_index;
        for(const auto& [why,n]:refused)totals["refused: "+why]+=n;
        if(b.replaced!=matched)std::printf("    note: the real path replaced %u, the header check expected %u\n",b.replaced,matched);
    }
    std::printf("totals:");for(const auto& [k,v]:totals)std::printf(" %s %u;",k.c_str(),v);std::printf("\n");
    for(const auto& f:folders)if(!f.empty()&&f.rfind("spr_",0)!=0&&!named_folders.count(f))std::printf("pack folder no PMT bank names: %s\n",f.c_str());
    std::map<std::string,unsigned> unused;
    for(const auto& [key,f]:files)if(!f.used&&f.folder.rfind("spr_",0)!=0)++unused[f.folder];
    for(const auto& [folder,n]:unused)std::printf("pack files no texture matches: %s %u\n",folder.empty()?"(root)":folder.c_str(),n);
    pmt_texture_replacement_configure(false,{});
    return 0;
}
