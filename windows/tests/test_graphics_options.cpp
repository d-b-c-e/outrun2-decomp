// The graphics preset ([Options] graphics, Wheelkit's app option) and the texture packs' default (graphics_options.hpp).
// No game, no file written.
#include "graphics_options.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
namespace {
int checks=0;
void check(bool v,const char* what){++checks;if(!v){std::fprintf(stderr,"graphics options: FAIL: %s\n",what);std::exit(1);}}
outrun::desktop::Ini ini(const std::string& text){outrun::desktop::Ini i;i.load_text(text,"OutRunDesktop.ini");return i;}
}
int main(){
    using namespace outrun::desktop;
    std::string note;
    // Nothing saved: High, the descriptor's default, applied in memory.
    {   auto i=ini("[Window]\nFrameLatency = 2\n");
        check(graphics_level(i,note)==3&&note.empty(),"absent: high");
        const auto line=apply_graphics_preset(i);
        check(i.text("Graphics","AnisotropicFiltering")=="8"&&i.text("Graphics","ReflectionResolution")=="512"&&
              i.flag("Graphics","TransparencySupersampling",false)&&!i.flag("Graphics","DisableVehicleLODs",true),"high's four values");
        check(i.flag("Graphics","FixZBufferPrecision",false)&&i.flag("Graphics","DisableStageCulling",false),
              "high: the z-buffer fix and the whole stage drawn (decision 0027)");
        check(line.rfind("graphics: high:",0)==0,"the log line names the level");}
    // Each saved level, any case; Off is the game as it shipped.
    {   auto i=ini("[Options]\ngraphics = Ultra\n");apply_graphics_preset(i);
        check(i.text("Graphics","AnisotropicFiltering")=="16"&&i.text("Graphics","ReflectionResolution")=="1024"&&i.flag("Graphics","DisableVehicleLODs",false)&&
              i.flag("Graphics","FixZBufferPrecision",false)&&i.flag("Graphics","DisableStageCulling",false),"ultra");}
    {   auto i=ini("[Options]\ngraphics = medium\n");apply_graphics_preset(i);
        check(!i.flag("Graphics","FixZBufferPrecision",true)&&!i.flag("Graphics","DisableStageCulling",true),"medium: neither");}
    {   auto i=ini("[Graphics]\nDisableStageCulling = true\n");
        check(graphics_level(i,note)==-1&&note.find("DisableStageCulling")!=std::string::npos,"a saved whole-stage key without a choice is custom");}
    {   auto i=ini("[Options]\ngraphics = off\n[Graphics]\nAnisotropicFiltering = 16\n");apply_graphics_preset(i);
        check(i.text("Graphics","AnisotropicFiltering")=="0"&&i.text("Graphics","ReflectionResolution")=="128"&&
              !i.flag("Graphics","TransparencySupersampling",true),"a chosen level wins over the file's keys for the run");}
    // Custom keeps the file's own values; absent with one of the four keys saved is custom (never overridden unasked).
    {   auto i=ini("[Options]\ngraphics = custom\n[Graphics]\nAnisotropicFiltering = 4\n");
        check(graphics_level(i,note)==-1,"custom");apply_graphics_preset(i);
        check(i.text("Graphics","AnisotropicFiltering")=="4"&&!i.has_value("Graphics","ReflectionResolution"),"custom leaves the file alone");}
    {   auto i=ini("[Graphics]\nReflectionResolution = 2048\n");
        check(graphics_level(i,note)==-1&&note.find("ReflectionResolution")!=std::string::npos,"a saved key without a choice is custom, said why");}
    {   auto i=ini("[Options]\ngraphics = extreme\n");
        check(graphics_level(i,note)==3&&note.find("refused")!=std::string::npos,"an unknown level is refused: high");}
    // Texture packs: on when the pack has pictures of the kind, unless the file says otherwise.
    check(texture_folder_is_ui("spr_sprani_sumo_fe_cvt_Exst")&&texture_folder_is_ui("SPR_font_xst")&&!texture_folder_is_ui("cs_CS_BEAC_pmt")&&
          !texture_folder_is_ui("obj_othcar_pmt")&&!texture_folder_is_ui("spr")&&!texture_folder_is_ui(""),"spr_ folders are the menus and HUD");
    {   const auto i=ini("[Window]\n");
        check(texture_replacement_on(i,"SceneTextureReplacement",1198)&&texture_replacement_on(i,"UITextureReplacement",257),"the owner's pack: both on");
        check(!texture_replacement_on(i,"SceneTextureReplacement",0)&&!texture_replacement_on(i,"UITextureReplacement",0),"no pack: off");}
    {   const auto i=ini("[Graphics]\nUITextureReplacement = false\nSceneTextureReplacement = true\n");
        check(!texture_replacement_on(i,"UITextureReplacement",257),"a saved false keeps it off");
        check(texture_replacement_on(i,"SceneTextureReplacement",0),"a saved true stays on (the loader then finds nothing)");}
    std::printf("graphics options: %d checks passed\n",checks);
    return 0;
}
