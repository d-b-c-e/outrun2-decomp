#include "ffb_shadow.hpp"
#include <utility>

namespace outrun::desktop::ffb::shadow {
namespace {
const char* wall_event_name(producer2::WallEvent event){
    switch(event){
    case producer2::WallEvent::None:return "none";
    case producer2::WallEvent::Rebound:return "rebound";
    case producer2::WallEvent::Scrape:return "scrape";
    }
    return "unknown";
}
}
Core::Core(std::function<bool(const std::string&)> write,const producer2::Strengths& strengths,RequestSink request_sink)
    :write_(std::move(write)),request_sink_(std::move(request_sink)),stream_(strengths){}
std::string Core::header(const producer2::Strengths& strengths,bool selected){
    const auto base=producer2::trace_header();
    const auto line=base.find('\n');
    auto columns=base.substr(line+1);columns.pop_back(); // drop the newline; the wall columns follow the @2 ones
    return std::string("# dbce.outrun-producer-ffb-shadow-trace@4,model=")+producer2::ModelId+
        ",output="+(selected?"selected-request":"software-only")+
        ",seam=just-completed-car-control,steering_percent="+
        std::to_string(strengths.steering_percent)+",shift_percent="+
        std::to_string(strengths.shift_percent)+"\n"
        "origin,frame_counter,update_index,gate,continuity_token,recovery_pending,"+columns+
        ",rebound_283,contact_side_281,wall_contact_4_20000,wall_event\n";
}
void Core::set_writer(std::function<bool(const std::string&)> write) noexcept {write_=std::move(write);trace_failed_=false;}
void Core::invalidate() noexcept {stream_.reset();have_valid_pair_=false;neutral_latched_=true;}
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
bool Core::recovery(const Gate& gate,const char* origin){
    if(continuity_!=gate.continuity){
        continuity_=gate.continuity;
        neutral_latched_=false; // every successful tow/snap gets its own zero row, even after a neutral countdown
        neutral(gate,origin,gate.recovery_pending?"recovery-pending":"recovery-snap");
        return true; // the control call that changed continuity is never a model sample
    }
    if(gate.recovery_pending){neutral(gate,origin,"recovery-pending");return true;}
    return false;
}
bool Core::write_row(const Gate& gate,const char* origin,const char* why,const producer2::Row& row) noexcept {
    try{if(request_sink_)request_sink_(gate,row);}catch(...){stop();return false;}
    if(write_){
        bool written=false;
        try{
            const std::string prefix=std::string(origin)+","+std::to_string(gate.frame_counter)+","+
                std::to_string(gate.update_index)+","+why+","+std::to_string(gate.continuity)+","+
                std::to_string(unsigned(gate.recovery_pending))+",";
            auto csv=producer2::trace_csv(row);csv.pop_back();
            written=write_(prefix+csv+","+std::to_string(unsigned(row.rebound_283))+","+
                std::to_string(unsigned(row.contact_side_281))+","+std::to_string(unsigned(row.wall_contact))+","+
                wall_event_name(row.wall_event)+"\n");
        }catch(...){}
        if(!written){write_={};trace_failed_=true;}
    }
    if(write_||request_sink_)return true;
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
        if(!previous_game_||!gate.car_active)continuity_=gate.continuity;
        else if(recovery(gate,"frame"))return;
        if(!previous_game_||paused_||panel_open_||!gate.car_active)
            neutral(gate,"frame",!previous_game_?"outside-game":paused_?"pause":panel_open_?"panel-open":"no-car");
    }catch(...){stop();}
}
void Core::observe(const Sample& sample) noexcept {
    if(!active_)return;
    try{
        const auto& g=sample.gate;
        mode(g.mode);generation(g,"control");
        if(!previous_game_||!g.car_active)continuity_=g.continuity;
        else if(recovery(g,"control"))return;
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
void Core::stop() noexcept {active_=false;stream_.reset();write_={};request_sink_={};}
}
