#pragma once
// Triple screens, the game's side. Which screens apply, where the window opens, the centre screen, the screens'
// measurements and the three views' matrices are the Wheelkit runtime's (wheelkit.h wk_triples_get, wk_triples_views;
// wheelkit_runtime.cpp). Here: how this game draws over a span ([Triple] Views) and the stage sections it then needs.
// Tested offline (windows/tests/test_triple_screens.cpp). Design and history: windows/docs/TRIPLE-SCREENS.md.
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
namespace outrun::desktop {
struct ScreenRect { long left{},top{},right{},bottom{}; bool primary{}; };
// The planar span's aspect (pc_screen.hpp g_pc_wide_aspect): the window's width / height over a span wider than 16:9;
// 0 otherwise (one 16:9 view, fitted).
inline float planar_aspect(bool span,unsigned width,unsigned height){
    if(!span||!width||!height)return 0.f;
    if(std::uint64_t(width)*9u<=std::uint64_t(height)*16u)return 0.f;
    return float(double(width)/double(height));
}
// [Triple] Views: Planar, one wide view over the span (the default), or Angled, three projected views, each at its
// screen's angle (the runtime's matrices for the rig). Read once at start; Angled applies only over a span whose
// measurements the runtime could use.
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
}
