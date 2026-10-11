#pragma once
// R3 (Astra 16754, decision A): a first latched run's recorded normalized Frames, played at the adapter boundary in a
// second, cold latched run. Adapter/action replay: not a raw-to-Core replay, a mappings test or a route claim.
// - The tape is a sealed capture's validated export (P21Finalizer --seal-export). Only its race part is kept: from the
//   first row where the recorded adapter was in gameplay with the run Running, through the last consecutive Running row.
// - Live Core stays authoritative. Playback starts at the live race entry (in a race, run Running) and plays one row per
//   tick only while the live run is Running; any other live tick ends it, and from then the live frame (neutral in a
//   latched run) goes to the adapter, as it does before the start and after the tape's last row.
// - A played frame is the live frame with the tape row's actions and edges, which is all the adapter reads besides
//   suppress_native (kept live). Its actions carry no sample index: they come from the tape, not a live sample.
#include "outrun_capture.hpp"
#include <array>
#include <string>
#include <vector>
namespace outrun::desktop {

class Tape {
public:
    struct Row { std::uint64_t seq{}; std::array<dbce::shim::ActionValue,dbce::shim::ActionCount> action{}; std::uint64_t down{},up{}; };
    // Strict: an unreadable export, a row that does not decode, or no race rows with the run Running throws.
    explicit Tape(const std::filesystem::path& exported){
        bool in_race=false,done=false;
        source_rows_=rec::read_export(exported,[&](const rec::Channels& c){
            const auto f=rec::decode(frame_channels(c));const auto s=decoded_signals(c);
            if(done)return;
            const bool running=f.run==dbce::shim::Run::Running;
            if(!in_race){if(!(s.in_gameplay&&running))return;in_race=true;}
            if(!running){done=true;return;}
            Row r;r.seq=f.seq;r.action=f.action;r.down=f.down;r.up=f.up;
            for(auto& a:r.action)a.sample=-1;
            rows_.push_back(r);
        });
        if(rows_.empty())throw std::runtime_error("the tape has no race rows with the run Running");
    }
    std::size_t size()const{return rows_.size();}
    std::uint64_t source_rows()const{return source_rows_;}
    const Row& row(std::size_t i)const{return rows_.at(i);}
    bool started()const{return started_;}
    bool ended()const{return ended_;}
    std::size_t played()const{return next_;}
    const std::string& end_reason()const{return end_reason_;}
    // Input thread, once per tick: the frame the adapter applies; `row` is the tape row played, or -1.
    const dbce::shim::Frame& play(const dbce::shim::Frame& live,bool live_race,std::int64_t& row){
        row=-1;
        if(ended_)return live;
        const bool running=live.run==dbce::shim::Run::Running;
        if(!started_){if(!(live_race&&running))return live;started_=true;}
        if(!running){ended_=true;end_reason_=std::string("live run ")+dbce::shim::run_name(live.run);return live;}
        played_=live;const auto& r=rows_[next_];
        played_.action=r.action;played_.down=r.down;played_.up=r.up;
        row=std::int64_t(next_);
        if(++next_==rows_.size()){ended_=true;end_reason_="tape complete";}
        return played_;
    }
private:
    std::vector<Row> rows_;
    std::uint64_t source_rows_{};
    std::size_t next_{};
    bool started_{},ended_{};
    std::string end_reason_;
    dbce::shim::Frame played_;
};
}
