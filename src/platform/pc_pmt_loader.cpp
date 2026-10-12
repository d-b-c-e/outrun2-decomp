#include "platform/pc_pmt_loader.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
namespace outrun::platform {
// XXH32 (the xxHash 32-bit hash, as OutRun2006Tweaks names its replacement textures with).
std::uint32_t pmt_xxh32(const std::uint8_t* p,std::size_t len,std::uint32_t seed){
    constexpr std::uint32_t P1=2654435761u,P2=2246822519u,P3=3266489917u,P4=668265263u,P5=374761393u;
    auto rotl=[](std::uint32_t x,int r){return (x<<r)|(x>>(32-r));};
    auto rd=[](const std::uint8_t* q){std::uint32_t v;std::memcpy(&v,q,4);return v;};
    auto round=[&](std::uint32_t a,std::uint32_t in){a+=in*P2;a=rotl(a,13);return a*P1;};
    std::size_t left=len;std::uint32_t h;   // the remaining length, so no pointer is formed past the end
    if(left>=16){
        std::uint32_t v1=seed+P1+P2,v2=seed+P2,v3=seed,v4=seed-P1;
        do{v1=round(v1,rd(p));v2=round(v2,rd(p+4));v3=round(v3,rd(p+8));v4=round(v4,rd(p+12));p+=16;left-=16;}while(left>=16);
        h=rotl(v1,1)+rotl(v2,7)+rotl(v3,12)+rotl(v4,18);
    }else h=seed+P5;
    h+=std::uint32_t(len);
    for(;left>=4;p+=4,left-=4){h+=rd(p)*P3;h=rotl(h,17)*P4;}
    for(;left;++p,--left){h+=std::uint32_t(*p)*P5;h=rotl(h,11)*P1;}
    h^=h>>15;h*=P2;h^=h>>13;h*=P3;h^=h>>16;return h;
}
namespace {
struct Replacement {
    bool enabled{};std::filesystem::path root;
    std::map<std::string,std::filesystem::path> files;   // lower-case relative path with '/' -> file
    std::map<std::string,int> folders;                   // lower-case first components of those paths
    PmtTextureReplacementStats stats;
    std::map<std::string,PmtTextureReplacementBank> banks;
};
Replacement& replacement(){static Replacement r;return r;}
std::string lower(std::string s){for(auto& ch:s)ch=char(std::tolower(static_cast<unsigned char>(ch)));return s;}
// A replacement file read whole, refused above 128 MB (a 4096x4096 A8R8G8B8 chain is 90 MB).
bool read_replacement(const std::filesystem::path& path,std::vector<std::uint8_t>& out){
    try{
        std::error_code e;const auto n=std::filesystem::file_size(path,e);
        if(e||n<0x80u||n>(std::uintmax_t(128u)<<20))return false;
        std::ifstream f(path,std::ios::binary);if(!f)return false;
        out.resize(std::size_t(n));f.read(reinterpret_cast<char*>(out.data()),std::streamsize(n));
        return f.gcount()==std::streamsize(n);
    }catch(...){return false;}   // allocation or stream failure: the stock texture
}
// A DDS the device's reader (pc_d3dx_texture_from_file) accepts, checked before the device sees it: a file that reader
// refuses is a device error, and the renderer ends the run on any device error (renderer_present.inc). At least as
// strict as that reader: DXT1/3/5 or A8R8G8B8, the request's cube kind, 1..16384 texels a side, no more levels than
// the full chain, every stored level inside the file.
bool replacement_usable(const std::vector<std::uint8_t>& f,bool cube){
    if(f.size()<0x80u||std::memcmp(f.data(),"DDS ",4)!=0)return false;
    auto u=[&](std::size_t o){std::uint32_t v;std::memcpy(&v,f.data()+o,4);return v;};
    if(u(4)!=124u)return false;                                                 // dwSize
    const std::uint32_t height=u(12),width=u(16),levels=std::max(1u,u(28));
    const std::uint32_t pf=u(80),fourcc=u(84),bits=u(88),caps2=u(112);
    std::uint32_t block=0;                                                      // bytes per 4x4 block, 0 = A8R8G8B8
    if(pf&4u){if(fourcc==0x31545844u)block=8;else if(fourcc==0x33545844u||fourcc==0x35545844u)block=16;else return false;}
    else if(!(bits==32u&&u(92)==0xff0000u&&u(96)==0xff00u&&u(100)==0xffu))return false;
    if(cube!=(caps2!=0u)||width<1u||height<1u||width>16384u||height>16384u)return false;
    std::uint32_t chain=1;for(std::uint32_t w=width,h=height;w>1||h>1;w=std::max(1u,w/2),h=std::max(1u,h/2))++chain;
    if(levels>chain)return false;
    std::uint64_t need=0x80;
    for(unsigned face=0;face<(cube?6u:1u);++face)
        for(std::uint32_t l=0,w=width,h=height;l<levels;++l,w=std::max(1u,w/2),h=std::max(1u,h/2))
            need+=block?std::uint64_t(std::max(1u,(w+3)/4))*std::max(1u,(h+3)/4)*block:std::uint64_t(w)*h*4u;
    return need<=f.size();
}
// The replacement DDS of a bank texture, if the pack has a usable one (Tweaks' search order: the first name present).
bool find_replacement(const std::string& pack,std::uint32_t index,const std::uint8_t* data,std::size_t size,bool cube,std::vector<std::uint8_t>& out){
    auto& r=replacement();
    if(size<0x80||std::memcmp(data,"DDS ",4)!=0)return false;
    std::uint32_t height,width;std::memcpy(&height,data+12,4);std::memcpy(&width,data+16,4);
    char name[64];std::snprintf(name,sizeof name,"%X_%ux%u.dds",pmt_xxh32(data,size),width,height);
    const std::string idx=std::to_string(index)+"_"+name,stem=lower(pack);
    auto& bank=r.banks[pack];if(!bank.looked)bank.folder=r.folders.count(stem)!=0;
    ++r.stats.looked;++bank.looked;
    for(const std::string& key:{stem+"/"+lower(idx),stem+"/"+lower(name),lower(idx),lower(std::string(name))}){
        const auto it=r.files.find(key);if(it==r.files.end())continue;
        if(read_replacement(it->second,out)&&replacement_usable(out,cube))return true;
        ++r.stats.failed;++bank.failed;out.clear();return false;
    }
    return false;
}
using driving::Bytes;
const std::uint8_t* video_bytes(const PcPmtResources& r,std::uint32_t source,std::uint32_t length){
    if(source>r.video_size||length>r.video_size-source)throw std::out_of_range("PMT video section");
    return r.video+source;
}
std::uint32_t upload(PcD3D9Device& device,std::uint32_t buffer,const std::uint8_t* from,std::uint32_t length){
    auto* p=device.lock(buffer,0,length,0);
    if(!p)throw std::runtime_error("PMT buffer Lock failed (the PC would copy through an invalid pointer)");
    std::memcpy(p,from,length);
    device.unlock(buffer);
    return buffer;
}
}
void pmt_objects_begin_42e490(PcPmtResources& r,PcPmtLoadCursor& c){
    auto s=r.view();
    r.object_table_20=0x14;
    s.put32(0x14,0x18);        // the PC stores the absolute pointer system+0x18
    r.object_count_0c=s.u32(4);
    c.object_95b230=0;c.offset_95b234=0x18;
    r.texture_table_24=0;
}
bool pmt_objects_step_42e4f0(PcPmtResources& r,PcPmtLoadCursor& c,PcD3D9Device& device,PcShaderCache& cache,
    const PcShaderGlobals& globals){
    auto s=r.view();
    if(c.object_95b230>=r.object_count_0c)return true;
    const auto o=c.offset_95b234+c.object_95b230*0x3cu;
    s.check(o,0x3c); // fields +04..+38 are relocated by the PC; they stay offsets here
    const auto info=s.u32(o+0x18);
    const auto groups=s.i32(info+0x24);
    for(std::int32_t g=0;g<groups;++g){
        const auto group=s.u32(o+0x30)+std::uint32_t(g)*0x2cu;
        for(std::int32_t k=0;k<s.i32(group);++k){
            const auto slot=s.u32(o+0x08)+std::uint32_t(g)*16u+std::uint32_t(k)*4u;
            const auto source=s.u32(s.u32(slot)+4);
            const auto length=s.u32(group+0x1c);
            const auto vb=device.create_vertex_buffer(length,0,0,1);
            s.put32(slot,vb?upload(device,vb,video_bytes(r,source,length),length):0u);
        }
        {
            const auto slot=s.u32(o+0x04)+std::uint32_t(g)*4u;
            const auto source=s.u32(s.u32(slot)+4);
            const auto length=s.u32(group+0x18);
            const auto ib=device.create_index_buffer(length,0,0x65,1); // D3DFMT_INDEX16, D3DPOOL_MANAGED
            s.check(source,length); // index data is copied from the system section
            s.put32(slot,ib?upload(device,ib,r.system.data()+source,length):0u);
        }
        const auto count=s.i32(s.u32(o+0x18)+0x2c);
        for(std::int32_t k=0;k<count;++k){
            const auto rec=s.u32(o+0x0c)+std::uint32_t(count*g+k)*0x18u;
            const auto material=s.u32(o+0x34)+std::uint32_t(k)*0x58u;
            material_setup_40fd70(s.sub(rec,0x18),s.sub(material,0x58),s.sub(group,0x2c),device,cache,globals);
        }
    }
    ++c.object_95b230;
    if(c.object_95b230<r.object_count_0c)return false;
    c.offset_95b234+=r.object_count_0c*0x3cu;
    return true;
}
void pmt_textures_begin_42e850(PcPmtResources& r,PcPmtLoadCursor& c){
    auto s=r.view();
    const auto header=c.offset_95b234;
    r.texture_table_24=header;
    s.put32(header,header+4); // absolute pointer on the PC
    c.offset_95b234=header+s.u32(8)*4u+4u;
    r.texture_records_04=c.offset_95b234+0xc;
    c.texture_record_957be0=r.texture_records_04;
    c.texture_95b22c=0;
}
void pmt_texture_replacement_configure(bool enabled,const std::string& load_folder){
    auto& r=replacement();r=Replacement{};r.enabled=enabled;
    if(!enabled)return;
    std::error_code e;r.root=std::filesystem::u8path(load_folder);
    for(std::filesystem::recursive_directory_iterator it(r.root,e),end;!e&&it!=end;it.increment(e)){
        if(!it->is_regular_file(e))continue;
        auto rel=std::filesystem::relative(it->path(),r.root,e).generic_u8string();
        if(e||rel.size()<4||lower(rel.substr(rel.size()-4))!=".dds")continue;
        const auto key=lower(std::string(rel.begin(),rel.end()));
        if(const auto slash=key.find('/');slash!=std::string::npos)r.folders.emplace(key.substr(0,slash),0);
        r.files.emplace(key,it->path());
    }
    r.stats.files=std::uint32_t(r.files.size());
    std::printf("texture replacement: %u scene textures indexed under %s\n",r.stats.files,load_folder.c_str());
}
const PmtTextureReplacementStats& pmt_texture_replacement_stats(){return replacement().stats;}
const std::map<std::string,PmtTextureReplacementBank>& pmt_texture_replacement_banks(){return replacement().banks;}
bool pmt_textures_step_42e8c0(PcPmtResources& r,PcPmtLoadCursor& c,PcD3D9Device& device){
    auto s=r.view();
    const auto count=s.u32(8);
    if(c.texture_95b22c>=count)return true;
    const auto table=s.u32(r.texture_table_24);
    const auto slot=table+c.texture_95b22c*4u;
    const auto record=c.texture_record_957be0;
    const auto source=s.u32(record+4);
    s.put32(slot,0);
    PcTextureFileRequest q{};
    q.size=s.u32(record+0x10)+0x80u;
    q.data=video_bytes(r,source,q.size);
    std::uint32_t caps2;std::memcpy(&caps2,video_bytes(r,source+0x70,4),4); // DDS dwCaps2
    q.width=q.height=0xffffffffu;q.usage=0;q.format=0;q.pool=1;q.filter=1;q.colour_key=0;
    if(caps2){q.cube=true;q.mip_levels=0xffffffffu;q.mip_filter=1;}
    else{q.cube=false;q.mip_levels=4;q.mip_filter=3;}
    std::uint32_t handle=0;std::vector<std::uint8_t> swap;
    if(replacement().enabled&&!r.pack_name.empty()&&find_replacement(r.pack_name,c.texture_95b22c,q.data,q.size,q.cube,swap)){
        auto alt=q;alt.data=swap.data();alt.size=std::uint32_t(swap.size());   // the same request, the pack's file
        handle=device.create_texture_from_file(alt);
        auto& bank=replacement().banks[r.pack_name];
        if(handle){++replacement().stats.replaced;++bank.replaced;}
        else{++replacement().stats.failed;++bank.failed;}   // not created: the stock texture
    }
    if(!handle)handle=device.create_texture_from_file(q);
    s.put32(slot,handle);
    c.texture_record_957be0+=0x14;
    ++c.texture_95b22c;
    return c.texture_95b22c>=s.u32(8);
}
std::uint32_t pmt_object_shaders_4103f0(PcPmtResources& r,std::uint32_t index,std::uint32_t kind,PcD3D9Device& device,
    PcShaderCache& cache,const PcShaderGlobals& globals){
    device.set_vertex_shader(0);
    if(index>=r.object_count_0c)return 0u;
    auto s=r.view();
    const auto o=s.u32(r.object_table_20)+index*0x3cu;
    const auto info=s.u32(o+0x18);
    const std::int32_t groups=s.i32(info+0x24),count=s.i32(info+0x2c);
    for(std::int32_t g=0;g<groups;++g){
        const auto group=s.u32(o+0x30)+std::uint32_t(g)*0x2cu;
        for(std::int32_t k=0;k<count;++k){
            const auto rec=s.u32(o+0x0c)+std::uint32_t(count*g+k)*0x18u;
            const auto material=s.u32(o+0x34)+std::uint32_t(k)*0x58u;
            material_kind_setup_4104d0(s.sub(rec,0x18),s.sub(material,0x58),s.sub(group,0x2c),kind,device,cache,globals);
        }
    }
    return 1u;
}
std::uint32_t pmt_object_group_type_4066d0(PcPmtResources& r,std::uint32_t index,std::uint32_t type){
    if(index>=r.object_count_0c)return 0u;
    auto s=r.view();
    const auto o=s.u32(r.object_table_20)+index*0x3cu;
    const std::int32_t groups=s.i32(s.u32(o+0x18)+0x24);
    for(std::int32_t g=0;g<groups;++g)s.put32(s.u32(o+0x30)+std::uint32_t(g)*0x2cu+0x28u,type);
    return 1u;
}
std::uint32_t pmt_object_mesh_flags_406730(PcPmtResources& r,std::uint32_t index,std::uint32_t and_mask,std::uint32_t or_mask){
    if(index>=r.object_count_0c)return 0u;
    auto s=r.view();
    const auto mesh=s.u32(s.u32(r.object_table_20)+index*0x3cu+0x1c);
    s.put32(mesh,(s.u32(mesh)&and_mask)|or_mask);
    return 1u;
}
}
