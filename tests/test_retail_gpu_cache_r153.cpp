#include "platform/retail_gpu_cache.hpp"
#include "platform/loader_asset_pack.hpp"
#include "platform/frontend_preview_pack.hpp"
#include "platform/game_ui_pack.hpp"
#include "platform/pc_pmt_loader.hpp"
#include "switch_renderer.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>
#include <zlib.h>
using namespace outrun::platform;
namespace fs=std::filesystem;
namespace {
unsigned checks{};void req(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
void put(const fs::path&p,const std::vector<std::uint8_t>&b){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));if(!f)throw std::runtime_error("fixture write");}
void copy_file_fixture(const fs::path&a,const fs::path&b){fs::create_directories(b.parent_path());fs::copy_file(a,b,fs::copy_options::overwrite_existing);}
const LoaderAssetRecord& rec(const LoaderAssetPack&p,std::uint32_t id,std::uint32_t mode){const auto*r=find_loader_asset(p,id,mode);if(!r)throw std::runtime_error("loader record missing");return *r;}
std::vector<std::uint8_t> zip(const std::vector<std::uint8_t>&raw){uLongf n=compressBound(raw.size());std::vector<std::uint8_t> out(n);if(compress2(out.data(),&n,raw.data(),raw.size(),9)!=Z_OK)throw std::runtime_error("zlib fixture");out.resize(n);return out;}
std::vector<std::uint8_t> wrap(const std::vector<std::uint8_t>&ani){std::vector<std::uint8_t> raw(4+ani.size());const auto n=std::uint32_t(ani.size());for(unsigned i=0;i<4;++i)raw[i]=std::uint8_t(n>>(i*8));std::copy(ani.begin(),ani.end(),raw.begin()+4);return zip(raw);}
}
int main(int argc,char**argv){try{
 req(argc==4,"usage LOADER FRONTEND LOADING");std::string e;LoaderAssetPack loader;FrontendPreviewPack fe,loading;
 req(load_loader_asset_pack_file(argv[1],loader,&e),"loader fixture");req(load_frontend_preview_pack_file(argv[2],fe,&e),"frontend fixture");req(load_frontend_preview_pack_file(argv[3],loading,&e),"loading fixture");
 const auto root=fs::temp_directory_path()/"outrun-r152-auto-cache";fs::remove_all(root);
 // A game folder holds the 250 GTO model (retail_asset_store_is_game_root).
 put(root/"CARS/obj_plcar_250gto_pmt.sz",rec(loader,0x0b,8).bytes);
 put(root/"Sprite/spr_SPRANI_SUMO_FE_CVT_Exst.sz",rec(loader,0x44,9).bytes);
 put(root/"Sprite/spr_SPRANI_LOADING_CVT_Exst.sz",rec(loader,0x2f,2).bytes);
 put(root/"Sprani/ani_SPRANI_SUMO_FE_CVT.sz",wrap(fe.animation));
 put(root/"Sprani/ani_SPRANI_LOADING_CVT.sz",wrap(loading.animation));
 put(root/"Scripts/bin/Races.bin",{1});put(root/"Scripts/bin/csc_data_cvt.bin",{1});put(root/"Stage/BEAC/coli_CS_BEAC_bin.sz",{1});
 RetailAssetStore store{};req(retail_asset_store_open(store,root.string(),&e),"retail root");std::vector<std::uint8_t> pack;
 {const bool ok=build_retail_start_loading(store,pack,&e);if(!ok)throw std::runtime_error(e);++checks;}
 req(!fs::exists(root/"Cache")&&!fs::exists(root/"start_loading_en.ldp2"),"nothing written next to the game files");
 FrontendPreviewPack load2;{const bool parsed=parse_frontend_preview_pack(pack.data(),pack.size(),load2,&e);req(parsed,e.c_str());}req(!load2.scenes.empty()&&!load2.textures.empty()&&load2.animation.size()==loading.animation.size(),"START loading pack");
 outrun::switch_runtime::SwitchRenderer renderer;
 using namespace outrun::switch_runtime;
 {const bool ok=switch_renderer_initialize(renderer,nullptr,e,&pack,nullptr,nullptr,nullptr);req(ok,e.c_str());}
 req(switch_renderer_set_frontend_token(renderer,0x44008bu),"no SUMO_FE pack: the PC frontend token selects nothing");
 req(switch_renderer_set_start_loading_scene(renderer,0u)&&switch_renderer_set_start_loading_visible(renderer,true),"show START loading");
 req(switch_renderer_draw(renderer)&&switch_renderer_stats(renderer).loading_frames==1u,"START loading drawn");
 switch_renderer_shutdown(renderer);
 // UITextureReplacement on the START loading picture. On with no file for its textures (empty pack, or files of other
 // names) builds the stock OR2LDP2 bytes; a file for texture 0 builds OR2LDP3 with that texture rgba8 at the file's
 // size (the stock size kept for the crops) and every other part as before; off again builds the stock bytes (the pack
 // is rebuilt in memory at each start, nothing kept between builds).
 {const auto load=root/"textures/load";std::vector<std::uint8_t> xst,again;
  req(retail_asset_read_relative_inflated(store,"Sprite/spr_SPRANI_LOADING_CVT_Exst.sz",xst,128u<<20,&e),"START XST");
  std::vector<std::size_t> dds;for(std::size_t i=0;i+4<=xst.size();++i)if(std::memcmp(xst.data()+i,"DDS ",4)==0){dds.push_back(i);i+=3;}
  req(dds.size()==5u,"five START DDS pages");
  const auto rd=[&](std::size_t o){std::uint32_t v;std::memcpy(&v,xst.data()+o,4);return v;};
  const std::uint32_t sh=rd(dds[0]+12),sw=rd(dds[0]+16);char name[64];
  std::snprintf(name,sizeof name,"0_%X_%ux%u.dds",pmt_xxh32(xst.data()+dds[0],dds[1]-dds[0]),sw,sh);
  std::vector<std::uint8_t> file(0x80+16*8*4);{auto p=[&](std::size_t o,std::uint32_t v){std::memcpy(file.data()+o,&v,4);};   // A8R8G8B8 16x8
   std::memcpy(file.data(),"DDS ",4);p(4,124);p(12,8);p(16,16);p(28,1);p(76,32);p(80,0x41);p(88,32);p(92,0xff0000u);p(96,0xff00u);p(100,0xffu);p(104,0xff000000u);
   for(std::size_t k=0x80;k<file.size();++k)file[k]=std::uint8_t(k*29u);}
  std::vector<std::uint8_t> rgba(file.begin()+0x80,file.end());for(std::size_t k=0;k<rgba.size();k+=4)std::swap(rgba[k],rgba[k+2]);   // B,G,R,A -> R,G,B,A
  fs::create_directories(load);texture_replacement_configure(false,true,load.string());
  req(build_retail_start_loading(store,again,&e)&&again==pack,"UI replacement on, empty pack: stock bytes");
  put(load/"spr_SPRANI_LOADING_CVT_Exst"/"0_DEADBEEF_16x16.dds",file);put(load/"spr_etc_xst"/name,file);
  texture_replacement_configure(false,true,load.string());
  req(build_retail_start_loading(store,again,&e)&&again==pack,"UI replacement on, no file of a START texture's name: stock bytes");
  req(ui_texture_replacement_stats().looked==5u&&ui_texture_replacement_stats().replaced==0u,"every START texture looked up");
  put(load/"spr_SPRANI_LOADING_CVT_Exst"/name,file);texture_replacement_configure(false,true,load.string());
  req(build_retail_start_loading(store,again,&e),e.c_str());req(ui_texture_replacement_stats().replaced==1u,"one START texture replaced");
  FrontendPreviewPack hd;{const bool parsed=parse_frontend_preview_pack(again.data(),again.size(),hd,&e);req(parsed,e.c_str());}
  req(std::memcmp(again.data(),"OR2LDP3",8)==0&&hd.textures.size()==load2.textures.size(),"replaced START pack is OR2LDP3");
  for(std::size_t i=0;i<hd.textures.size();++i){const auto& t=hd.textures[i];const auto& s=load2.textures[i];
   if(t.source_index==0u)req(t.format==MeshPreviewTextureFormat::rgba8&&t.width==16u&&t.height==8u&&t.bytes==rgba&&
       frontend_uv_width(t)==sw&&frontend_uv_height(t)==sh&&s.width==sw&&s.height==sh,"texture 0: the file's texels, stock size for the crops");
   else req(t.source_index==s.source_index&&t.format==s.format&&t.width==s.width&&t.height==s.height&&t.uv_width==0u&&t.bytes==s.bytes,"other START textures stock");}
  req(hd.scenes.size()==load2.scenes.size()&&hd.draws.size()==load2.draws.size()&&hd.animation==load2.animation,"scenes, draws and animation unchanged");
  for(std::size_t i=0;i<hd.draws.size();++i)req(hd.draws[i].source_texture==load2.draws[i].source_texture&&hd.draws[i].crop==load2.draws[i].crop,"draw crops unchanged");
  GameUiPack view;{const bool ok=make_frontend_animation_view(hd,view,&e);req(ok,e.c_str());}req(view.textures[0].width==sw&&view.textures[0].height==sh,"animated crops bounded by the stock size");
  texture_replacement_configure(false,false,{});
  req(build_retail_start_loading(store,again,&e)&&again==pack,"UI replacement off again: stock bytes");}
 fs::remove_all(root);std::printf("retail_gpu_cache_r153: %u checks passed; the START loading picture is built in memory from the game files\n",checks);return 0;
 }catch(const std::exception&x){std::fprintf(stderr,"FAILED: %s\n",x.what());return 1;}}
