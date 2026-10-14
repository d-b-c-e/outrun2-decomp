#include "ffb_shadow.hpp"
#include <utility>

namespace outrun::desktop::ffb::shadow {
Core::Core(std::function<bool(const std::string&)> write):write_(std::move(write)){}
std::string Core::header(){
    const auto base=producer2::trace_header();
    const auto line=base.find('\n');
    return std::string("# ")+producer2::TraceId+",model="+producer2::ModelId+
        ",output=software-only,seam=just-completed-car-control\n"
        "origin,frame_counter,update_index,gate,"+base.substr(line+1);
}
void Core::mode(std::uint32_t current) {
    const bool game=current==16u;
    if(game&&!previous_game_){++epoch_;if(epoch_==0u)++epoch_;stream_.reset();neutral_latched_=false;have_valid_pair_=false;}
    previous_game_=game;
}
void Core::generation(const Gate& gate,const char* origin){
    if(generation_==gate.generation)return;
    if(generation_!=0u)neutral(gate,origin,"new-car");
    generation_=gate.generation;
    stream_.reset();have_valid_pair_=false;
}
bool Core::write_row(const Gate& gate,const char* origin,const char* why,const producer2::Row& row) noexcept {
    try{
        const std::string prefix=std::string(origin)+","+std::to_string(gate.frame_counter)+","+
            std::to_string(gate.update_index)+","+why+",";
        if(write_&&write_(prefix+producer2::trace_csv(row)))return true;
    }catch(...){}
    stop();return false;
}
void Core::neutral(const Gate& gate,const char* origin,const char* why) {
    stream_.reset();
    if(neutral_latched_||!active_)return;
    neutral_latched_=true;
    producer2::Frame f{};f.race_epoch=epoch_;f.car_generation=gate.generation;f.source_tick=gate.attempt;
    const auto row=stream_.step(f);
    (void)write_row(gate,origin,why,row);
}
void Core::before_frame(const Gate& gate) noexcept {
    if(!active_)return;
    try{
        mode(gate.mode);generation(gate,"frame");panel_open_=gate.panel_open;paused_=gate.paused;
        if(!previous_game_||paused_||panel_open_||!gate.car_active)
            neutral(gate,"frame",!previous_game_?"outside-game":paused_?"pause":panel_open_?"panel-open":"no-car");
    }catch(...){stop();}
}
void Core::observe(const Sample& sample) noexcept {
    if(!active_)return;
    try{
        const auto& g=sample.gate;
        mode(g.mode);generation(g,"control");
        if(!previous_game_||paused_||panel_open_||g.paused||g.panel_open||!g.car_active||!sample.producer_fresh){
            neutral(g,"control",!previous_game_?"outside-game":(paused_||g.paused)?"pause":
                    (panel_open_||g.panel_open)?"panel-open":!g.car_active?"no-car":"unproven-producer");
            return;
        }
        if(have_valid_pair_&&valid_frame_==g.frame_counter&&valid_update_==g.update_index){neutral(g,"control","duplicate-tick");return;}
        producer2::Frame f{};
        f.mode=g.mode;f.race_epoch=epoch_;f.car_generation=g.generation;f.source_tick=g.attempt;
        f.producer_fresh=true;f.car=sample.car;f.car_bytes=sample.car_bytes;f.dt_seconds=1.0f/60.0f;
        const auto row=stream_.step(f);
        if(row.state==producer2::State::Neutral){
            if(!neutral_latched_){neutral_latched_=true;(void)write_row(g,"control","producer-rejected",row);}
        }else{
            neutral_latched_=false;have_valid_pair_=true;valid_frame_=g.frame_counter;valid_update_=g.update_index;
            (void)write_row(g,"control","fresh",row);
        }
    }catch(...){stop();}
}
void Core::stop() noexcept {active_=false;stream_.reset();write_={};}
}
