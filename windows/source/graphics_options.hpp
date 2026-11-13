#pragma once
// The game's one graphics option and its texture packs, read once at start (window_desktop.cpp), before any [Graphics]
// key is read. Tested offline (windows/tests/test_graphics_options.cpp).
//
// Graphics: Wheelkit's app option "Graphics" (games/outrun-2006/wheelkit-mod.json in the Wheelkit repository), saved as
// [Options] graphics in OutRunDesktop.ini. off / low / medium / high / ultra set six Tweaks keys of [Graphics] for this
// run, in memory (the file keeps what it holds); custom keeps the file's own [Graphics] values. Absent: high, the
// descriptor's default, unless the file itself sets one of the six keys: saved values are never overridden unasked,
// so that is custom. The levels are the old desktop panel's preset (desktop_quality.hpp, removed with that panel in 692e090)
// without its two texture rows, which the packs now decide, and with the z-buffer fix and the whole stage drawn at High and Ultra.
//
// Texture packs: SceneTextureReplacement / UITextureReplacement that the file does not set are on when the pack folder
// (<game folder>/<TextureBaseFolder>/load, Tweaks' layout) has pictures for them: the spr_* folders are the menus and
// the HUD, every other folder the stages, cars and objects. A saved false keeps a kind off.
#include "desktop_settings.hpp"
#include <array>
#include <cctype>
#include <string>
namespace outrun::desktop {
// High and Ultra also stop far scenery flickering (FixZBufferPrecision) and draw the whole stage (DisableStageCulling:
// no pop-in, more draws): the owner's Tweaks setup had both on (Wheelkit decision 0027, 2026-11-13).
inline constexpr std::array<const char*,6> GraphicsKeys{"AnisotropicFiltering","ReflectionResolution","TransparencySupersampling",
                                                       "DisableVehicleLODs","FixZBufferPrecision","DisableStageCulling"};
struct GraphicsLevel { const char* id; std::array<const char*,6> values; };
inline constexpr std::array<GraphicsLevel,5> GraphicsLevels{{
    {"off",{"0","128","false","false","false","false"}},      // the game as it shipped
    {"low",{"2","128","false","false","false","false"}},
    {"medium",{"4","256","false","false","false","false"}},
    {"high",{"8","512","true","false","true","true"}},        // the default
    {"ultra",{"16","1024","true","true","true","true"}}}};    // also every other car at its full model
// Which level applies: 0..4, or -1 for custom (the file's own values). `note` says why when that is not what was saved.
inline int graphics_level(const Ini& ini,std::string& note){
    note.clear();
    std::string saved=ini.text("Options","graphics");
    for(auto& c:saved)c=char(std::tolower(static_cast<unsigned char>(c)));
    if(saved=="custom")return -1;
    for(int i=0;i<int(GraphicsLevels.size());++i)if(saved==GraphicsLevels[std::size_t(i)].id)return i;
    if(!saved.empty()){note="[Options] graphics = "+saved.substr(0,32)+" refused (off, low, medium, high, ultra or custom); ";}
    for(const char* key:GraphicsKeys)if(ini.has_value("Graphics",key)){note+="the file sets [Graphics] "+std::string(key)+", so custom";return -1;}
    if(!saved.empty())note+="high";
    return 3;
}
// Applies the level for this run and returns the log line.
inline std::string apply_graphics_preset(Ini& ini){
    std::string note;const int level=graphics_level(ini,note);
    std::string line="graphics: ";
    if(level<0)line+="custom (the [Graphics] values in OutRunDesktop.ini)";
    else{
        const auto& l=GraphicsLevels[std::size_t(level)];
        for(std::size_t k=0;k<GraphicsKeys.size();++k)ini.set("Graphics",GraphicsKeys[k],l.values[k]);
        line+=std::string(l.id)+": anisotropic filtering "+l.values[0]+"x, car reflection "+l.values[1]+
              ", transparency supersampling "+l.values[2]+", other cars always full detail "+l.values[3]+
              ", z-buffer fix "+l.values[4]+", whole stage drawn "+l.values[5];
    }
    if(!note.empty())line+=" ("+note+")";
    return line;
}
// A pack folder's pictures by kind, from the folder names under load/ (a picture at the root counts as a stage one).
struct TexturePack { unsigned scene{},ui{}; };
inline bool texture_folder_is_ui(const std::string& folder){
    return folder.size()>=4&&std::tolower(static_cast<unsigned char>(folder[0]))=='s'&&std::tolower(static_cast<unsigned char>(folder[1]))=='p'&&
           std::tolower(static_cast<unsigned char>(folder[2]))=='r'&&folder[3]=='_';
}
// [Graphics] key of one kind: the saved value, else on when the pack has pictures of that kind.
inline bool texture_replacement_on(const Ini& ini,const char* key,unsigned pictures){
    return ini.has_value("Graphics",key)?ini.flag("Graphics",key,false):pictures>0u;
}
}
