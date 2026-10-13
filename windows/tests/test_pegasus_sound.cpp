// FixPegasusClopping (src/platform/pc_screen.hpp): the exact 4249F0 sound requests of SndOff_PEGA 44BCC0, which the race
// code (race_area.cpp event_callback) issues in order. No game, no sound device.
#include "platform/pc_screen.hpp"
#include <cstdio>
#include <cstdlib>
namespace {
int checks=0;
void check(bool v,const char* what){++checks;if(!v){std::fprintf(stderr,"pegasus sound: FAIL: %s\n",what);std::exit(1);}}
}
int main(){
    using namespace outrun::platform;
    check(g_pc_fix_pegasus_clopping,"on by default, as Tweaks' FixPegasusClopping");
    unsigned r[2]{0xdeadu,0xdeadu};
    check(pc_pegasus_sound_off_44bcc0(true,r)==2&&r[0]==0x808cu&&r[1]==0x808du,
          "with the fix: the PC's stop of 0x8C, then SND_STOP | 0x8D (the clop loop 44BB30 started as 0x88D)");
    unsigned s[2]{0xdeadu,0xdeadu};
    check(pc_pegasus_sound_off_44bcc0(false,s)==1&&s[0]==0x808cu&&s[1]==0xdeadu,"without the fix: only the PC's own stop of 0x8C");
    // The stop flag and the loop flag are Tweaks' (game.hpp SND_STOP 0x8000, SND_LOOP 0x800): the clop the event loops.
    check((r[1]&0x7fffu)==(0x88du&0x7ffu),"the added stop names the sound the event loops");
    std::printf("pegasus sound: %d checks passed\n",checks);
    return 0;
}
