#include "platform/pc_pmt_loader.hpp"
#include "platform/pc_dds.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
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
    bool scene{},ui{},any_folder{};std::filesystem::path root;
    bool scene_extract{},ui_extract{};std::filesystem::path dump;
    std::set<std::string> dump_attempts;
    PmtTextureExtractionStats extraction,ui_extraction;
    UiPromptSelection prompts;
    std::map<std::string,std::filesystem::path> files;   // lower-case relative path with '/' -> file
    std::map<std::string,int> folders;                   // lower-case first components of those paths
    // TextureMatchAnyFolder: lower-case "<hash>_<w>x<h>.dds" (index prefix dropped) -> its files anywhere in the pack,
    // and the answer once decided: the file to use, or empty when the files differ (ambiguous).
    std::map<std::string,std::vector<std::filesystem::path>> by_name;
    std::map<std::string,std::filesystem::path> resolved;
    PmtTextureReplacementStats stats,ui_stats;           // scene (PMT) banks, sprite (XST) banks
    std::map<std::string,PmtTextureReplacementBank> banks,ui_banks;
};
// A pack file name without Tweaks' index prefix: "12_ab12cd34_64x64.dds" -> "ab12cd34_64x64.dds". Only a run of
// digits followed by a second underscore is a prefix ("12345678_64x64.dds" has none).
std::string without_index(const std::string& name){
    const auto u=name.find('_');
    if(u==std::string::npos||u==0||name.find('_',u+1)==std::string::npos)return name;
    for(std::size_t k=0;k<u;++k)if(!std::isdigit(static_cast<unsigned char>(name[k])))return name;
    return name.substr(u+1);
}
// Tweaks' FxtHashRemappings: sprites the FXT mod edits in the game files, looked up by the vanilla file's hash (when
// the size matches too), so a pack made from vanilla dumps still applies.
struct FxtRemap { std::uint32_t from,to,width,height; };
constexpr FxtRemap FxtRemaps[]{
    {0x8F1D69D3u,0x1F77CB88u,128,128},                                                        // spr_sprani_etc_cvt_Exst
    {0x8F663222u,0x4AFC1BEDu,512,512},{0x469F516Du,0xF043316Bu,1024,512},{0x86325B15u,0x8B52FEECu,1024,512}, // spr_sprani_CLAR_RANK_Exst
    {0x1025284Eu,0xC4A2937Bu,1024,1024},{0xFDC4E73Bu,0x39229D64u,1024,1024},                  // spr_sprani_game_cvt_Exst
    {0x50D487A9u,0xACF61D7Cu,1024,512},{0x2F7DFED1u,0x31C58963u,512,256},{0x678170B1u,0x4DC09A74u,1024,1024}, // spr_sprani_sumo_fe_cvt_Exst
    {0x698EEA48u,0x2DA43E41u,1024,1024}};                                                     // spr_sprani_selector_cvt_Exst
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
bool replacement_usable(const std::uint8_t* data,std::size_t size,bool cube){
    if(!data||size<0x80u||std::memcmp(data,"DDS ",4)!=0)return false;
    auto u=[&](std::size_t o){std::uint32_t v;std::memcpy(&v,data+o,4);return v;};
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
    return need<=size;
}
bool replacement_usable(const std::vector<std::uint8_t>& f,bool cube){return replacement_usable(f.data(),f.size(),cube);}
std::string stock_texture_name(const std::uint8_t* data,std::size_t size,std::uint32_t* hash_out=nullptr){
    std::uint32_t height,width;std::memcpy(&height,data+12,4);std::memcpy(&width,data+16,4);
    auto hash=pmt_xxh32(data,size);
    for(const auto& f:FxtRemaps)if(f.from==hash&&f.width==width&&f.height==height){hash=f.to;break;}
    if(hash_out)*hash_out=hash;
    char name[64];std::snprintf(name,sizeof name,"%X_%ux%u.dds",hash,width,height);return name;
}
// No GPU readback/encoding: keep the complete original file, not generated mip levels.
// All failures stay observational; the already-created stock resource is untouched.
void extract_stock_texture(bool ui,const std::string& pack,std::uint32_t index,const PcTextureFileRequest& q){
    auto& r=replacement();if(!(ui?r.ui_extract:r.scene_extract))return;
    auto& extraction=ui?r.ui_extraction:r.extraction;
    try{
        if(pack.empty()||pack=="."||pack==".."||pack.find_first_of("/\\:")!=std::string::npos||pack.find('\0')!=std::string::npos||
           q.size>(128u<<20)||!replacement_usable(q.data,q.size,q.cube)){
            ++extraction.unsupported;return;
        }
        std::uint32_t caps2;std::memcpy(&caps2,q.data+112,4);
        if(q.cube&&(caps2&0xfe00u)!=0xfe00u){++extraction.unsupported;return;}   // all six cube faces
        const auto name=std::to_string(index)+"_"+stock_texture_name(q.data,q.size);
        if(!r.dump_attempts.insert(pack+"/"+name).second)return;
        const auto directory=r.dump/std::filesystem::u8path(pack),path=directory/std::filesystem::u8path(name);
        auto failed=[&](const char* why){++extraction.failed;if(extraction.error.empty())extraction.error=why;};
        std::error_code error;std::filesystem::create_directories(directory,error);
        if(error){failed("cannot create dump directory");return;}
        // C11 exclusive create: never truncate an owner's existing dump, even across processes.
        errno=0;
#ifdef _WIN32
        auto* file=_wfopen(path.c_str(),L"wbx");
#else
        auto* file=std::fopen(path.c_str(),"wbx");
#endif
        if(!file){if(errno==EEXIST)++extraction.existing;else failed("cannot exclusively create dump file");return;}
        const bool wrote=std::fwrite(q.data,1,q.size,file)==q.size;
        const bool closed=std::fclose(file)==0;
        if(!wrote||!closed){std::filesystem::remove(path,error);failed("cannot finish dump file");return;}
        ++extraction.written;
    }catch(...){++extraction.failed;}   // optional extraction cannot abort texture creation
}
// An A8B8G8R8 (or X8B8G8R8) file as the A8R8G8B8 the device reads: R and B swapped in every texel and the masks
// rewritten, as Tweaks' loader converts D3DFMT_A8B8G8R8. About half of the owner's UI pack is stored this way.
void abgr_to_argb(std::vector<std::uint8_t>& f){
    if(f.size()<0x80u)return;
    auto u=[&](std::size_t o){std::uint32_t v;std::memcpy(&v,f.data()+o,4);return v;};
    if((u(80)&4u)||u(88)!=32u||u(92)!=0xffu||u(96)!=0xff00u||u(100)!=0xff0000u)return;
    for(std::size_t k=0x80;k+4<=f.size();k+=4)std::swap(f[k],f[k+2]);
    const std::uint32_t r=0xff0000u,b=0xffu;std::memcpy(f.data()+92,&r,4);std::memcpy(f.data()+100,&b,4);
}
// TextureMatchAnyFolder: the one file to use for a same-name match elsewhere in the pack, decided once per name. Several
// files are compared by size, then byte for byte; any difference (or a file that cannot be read) makes the name
// ambiguous (empty path).
std::filesystem::path any_folder_file(Replacement& r,const std::string& name){
    if(const auto it=r.resolved.find(name);it!=r.resolved.end())return it->second;
    const auto& list=r.by_name.at(name);
    std::filesystem::path pick=list.front();
    if(list.size()>1){
        std::error_code e;const auto n=std::filesystem::file_size(list.front(),e);
        std::vector<std::uint8_t> first,other;
        bool same=!e&&read_replacement(list.front(),first);
        for(std::size_t k=1;same&&k<list.size();++k){
            const auto m=std::filesystem::file_size(list[k],e);
            same=!e&&m==n&&read_replacement(list[k],other)&&other==first;
        }
        if(!same)pick.clear();
    }
    return r.resolved[name]=pick;
}
// The replacement DDS of a bank texture, if the pack has a usable one (Tweaks' search order: the first name present;
// then, with TextureMatchAnyFolder, a same-name file in another folder). `other`: it came from another folder.
bool find_replacement(bool ui,const std::string& pack,std::uint32_t index,const std::uint8_t* data,std::size_t size,bool cube,std::vector<std::uint8_t>& out,
    bool& other){
    other=false;
    auto& r=replacement();
    if(size<0x80||std::memcmp(data,"DDS ",4)!=0)return false;
    std::uint32_t hash{};const auto name=stock_texture_name(data,size,&hash);
    const std::string idx=std::to_string(index)+"_"+name,stem=lower(pack);
    auto& stats=ui?r.ui_stats:r.stats;
    auto& bank=(ui?r.ui_banks:r.banks)[pack];if(!bank.looked)bank.folder=r.folders.count(stem)!=0;
    ++stats.looked;++bank.looked;
    // Tweaks51acc: six pad-specific paths, only for its six post-FXT prompt
    // hashes, before the same four generic names. Reuse the indexed file map,
    // conversion, validation, device creation and stock fallback below.
    std::array<std::string,10> keys;std::size_t key_count=0;
    if(const char* pad=r.prompts.folder(ui,hash)){
        const std::string family(pad);
        keys={stem+"/"+family+"/"+lower(idx),stem+"/"+family+"/"+lower(name),
              family+"/"+stem+"/"+lower(idx),family+"/"+stem+"/"+lower(name),
              family+"/"+lower(idx),family+"/"+lower(name)};
        key_count=6;
    }
    for(const auto& key:{stem+"/"+lower(idx),stem+"/"+lower(name),lower(idx),lower(std::string(name))})keys[key_count++]=key;
    for(std::size_t k=0;k<key_count;++k){
        const auto& key=keys[k];
        const auto it=r.files.find(key);if(it==r.files.end())continue;
        if(read_replacement(it->second,out)){abgr_to_argb(out);if(replacement_usable(out,cube))return true;}
        ++stats.failed;++bank.failed;out.clear();return false;
    }
    if(!r.any_folder)return false;
    const auto key=lower(std::string(name));
    if(!r.by_name.count(key))return false;
    const auto file=any_folder_file(r,key);
    if(file.empty()){++stats.ambiguous;++bank.ambiguous;return false;}
    if(read_replacement(file,out)){abgr_to_argb(out);if(replacement_usable(out,cube)){other=true;return true;}}
    ++stats.failed;++bank.failed;out.clear();return false;
}
// The texture of request q, created from the pack's replacement when it has a usable one (the same request, only the
// file differs, as Tweaks does), else from q. A replacement the device does not create leaves the stock texture.
std::uint32_t create_from_pack(PcD3D9Device& device,bool ui,const std::string& pack,std::uint32_t index,const PcTextureFileRequest& q,
    bool& replaced){
    replaced=false;std::vector<std::uint8_t> swap;
    const auto start=std::chrono::steady_clock::now();
    auto& r=replacement();
    auto spent=[&]{auto& m=ui?r.ui_banks:r.banks;
        if(const auto it=m.find(pack);it!=m.end())it->second.seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();};
    bool other=false;
    if((ui?r.ui:r.scene)&&find_replacement(ui,pack,index,q.data,q.size,q.cube,swap,other)){
        auto alt=q;alt.data=swap.data();alt.size=std::uint32_t(swap.size());
        const auto handle=device.create_texture_from_file(alt);
        auto& stats=ui?r.ui_stats:r.stats;auto& bank=(ui?r.ui_banks:r.banks)[pack];
        spent();
        if(handle){++stats.replaced;++bank.replaced;if(other){++stats.other;++bank.other;}replaced=true;return handle;}
        ++stats.failed;++bank.failed;
    }else spent();
    const auto handle=device.create_texture_from_file(q);
    if(handle)extract_stock_texture(ui,pack,index,q);
    return handle;
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
void pmt_texture_replacement_configure(bool enabled,const std::string& load_folder){texture_replacement_configure(enabled,false,load_folder);}
void texture_replacement_set_pad_family(UiPromptFamily family){replacement().prompts.current=family;}
void texture_replacement_configure(bool scene,bool ui,const std::string& load_folder,bool any_folder,bool scene_extract,bool ui_extract){
    auto& r=replacement();r=Replacement{};r.scene=scene;r.ui=ui;r.any_folder=any_folder;
    r.scene_extract=scene_extract;r.ui_extract=ui_extract;
    if(scene_extract||ui_extract){
        if(load_folder.empty()){
            for(auto* stats:{scene_extract?&r.extraction:nullptr,ui_extract?&r.ui_extraction:nullptr})
                if(stats){++stats->failed;stats->error="empty extraction base";}
            r.scene_extract=r.ui_extract=false;
        }
        else{r.dump=std::filesystem::u8path(load_folder).parent_path()/"dump";
            std::printf("texture extraction: scene %s, UI %s; original DDS files in the sibling dump folder of %s\n",
                scene_extract?"on":"off",ui_extract?"on":"off",load_folder.c_str());}
    }
    if(!scene&&!ui)return;
    std::error_code e;r.root=std::filesystem::u8path(load_folder);
    for(std::filesystem::recursive_directory_iterator it(r.root,e),end;!e&&it!=end;it.increment(e)){
        if(!it->is_regular_file(e))continue;
        auto rel=std::filesystem::relative(it->path(),r.root,e).generic_u8string();
        if(e||rel.size()<4||lower(rel.substr(rel.size()-4))!=".dds")continue;
        const auto key=lower(std::string(rel.begin(),rel.end()));
        if(const auto slash=key.find('/');slash!=std::string::npos)r.folders.emplace(key.substr(0,slash),0);
        r.files.emplace(key,it->path());
        if(any_folder){const auto slash=key.find_last_of('/');r.by_name[without_index(slash==std::string::npos?key:key.substr(slash+1))].push_back(it->path());}
    }
    const auto files=std::uint32_t(r.files.size());
    if(scene)r.stats.files=files;
    if(ui)r.ui_stats.files=files;
    std::printf("texture replacement: %u files indexed under %s (scene %s, UI %s)\n",files,load_folder.c_str(),
        scene?"on":"off",ui?"on":"off");
}
const PmtTextureReplacementStats& pmt_texture_replacement_stats(){return replacement().stats;}
const PmtTextureExtractionStats& pmt_texture_extraction_stats(){return replacement().extraction;}
const PmtTextureExtractionStats& ui_texture_extraction_stats(){return replacement().ui_extraction;}
bool ui_texture_extraction_enabled(){return replacement().ui_extract;}
void ui_texture_extract_stock(const std::string& pack,std::uint32_t index,const std::uint8_t* stock,std::size_t size){
    if(!replacement().ui_extract)return;
    if(size>(128u<<20)){++replacement().ui_extraction.unsupported;return;}
    PcTextureFileRequest q{};q.data=stock;q.size=std::uint32_t(size);
    extract_stock_texture(true,pack,index,q);
}
const std::map<std::string,PmtTextureReplacementBank>& pmt_texture_replacement_banks(){return replacement().banks;}
bool ui_texture_replacement_enabled(){return replacement().ui;}
std::uint32_t ui_texture_replacement_create(PcD3D9Device& device,const std::string& pack,std::uint32_t index,const PcTextureFileRequest& q,
    bool& replaced){
    if(!(replacement().ui||replacement().ui_extract)||pack.empty()){replaced=false;return device.create_texture_from_file(q);}
    return create_from_pack(device,true,pack,index,q,replaced);
}
bool ui_texture_replacement_rgba(const std::string& pack,std::uint32_t index,const std::uint8_t* stock,std::size_t size,
    std::size_t max_bytes,std::uint32_t& width,std::uint32_t& height,std::vector<std::uint8_t>& rgba){
    rgba.clear();width=height=0;
    auto& r=replacement();
    if(!r.ui||pack.empty())return false;
    const auto start=std::chrono::steady_clock::now();
    std::vector<std::uint8_t> file;bool other=false;
    const bool found=find_replacement(true,pack,index,stock,size,false,file,other);
    auto& bank=r.ui_banks[pack];
    if(found){
        // The device reader's own decode (one level, the size from the file, as the sprite banks' request).
        PcTextureFileRequest q{};
        q.data=file.data();q.size=std::uint32_t(file.size());
        q.width=q.height=0xffffffffu;q.mip_levels=1;q.usage=0;q.format=0;q.pool=1;q.filter=1;q.mip_filter=1;q.colour_key=0;
        PcDdsTexture t{};std::string error;
        if(pc_d3dx_texture_from_file(q,t,error)&&!t.faces.empty()&&!t.faces[0].empty()){
            const auto& top=t.faces[0][0];
            if(top.width>=1u&&top.height>=1u&&std::size_t(top.width)*top.height*4u<=max_bytes){
                auto texels=pc_dds_rgba(t,0,0);
                if(texels.size()==std::size_t(top.width)*top.height*4u){
                    rgba=std::move(texels);width=top.width;height=top.height;
                    ++r.ui_stats.replaced;++bank.replaced;if(other){++r.ui_stats.other;++bank.other;}
                }
            }
        }
        if(rgba.empty()){++r.ui_stats.failed;++bank.failed;}
    }
    bank.seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    return !rgba.empty();
}
const PmtTextureReplacementStats& ui_texture_replacement_stats(){return replacement().ui_stats;}
const std::map<std::string,PmtTextureReplacementBank>& ui_texture_replacement_banks(){return replacement().ui_banks;}
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
    bool replaced=false;
    const auto handle=(replacement().scene||replacement().scene_extract)&&!r.pack_name.empty()?create_from_pack(device,false,r.pack_name,c.texture_95b22c,q,replaced)
                                                                :device.create_texture_from_file(q);
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
