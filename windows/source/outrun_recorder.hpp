#pragma once
// The latched desktop capture (Astra 16553, R1): ProviderHost's recorder for OutRunDesktop.
// - Lazy, as the host requires: nothing starts before the first push. The first push fixes the channel schema, runs the
//   worst-case preflight, creates the capture folder and starts the reviewed P20 worker (windows/host/recorder).
// - One push per tick, on the input thread (the producer); the worker alone runs the session writer.
// - One sample slot per profile device (the device table set before the first push), so the fixed schema survives
//   device discovery, loss and reopen (Astra 16741).
// - The host's stop: finish (the producer declares its tail), then a bounded drain and the worker thread's own join.
//   Only an eligible, joined result writes the members (worker report, exact profile bytes, contract, budget, devices);
//   anything else reports a non-Complete outcome, so the host stays unresolved rather than claiming a capture.
// The finalizer (out of process) seals it only with the host's resolved report and an observed normal exit (P21).
#include "frame_recorder.hpp"
#include "outrun_capture.hpp"
namespace outrun::desktop {

constexpr const char* CaptureInputContract=rec::FrameInputContract;   // the frame codec's contract

struct CaptureSettings {
    std::filesystem::path folder;              // created new by the first push (inside the fenced run folder)
    std::string profile;                       // the exact [Controls] bytes Core parsed
    std::string run_nonce,adapter_version;
    double hz=60,seconds=300;                  // the worst-case preflight: 60 Hz for 300 s
    dbce::session::Limits limits{512ull*1024*1024,256*1024,300};
    std::size_t slots=510;                     // 510 fixed slots of 8216 bytes stay under 4 MiB
    std::function<void()> before_write;        // test seam (QueuedSink's): empty in the desktop
    // The contract and the context line: a latched run's @1 by default (byte-compatible with R1b); an ordinary
    // recording (Play with Recording) passes CaptureContract2 and its own context.
    std::string contract=CaptureContract;
    std::string context="OutRunDesktop latched shim run; devices.json carries the device table and caps";
};

// OutRunDesktop's recorder: the shared frame recorder with OutRun's channels, contract, game id and metadata, so its
// captures stay byte-compatible with the R1b ones (dbce.shim.outrun-capture@1).
class ShimRecorder {
    rec::FrameRecorder recorder_;
    static rec::FrameCaptureSettings shared(CaptureSettings c){
        rec::FrameCaptureSettings s;
        s.folder=std::move(c.folder);s.profile=std::move(c.profile);s.run_nonce=std::move(c.run_nonce);s.adapter_version=std::move(c.adapter_version);
        s.game="outrun2006-decomp-desktop";s.bundle_meta=std::move(c.contract);
        s.context=std::move(c.context);
        s.properties={{"adapterConsumedOffset","previous tick; adapter record fields are this tick"}};
        s.hz=c.hz;s.seconds=c.seconds;s.limits=c.limits;s.slots=c.slots;s.before_write=std::move(c.before_write);
        return s;
    }
public:
    explicit ShimRecorder(CaptureSettings c):recorder_(shared(std::move(c))){}
    // Input thread: the device table the members record (set after the first tick, refreshed before the stop).
    void set_devices(std::vector<dbce::shim::DeviceReport> d){recorder_.set_devices(std::move(d));}
    // Input thread, one per tick. A refused start or push is recorded; it never throws into the game loop.
    void push(const Snapshot& s){recorder_.push(s.frame,game_channels(s));}
    void finish(){recorder_.finish();}
    using Drained=rec::FrameRecorder::Drained;
    Drained drain(dbce::shim::hosting::Clock::time_point deadline){return recorder_.drain(deadline);}
    const std::string& refusal()const{return recorder_.refusal();}
    std::uint64_t pushed()const{return recorder_.pushed();}
    std::uint64_t rejected()const{return recorder_.rejected();}
    std::uint64_t last()const{return recorder_.last();}
    bool started()const{return recorder_.started();}
    const std::optional<rec::Report>& drained_report()const{return recorder_.drained_report();}
};
}
