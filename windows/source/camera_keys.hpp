#pragma once
#include "hud_toggle.hpp"
#include <array>
#include <algorithm>
namespace outrun::desktop {
inline constexpr const char* CameraKeyFields[]{"KeyUp","KeyDown","KeyForward","KeyBack","KeyLeft","KeyRight","KeyPitchDown","KeyPitchUp","KeyFovUp","KeyFovDown","KeyReset"};
inline constexpr const char* CameraKeyLabels[]{"Move up","Move down","Move forward","Move back","Move left","Move right","Look down","Look up","Widen FOV","Narrow FOV","Reset view"};
inline std::string camera_key_name(const std::string& text){
    auto name=hud_toggle_setting_name(text);
    if(name=="NONE"||name=="UNBOUND")return {};
    if(name.rfind("KEYPAD",0)==0)name="NUMPAD"+name.substr(6);
    if(name=="NUMPADPLUS")return "ADD";if(name=="NUMPADMINUS")return "SUBTRACT";
    if(name=="NUMPADDIVIDE")return "DIVIDE";if(name=="NUMPADMULTIPLY")return "MULTIPLY";
    return name;
}
inline std::string camera_key_display(const std::string& name){
    if(name.empty())return "Unbound";if(name=="ADD")return "Numpad +";if(name=="SUBTRACT")return "Numpad -";
    return name.rfind("NUMPAD",0)==0?"Numpad "+name.substr(6):name;
}
struct CameraKeys {
    std::array<std::string,11> names{{"NUMPAD9","NUMPAD3","NUMPAD8","NUMPAD2","NUMPAD4","NUMPAD6","NUMPAD7","NUMPAD1","SUBTRACT","ADD","NUMPAD5"}};
    bool migrated{};
};
template<class Text,class Has> CameraKeys camera_keys_load(Text text,Has has){
    CameraKeys out;unsigned present=0;
    for(unsigned i=0;i<11;++i)if(has(CameraKeyFields[i])){out.names[i]=camera_key_name(text(CameraKeyFields[i]));++present;}
    if(present!=11)return out; // missing keys get current defaults; partial/custom sets are never guessed to be old defaults
    if(out.names==CameraKeys{}.names)return out;
    static const char* const previous[][11]={
        {"NUMPAD8","NUMPAD2","NUMPAD9","NUMPAD7","NUMPAD4","NUMPAD6","NUMPAD1","NUMPAD3","ADD","SUBTRACT","NUMPAD0"},
        {"NUMPAD8","NUMPAD2","NUMPAD9","NUMPAD7","NUMPAD4","NUMPAD6","NUMPAD3","NUMPAD1","ADD","SUBTRACT","NUMPAD0"},
        {"NUMPAD9","NUMPAD3","NUMPAD8","NUMPAD2","DIVIDE","MULTIPLY","NUMPAD6","NUMPAD4","ADD","SUBTRACT","NUMPAD5"},
        {"NUMPAD9","NUMPAD3","NUMPAD8","NUMPAD2","DIVIDE","MULTIPLY","NUMPAD4","NUMPAD6","ADD","SUBTRACT","NUMPAD5"},
        {"NUMPAD9","NUMPAD3","NUMPAD8","NUMPAD2","NUMPAD4","NUMPAD6","NUMPAD7","NUMPAD1","ADD","SUBTRACT","NUMPAD5"}};
    for(const auto& old:previous)for(unsigned direction=0;direction<2;++direction){
        bool same=true;for(unsigned i=0;i<11;++i){const unsigned at=direction&&i==8?9:direction&&i==9?8:i;same=same&&out.names[i]==old[at];}
        if(same){out=CameraKeys{};out.migrated=true;return out;}
    }
    return out;
}
struct CameraKeyRepeat {
    HudToggleEdge edge;std::uint64_t next{};bool repeated{};
    bool step(bool down,bool blocked,std::uint64_t now,bool repeatable){
        const bool prev=edge.prev,waiting=edge.wait;const bool release=edge.step(down,blocked);
        if(blocked||waiting){next=0;repeated=false;return false;}
        if(down&&!prev){next=now+350;repeated=false;}
        if(release){const bool tap=!repeated;next=0;repeated=false;return tap;}
        if(down&&repeatable&&next&&now>=next){next=now+100;repeated=true;return true;}
        return false;
    }
};
}
