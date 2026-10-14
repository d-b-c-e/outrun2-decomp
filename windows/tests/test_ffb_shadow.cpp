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
        require(Core::header().find("shadow-trace@3")!=std::string::npos&&
                Core::header().find("origin,frame_counter,update_index,gate,continuity_token,recovery_pending,state")!=std::string::npos,
                "versioned recovery columns and native tick");
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
        auto step=[&]{s.gate=g;core.before_frame(g);core.observe(s);};
        step();require(rows.size()==1&&contains(rows,"fresh,0,0,1,new-epoch-or-car"),"first direct sample primes");
        g.update_index=1;g.attempt=2;step();
        require(rows.size()==2&&contains(rows,"10,1,fresh,0,0,2,none"),"next native tick computes");
        // A nested PasPlCar call can write the same steer bytes; only direct dispatch is fresh.
        g.frame_counter=11;g.update_index=0;g.attempt=3;s.producer_fresh=false;step();
        require(rows.size()==3&&contains(rows,"unproven-producer,0,0,0,outside-game"),"nested call resets once");
        g.update_index=1;g.attempt=4;s.producer_fresh=true;step();
        require(rows.size()==4&&contains(rows,"11,1,fresh,0,0,1,new-epoch-or-car"),"direct resume primes");
        g.frame_counter=12;g.update_index=0;g.attempt=4;g.panel_open=true;core.before_frame(g);
        require(rows.size()==5&&contains(rows,"frame,12,0,panel-open,0,0,0,outside-game"),"F6 frame neutral without game callback");
        s.gate=g;core.observe(s);require(rows.size()==5,"F6 cannot revive a fresh-looking neutral steer");
        g.update_index=1;g.attempt=5;g.panel_open=false;step();
        require(rows.size()==6&&contains(rows,"12,1,fresh,0,0,1,new-epoch-or-car"),"F6 close primes");
        g.frame_counter=13;g.update_index=0;g.paused=true;core.before_frame(g);
        require(rows.size()==7&&contains(rows,"frame,13,0,pause,0,0,0,outside-game"),"pause frame neutral without mode change or callback");
        g.update_index=1;g.attempt=6;g.paused=false;step();
        require(rows.size()==8&&contains(rows,"13,1,fresh,0,0,1,new-epoch-or-car"),"unpause primes");
        g.attempt=7;step();require(rows.size()==9&&contains(rows,"duplicate-tick,0,0,0,outside-game"),"duplicate pair resets");
        g.attempt=8;step();require(rows.size()==9,"duplicate pair cannot recover in same native tick");
        g.frame_counter=14;g.update_index=0;g.attempt=9;step();
        require(rows.size()==10&&contains(rows,"14,0,fresh,0,0,1,new-epoch-or-car"),"following tick primes");
        g.frame_counter=15;g.generation=2;g.attempt=1;step();
        require(rows.size()==12&&contains(rows,"frame,15,0,new-car,0,0,0,outside-game")&&
                contains(rows,"15,0,fresh,0,0,1,new-epoch-or-car"),"new car neutral then primes");

        // A same-control immediate tow is neutral even with a fresh steer write.
        // A delayed tow stays neutral for its countdown, including false/duplicate callbacks,
        // and the later snap is neutral before the following direct sample primes.
        std::vector<std::string> recovery_rows;
        Core recovering([&](const std::string& row){recovery_rows.push_back(row);return true;});
        Gate r{};r.mode=16;r.generation=1;r.car_active=true;r.frame_counter=30;r.attempt=1;
        Sample rs{};rs.car=car.data();rs.car_bytes=car.size();rs.producer_fresh=true;
        auto recover_step=[&]{rs.gate=r;recovering.before_frame(r);recovering.observe(rs);};
        auto recover_call=[&](std::uint32_t token,bool pending){
            recovering.before_frame(r);r.continuity=token;r.recovery_pending=pending;
            rs.gate=r;recovering.observe(rs);
        };
        recover_step();r.frame_counter=31;r.attempt=2;recover_step();
        require(recovery_rows.size()==2&&contains(recovery_rows,"31,0,fresh,0,0,2,none"),"pre-recovery computed");
        r.frame_counter=32;r.attempt=3;recover_call(1,false);
        require(recovery_rows.size()==3&&contains(recovery_rows,"control,32,0,recovery-snap,1,0,0,outside-game"),
                "same-call immediate tow neutral");
        r.frame_counter=33;r.attempt=4;recover_step();
        require(recovery_rows.size()==4&&contains(recovery_rows,"33,0,fresh,1,0,1,new-epoch-or-car"),
                "immediate tow next direct sample primes");
        r.frame_counter=34;r.attempt=5;recover_step();
        require(recovery_rows.size()==5&&contains(recovery_rows,"34,0,fresh,1,0,2,none"),"post-tow computes without stale gear event");
        r.frame_counter=35;r.attempt=6;recover_call(2,true);
        require(recovery_rows.size()==6&&contains(recovery_rows,"control,35,0,recovery-pending,2,1,0,outside-game"),
                "delayed tow neutral at scheduling call");
        r.frame_counter=36;r.attempt=7;rs.producer_fresh=false;recover_step();
        require(recovery_rows.size()==6,"pending recovery ignores failed producer");
        r.frame_counter=37;r.attempt=8;rs.producer_fresh=true;recover_step();
        require(recovery_rows.size()==6,"pending recovery cannot compute during countdown");
        r.frame_counter=38;r.attempt=9;recover_call(3,false);
        require(recovery_rows.size()==7&&contains(recovery_rows,"control,38,0,recovery-snap,3,0,0,outside-game"),
                "delayed snap neutral on same control call");
        r.frame_counter=39;r.attempt=10;recover_step();
        require(recovery_rows.size()==8&&contains(recovery_rows,"39,0,fresh,3,0,1,new-epoch-or-car"),
                "next direct sample after snap primes");
        r.frame_counter=40;r.attempt=11;r.mode=13;recovering.before_frame(r);
        require(recovery_rows.size()==9&&contains(recovery_rows,"frame,40,0,outside-game,3,0,0,outside-game"),
                "race exit neutral after recovery");

        bool gameplay_success=true;platform::NativeCarProducerObserver observer{nullptr,throws};
        platform::NativeCarProducerObservation observation{};
        platform::notify_car_producer(observer,observation);
        require(gameplay_success&&observer.observe==nullptr,"throwing observer detaches without gameplay fault");
        int writes=0;
        Core failing([&](const std::string&){return ++writes<2;});
        g.frame_counter=20;g.update_index=0;g.attempt=1;failing.before_frame(g);s.gate=g;failing.observe(s);
        require(failing.active(),"initial row written");
        g.update_index=1;g.attempt=2;failing.before_frame(g);s.gate=g;failing.observe(s);
        require(!failing.active(),"writer failure detaches only shadow");
        failing.observe(s);require(writes==2,"detached writer never retried");
        std::printf("ffb shadow %d checks passed\n",checks);return 0;
    }catch(const std::exception& ex){std::fprintf(stderr,"ffb shadow failed: %s\n",ex.what());return 1;}
}
