// The race-view diagnostic line (sana 70042): one line per change of mode, race view or FOV override; nothing for
// repeated frames; menu and attract view switches are not tracked. Needs no game files.
#include "platform/race_view_note.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace outrun::platform;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"race view note line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
RaceViewSample chase(std::uint8_t view){
    RaceViewSample s{};
    s.mode_78026c=0x10;s.variant_780258=0;s.race=true;s.view_34a=view;
    s.fov_y_a0=0.6719f;s.fov_x_a4=1.1118f;s.znear_bc=1.0f;s.aspect_b8=2560.f/1440.f;s.width=2560;s.height=1440;
    s.fov_override=false;s.znear_fix=true;
    return s;
}
bool has(const std::optional<std::string>& line,const char* text){return line&&line->find(text)!=std::string::npos;}
}
int main(){
    RaceViewNote note;
    const auto first=note.observe(chase(2));
    CHECK(has(first,"race view: mode 0x10 variant 0 view 2;"));
    CHECK(has(first,"FOV 38.5 deg vertical / 63.7 deg horizontal, near 1.00, aspect 1.778, render 2560x1440"));
    CHECK(has(first,"FOV override off, near-plane fix on (FixZBufferPrecision)"));
    for(int frame=0;frame<100;++frame)CHECK(!note.observe(chase(2)));          // repeated frames log nothing
    auto moved=chase(2);moved.fov_y_a0=0.5f;moved.znear_bc=0.25f;
    CHECK(!note.observe(moved));                                               // a value change alone is not a view change
    CHECK(has(note.observe(chase(0)),"view 0;"));                              // the camera button: a new line
    auto triple=chase(0);triple.fov_override=true;
    CHECK(has(note.observe(triple),"FOV override on ([Triple] MatchGameFov = false)"));
    auto goal=chase(0);goal.mode_78026c=0x13;
    CHECK(has(note.observe(goal),"mode 0x13"));                                // a mode change in race views
    RaceViewSample menu{};menu.mode_78026c=0x20;menu.race=false;menu.view_34a=0x1e;
    CHECK(has(note.observe(menu),"race view: none (mode 0x20"));
    for(std::uint8_t v=0;v<40;++v){menu.view_34a=v;CHECK(!note.observe(menu));}   // menu/attract view switches: no line
    CHECK(has(note.observe(chase(2)),"view 2;"));                              // back to the race: logged again
    std::printf("PASS race view note: %u checks\n",checks);
    return 0;
}
