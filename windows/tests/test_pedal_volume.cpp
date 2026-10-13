// FixFullPedalChecks (src/platform/race_input.hpp): the accelerator / brake volumes as 453720 / 453750 report them, with
// the pedal words both input backends write (lround(value*255): source/outrun_adapter.cpp for the shim,
// source/wheel_input.cpp for Native, into 5A7B50[0]'s accelerator 0x0D / brake 0x0C words) through the game's own
// 453860 update. No device, no game.
#include "platform/race_input.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
namespace {
int checks=0;
void check(bool v,const char* what){++checks;if(!v){std::fprintf(stderr,"pedal volume: FAIL: %s\n",what);std::exit(1);}}
using namespace outrun::platform;
// Both backends' pedal word for a normalized pedal value.
std::uint32_t backend_word(double value){return std::uint32_t(std::lround(value*255.0));}
struct Race {
    PcInputAnalog a{};float filter{};PcInputDevice d{};PcInputConfig cfg=pc_input_joystick_config(0);
    void tick(std::uint32_t accel,std::uint32_t brake){
        d.axes_94[cfg.accel_axis]=std::int16_t(accel);d.axes_94[cfg.brake_axis]=std::int16_t(brake);
        pc_input_analog_453860(a,filter,d,cfg,0,16);
    }
};
std::int32_t expected(std::int32_t raw,bool fix){return fix&&raw>=250&&raw<255?255:raw;}
}
int main(){
    check(g_pc_fix_full_pedal_checks,"on by default, as Tweaks' FixFullPedalChecks");
    check(pc_input_joystick_config(0).accel_axis==0x0d&&pc_input_joystick_config(0).brake_axis==0x0c,
          "the backends' words are 5A7B50[0]'s accelerator and brake");
    for(const bool fix:{true,false}){
        g_pc_fix_full_pedal_checks=fix;
        // Every pedal word 0..255, current and previous, on both pedals.
        for(std::int32_t raw=0;raw<=255;++raw){
            Race r;r.tick(std::uint32_t(raw),std::uint32_t(raw));
            if(pc_input_volume_current(r.a,1)!=expected(raw,fix)||pc_input_volume_current(r.a,2)!=expected(raw,fix))
                check(false,"current accelerator / brake volume");
            r.tick(0,0);   // the previous tick's values move to +8
            if(pc_input_volume_previous(r.a,1)!=expected(raw,fix)||pc_input_volume_previous(r.a,2)!=expected(raw,fix))
                check(false,"previous accelerator / brake volume");
            if(pc_input_volume_current(r.a,1)!=0||pc_input_volume_current(r.a,2)!=0)check(false,"released pedals read 0");
        }
        check(true,fix?"0..255 with the fix: 250..254 read as 255":"0..255 without the fix: unchanged");
    }
    g_pc_fix_full_pedal_checks=true;
    // A pedal pressed to the end of a real travel: the backends' words just under 255 now meet the full-press checks.
    for(const double v:{0.98,0.982,0.99,0.995,0.998,1.0}){
        Race r;r.tick(backend_word(v),backend_word(v));
        if(pc_input_volume_current(r.a,1)!=255||pc_input_volume_current(r.a,2)!=255)check(false,"a nearly full pedal reads full");
    }
    check(backend_word(0.98)==250&&backend_word(0.978)==249,"the 250 edge is the backends' lround(0.98 * 255)");
    {Race r;r.tick(backend_word(0.978),backend_word(0.978));
     check(pc_input_volume_current(r.a,1)==249&&pc_input_volume_current(r.a,2)==249,"249 stays 249: only 250..254 move");}
    // The stored channels, and so the edges and the consumed capture, are as written.
    {Race r;r.tick(252,253);
     check(r.a.current[1]==252&&r.a.current[2]==253,"the stored 7D6810 values are unchanged");
     check(r.a.accel_edge_1c==1&&r.a.brake_edge_2c==1,"the edges come from the stored values");
     check(pc_input_volume_current(r.a,1)==255&&r.a.current[1]==252,"reading does not write");}
    // Only channels 1 and 2: steering (0) and the other axes (3..6) read as stored, and so do negative values.
    {PcInputAnalog a{};
     for(unsigned ch=0;ch<7;++ch){a.current[ch]=252;a.previous[ch]=-252;}
     for(const unsigned ch:{0u,3u,4u,5u,6u})
         if(pc_input_volume_current(a,ch)!=252||pc_input_volume_previous(a,ch)!=-252)check(false,"other channels are unchanged");
     check(pc_input_volume_previous(a,1)==-252&&pc_input_volume_previous(a,2)==-252,"negative pedal values are unchanged");
     check(pc_input_volume_current(a,1)==255&&pc_input_volume_current(a,2)==255,"channels 1 and 2 are the pedals");}
    static_assert(pc_input_volume_read(254,1,true)==255&&pc_input_volume_read(254,1,false)==254&&pc_input_volume_read(255,2,true)==255&&
                  pc_input_volume_read(254,0,true)==254&&pc_input_volume_read(256,1,true)==256,"the rule as a constant expression");
    std::printf("pedal volume: %d checks passed\n",checks);
    return 0;
}
