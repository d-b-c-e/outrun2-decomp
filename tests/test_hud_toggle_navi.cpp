// [Misc] HudToggleKey routing (src/platform/pc_screen.hpp): the race HUD's NaviPub_Disp (4BEB00) draws nothing while
// 8447F8 is 0, and each posted toggle flips that flag as Tweaks does, applied as race_hud_runtime.cpp applies it just
// before the display. NaviPub's own globals only (their EXE contents are compiled in); no game files, window or input.
#include "platform/race_hud_navi.hpp"
#include "platform/race_area.hpp"
#include "platform/pc_screen.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
int checks=0;
void check(bool v,const char* what){++checks;if(!v){std::fprintf(stderr,"hud toggle navi: FAIL: %s\n",what);std::exit(1);}}
using namespace outrun::platform;
std::uint32_t flag(const NaviPubGlobals& g){std::uint32_t v;std::memcpy(&v,g.g842800.data()+(PcNaviDisplayFlag-0x842800u),4);return v;}
void set_flag(NaviPubGlobals& g,std::uint32_t v){std::memcpy(g.g842800.data()+(PcNaviDisplayFlag-0x842800u),&v,4);}
// race_hud_runtime.cpp, just before navi_pub_display_4beb00: the toggles posted since the last display.
void apply_posted(NaviPubGlobals& g){if(const auto r=g_pc_hud_toggle_requests.exchange(0u))set_flag(g,pc_hud_toggle_apply(flag(g),r));}
struct Display { bool ok; bool ran; std::uint32_t hearts; };
// One NaviPub_Disp on these globals alone. "ran": it went past the flag (it wrote a global, called the heart service
// or reached memory this fixture does not map); with the flag at 0 it must do none of these.
Display display(NaviPubGlobals& g){
    const auto before=g.g842800;
    PcRaceMemory m;g.map(m);
    NaviPubServices s;s.m=&m;
    std::uint32_t hearts=0;s.heart_user=&hearts;s.heart=[](void* u,std::uint32_t){++*static_cast<std::uint32_t*>(u);return true;};
    const bool ok=navi_pub_display_4beb00(s);
    return {ok,g.g842800!=before||hearts>0||s.fault!=0||s.missing!=0||!ok,hearts};
}
}
int main(){
    NaviPubGlobals g;
    set_flag(g,1u);                                 // as the HUD's init (4BC9E0) leaves it
    check(display(g).ran,"shown (8447F8 = 1): NaviPub_Disp goes on past its flag");
    set_flag(g,1u);
    g_pc_hud_toggle_requests.fetch_add(1u);         // the key's release (hud_toggle.cpp)
    apply_posted(g);
    check(flag(g)==0u&&g_pc_hud_toggle_requests.load()==0u,"one toggle: the flag is 0 and the request is taken");
    const auto hidden=display(g);
    check(hidden.ok&&!hidden.ran&&hidden.hearts==0u,"hidden: NaviPub_Disp returns at once (no global written, no service, no draw)");
    check(display(g).ok&&!display(g).ran,"each later display stays hidden until the next toggle");
    g_pc_hud_toggle_requests.fetch_add(1u);
    apply_posted(g);
    check(flag(g)==1u&&display(g).ran,"the next toggle shows the HUD again");
    // Presses posted outside a race are dropped by the init (race_hud_runtime.cpp): nothing is pending at the display.
    g_pc_hud_toggle_requests.fetch_add(1u);
    g_pc_hud_toggle_requests.store(0u);             // 4BC9E0's init
    set_flag(g,1u);apply_posted(g);
    check(flag(g)==1u,"a press before the race's init changes nothing");
    std::printf("hud toggle navi: %d checks passed\n",checks);
    return 0;
}
