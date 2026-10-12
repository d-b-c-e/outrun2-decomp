#pragma once
// [Triple] Screens (STD-022: Off / Surround / Separate monitors) and the pure rules behind it, tested offline
// (windows/tests/test_triple_screens.cpp); window_desktop.cpp feeds them the INI text and EnumDisplayMonitors.
// Design and review: windows/docs/TRIPLE-SCREENS.md.
#include "triple_rig.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace outrun::desktop {
enum class TripleScreens { off, surround, separate };
inline const char* triple_screens_name(TripleScreens m){
    return m==TripleScreens::surround?"Surround":m==TripleScreens::separate?"Separate monitors":"Off";
}
// The three STD-022 names, case-insensitive, surrounding blanks ignored; "Separate" alone is accepted too. Anything
// else gives nothing (the caller logs it and Off applies).
inline std::optional<TripleScreens> parse_triple_screens(std::string_view text){
    while(!text.empty()&&std::isspace(static_cast<unsigned char>(text.front())))text.remove_prefix(1);
    while(!text.empty()&&std::isspace(static_cast<unsigned char>(text.back())))text.remove_suffix(1);
    std::string s(text);
    for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));
    if(s=="off")return TripleScreens::off;
    if(s=="surround")return TripleScreens::surround;
    if(s=="separate monitors"||s=="separate")return TripleScreens::separate;
    return std::nullopt;
}
struct ScreenRect { long left{},top{},right{},bottom{}; };
// Separate monitors (STD-015): the rectangle over exactly three monitors of equal size side by side with no gap or
// overlap, in any enumeration order; anything else (including Surround, which Windows reports as one display) gives
// false. The Tweaks fork's TripleSpanFromMonitors (dbce-mods-outrun-2006 src/triple_span.hpp, f4e9f01), same rule.
inline bool triple_span_from_monitors(std::vector<ScreenRect> r,ScreenRect& out){
    if(r.size()!=3)return false;
    std::sort(r.begin(),r.end(),[](const ScreenRect& a,const ScreenRect& b){return a.left<b.left;});
    for(std::size_t i=0;i<r.size();++i){
        if(r[i].right<=r[i].left||r[i].bottom<=r[i].top)return false;
        if(r[i].top!=r[0].top||r[i].bottom!=r[0].bottom||r[i].right-r[i].left!=r[0].right-r[0].left)return false;
        if(i&&r[i].left!=r[i-1].right)return false;
    }
    out={r[0].left,r[0].top,r[2].right,r[0].bottom};
    return true;
}
// The planar span's aspect (pc_screen.hpp g_pc_wide_aspect): the window's width / height when the selector is not Off
// and the window is wider than 16:9; 0 otherwise (one 16:9 view, fitted).
inline float planar_aspect(TripleScreens mode,unsigned width,unsigned height){
    if(mode==TripleScreens::off||!width||!height)return 0.f;
    if(std::uint64_t(width)*9u<=std::uint64_t(height)*16u)return 0.f;
    return float(double(width)/double(height));
}
// [Triple] Views, below the selector (STD-022 options sit there): Planar, one wide view over the span (the default:
// Tweaks parity, the look so far), or Angled, three projected views, each at its panel's angle (TRIPLE-SCREENS.md step
// 4; experimental: the toolkit's fixture rig, 32 in 16:9 panels, sides at 70 degrees, 8 mm gaps, the eye matched to the
// game's FOV, not yet the owner's measured rig). Read once at start; Angled applies only over a span.
enum class TripleProjection { planar, angled };
inline const char* triple_projection_name(TripleProjection p){return p==TripleProjection::angled?"Angled":"Planar";}
// The two names, case-insensitive, surrounding blanks ignored; anything else gives nothing (logged; Planar applies).
inline std::optional<TripleProjection> parse_triple_projection(std::string_view text){
    while(!text.empty()&&std::isspace(static_cast<unsigned char>(text.front())))text.remove_prefix(1);
    while(!text.empty()&&std::isspace(static_cast<unsigned char>(text.back())))text.remove_suffix(1);
    std::string s(text);
    for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));
    if(s=="planar")return TripleProjection::planar;
    if(s=="angled")return TripleProjection::angled;
    return std::nullopt;
}
// What applies when Angled is asked for, and why not otherwise (nullptr: it applies). A latched or test process applies
// no [Triple] key: it records Views as asked for and refuses it here. Angled needs the borderless window and a span the
// selector admitted: Surround over a window wider
// than 16:9, or Separate monitors whose three-monitor span was found (sana 35719: a failed Separate span falls back to
// the primary monitor, which may itself be wider than 16:9, and must not get three panels).
inline const char* triple_projection_refusal(bool latched,bool borderless,TripleScreens mode,bool separate_span,float aspect){
    if(latched)return "not applied in a test session";
    if(!borderless)return "needs the borderless window";
    if(mode==TripleScreens::off)return "needs Surround or Separate monitors";
    if(mode==TripleScreens::separate&&!separate_span)return "Separate monitors found no three-monitor span";
    if(!(aspect>0.f))return "needs a window wider than 16:9";
    return nullptr;
}
// The stage-section union applied Angled views draw where [Graphics] leaves DrawDistanceIncrease / DrawDistanceBehind at
// 0: 64 ahead and 64 behind. Behind fills the side views past 90 degrees (the beach's hard line); ahead stops the side
// views' pop-in of stage objects that the forward lists add only a few sections on (the start's rock face). Hub
// triples-20261012: runs H1-H3, timing T1-T4 (+64 ahead: same 60 fps, p95 +0.2-0.4 ms). OR2_TRIPLE_AHEAD (0..1024) /
// OR2_TRIPLE_BEHIND (0..256) replace a default, never a set key; any other text in them is ignored.
struct TripleUnion { int ahead{},behind{}; };
inline int triple_union_env(const char* text,int fallback,int max){
    if(!text)return fallback;
    char* end=nullptr;const long n=std::strtol(text,&end,10);
    return end!=text&&!*end&&n>=0&&n<=max?int(n):fallback;
}
inline TripleUnion triple_default_union(int increase,int behind,const char* env_ahead,const char* env_behind){
    return {increase>0?increase:triple_union_env(env_ahead,64,1024),behind>0?behind:triple_union_env(env_behind,64,256)};
}
// The [Triple] rig keys for applied Angled views (TRIPLE-SCREENS.md "Rig settings and the physical eye"; sana 37757):
// the portfolio's names and ranges (Wheelkit RigProfile.GeometryValue / ScreenSetup, DRIVE's [Triple]). The defaults
// are the toolkit's fixture rig with its 8 mm gap, exactly (fixture_rig), and MatchGameFov = true: the views as before.
struct TripleRigSettings {
    double panel_width_mm{fixture_rig(8.).panel_width_mm},panel_height_mm{fixture_rig(8.).panel_height_mm};
    double eye_distance_mm{660.},side_angle_deg{70.},bezel_mm{8.};
    bool match_game_fov{true};
    bool panel_height_set{false};   // PanelHeightMm was in the INI and admitted (not the fixture default)
};
// The views take each panel's height from its width and the 16:9 render (triple_views). A saved PanelHeightMm that
// differs from that by more than 0.5 mm is named as not used; an absent one is not (Wheelkit's Apply writes the width
// only, so the fixture's default height would otherwise be reported for every other width; nene 40406).
inline bool triple_height_unused(const TripleRigSettings& s){
    return s.panel_height_set&&std::fabs(s.panel_height_mm-s.panel_width_mm*9./16.)>0.5;
}
// Reads them through text(key) (the INI's [Triple] value, "" when absent). A value that is not a finite number in its
// range (SideAngle: 0 to below 90), or a MatchGameFov other than true / false (any case, blanks around ignored), is
// refused: one note each in `notes`, and that key's default applies.
template<class Text> TripleRigSettings parse_triple_rig(Text text,std::vector<std::string>& notes){
    TripleRigSettings s;
    const auto trimmed=[](std::string t){
        while(!t.empty()&&std::isspace(static_cast<unsigned char>(t.back())))t.pop_back();
        std::size_t a=0;while(a<t.size()&&std::isspace(static_cast<unsigned char>(t[a])))++a;
        return t.substr(a);};
    struct Key { const char* name; double TripleRigSettings::*field; double min,max; bool below_max; };
    static constexpr Key keys[]{{"PanelWidthMm",&TripleRigSettings::panel_width_mm,100.,3000.,false},
        {"PanelHeightMm",&TripleRigSettings::panel_height_mm,100.,2000.,false},{"EyeDistanceMm",&TripleRigSettings::eye_distance_mm,200.,3000.,false},
        {"SideAngle",&TripleRigSettings::side_angle_deg,0.,90.,true},{"BezelMm",&TripleRigSettings::bezel_mm,0.,100.,false}};
    for(const Key& k:keys){
        const std::string t=trimmed(std::string(text(k.name)));if(t.empty())continue;
        char* end=nullptr;const double v=std::strtod(t.c_str(),&end);
        if(end!=t.c_str()&&*end=='\0'&&std::isfinite(v)&&v>=k.min&&(k.below_max?v<k.max:v<=k.max)){
            s.*k.field=v;if(k.field==&TripleRigSettings::panel_height_mm)s.panel_height_set=true;continue;}
        char note[160];std::snprintf(note,sizeof note,"%s = %.32s refused (%g to %s%g); %g applies",k.name,t.c_str(),k.min,
                                     k.below_max?"below ":"",k.max,s.*k.field);
        notes.push_back(note);
    }
    std::string m=trimmed(std::string(text("MatchGameFov")));
    for(char& ch:m)ch=char(std::tolower(static_cast<unsigned char>(ch)));
    if(m=="false")s.match_game_fov=false;
    else if(!m.empty()&&m!="true")notes.push_back("MatchGameFov = "+m.substr(0,32)+" refused (true or false); true applies");
    return s;
}
inline TripleRig triple_rig_from(const TripleRigSettings& s){
    return {s.panel_width_mm,s.panel_height_mm,s.eye_distance_mm,s.side_angle_deg,s.side_angle_deg,0.,s.bezel_mm};
}
// MatchGameFov = false: the race camera's vertical FOV (radians) at which the centre panel's frustum is the physical
// eye's. It is taken from the panel's width and the render's aspect (the panel's pixels), so triple_views' matched eye is
// exactly EyeDistanceMm; PanelHeightMm agrees with it when the panel's millimetres have its pixels' aspect.
inline double triple_physical_fov_y(const TripleRigSettings& s,double render_aspect){
    return 2.*std::atan(s.panel_width_mm/2./s.eye_distance_mm/render_aspect);
}
// What the window and the views are, for runtime.log and the F6 Cameras page (filled once the window opens).
struct TripleStatus {
    TripleScreens mode{TripleScreens::off};
    ScreenRect window{};
    std::string where;        // "the primary monitor", "over three monitors", or "refused: <why> (<n> found); the primary monitor"
    float aspect{};           // planar_aspect(): 0 = one 16:9 view; otherwise the planar span's width / height
    TripleProjection requested{TripleProjection::planar},applied{TripleProjection::planar};
    std::string projection_note;   // why Angled was not applied ("" when it was, or when Planar was asked for)
};
const TripleStatus& triple_screens_status();   // window_desktop.cpp
// Why triple_span_from_monitors refused, for the log line.
inline const char* triple_span_refusal(const std::vector<ScreenRect>& r){
    if(r.size()!=3)return "not exactly three monitors";
    return "the three monitors are not equal and side by side without gap or overlap";
}
}
