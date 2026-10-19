#pragma once
// Optional visual tweaks only. No input, force, calibration, display geometry, pacing or asset extraction.
#include "desktop_settings.hpp"
#include <array>
#include <cctype>
namespace outrun::desktop::quality {
inline constexpr int Custom=5;
inline constexpr const char* Names[]{"Off","Low","Medium","High","Ultra","Custom"};
struct Setting {const char* key;const char* label;const char* fallback;bool boolean;std::array<const char*,5> levels;};
inline constexpr std::array<Setting,7> Settings{{
    {"AnisotropicFiltering","Texture filtering","0",false,{"0","2","4","8","16"}},
    {"ReflectionResolution","Reflection size","128",false,{"128","128","256","512","1024"}},
    {"TransparencySupersampling","Smoother cutout edges (needs MSAA)","false",true,{"false","false","false","true","true"}},
    {"DisableVehicleLODs","Full-detail distant cars","false",true,{"false","false","false","false","true"}},
    {"SceneTextureReplacement","HD cars and stages (installed pack)","false",true,{"false","false","false","true","true"}},
    {"UITextureReplacement","HD menus and HUD (installed pack)","false",true,{"false","false","true","true","true"}},
    {"EnableTextureCache","Cache replacement textures","false",true,{"false","false","false","true","true"}}
}};
inline SettingsEdits snapshot(const Ini& settings){
    SettingsEdits out;
    for(const auto& setting:Settings)out.push_back({setting.key,settings.has_value("Graphics",setting.key)?
        std::optional<std::string>(settings.text("Graphics",setting.key)):std::nullopt});
    return out;
}
inline SettingsEdits preset(int level){
    SettingsEdits out;if(level<0||level>=Custom)return out;
    for(const auto& setting:Settings)out.push_back({setting.key,std::string(setting.levels[std::size_t(level)])});
    return out;
}
inline std::string normalized(const Setting& setting,std::string text){
    if(setting.boolean){
        for(auto& ch:text)ch=char(std::tolower(static_cast<unsigned char>(ch)));
        if(text=="1"||text=="yes"||text=="on")return "true";
        if(text=="0"||text=="no"||text=="off")return "false";
    }
    return text;
}
inline int detect(const Ini& settings){
    for(int level=0;level<Custom;++level){bool same=true;
        for(const auto& setting:Settings)if(normalized(setting,settings.text("Graphics",setting.key,setting.fallback))!=setting.levels[std::size_t(level)]){same=false;break;}
        if(same)return level;
    }
    return Custom;
}
inline std::string display(const Setting& setting,const std::string& value){
    const auto text=normalized(setting,value);
    return setting.boolean?(text=="true"?"On":text=="false"?"Off":value):value;
}
inline std::vector<std::string> undo_document(const SettingsEdits& previous,const SettingsEdits& expected){
    std::vector<std::string> out{"Schema = 1"};
    for(const auto& edit:previous){out.push_back("Present."+edit.first+" = "+(edit.second?"1":"0"));
        if(edit.second)out.push_back("Previous."+edit.first+" = "+*edit.second);}
    for(const auto& edit:expected)out.push_back("Expected."+edit.first+" = "+*edit.second);
    return out;
}
inline bool read_undo(const Ini& undo,SettingsEdits& previous,SettingsEdits& expected){
    previous.clear();expected.clear();if(undo.text("GraphicsUndo","Schema")!="1")return false;
    for(const auto& setting:Settings){const std::string key=setting.key;
        const auto present=undo.text("GraphicsUndo","Present."+key);
        if((present!="0"&&present!="1")||!undo.has_value("GraphicsUndo","Expected."+key)||
           undo.has_value("GraphicsUndo","Previous."+key)!=(present=="1"))return false;
        previous.push_back({key,present=="1"?std::optional<std::string>(undo.text("GraphicsUndo","Previous."+key)):std::nullopt});
        expected.push_back({key,undo.text("GraphicsUndo","Expected."+key)});
    }
    for(int level=0;level<Custom;++level)if(expected==preset(level))return true;
    return false;
}

}
