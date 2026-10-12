#pragma once
// Consumed playback, the smallest proof (hub artifacts/outrun-recording-package-20261012/PLAYBACK-SCOPE.md; sana
// 35159): an ordinary @2 recording's consumed input, put back where it was captured. In an admitted latched replay run
// only ([ShimReplay] Mode = consumed, shim_input.cpp), platform::native_input_replay_hook replaces each tick's input at
// 453BB0, after the PC layer's arbitration and before race_input_update_453bb0, with the recorded tick's final.* and the
// race update's PC-layer argument (final.pcDrive). Row i plays at the run's input tick i, from the first. After the last
// row, or for a refused tape, the seam holds neutral, never the live input. An input/startup proof: whether the game
// then follows the recording is measured on what it did (mode, gear, speed; the first divergence named), not claimed.
#include "outrun_capture.hpp"
#include <memory>
#include <string>
#include <vector>
namespace outrun::desktop {

class ConsumedTape {
public:
    struct Row { std::uint64_t seq{}; Replayed replayed; };
    // The managed export of a sealed @2 recording (P21Finalizer). Strict: a row without the replay channels (a tape
    // recorded before the replay seam), a value out of range, or no rows throws.
    explicit ConsumedTape(const std::filesystem::path& exported){
        source_rows_=rec::read_export(exported,[&](const rec::Channels& c){
            Row r;r.seq=rec::decode(frame_channels(c)).seq;r.replayed=decoded_replay(c);rows_.push_back(r);
        });
        if(rows_.empty())throw std::runtime_error("the tape has no rows");
        for(std::size_t i=1;i<rows_.size();++i)if(rows_[i].seq!=rows_[i-1].seq+1)throw std::runtime_error("the tape's rows are not consecutive frames");
    }
    std::size_t size()const{return rows_.size();}
    std::uint64_t source_rows()const{return source_rows_;}
    const Row& row(std::size_t i)const{return rows_.at(i);}
private:
    std::vector<Row> rows_;
    std::uint64_t source_rows_{};
};

// The seam's state, on the input thread.
class ConsumedReplay {
public:
    ConsumedReplay(std::unique_ptr<ConsumedTape> tape,std::string refusal):tape_(std::move(tape)),refusal_(std::move(refusal)){}
    // One call per tick at the seam: the next row's input and PC-layer argument; neutral once the tape is done or when it
    // was refused (the device stays connected and the PC layer on, as the F6 panel's neutral input).
    void apply(platform::NativeInputState& in,bool& pc_drive){
        ++ticks_;
        if(tape_&&next_<tape_->size()){
            const auto& r=tape_->row(next_);last_row_=std::int64_t(next_);++next_;
            in=r.replayed.input;pc_drive=r.replayed.pc_drive;
            return;
        }
        last_row_=-1;
        const bool connected=in.connected,layer=in.pc_input_layer;
        in=platform::NativeInputState{};in.connected=connected;in.pc_input_layer=layer;pc_drive=false;
    }
    bool admitted()const{return tape_!=nullptr;}
    const std::string& refusal()const{return refusal_;}
    std::size_t played()const{return next_;}
    std::size_t size()const{return tape_?tape_->size():0;}
    bool ended()const{return !tape_||next_==tape_->size();}
    std::uint64_t ticks()const{return ticks_;}
    std::int64_t last_row()const{return last_row_;}   // the row the last tick played, or -1 (neutral)
private:
    std::unique_ptr<ConsumedTape> tape_;
    std::string refusal_;
    std::size_t next_{};
    std::uint64_t ticks_{};
    std::int64_t last_row_{-1};
};
}
