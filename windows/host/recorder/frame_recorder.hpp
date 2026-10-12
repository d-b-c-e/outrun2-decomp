#pragma once
// The shared frame recorder: ProviderHost's recorder for any game (Astra 16553/18002), moved out of OutRunDesktop's
// ShimRecorder (which is now an adapter over it, byte-compatible with its captures).
// - Lazy, as the host requires: nothing starts before the first push. The first push fixes the channel schema (the Core
//   frame with one sample slot per profile device, plus the game's own channels), runs the worst-case preflight,
//   creates the capture folder new and starts the reviewed P20 worker.
// - One push per Core frame, on the input thread (the producer); the worker alone runs the session writer. Rows must be
//   consecutive frames: the worker refuses a gap, and the capture is then not eligible.
// - The host's stop: finish (the producer declares its tail), then a bounded drain and the worker thread's own join.
//   Only an eligible, joined result writes the members (worker report, exact profile bytes, contract, budget, devices);
//   anything else reports a non-Complete outcome, so the host stays unresolved rather than claiming a capture.
// The finalizer (out of process) seals it only with the host's resolved report and an observed normal exit (P21).
#include "capture.hpp"
#include "control_journal.hpp"
#include "host_lifecycle.hpp"
#include <optional>
#include <utility>
namespace dbce::shim::recorder {

constexpr const char* FrameInputContract = "shim-frame-audit-2-pending-unadopted";   // the frame codec's contract
// A game's capture under the generic contract: bundle.meta is this line, then "game=<id>\n" (bound to the metadata).
constexpr const char* GenericContract = "dbce.shim.frame-capture@1\n";
inline std::string generic_bundle_meta(const std::string& game) { return std::string(GenericContract) + "game=" + game + "\n"; }

struct FrameCaptureSettings {
    std::filesystem::path folder;              // created new by the first push (inside the run folder)
    std::string profile;                       // the exact [Controls] bytes Core parsed
    std::string run_nonce, adapter_version;
    std::string game;                          // the session metadata's game id
    std::string bundle_meta;                   // bundle.meta's exact bytes (the capture contract)
    std::string context;
    std::vector<std::pair<std::string, std::string>> properties;   // further metadata properties
    double hz = 60, seconds = 300;             // the worst-case preflight: 60 Hz for 300 s
    dbce::session::Limits limits{512ull * 1024 * 1024, 256 * 1024, 300};
    std::size_t slots = 510;                   // 510 fixed slots of 8216 bytes stay under 4 MiB
    std::function<void()> before_write;        // test seam (QueuedSink's): empty in a game
};

class FrameRecorder {
    FrameCaptureSettings cfg_;
    std::vector<DeviceReport> devices_;
    std::shared_ptr<Identity> id_;
    std::unique_ptr<Controller> controller_;
    std::optional<Producer> producer_;
    std::vector<std::string> names_;
    std::uint64_t first_{}, last_{}; std::int64_t origin_{}; std::size_t slots_{};
    bool started_{}, refused_{}, finished_{};
    std::uint64_t pushed_{}, rejected_{};
    std::string refusal_;
    std::optional<Report> drained_;   // the worker report the drain saw (an ordinary recording's completion proof)

    static std::string bits(const std::bitset<128>& b) { std::string s; for (int i = 127; i >= 0; --i) s += b[std::size_t(i)] ? '1' : '0'; return s; }
    std::string devices_json() const {
        using dbce::shim::control::journal_detail::json_string;
        std::string j = "[";
        for (std::size_t d = 0; d < devices_.size(); ++d) {
            const auto& r = devices_[d]; const auto& c = r.caps;
            j += std::string(d ? "," : "") + "\n {\"index\":" + std::to_string(d) + ",\"instance\":"; json_string(j, r.identity.dev, 128);
            j += ",\"product\":"; json_string(j, r.identity.prod, 128); j += ",\"name\":";
            if (!json_string(j, r.identity.name, 1560)) j += "null";
            j += ",\"epoch\":" + std::to_string(r.epoch) + ",\"opened\":" + (r.opened ? "true" : "false") + ",\"axes\":[";
            for (int a = 0; a < 8; ++a) j += std::string(a ? "," : "") + "{\"present\":" + (c.axis[std::size_t(a)] ? "true" : "false") + ",\"rangeOk\":" + (c.range_ok[std::size_t(a)] ? "true" : "false") +
                ",\"min\":" + std::to_string(c.min[std::size_t(a)]) + ",\"max\":" + std::to_string(c.max[std::size_t(a)]) + "}";
            j += "],\"buttons\":\"" + bits(c.button) + "\",\"povs\":[";
            for (int p = 0; p < 4; ++p) j += std::string(p ? "," : "") + (c.pov[std::size_t(p)] ? "true" : "false");
            j += "]}";
        }
        return j + "\n]\n";
    }
    void members(const Report& r) {
        report(cfg_.folder, r, *id_, first_, origin_);
        write_new(cfg_.folder / "profile.ini", cfg_.profile);
        write_new(cfg_.folder / "bundle.meta", cfg_.bundle_meta);
        auto b = dbce::session::jsonStream(); b << names_.size() << '\t' << row_bound(names_) << '\t' << cfg_.slots * sizeof(Packet) << '\n';
        write_new(cfg_.folder / "budget.tsv", b.str());
        write_new(cfg_.folder / "devices.json", devices_json());
    }
    Channels row(const Frame& f, const Channels& game) const {
        auto c = encode(f, slots_);
        const auto frame_channels = c.size();
        c.insert(game.begin(), game.end());
        if (c.size() != frame_channels + game.size()) throw std::invalid_argument("a game channel name collides with a frame channel");
        return c;
    }
public:
    explicit FrameRecorder(FrameCaptureSettings c) : cfg_(std::move(c)) {}
    // Input thread: the device table the members record (set after the first tick, refreshed before the stop).
    void set_devices(std::vector<DeviceReport> d) { devices_ = std::move(d); }
    // Input thread, one per Core frame. A refused start or push is recorded; it never throws into the game loop.
    void push(const Frame& f, const Channels& game) {
        if (refused_ || finished_) return;
        try {
            if (!started_) {
                started_ = true;
                if (devices_.empty()) throw std::runtime_error("device table required before the first capture row");
                if (cfg_.game.empty() || cfg_.bundle_meta.empty()) throw std::runtime_error("capture game and contract required");
                slots_ = devices_.size();
                names_ = schema(row(f, game));
                preflight(names_, cfg_.hz, cfg_.seconds, cfg_.limits);
                if (!CreateDirectoryW(cfg_.folder.c_str(), nullptr)) throw std::runtime_error("capture folder exists or cannot be created");
                first_ = f.seq; origin_ = f.tick_us;
                id_ = std::make_shared<Identity>();   // this (input) thread is the producer; the writer refuses to run here
                dbce::session::Metadata m{cfg_.game, cfg_.adapter_version, "native-session-1", {}, {}};
                m.properties = {{"inputContract", FrameInputContract}, {"profileSha256", dbce::shim::hex(dbce::shim::sha256(cfg_.profile))},
                                {"runNonce", cfg_.run_nonce}, {"context", cfg_.context}};
                for (const auto& p : cfg_.properties) m.properties[p.first] = p.second;
                controller_ = std::make_unique<Controller>(cfg_.slots, std::uint32_t(names_.size()), cfg_.slots * sizeof(Packet),
                    std::make_unique<QueuedSink>(cfg_.folder / "source.jsonl", names_, id_, origin_, std::move(m), cfg_.limits, cfg_.before_write), first_);
                producer_.emplace(controller_->producer());
            }
            if (producer_->push(packet_of(row(f, game), names_, f.seq, f.tick_us))) { last_ = f.seq; ++pushed_; } else ++rejected_;
        } catch (const std::exception& e) { refused_ = true; refusal_ = e.what(); }
    }
    void finish() {
        if (finished_) return;
        finished_ = true;
        if (producer_ && pushed_) producer_->finish(last_);
        else if (controller_) controller_->cancel();   // nothing captured: never a qualifying empty capture
    }
    struct Drained { dbce::shim::hosting::RecOutcome outcome; bool joined; };
    Drained drain(dbce::shim::hosting::Clock::time_point deadline) {
        using O = dbce::shim::hosting::RecOutcome;
        if (!controller_) return {O::Cancelled, true};   // never started (or refused before the worker): no capture exists
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - dbce::shim::hosting::Clock::now());
        const auto r = controller_->drain(left.count() > 0 ? left : std::chrono::milliseconds(0));
        drained_ = r;
        const bool joined = controller_->join_until(deadline);
        const bool eligible = r.may_seal() && r.capture_counts_final && !r.first_rejected && r.wrong_producer == 0 && !refused_ && !rejected_;
        if (eligible && joined) {
            try { members(r); return {O::Complete, true}; } catch (const std::exception& e) { refusal_ = e.what(); return {O::Cancelled, true}; }
        }
        return {r.drain_timed_out ? O::TimedOut : O::Cancelled, joined};
    }
    const std::string& refusal() const { return refusal_; }
    std::uint64_t pushed() const { return pushed_; }
    std::uint64_t rejected() const { return rejected_; }   // pushes the producer refused (a full queue, ...)
    std::uint64_t last() const { return last_; }           // the last frame pushed
    const std::optional<Report>& drained_report() const { return drained_; }
    bool started() const { return started_; }
};
}
