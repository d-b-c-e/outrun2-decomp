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
#include "control_journal.hpp"
#include "host_lifecycle.hpp"
#include "outrun_capture.hpp"
#include <optional>
namespace outrun::desktop {

constexpr const char* CaptureContract="dbce.shim.outrun-capture@1\n";
constexpr const char* CaptureInputContract="shim-frame-audit-2-pending-unadopted";   // the frame codec's contract

struct CaptureSettings {
    std::filesystem::path folder;              // created new by the first push (inside the fenced run folder)
    std::string profile;                       // the exact [Controls] bytes Core parsed
    std::string run_nonce,adapter_version;
    double hz=60,seconds=300;                  // the worst-case preflight: 60 Hz for 300 s
    dbce::session::Limits limits{512ull*1024*1024,256*1024,300};
    std::size_t slots=510;                     // 510 fixed slots of 8216 bytes stay under 4 MiB
    std::function<void()> before_write;        // test seam (QueuedSink's): empty in the desktop
};

class ShimRecorder {
    CaptureSettings cfg_;
    std::vector<dbce::shim::DeviceReport> devices_;
    std::shared_ptr<rec::Identity> id_;
    std::unique_ptr<rec::Controller> controller_;
    std::optional<rec::Producer> producer_;
    std::vector<std::string> names_;
    std::uint64_t first_{},last_{};std::int64_t origin_{};std::size_t slots_{};
    bool started_{},refused_{},finished_{};
    std::uint64_t pushed_{},rejected_{};
    std::string refusal_;

    static std::string bits(const std::bitset<128>& b){std::string s;for(int i=127;i>=0;--i)s+=b[std::size_t(i)]?'1':'0';return s;}
    std::string devices_json()const{
        using dbce::shim::control::journal_detail::json_string;
        std::string j="[";
        for(std::size_t d=0;d<devices_.size();++d){
            const auto& r=devices_[d];const auto& c=r.caps;
            j+=std::string(d?",":"")+"\n {\"index\":"+std::to_string(d)+",\"instance\":";json_string(j,r.identity.dev,128);
            j+=",\"product\":";json_string(j,r.identity.prod,128);j+=",\"name\":";
            if(!json_string(j,r.identity.name,1560))j+="null";
            j+=",\"epoch\":"+std::to_string(r.epoch)+",\"opened\":"+(r.opened?"true":"false")+",\"axes\":[";
            for(int a=0;a<8;++a)j+=std::string(a?",":"")+"{\"present\":"+(c.axis[std::size_t(a)]?"true":"false")+",\"rangeOk\":"+(c.range_ok[std::size_t(a)]?"true":"false")+
                ",\"min\":"+std::to_string(c.min[std::size_t(a)])+",\"max\":"+std::to_string(c.max[std::size_t(a)])+"}";
            j+="],\"buttons\":\""+bits(c.button)+"\",\"povs\":[";
            for(int p=0;p<4;++p)j+=std::string(p?",":"")+(c.pov[std::size_t(p)]?"true":"false");
            j+="]}";
        }
        return j+"\n]\n";
    }
    void members(const rec::Report& r){
        rec::report(cfg_.folder,r,*id_,first_,origin_);
        rec::write_new(cfg_.folder/"profile.ini",cfg_.profile);
        rec::write_new(cfg_.folder/"bundle.meta",CaptureContract);
        auto b=dbce::session::jsonStream();b<<names_.size()<<'\t'<<rec::row_bound(names_)<<'\t'<<cfg_.slots*sizeof(rec::Packet)<<'\n';
        rec::write_new(cfg_.folder/"budget.tsv",b.str());
        rec::write_new(cfg_.folder/"devices.json",devices_json());
    }
public:
    explicit ShimRecorder(CaptureSettings c):cfg_(std::move(c)){}
    // Input thread: the device table the members record (set after the first tick, refreshed before the stop).
    void set_devices(std::vector<dbce::shim::DeviceReport> d){devices_=std::move(d);}
    // Input thread, one per tick. A refused start or push is recorded; it never throws into the game loop.
    void push(const Snapshot& s){
        if(refused_||finished_)return;
        try{
            if(!started_){
                started_=true;
                if(devices_.empty())throw std::runtime_error("device table required before the first capture row");
                slots_=devices_.size();
                names_=rec::schema(channels(s,slots_));
                rec::preflight(names_,cfg_.hz,cfg_.seconds,cfg_.limits);
                if(!CreateDirectoryW(cfg_.folder.c_str(),nullptr))throw std::runtime_error("capture folder exists or cannot be created");
                first_=s.frame.seq;origin_=s.frame.tick_us;
                id_=std::make_shared<rec::Identity>();   // this (input) thread is the producer; the writer refuses to run here
                dbce::session::Metadata m{"outrun2006-decomp-desktop",cfg_.adapter_version,"native-session-1",{},{}};
                m.properties={{"inputContract",CaptureInputContract},{"profileSha256",dbce::shim::hex(dbce::shim::sha256(cfg_.profile))},
                    {"runNonce",cfg_.run_nonce},{"context","OutRunDesktop latched shim run; devices.json carries the device table and caps"},
                    {"adapterConsumedOffset","previous tick; adapter record fields are this tick"}};
                controller_=std::make_unique<rec::Controller>(cfg_.slots,std::uint32_t(names_.size()),cfg_.slots*sizeof(rec::Packet),
                    std::make_unique<rec::QueuedSink>(cfg_.folder/"source.jsonl",names_,id_,origin_,std::move(m),cfg_.limits,cfg_.before_write),first_);
                producer_.emplace(controller_->producer());
            }
            if(producer_->push(packet(s,names_,slots_))){last_=s.frame.seq;++pushed_;}else ++rejected_;
        }catch(const std::exception& e){refused_=true;refusal_=e.what();}
    }
    void finish(){
        if(finished_)return;
        finished_=true;
        if(producer_&&pushed_)producer_->finish(last_);
        else if(controller_)controller_->cancel();   // nothing captured: never a qualifying empty capture
    }
    struct Drained{dbce::shim::hosting::RecOutcome outcome;bool joined;};
    Drained drain(dbce::shim::hosting::Clock::time_point deadline){
        using O=dbce::shim::hosting::RecOutcome;
        if(!controller_)return {O::Cancelled,true};   // never started (or refused before the worker): no capture exists
        const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-dbce::shim::hosting::Clock::now());
        const auto r=controller_->drain(left.count()>0?left:std::chrono::milliseconds(0));
        const bool joined=controller_->join_until(deadline);
        const bool eligible=r.may_seal()&&r.capture_counts_final&&!r.first_rejected&&r.wrong_producer==0&&!refused_&&!rejected_;
        if(eligible&&joined){
            try{members(r);return {O::Complete,true};}catch(const std::exception& e){refusal_=e.what();return {O::Cancelled,true};}
        }
        return {r.drain_timed_out?O::TimedOut:O::Cancelled,joined};
    }
    const std::string& refusal()const{return refusal_;}
    std::uint64_t pushed()const{return pushed_;}
};
}
