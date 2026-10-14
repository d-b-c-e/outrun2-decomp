#include "ffb_shadow.hpp"
#include "platform/car_producer_observer.hpp"
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdio>

using namespace outrun::desktop::ffb::shadow;
namespace producer=outrun::desktop::ffb::producer2;
namespace platform=outrun::platform;
namespace {
int checks{};
void require(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
template<class T> void put(std::array<std::uint8_t,0xd52>& car,std::size_t at,T value){std::memcpy(car.data()+at,&value,sizeof value);}
bool contains(const std::vector<std::string>& rows,const char* needle){
    for(const auto& row:rows)if(row.find(needle)!=std::string::npos)return true;return false;
}
void throws(void*,const platform::NativeCarProducerObservation&){throw std::runtime_error("optional observer");}
}
int main(){
    try{
        require(Core::header().find("seam=just-completed-car-control")!=std::string::npos,"truthful seam");
        require(Core::header().find("frame_counter,update_index,gate,state")!=std::string::npos,"native tick columns");
        require(platform::game_steer_written(16,120)&&!platform::game_steer_written(16,121)&&
                !platform::game_steer_written(13,0),"same-mode and D50 steering write gate");
        require(platform::fresh_car_producer(true,true,1,true,true,0),"direct successful GamePlCar fresh");
        require(!platform::fresh_car_producer(false,true,1,true,true,0)&&
                !platform::fresh_car_producer(true,true,1,true,true,0x00800000u)&&
                !platform::fresh_car_producer(true,true,1,false,true,0),"nested, Pas bit and failed control refuse");
        std::array<std::uint8_t,0xd52> car{};
        put(car,0x202,std::int16_t(16384));put(car,0x1f8,72.0f);put(car,0x208,std::uint32_t(2));
        std::vector<std::string> rows;
        Core core([&](const std::string& row){rows.push_back(row);return true;});
        Gate g{};g.mode=16;g.generation=1;g.car_active=true;g.frame_counter=10;g.update_index=0;g.attempt=1;
        Sample s{};s.car=car.data();s.car_bytes=car.size();s.producer_fresh=true;
        auto step=[&]{s.gate=g;core.before_tick(g);core.observe(s);};
        step();require(rows.size()==1&&contains(rows,"fresh,1,new-epoch-or-car"),"first direct sample primes");
        g.update_index=1;g.attempt=2;step();
        require(rows.size()==2&&contains(rows,"10,1,fresh,2,none"),"next native tick computes");
        // A nested PasPlCar call can write the same steer bytes; only direct dispatch is fresh.
        g.frame_counter=11;g.update_index=0;g.attempt=3;s.producer_fresh=false;step();
        require(rows.size()==3&&contains(rows,"unproven-producer,0,outside-game"),"nested call resets once");
        g.update_index=1;g.attempt=4;s.producer_fresh=true;step();
        require(rows.size()==4&&contains(rows,"11,1,fresh,1,new-epoch-or-car"),"direct resume primes");
        g.frame_counter=12;g.update_index=0;g.attempt=4;g.panel_open=true;core.before_tick(g);
        require(rows.size()==5&&contains(rows,"panel-open,0,outside-game"),"F6 neutral without game callback");
        s.gate=g;core.observe(s);require(rows.size()==5,"F6 cannot revive a fresh-looking neutral steer");
        g.update_index=1;g.attempt=5;g.panel_open=false;step();
        require(rows.size()==6&&contains(rows,"12,1,fresh,1,new-epoch-or-car"),"F6 close primes");
        g.frame_counter=13;g.update_index=0;g.paused=true;core.before_tick(g);
        require(rows.size()==7&&contains(rows,"pause,0,outside-game"),"pause neutral without mode change or callback");
        g.update_index=1;g.attempt=6;g.paused=false;step();
        require(rows.size()==8&&contains(rows,"13,1,fresh,1,new-epoch-or-car"),"unpause primes");
        g.attempt=7;step();require(rows.size()==9&&contains(rows,"duplicate-tick,0,outside-game"),"duplicate pair resets");
        g.attempt=8;step();require(rows.size()==9,"duplicate pair cannot recover in same native tick");
        g.frame_counter=14;g.update_index=0;g.attempt=9;step();
        require(rows.size()==10&&contains(rows,"14,0,fresh,1,new-epoch-or-car"),"following tick primes");
        g.frame_counter=15;g.generation=2;g.attempt=1;step();
        require(rows.size()==12&&contains(rows,"15,0,new-car,0,outside-game")&&
                contains(rows,"15,0,fresh,1,new-epoch-or-car"),"new car neutral then primes");

        bool gameplay_success=true;platform::NativeCarProducerObserver observer{nullptr,throws};
        platform::NativeCarProducerObservation observation{};
        platform::notify_car_producer(observer,observation);
        require(gameplay_success&&observer.observe==nullptr,"throwing observer detaches without gameplay fault");
        int writes=0;
        Core failing([&](const std::string&){return ++writes<2;});
        g.frame_counter=20;g.update_index=0;g.attempt=1;failing.before_tick(g);s.gate=g;failing.observe(s);
        require(failing.active(),"initial row written");
        g.update_index=1;g.attempt=2;failing.before_tick(g);s.gate=g;failing.observe(s);
        require(!failing.active(),"writer failure detaches only shadow");
        failing.observe(s);require(writes==2,"detached writer never retried");
        std::printf("ffb shadow %d checks passed\n",checks);return 0;
    }catch(const std::exception& ex){std::fprintf(stderr,"ffb shadow failed: %s\n",ex.what());return 1;}
}
