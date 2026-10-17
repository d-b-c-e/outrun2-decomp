// FineSteering (src/platform/race_input.hpp): the car's +0x202 steering word from 453860's steering value before its
// -127..127 step, through the game's own 453860 update with the steering word both input backends write (5A7B50[0]'s
// steer axis 0x0E, a signed 16-bit word). The 49FAD0 write in GAME is race_player_car.cpp's. No device, no game.
#include "platform/race_input.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
namespace {
int checks=0;
void check(bool v,const char* what){++checks;if(!v){std::fprintf(stderr,"fine steering: FAIL: %s\n",what);std::exit(1);}}
using namespace outrun::platform;
struct Race {
    PcInputAnalog a{};float filter{};PcInputDevice d{};PcInputConfig cfg;
    explicit Race(PcInputConfig c):cfg(c){}
    void tick(std::int16_t steer,std::uint32_t buttons,float* fine){
        d.axes_94[cfg.steer_axis]=steer;d.buttons_04=buttons;
        pc_input_analog_453860(a,filter,d,cfg,4,16,fine);
    }
};
bool same(const PcInputAnalog& x,const PcInputAnalog& y){
    return x.current==y.current&&x.previous==y.previous&&x.accel_edge_1c==y.accel_edge_1c&&x.brake_edge_2c==y.brake_edge_2c;
}
}
int main(){
    check(!g_pc_fine_steering,"off by default");
    // The word itself: k/127 is the PC's k << 8, full lock is 0x7F00, and nothing passes it.
    for(std::int32_t k=-127;k<=127;++k)
        if(pc_input_fine_steer_202(float(k)/127.f)!=std::int16_t(k*256))check(false,"k/127 gives the PC's k << 8");
    check(true,"k/127 gives the PC's k << 8 for every k");
    check(pc_input_fine_steer_202(1.f)==0x7f00&&pc_input_fine_steer_202(-1.f)==-0x7f00,"full lock is +-0x7F00");
    check(pc_input_fine_steer_202(1.5f)==0x7f00&&pc_input_fine_steer_202(-7.f)==-0x7f00,"clamped to full lock");
    check(pc_input_fine_steer_202(std::nanf(""))==0,"NaN is neutral");
    check(pc_input_fine_steer_202(0.75f/32512.f)==1&&pc_input_fine_steer_202(-0.75f/32512.f)==-1&&
          pc_input_fine_steer_202(0.25f/32512.f)==0,"rounded, not truncated");
    // Every steering word on the analogue path (a joystick configuration): the PC's channels and filter stay byte for
    // byte as without the setting; the fine word sits within one PC step of current[0] << 8, on the same side.
    const auto joystick=pc_input_joystick_config(0);
    check((joystick.flags&1u)!=0u&&joystick.steer_axis==0x0e,"5A7B50[0] is analogue with the backends' steer word 0x0E");
    std::set<std::int16_t> fine_words,pc_words;std::int16_t last=-0x7f00;bool monotonic=true,within=true,identical=true;
    for(std::int32_t word=-32768;word<=32767;++word){
        Race with(joystick),without(joystick);float fine=99.f;
        with.tick(std::int16_t(word),0,&fine);without.tick(std::int16_t(word),0,nullptr);
        if(!same(with.a,without.a)||std::memcmp(&with.filter,&without.filter,sizeof(float))!=0)identical=false;
        const std::int16_t f=pc_input_fine_steer_202(fine),pc=std::int16_t(with.a.current[0]*256);
        const std::int32_t gap=std::int32_t(f)-std::int32_t(pc);
        if(gap<-256||gap>256||(gap!=0&&(gap>0)!=(f>0)))within=false;
        if(f<last)monotonic=false;
        last=f;fine_words.insert(f);pc_words.insert(pc);
    }
    check(identical,"current[0..6], the edges and the filter are unchanged by the fine value");
    check(within,"the fine word is within one PC step of current[0] << 8, away from neutral");
    check(monotonic,"the fine word never decreases as the steering word rises");
    check(pc_words.size()<=255u&&pc_words.size()>=254u,"the PC's +0x202 has at most 255 steps over the 16-bit wheel");
    check(fine_words.size()>100u*pc_words.size(),"the fine word resolves the 16-bit wheel");
    check(*fine_words.begin()==-0x7f00&&*fine_words.begin()==*pc_words.begin(),"the left end is full lock, as the PC's");
    check(*fine_words.rbegin()>=*pc_words.rbegin()&&*fine_words.rbegin()<=0x7f00,"the right end reaches the PC's and stays in lock");
    // Inside the dead zone (1/128 on the analogue path) the fine word is neutral, as the PC's.
    {Race r(joystick);float fine=99.f;r.tick(200,0,&fine);check(fine==0.f&&pc_input_fine_steer_202(fine)==0,"dead zone stays neutral");}
    // The steering buttons' filter (0x80 right) wins when it is larger, as in 453860: the fine value is the same choice.
    {Race r(joystick);float fine=99.f;r.tick(0,0x80u,&fine);
     check(fine>0.f&&std::int32_t(fine*127.f)==r.a.current[0],"a button-filter steer is the value current[0] was stepped from");}
    // The digital path (the keyboard table: no analogue flag, 4537C0's smoother) leaves the fine value untouched.
    {const auto keyboard=pc_input_keyboard_config();
     check((keyboard.flags&1u)==0u,"5A7B30 has no analogue flag");
     Race with(keyboard),without(keyboard);float fine=99.f;
     for(const std::int16_t word:{std::int16_t(32767),std::int16_t(-20000),std::int16_t(0)}){
         with.tick(word,0,&fine);without.tick(word,0,nullptr);
         check(same(with.a,without.a),"the digital path's channels are unchanged");
     }
     check(fine==99.f,"the digital path never writes the fine value");}
    std::printf("fine steering: %d checks passed\n",checks);
    return 0;
}
