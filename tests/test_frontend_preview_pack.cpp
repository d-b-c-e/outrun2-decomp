#include "platform/frontend_preview_pack.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace outrun::platform;
namespace {
unsigned checks{};
void require(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
bool changed(const std::vector<GameUiDraw>& a,const std::vector<GameUiDraw>& b){
    if(a.size()!=b.size())return true;
    for(std::size_t i=0;i<a.size();++i){
        if(a[i].visible!=b[i].visible||
           std::fabs(a[i].opacity-b[i].opacity)>0.001f)return true;
        for(std::size_t c=0;c<4u;++c)for(std::size_t axis=0;axis<2u;++axis)
            if(std::fabs(a[i].corners[c][axis]-b[i].corners[c][axis])>0.001f)
                return true;
    }
    return false;
}
void put32(std::vector<std::uint8_t>& b,std::size_t o,std::uint32_t v){for(unsigned i=0;i<4u;++i)b[o+i]=std::uint8_t(v>>(i*8u));}
void putf(std::vector<std::uint8_t>& b,std::size_t o,float v){std::uint32_t bits{};std::memcpy(&bits,&v,4u);put32(b,o,bits);}
std::uint32_t crc32(const std::uint8_t* data,std::size_t size){std::uint32_t crc=0xffffffffu;for(std::size_t i=0;i<size;++i){crc^=data[i];for(unsigned bit=0;bit<8u;++bit)crc=(crc>>1u)^(0xedb88320u&std::uint32_t(0u-(crc&1u)));}return ~crc;}
std::vector<std::uint8_t> synthetic(){
    constexpr std::array<std::uint32_t,FrontendPreviewPackExpectedScenes> scene_draws{{3u,3u,3u,2u,4u,2u,3u,3u,3u}};
    constexpr std::size_t texture_offset=FrontendPreviewPackHeaderSize;
    constexpr std::size_t scene_offset=texture_offset+FrontendPreviewPackExpectedTextures*FrontendPreviewPackRecordSize;
    constexpr std::size_t draw_offset=scene_offset+FrontendPreviewPackExpectedScenes*FrontendPreviewSceneRecordSize;
    constexpr std::size_t payload_offset=draw_offset+FrontendPreviewPackExpectedDraws*FrontendPreviewDrawRecordSize;
    constexpr std::size_t payload_bytes=FrontendPreviewPackExpectedTextures*8u;
    std::vector<std::uint8_t> out(payload_offset+payload_bytes);
    const std::uint8_t magic[8]={'O','R','2','F','E','P','3',0};std::memcpy(out.data(),magic,8u);
    put32(out,8u,FrontendPreviewPackVersion);put32(out,12u,FrontendPreviewPackHeaderSize);
    put32(out,16u,FrontendPreviewPackExpectedTextures);put32(out,20u,FrontendPreviewPackRecordSize);
    put32(out,24u,FrontendPreviewPackExpectedScenes);put32(out,28u,FrontendPreviewSceneRecordSize);
    put32(out,32u,FrontendPreviewPackExpectedDraws);put32(out,36u,FrontendPreviewDrawRecordSize);
    put32(out,40u,texture_offset);put32(out,44u,scene_offset);put32(out,48u,draw_offset);
    put32(out,52u,payload_offset);put32(out,56u,std::uint32_t(out.size()));
    put32(out,60u,FrontendPreviewSourceTextureCount);put32(out,64u,FrontendPreviewSourceSceneCount);
    put32(out,68u,123u);put32(out,72u,456u);put32(out,76u,789u);put32(out,80u,1011u);
    std::size_t running=payload_offset;
    for(std::size_t i=0;i<FrontendPreviewPackExpectedTextures;++i){const auto record=texture_offset+i*FrontendPreviewPackRecordSize;put32(out,record,FrontendPreviewSourceIndices[i]);put32(out,record+4u,4u);put32(out,record+8u,4u);put32(out,record+12u,1u);put32(out,record+16u,std::uint32_t(running));put32(out,record+20u,8u);for(std::size_t j=0;j<8u;++j)out[running+j]=std::uint8_t(i*8u+j);put32(out,record+24u,crc32(out.data()+running,8u));running+=8u;}
    std::uint32_t first_draw=0u;
    for(std::size_t scene=0;scene<scene_draws.size();++scene){const auto record=scene_offset+scene*FrontendPreviewSceneRecordSize;put32(out,record,FrontendPreviewTokens[scene]);put32(out,record+4u,FrontendPreviewSceneWidth);put32(out,record+8u,FrontendPreviewSceneHeight);put32(out,record+12u,first_draw);put32(out,record+16u,scene_draws[scene]);for(std::uint32_t local=0;local<scene_draws[scene];++local){const auto index=first_draw+local;const auto draw=draw_offset+index*FrontendPreviewDrawRecordSize;put32(out,draw,FrontendPreviewSourceIndices[index%FrontendPreviewSourceIndices.size()]);put32(out,draw+12u,4u);put32(out,draw+16u,4u);const float x=float(local*4u);const float corners[8]={x,0.0f,x+4.0f,0.0f,x+4.0f,4.0f,x,4.0f};for(std::size_t c=0;c<8u;++c)putf(out,draw+20u+c*4u,corners[c]);put32(out,draw+52u,local);}first_draw+=scene_draws[scene];}
    return out;
}
// A START loading pack: one texture, nine scenes of one draw each cropped (0,0,crop_right,4). OR2LDP2 with a BC1 4x4
// texture; with `hd`, OR2LDP3 with an rgba8 w x h texture and `uv` ((stock width << 16) | stock height) in its last word.
std::vector<std::uint8_t> synthetic_loading(bool hd,std::uint32_t w=8u,std::uint32_t h=8u,std::uint32_t uv=(4u<<16u)|4u,std::uint32_t crop_right=4u){
    constexpr std::size_t texture_offset=FrontendPreviewPackHeaderSize,scene_offset=texture_offset+FrontendPreviewPackRecordSize;
    constexpr std::size_t draw_offset=scene_offset+9u*FrontendPreviewSceneRecordSize,payload_offset=draw_offset+9u*FrontendPreviewDrawRecordSize;
    const std::size_t payload_bytes=hd?std::size_t(w)*h*4u:8u,animation_offset=payload_offset+payload_bytes;
    std::vector<std::uint8_t> out(animation_offset+40u);
    std::memcpy(out.data(),hd?"OR2LDP3":"OR2LDP2",8u);put32(out,8u,hd?3u:2u);put32(out,12u,FrontendPreviewPackHeaderSize);
    put32(out,16u,1u);put32(out,20u,FrontendPreviewPackRecordSize);put32(out,24u,9u);put32(out,28u,FrontendPreviewSceneRecordSize);
    put32(out,32u,9u);put32(out,36u,FrontendPreviewDrawRecordSize);
    put32(out,40u,texture_offset);put32(out,44u,scene_offset);put32(out,48u,draw_offset);put32(out,52u,payload_offset);
    put32(out,56u,std::uint32_t(out.size()));put32(out,60u,5u);put32(out,64u,9u);put32(out,68u,1u);put32(out,72u,2u);put32(out,76u,3u);put32(out,80u,4u);
    for(std::size_t j=0;j<payload_bytes;++j)out[payload_offset+j]=std::uint8_t(j*7u);
    put32(out,texture_offset+4u,hd?w:4u);put32(out,texture_offset+8u,hd?h:4u);put32(out,texture_offset+12u,hd?4u:1u);
    put32(out,texture_offset+16u,payload_offset);put32(out,texture_offset+20u,std::uint32_t(payload_bytes));
    put32(out,texture_offset+24u,crc32(out.data()+payload_offset,payload_bytes));if(hd)put32(out,texture_offset+28u,uv);
    for(std::uint32_t i=0;i<9u;++i){
        const auto scene=scene_offset+i*FrontendPreviewSceneRecordSize;put32(out,scene,0x002f0000u+i);
        put32(out,scene+4u,FrontendPreviewSceneWidth);put32(out,scene+8u,FrontendPreviewSceneHeight);put32(out,scene+12u,i);put32(out,scene+16u,1u);
        const auto draw=draw_offset+i*FrontendPreviewDrawRecordSize;put32(out,draw+12u,crop_right);put32(out,draw+16u,4u);
        const float corners[8]={0.0f,0.0f,640.0f,0.0f,640.0f,480.0f,0.0f,480.0f};for(std::size_t c=0;c<8u;++c)putf(out,draw+20u+c*4u,corners[c]);}
    put32(out,animation_offset,40u);put32(out,84u,std::uint32_t(animation_offset));put32(out,88u,40u);put32(out,92u,crc32(out.data()+animation_offset,40u));
    return out;
}
void relabel(std::vector<std::uint8_t>& b,bool hd){std::memcpy(b.data(),hd?"OR2LDP3":"OR2LDP2",8u);put32(b,8u,hd?3u:2u);}
}
int main(int argc,char** argv){
    auto bytes=synthetic();FrontendPreviewPack pack{};std::string error;
    require(parse_frontend_preview_pack(bytes.data(),bytes.size(),pack,&error),"valid synthetic OR2FEP3");
    require(pack.textures.size()==10u&&pack.scenes.size()==9u&&pack.draws.size()==26u,"complete frontend menu retained");
    require(pack.scenes[0].token==FrontendPreviewInitialToken&&pack.scenes[4].draw_count==4u&&pack.scenes[8].first_draw==23u,"scene ranges retained");
    require(find_frontend_preview_scene(pack,0x0044008bu)==&pack.scenes[4]&&find_frontend_preview_scene(pack,0xdeadbeefu)==nullptr,"token lookup");
    auto bad=bytes;bad[0]='X';require(!parse_frontend_preview_pack(bad.data(),bad.size(),pack,&error),"bad magic rejected");
    bad=bytes;put32(bad,FrontendPreviewPackHeaderSize,72u);require(!parse_frontend_preview_pack(bad.data(),bad.size(),pack,&error),"wrong source index rejected");
    bad=bytes;put32(bad,FrontendPreviewPackHeaderSize+FrontendPreviewPackExpectedTextures*FrontendPreviewPackRecordSize,0x44u);require(!parse_frontend_preview_pack(bad.data(),bad.size(),pack,&error),"wrong scene token rejected");
    bad=bytes;bad.back()^=1u;require(!parse_frontend_preview_pack(bad.data(),bad.size(),pack,&error),"bad payload crc rejected");
    bad=bytes;put32(bad,FrontendPreviewPackHeaderSize+12u,4u);require(!parse_frontend_preview_pack(bad.data(),bad.size(),pack,&error),"menu pack: rgba8 rejected (BC1..BC3 only)");
    // START loading packs: OR2LDP2 as before; OR2LDP3 carries replaced (UITextureReplacement) textures as rgba8 at their
    // file size, the stock size in the record's last word as the crops' texel space.
    {   FrontendPreviewPack loading{};
        auto stock=synthetic_loading(false);
        require(parse_frontend_preview_pack(stock.data(),stock.size(),loading,&error)&&loading.textures.size()==1u&&loading.textures[0].uv_width==0u&&
            frontend_uv_width(loading.textures[0])==4u&&frontend_uv_height(loading.textures[0])==4u,"OR2LDP2: crops in the texture's own size");
        auto hd=synthetic_loading(true);
        require(parse_frontend_preview_pack(hd.data(),hd.size(),loading,&error)&&loading.textures[0].format==MeshPreviewTextureFormat::rgba8&&
            loading.textures[0].width==8u&&loading.textures[0].height==8u&&loading.textures[0].bytes.size()==256u&&
            frontend_uv_width(loading.textures[0])==4u&&frontend_uv_height(loading.textures[0])==4u,"OR2LDP3: rgba8 at the file size, crops in the stock size");
        hd=synthetic_loading(true,8u,8u,(4u<<16u)|4u,8u);
        require(!parse_frontend_preview_pack(hd.data(),hd.size(),loading,&error),"OR2LDP3: a crop past the stock size rejected (inside the file size)");
        for(const std::uint32_t uv:{0u,4u,4u<<16u,(2049u<<16u)|4u,(4u<<16u)|2049u}){
            hd=synthetic_loading(true,8u,8u,uv);require(!parse_frontend_preview_pack(hd.data(),hd.size(),loading,&error),"OR2LDP3: stock size 0 or above 2048 rejected");}
        hd=synthetic_loading(true,8u,8u,(2048u<<16u)|2048u);require(parse_frontend_preview_pack(hd.data(),hd.size(),loading,&error),"OR2LDP3: stock size 2048 accepted");
        hd=synthetic_loading(true,16384u,1u);require(parse_frontend_preview_pack(hd.data(),hd.size(),loading,&error),"OR2LDP3: 16384 texels a side accepted");
        hd=synthetic_loading(true,16385u,1u);require(!parse_frontend_preview_pack(hd.data(),hd.size(),loading,&error),"OR2LDP3: above 16384 texels a side rejected");
        hd=synthetic_loading(true,2560u,2560u);
        require(hd.size()>24u*1024u*1024u&&parse_frontend_preview_pack(hd.data(),hd.size(),loading,&error),"OR2LDP3: above the 24 MB stock bound accepted");
        {std::vector<std::uint8_t> huge(FrontendHdLoadingMaxBytes+1u);relabel(huge,true);
         require(!parse_frontend_preview_pack(huge.data(),huge.size(),loading,&error)&&error=="frontend pack size outside bounds","OR2LDP3: above 128 MB rejected");}
        bad=stock;put32(bad,FrontendPreviewPackHeaderSize+28u,(4u<<16u)|4u);require(!parse_frontend_preview_pack(bad.data(),bad.size(),loading,&error),"OR2LDP2: nonzero reserved word rejected");
        bad=synthetic_loading(true);relabel(bad,false);require(!parse_frontend_preview_pack(bad.data(),bad.size(),loading,&error),"OR2LDP2: rgba8 rejected");
        bad=stock;relabel(bad,true);
        require(parse_frontend_preview_pack(bad.data(),bad.size(),loading,&error)&&frontend_uv_width(loading.textures[0])==4u,"OR2LDP3: a stock BC texture kept as in OR2LDP2");
        put32(bad,FrontendPreviewPackHeaderSize+28u,(4u<<16u)|4u);require(!parse_frontend_preview_pack(bad.data(),bad.size(),loading,&error),"OR2LDP3: a BC texture's reserved word still zero");
    }
    if(argc>=2){FrontendPreviewPack real{};require(load_frontend_preview_pack_file(argv[1],real,&error),"generated SUMO_FE pack loads");require(real.source_xst_compressed_bytes==3727034u&&real.source_xst_decompressed_bytes==20220900u&&real.source_animation_compressed_bytes==159163u&&real.source_animation_decompressed_bytes==1104424u,"real SUMO_FE sources identified");require(find_frontend_preview_scene(real,FrontendPreviewInitialToken)!=nullptr,"original initial scene retained");if(real.scenes.size()==9u){require(real.draws.size()==26u&&real.textures.size()==10u,"legacy menu preview retained");}else{require(real.scenes.size()==124u&&real.draws.size()==2428u&&real.textures.size()==82u,"full-screen original archive retained");require(find_frontend_preview_scene(real,0x004400deu)!=nullptr,"late original full-screen scene retained");require(real.animation.size()==1104420u,"authored SUMO_FE animation retained");GameUiPack animation{};require(make_frontend_animation_view(real,animation,&error),error.c_str());for(const auto& scene:real.scenes){std::vector<GameUiDraw> draws;require(game_ui_scene_frame_draws(animation,scene.token&0xffffu,30.0f,draws,&error),error.c_str());require(draws.size()==scene.draw_count,"SUMO_FE animated layer count stable");}std::vector<GameUiDraw> first,later;require(game_ui_scene_frame_draws(animation,0u,0.0f,first,&error)&&game_ui_scene_frame_draws(animation,0u,60.0f,later,&error)&&changed(first,later),"authored SUMO_FE scene 0 visibly animates");}}
    if(argc>=3){FrontendPreviewPack loading{};require(load_frontend_preview_pack_file(argv[2],loading,&error),error.c_str());require(loading.source_texture_count==5u&&loading.source_scene_count==9u,"retail START archive source layout");require(loading.textures.size()==5u&&loading.scenes.size()==9u&&loading.draws.size()==171u,"all START loading scenes and sprites retained");require(find_frontend_preview_scene(loading,0x002f0000u)!=nullptr&&find_frontend_preview_scene(loading,0x002f0008u)!=nullptr,"START loading scene range retained");GameUiPack animation{};require(make_frontend_animation_view(loading,animation,&error),error.c_str());for(const auto& scene:loading.scenes){std::vector<GameUiDraw> draws;require(game_ui_scene_frame_draws(animation,scene.token&0xffffu,60.0f,draws,&error),error.c_str());require(draws.size()==scene.draw_count,"START animated layer count stable");}std::vector<GameUiDraw> first,later;require(game_ui_scene_frame_draws(animation,2u,0.0f,first,&error)&&game_ui_scene_frame_draws(animation,2u,60.0f,later,&error)&&changed(first,later),"authored START scene 2 visibly animates");}
    std::printf("frontend_preview_pack: %u checks passed\n",checks);return 0;
}
