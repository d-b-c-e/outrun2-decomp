#pragma once
#include <array>
#include <mutex>
namespace outrun::desktop {
// Six existing Controls actions only; no device polling or OS input injection.
struct PanelNavigation {
    static constexpr unsigned mask=63;
    struct Batch {std::array<unsigned,64> states{};unsigned count{},held{};bool reset{},active{};};
    void block(){std::lock_guard<std::mutex> lock(mutex_);block_locked();}
    void update(unsigned held,bool allowed){
        std::lock_guard<std::mutex> lock(mutex_);held&=mask;
        if(!allowed){block_locked();return;}
        if(!active_){active_=true;wait_=held;reset_=true;}
        if(overflow_){if(held)return;overflow_=false;wait_=0;}
        wait_&=held;const unsigned next=held&~wait_;
        if(next==held_)return;
        if(count_==states_.size()){count_=0;held_=0;reset_=true;overflow_=true;return;}
        states_[count_++]=held_=next;
    }
    Batch drain(){std::lock_guard<std::mutex> lock(mutex_);Batch b{states_,count_,held_,reset_,active_};count_=0;reset_=false;return b;}
private:
    void block_locked(){count_=held_=wait_=0;active_=overflow_=false;reset_=true;}
    std::mutex mutex_;std::array<unsigned,64> states_{};unsigned count_{},held_{},wait_{};bool active_{},reset_{true},overflow_{};
};
}
