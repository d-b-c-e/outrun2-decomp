// DefaultManualTransmission (src/platform/pc_screen.hpp): Tweaks' detour after 4DD250 on the transmission owner's bytes
// (the carousel's previous / current index at +E4 / +E8), and 4DD4A0's confirmation of +E8 into 830374. No game.
#include "platform/pc_screen.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
int checks=0;
void check(bool v,const char* what){++checks;if(!v){std::fprintf(stderr,"default transmission: FAIL: %s\n",what);std::exit(1);}}
std::uint32_t u32(const std::array<std::uint8_t,0x1f4>& o,std::size_t at){std::uint32_t v;std::memcpy(&v,o.data()+at,4);return v;}
}
int main(){
    using namespace outrun::platform;
    check(!g_pc_default_manual_transmission,"off by default, as Tweaks");
    // After 4DD250's carousel init: both indices 0 (Automatic); the rest of the object as the init left it.
    std::array<std::uint8_t,0x1f4> init{};for(std::size_t i=0;i<init.size();++i)init[i]=std::uint8_t(i*7u+3u);
    std::memset(init.data()+PcTransmissionPrevious,0,8);
    auto off=init;pc_default_manual_transmission_4dd250(false,off.data(),off.size());
    check(off==init,"off: the object is exactly as 4DD250 left it");
    check(pc_transmission_confirmed_4dd4a0(off.data())==0u,"off: confirming takes Automatic (0)");
    auto on=init;pc_default_manual_transmission_4dd250(true,on.data(),on.size());
    check(u32(on,PcTransmissionPrevious)==0u&&u32(on,PcTransmissionCurrent)==1u,"on: previous Automatic, current Manual");
    bool rest=true;for(std::size_t i=0;i<on.size();++i)if(i<PcTransmissionPrevious||i>=PcTransmissionCurrent+4u)rest=rest&&on[i]==init[i];
    check(rest,"on: no other byte changes");
    check(pc_transmission_confirmed_4dd4a0(on.data())==1u,"on: confirming takes Manual (1) into 830374");
    // A choice the player makes after it (the carousel moves +B4) is what confirming takes, as before.
    on[PcTransmissionCurrent]=0u;
    check(pc_transmission_confirmed_4dd4a0(on.data())==0u,"a later move back to Automatic is confirmed as Automatic");
    std::array<std::uint8_t,0xe8> shorter{};pc_default_manual_transmission_4dd250(true,shorter.data(),shorter.size());
    bool untouched=true;for(auto b:shorter)untouched=untouched&&b==0u;
    check(untouched,"a short object is left alone");
    std::printf("default transmission: %d checks passed\n",checks);
    return 0;
}
