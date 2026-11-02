#pragma once
// Extension 4 (shim_dll/dbce_shim_candidate.h): one explicitly selected capture candidate beside Core's immutable device
// table, so a consumer's in-game editor can bind a first device. The provider worker lists and opens it through the
// host's own provider (DirectInput: background + non-exclusive, no force call), never through a force front; the owner
// thread reads it once per ordinary tick, after Core. A candidate never resolves an action and never enters Core's
// frame, extension 3's snapshot, the journal or the recorder. An instance the profile binds is refused: extension 3
// already offers it. Nothing here runs without an explicit list or select request.
#include "shim.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace dbce::shim {

enum class CandidateState : std::uint8_t { None, Opening, Open, Lost, Refused };
inline const char* candidate_state_name(CandidateState s) {
    switch (s) {
    case CandidateState::None: return "none";
    case CandidateState::Opening: return "opening";
    case CandidateState::Open: return "open";
    case CandidateState::Lost: return "lost";
    case CandidateState::Refused: return "refused";
    }
    return "unknown";
}
// GUID text exactly as Core and the profile hold it: lower case and braced.
inline bool candidate_guid(const std::string& s) {
    if (s.size() != 38 || s.front() != '{' || s.back() != '}') return false;
    for (std::size_t i = 1; i < 37; ++i) {
        const char c = s[i];
        if (i == 9 || i == 14 || i == 19 || i == 24) { if (c != '-') return false; }
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

// One owner-thread read, kept for the frame it was made in.
struct CandidateRead {
    std::uint64_t frame_seq{};          // 0: no read this frame
    std::uint32_t epoch{};
    std::uint64_t device_seq{};         // increases by one per read, across epochs
    std::int64_t start_us{}, end_us{};
    Call poll{Call::NotCalled}, state{Call::NotCalled};
    bool valid{}, ended{};
    RawState physical;                  // the complete state when valid, otherwise its initial value
};

class Candidate {
public:
    struct Listing { std::uint64_t seq{}; bool pending{}; std::vector<DeviceInfo> devices; };
    struct View {
        CandidateState state{CandidateState::None};
        DeviceInfo identity;            // the selection; the identity read back from the handle once open
        std::uint32_t epoch{};          // the current or last open epoch (0: never opened)
        bool opened{};
        ObjectCaps caps;                // empty unless opened
        std::string reason;
        CandidateRead read;             // this frame's read, if any
    };

    // `bound`: the instances in Core's table. `idle_us`: an open or opening candidate that no getter observed for this
    // long is released. `pending_limit_us`: continuous pending reads for longer end the epoch, as in Core.
    explicit Candidate(std::vector<std::string> bound, std::int64_t idle_us = 2'000'000, std::int64_t pending_limit_us = 1'000'000)
        : bound_(std::move(bound)), idle_us_(idle_us), pending_limit_us_(pending_limit_us) {}

    // ---- owner thread
    bool bound(const std::string& instance) const { return std::find(bound_.begin(), bound_.end(), instance) != bound_.end(); }
    void request_list() { std::lock_guard<std::mutex> l(m_); list_requested_ = true; worker_listing_.pending = true; }
    // Replaces any candidate with this one and queues its open on the worker.
    bool select(const std::string& instance, const std::string& product, std::string& why) {
        if (!candidate_guid(instance) || !candidate_guid(product)) { why = "the instance and product must be lower-case braced GUID text"; return false; }
        if (bound(instance)) { why = "the profile already binds this device; the ordinary snapshot offers it"; return false; }
        end(CandidateState::None, "replaced");
        ++gen_;
        view_ = View{}; view_.state = CandidateState::Opening; view_.identity.dev = instance; view_.identity.prod = product;
        view_.epoch = epochs_; view_.reason = "opening";
        observed_us_ = Unobserved;
        std::lock_guard<std::mutex> l(m_);
        open_gen_ = gen_; open_target_ = view_.identity;
        return true;
    }
    void release(const std::string& why) {
        if (view_.state == CandidateState::None) return;
        end(CandidateState::None, why);
        ++gen_;
        std::lock_guard<std::mutex> l(m_);
        open_gen_ = 0;
    }
    // After Core's tick on the same thread: adopt the worker's results, release an unobserved candidate, read an open one.
    void tick(Provider& provider, std::uint64_t frame_seq, std::int64_t tick_us) {
        std::vector<Opened> results;
        {
            std::lock_guard<std::mutex> l(m_);
            results.swap(results_);
            if (worker_listing_.seq != listing_.seq || worker_listing_.pending != listing_.pending) listing_ = worker_listing_;
        }
        tick_seq_ = frame_seq; tick_us_ = tick_us;
        view_.read = CandidateRead{};
        for (auto& r : results) {
            if (r.gen != gen_ || view_.state != CandidateState::Opening) { if (r.ok) close_later(r.handle); continue; }
            if (!r.ok) { view_.state = CandidateState::Refused; view_.reason = r.why; continue; }
            handle_ = r.handle; view_.epoch = ++epochs_; view_.opened = true; view_.state = CandidateState::Open;
            view_.identity = r.opened; view_.caps = r.caps; view_.reason.clear(); pending_ = false;
        }
        const bool live = view_.state == CandidateState::Opening || view_.state == CandidateState::Open;
        if (!live) return;
        if (observed_us_ == Unobserved || tick_us < observed_us_) observed_us_ = tick_us;   // a clock that went back restarts the bound
        if (idle_us_ > 0 && tick_us - observed_us_ > idle_us_) { release("released: no capture getter observed it"); return; }
        if (view_.state != CandidateState::Open) return;
        CandidateRead r;
        r.frame_seq = frame_seq; r.epoch = epochs_; r.start_us = provider.now_us();
        r.poll = provider.poll(handle_);
        r.state = r.poll == Call::Ok ? provider.state(handle_, r.physical) : Call::NotCalled;
        r.end_us = provider.now_us(); r.device_seq = ++reads_;
        r.valid = r.poll == Call::Ok && r.state == Call::Ok;
        if (!r.valid) r.physical = RawState{};
        if (r.valid) pending_ = false;
        else if (r.poll == Call::Pending || r.state == Call::Pending) {
            if (!pending_) { pending_ = true; pending_since_ = r.end_us; }
            else if (r.end_us - pending_since_ > pending_limit_us_) r.ended = true;
        } else r.ended = true;   // lost or gone: a candidate is not reacquired; the user selects it again
        view_.read = r;
        if (r.ended) end(CandidateState::Lost, std::string("the read ended (") + call_text(r.poll == Call::Ok ? r.state : r.poll) +
                         "); select the device again");
    }
    // A getter that copied the current frame renews the explicit lifetime. No clock is read here.
    void observed(std::uint64_t frame_seq) { if (frame_seq && frame_seq == tick_seq_) observed_us_ = tick_us_; }
    std::uint64_t tick_seq() const { return tick_seq_; }
    std::int64_t tick_us() const { return tick_us_; }
    const Listing& listing() const { return listing_; }
    const View& view() const { return view_; }

    // ---- provider worker: one bounded pass. True when it called the provider.
    bool service(Provider& provider) {
        bool list = false; std::uint64_t gen = 0; DeviceInfo target; std::vector<Handle> close;
        {
            std::lock_guard<std::mutex> l(m_);
            list = list_requested_; list_requested_ = false;
            gen = open_gen_; open_gen_ = 0; target = open_target_;
            close.swap(to_close_);
        }
        for (const auto h : close) provider.close(h);
        if (!list && !gen) return !close.empty();
        const auto listed = provider.enumerate();   // outside the lock: it may be slow
        if (list) {
            std::lock_guard<std::mutex> l(m_);
            ++worker_listing_.seq; worker_listing_.devices = listed;
            worker_listing_.pending = list_requested_;   // a request made during this enumeration is answered next pass
        }
        if (gen) {
            Opened o; o.gen = gen;
            const DeviceInfo* found = nullptr;
            for (const auto& x : listed) if (x.dev == target.dev) found = &x;
            if (!found) o.why = "not attached";
            else if (found->prod != target.prod) o.why = "instance " + target.dev + " is product " + found->prod + ", not " + target.prod;
            else {
                auto r = provider.open(*found);
                if (!r.ok) o.why = "open failed: " + r.why;
                else if (r.opened.dev != found->dev || r.opened.prod != found->prod) { provider.close(r.handle); o.why = "the identity changed during the open"; }
                else {
                    for (std::size_t a = 0; a < 8; ++a) if (r.caps.max[a] <= r.caps.min[a]) r.caps.range_ok[a] = false;
                    o.ok = true; o.handle = r.handle; o.opened = r.opened; o.caps = r.caps;
                    if (o.opened.name.empty()) o.opened.name = found->name;
                }
            }
            std::lock_guard<std::mutex> l(m_);
            results_.push_back(std::move(o));
        }
        return true;
    }

private:
    static constexpr std::int64_t Unobserved = INT64_MIN;
    struct Opened { std::uint64_t gen{}; bool ok{}; Handle handle{}; DeviceInfo opened; ObjectCaps caps; std::string why; };
    static const char* call_text(Call c) {
        switch (c) {
        case Call::Ok: return "ok";
        case Call::AcquisitionLost: return "acquisition lost";
        case Call::Gone: return "gone";
        case Call::NotCalled: return "not called";
        case Call::Pending: return "pending";
        }
        return "unknown";
    }
    void close_later(Handle h) { if (!h) return; std::lock_guard<std::mutex> l(m_); to_close_.push_back(h); }
    // Hands an open handle back to the worker; the identity and epoch stay for the report.
    void end(CandidateState state, const std::string& why) {
        close_later(handle_); handle_ = 0;
        view_.opened = false; view_.caps = ObjectCaps{}; view_.state = state; view_.reason = why; pending_ = false;
    }

    const std::vector<std::string> bound_;
    const std::int64_t idle_us_, pending_limit_us_;
    // Under m_: owner -> worker requests and worker -> owner results.
    std::mutex m_;
    bool list_requested_{};
    std::uint64_t open_gen_{};
    DeviceInfo open_target_;
    std::vector<Handle> to_close_;
    std::vector<Opened> results_;
    Listing worker_listing_;
    // Owner thread only.
    std::uint64_t gen_{}, reads_{}, tick_seq_{};
    std::int64_t tick_us_{}, observed_us_{Unobserved}, pending_since_{};
    std::uint32_t epochs_{};
    Handle handle_{};
    bool pending_{};
    Listing listing_;
    View view_;
};
}
