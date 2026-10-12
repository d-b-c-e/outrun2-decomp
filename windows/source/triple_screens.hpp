#pragma once
// [Triple] Screens (STD-022: Off / Surround / Separate monitors) and the pure rules behind it, tested offline
// (windows/tests/test_triple_screens.cpp); window_desktop.cpp feeds them the INI text and EnumDisplayMonitors.
// Design and review: windows/docs/TRIPLE-SCREENS.md.
#include <algorithm>
#include <cctype>
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
// Why triple_span_from_monitors refused, for the log line.
inline const char* triple_span_refusal(const std::vector<ScreenRect>& r){
    if(r.size()!=3)return "not exactly three monitors";
    return "the three monitors are not equal and side by side without gap or overlap";
}
}
