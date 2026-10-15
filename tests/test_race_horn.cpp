// [Audio] AllowHorn (race_sound_runtime.cpp native_race_horn; Tweaks' AudioHooks_Update): the horn action's SE 0x1BC on the
// race SE queue 9563E8 (424940's ring: 32 entries, read 9560C0 / write 956124, gated by event 383).
#include "platform/native_runtime.hpp"
#include "platform/race_sound_runtime.hpp"
#include "platform/pc_screen.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
using namespace outrun::platform;
namespace {
unsigned checks{};
void req(bool v,const char* m){++checks;if(!v){std::fprintf(stderr,"FAIL: %s\n",m);std::exit(1);}}
}
int main(){
    auto c=std::make_unique<NativeRuntimeContext>();
    auto& w=c->race.car_world;
    auto entry=[&](unsigned i){std::uint32_t v;std::memcpy(&v,w.sound_entries_9563e8.data()+i*4u,4);return v;};
    auto cursor=[&](unsigned at){std::uint32_t v;std::memcpy(&v,w.sound_state.data()+at,4);return v;};
    auto reset=[&]{   // race sound init (race_sound.cpp): read 31, write 0, every entry consumed
        std::memset(w.sound_entries_9563e8.data(),0xff,w.sound_entries_9563e8.size());
        const std::uint32_t r=31u,wr=0u;std::memcpy(w.sound_state.data(),&r,4);std::memcpy(w.sound_state.data()+4,&wr,4);
        c->race.sound.horn_active=false;};
    c->event_state.slots[383].flags=2;                                       // event 383 (SOUND) open
    g_pc_allow_horn=true;
    reset();c->mode_state.current=32u;native_race_horn(*c,true);
    req(cursor(4)==0u&&!c->race.sound.horn_active,"the menus: no horn");
    c->mode_state.current=16u;native_race_horn(*c,true);
    req(entry(0)==0x9bcu&&cursor(4)==1u&&c->race.sound.horn_active,"GAME, held: 0x1BC | SND_LOOP queued");
    native_race_horn(*c,true);
    req(cursor(4)==1u,"held while the loop is still pending: the queue drops the copy");
    std::memset(w.sound_entries_9563e8.data(),0xff,4);                       // the race sound control consumed it
    native_race_horn(*c,true);
    req(entry(1)==0x9bcu&&cursor(4)==2u,"held after it was consumed: queued again every frame, as Tweaks");
    native_race_horn(*c,false);
    req(entry(2)==0x81bcu&&cursor(4)==3u&&!c->race.sound.horn_active,"released: 0x1BC | SND_STOP once");
    native_race_horn(*c,false);
    req(cursor(4)==3u,"still released: nothing more");
    reset();native_race_horn(*c,true);c->mode_state.current=18u;native_race_horn(*c,true);
    req(entry(0)==0x9bcu&&entry(1)==0x81bcu&&cursor(4)==2u&&!c->race.sound.horn_active,"leaving GAME while held stops it");
    reset();c->mode_state.current=16u;g_pc_allow_horn=false;native_race_horn(*c,true);
    req(cursor(4)==0u&&!c->race.sound.horn_active,"[Audio] AllowHorn = false: no horn");
    g_pc_allow_horn=true;c->event_state.slots[383].flags=0;native_race_horn(*c,true);
    req(cursor(4)==0u&&c->race.sound.horn_active,"event 383 closed: 424940 drops it (the state follows the action, as Tweaks)");
    std::printf("race horn: %u checks passed\n",checks);
}
