// [Audio] AllowHorn (race_sound_runtime.cpp native_race_horn; Tweaks' AudioHooks_Update): the horn action's SE 0x1BC on the
// race SE queue 9563E8 (424940's ring: 32 entries, read 9560C0 / write 956124, gated by event 383).
#include "platform/native_runtime.hpp"
#include "platform/race_sound_runtime.hpp"
#include "platform/race_area_runtime.hpp"
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
    // The stop is kept until 424940 takes it (sana #129986): an accepted loop, a release while event 383 is paused
    // (flags 0x12: 424940 refuses), then the queue open again with the horn still released.
    reset();c->event_state.slots[383].flags=2;native_race_horn(*c,true);
    req(entry(0)==0x9bcu&&c->race.sound.horn_active,"the loop is accepted");
    c->event_state.slots[383].flags=0x12;native_race_horn(*c,false);
    req(cursor(4)==1u&&c->race.sound.horn_active,"released while event 383 is paused: the stop is refused and stays pending");
    c->event_state.slots[383].flags=2;native_race_horn(*c,false);
    req(entry(1)==0x81bcu&&cursor(4)==2u&&!c->race.sound.horn_active,"the queue open again, still released: the pending stop is sent");
    native_race_horn(*c,false);
    req(cursor(4)==2u,"and only once");
    // The full ring refuses it too (read == write): the stop waits for room.
    reset();native_race_horn(*c,true);
    {const std::uint32_t r=1u;std::memcpy(w.sound_state.data(),&r,4);}           // write 1 == read 1: full
    native_race_horn(*c,false);
    req(c->race.sound.horn_active&&entry(1)!=0x81bcu,"a full ring: the stop stays pending");
    {const std::uint32_t r=0u;std::memcpy(w.sound_state.data(),&r,4);}           // room again
    native_race_horn(*c,false);
    req(entry(1)==0x81bcu&&!c->race.sound.horn_active,"room again: the stop is sent");
    // ClearAllSound does not end the stop obligation (lola #130449): 424700 runs 427630 and then flushes the ring in the
    // same callback, so a loop still queued plays after the clear. The real 424790 destroy runs the production 427630.
    reset();c->mode_state.current=16u;native_race_horn(*c,true);
    req(entry(0)==0x9bcu&&c->race.sound.horn_active,"a loop is queued");
    // Destroy reads no AREA records; an empty current-frame mapping avoids rebuilding unrelated retail EXE data.
    native_race_area(*c).memory_frame=c->completed_frames+1u;
    {const auto clears=c->race.sound.clear_alls;
     req(native_race_sound_invoke(*c,0x424790u,nullptr)&&c->race.sound.clear_alls==clears+1u,"the production ClearAllSound ran (424790)");}
    req(c->race.sound.horn_active,"after the clear the horn still owes its stop");
    std::memset(w.sound_entries_9563e8.data(),0xff,4);                       // 4249A0 flushed (played) the queued loop
    c->event_state.slots[383].flags=2;native_race_horn(*c,false);
    req(entry(1)==0x81bcu&&!c->race.sound.horn_active,"the next released tick still sends the stop");
    // The seam (sana #129986): 453BB0's input step sounds the horn from the FINAL input, after a consumed playback
    // replaced the live one (as decoded_consumed restores final.horn), once per tick.
    reset();c->mode_state.current=16u;
    native_input_replay_hook()=[](NativeInputState& in,bool&,const NativeRuntimeContext&){in.horn=true;};
    c->input_state=NativeInputState{};native_input_step_453bb0(*c);
    req(entry(0)==0x9bcu&&c->race.sound.horn_active,"a replayed tick with the horn held sounds it (live input released)");
    native_input_replay_hook()=[](NativeInputState& in,bool&,const NativeRuntimeContext&){in.horn=false;};
    c->input_state=NativeInputState{};c->input_state.horn=true;native_input_step_453bb0(*c);
    req(entry(1)==0x81bcu&&!c->race.sound.horn_active,"a replayed release wins over a live held horn: the stop");
    native_input_replay_hook()=nullptr;
    std::memset(w.sound_entries_9563e8.data(),0xff,4);                       // the first loop was consumed meanwhile
    c->input_state=NativeInputState{};c->input_state.horn=true;native_input_step_453bb0(*c);
    req(entry(2)==0x9bcu&&c->race.sound.horn_active,"no playback: the live input's horn");
    std::printf("race horn: %u checks passed\n",checks);
}
