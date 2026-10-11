// Offline composition: the SAME reviewed provider host owns the real recorder.
// Reuse the prior fixture's codec, writer and identity-bound supervisor verbatim.
// Its fake-provider child is compiled for regression tooling but never selected here.
#define child p21j_fixture_child
#define wmain p21j_fixture_wmain
#include "p21_recorder_integration/integration.cpp"
#undef wmain
#undef child
#include "p21_provider_host/host.hpp"
#include "shim_fake_com.hpp"

class ActualRecorder {
    fs::path dir_;
    std::shared_ptr<Identity> id_;
    std::vector<std::string> names_;
    std::unique_ptr<rs::Controller> controller_;
    std::optional<rs::Producer> producer_;
    std::uint64_t first_{}, last_{};
    std::int64_t origin_{};
public:
    ActualRecorder(fs::path dir, std::shared_ptr<Identity> id, bool stuck) : dir_(std::move(dir)), id_(std::move(id)) {
        id_->resume = !stuck;
    }
    void push(const Snapshot& s) {
        if (!controller_) {
            names_ = schema(channels(s));
            check(names_.size() == 382, "real provider has the complete two-device schema");
            preflight(names_, 62.5, .24);
            first_ = s.frame.seq; origin_ = s.frame.tick_us;
            controller_ = std::make_unique<rs::Controller>(16, std::uint32_t(names_.size()), 16 * sizeof(rs::Packet),
                std::make_unique<QueuedSink>(dir_ / "source.jsonl", names_, id_, origin_), first_);
            producer_.emplace(controller_->producer());
        }
        check(producer_->push(packet(s, names_)), "real-provider frame accepted by actual recorder");
        last_ = s.frame.seq;
    }
    void finish() { if (producer_) producer_->finish(last_); }
    p21::Recorder::Drained drain(p21::Clock::time_point deadline) {
        if (!controller_) return {p21::RecOutcome::Cancelled, true};
        const auto result = controller_->drain(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - p21::Clock::now()));
        const bool joined = controller_->join_until(deadline);
        const bool eligible = result.may_seal() && result.capture_counts_final && !result.first_rejected && result.wrong_producer == 0;
        if (eligible && joined) {
            report(dir_, result, *id_, first_, origin_);
            write_new(dir_ / "profile.ini", fixture_profile());
            write_new(dir_ / "bundle.meta", Contract);
            auto b = dbce::session::jsonStream(); b << names_.size() << '\t' << row_bound(names_) << '\t' << sizeof(rs::Packet) * 16 << '\n';
            write_new(dir_ / "budget.tsv", b.str());
        }
        return {eligible ? p21::RecOutcome::Complete : result.outcome == rs::Outcome::TimedOut ? p21::RecOutcome::TimedOut : p21::RecOutcome::Cancelled, joined};
    }
};

using CombinedHost = p21k::BasicProviderHost<ActualRecorder>;
static const p21k::Budget CombinedBudget{1000, 200, 200, 200};

static int combined_child(const std::string& mode, const fs::path& dir) {
    check(fs::create_directory(dir), "new combined capture");
    auto* input = new fakecom::Input; // owns fake storage until process exit, including unresolved driver calls
    fakecom::DeviceScript wheel, shifter;
    check(guid_parse(W, wheel.instance) && guid_parse(WP, wheel.product) &&
          guid_parse(S, shifter.instance) && guid_parse(SP, shifter.product), "fixture identities parsed");
    wheel.name = L"MOZA R12 Base"; wheel.buttons = 128; wheel.state.lX = 32768;
    shifter.name = L"DS-8X Shifter"; shifter.buttons = 128;
    wheel.acquire_armed = std::make_shared<std::atomic<bool>>(false);
    wheel.acquire_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wheel.acquire_release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wheel.acquire_delay_ms = -1;
    wheel.unacquire_armed = std::make_shared<std::atomic<bool>>(false);
    wheel.unacquire_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wheel.unacquire_release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wheel.unacquire_delay_ms = mode == "release-within" ? 100 : -1;
    check(wheel.acquire_entered && wheel.acquire_release && wheel.unacquire_entered && wheel.unacquire_release, "driver barriers created");
    const auto wi = input->add(wheel), si = input->add(shifter);
    auto id = std::make_shared<Identity>();
    auto recorder = std::make_unique<ActualRecorder>(dir, id, mode == "writer-stuck");
    // Ordinary physical reads over FAKE COM, no injection admission or real device.
    auto host = std::make_unique<CombinedHost>(dir.wstring(), id->nonce, input, fixture_profile(), false,
        std::move(recorder), outrun_capability());
    bool ready = false;
    for (int n = 0; n < 400 && !ready; ++n) {
        const auto& f = host->tick();
        ready = f.samples.size() == 2 && f.samples[0].valid && f.samples[1].valid;
        if (!ready) Sleep(5);
    }
    check(ready, "both devices opened and sampled through the real provider");
    Adapter adapter("Sequential");
    std::vector<Snapshot> original;
    for (int t = 0; t < 16; ++t) {
        {
            std::lock_guard<std::mutex> lock(input->m);
            auto& w = input->scripts[wi].state; auto& s = input->scripts[si].state;
            w.lX = t < 2 ? 32768 : t < 8 ? 65535 : 0;
            w.lZ = t == 0 ? 0 : t < 7 ? 32768 : 0; w.lRz = t == 5 ? 65535 : 0;
            w.rgbButtons[5] = (t == 3 || t == 4) ? 0x80 : 0;
            w.rgbButtons[7] = (t == 11 || t == 12) ? 0x80 : 0;
            w.rgbButtons[127] = std::uint8_t(t + 1);
            s.rgbButtons[11] = (t == 2 || t == 3 || t == 5) ? 0x80 : 0;
            if (t == 4) input->scripts[si].reads.push_back({E_PENDING, DI_OK});
            if (t == 6) input->scripts[si].reads.push_back({DI_OK, E_PENDING});
        }
        GameView view; view.mode = t >= 11 && t <= 12 ? 3 : 16; view.car = t != 13; view.gear = 2;
        view.speed = float(t * 2); view.consumed_steer = t - 8; view.consumed_accel = t * 10;
        NativeInputState sink{}; const Frame frame = host->tick();
        adapter.apply(frame, view, sink);
        original.push_back({frame, adapter.signals(), sink, view});
        host->recorder().push(original.back());
    }
    check(original[4].frame.samples[1].poll == Call::Pending && original[6].frame.samples[1].state == Call::Pending,
        "real provider Pending survives into the recorder's original snapshots");
    check(original[5].frame.action[action_index("shiftUp")].status == Status::Rearming,
        "real provider held button rearms after Pending");
    // Independent producer-side snapshots, outside the capture seal, compare every recorded numeric field later.
    auto rows = dbce::session::jsonStream();
    for (const auto& s : original) {
        const auto c = channels(s); rows << "ROW " << c.size() << '\n';
        for (const auto& v : c) rows << v.first << ' ' << v.second << '\n';
    }
    write_new(dir.parent_path() / "producer.tsv", rows.str());
    if (mode.find("provider-stuck") != std::string::npos) {
        { std::lock_guard<std::mutex> lock(input->m); input->scripts[wi].reads.push_back({DI_OK, DIERR_INPUTLOST}); }
        wheel.acquire_armed->store(true);
        for (int n = 0; n < 400 && WaitForSingleObject(wheel.acquire_entered, 0) != WAIT_OBJECT_0; ++n) { host->tick(); Sleep(5); }
        check(WaitForSingleObject(wheel.acquire_entered, 0) == WAIT_OBJECT_0, "real provider is inside blocked reacquisition");
    }
    if (mode == "release-stuck" || mode == "release-within") wheel.unacquire_armed->store(true);
    const auto r = host->stop(CombinedBudget);
    check(r.elapsed_us < 2'000'000, "combined shutdown returns inside phase budgets and report allowance");
    if (mode == "release-stuck" || mode == "release-within")
        check(WaitForSingleObject(wheel.unacquire_entered, 0) == WAIT_OBJECT_0, "real provider shutdown reached Unacquire");
    const auto& c = input->counters;
    auto counters = dbce::session::jsonStream();
    counters << "{\"creates\":" << c.creates.load() << ",\"polls\":" << c.polls.load() << ",\"reads\":" << c.state_reads.load()
        << ",\"forceCalls\":" << c.force_calls.load() << ",\"releaseDuringCall\":" << c.release_during_read.load()
        << ",\"afterRelease\":" << c.after_release.load() << ",\"inputRefs\":" << input->refs.load() << "}";
    write_new(dir.parent_path() / "provider-counters.json", counters.str());
    check(c.force_calls == 0 && c.release_during_read == 0 && c.after_release == 0, "no force or use after provider release");
    if (!r.resolved) {
        if (mode.find("ordinary") == std::string::npos) host->park_for_supervisor();
        host->forced_exit();
    }
    host.reset();
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring(argv[1]) == L"--replay") {
            const fs::path dir(argv[2]);
            const auto rows = read_export(dir.parent_path() / "validated.tsv");
            const auto producer = read_export(dir.parent_path() / "producer.tsv");
            check(rows.size() == 16 && rows == producer, "recorded bytes preserve every real-provider producer channel");
            Adapter adapter("Sequential");
            for (const auto& row : rows) {
                const auto f = decode(frame_channels(row));
                NativeInputState sink{}; adapter.apply(f, decoded_view(row), sink);
                same_signals(adapter.signals(), decoded_signals(row));
            }
            std::cout << "PASS real-provider recording and action replay: 16 frames, " << checks << " checks\n";
            return 0;
        }
        if (argc == 4 && std::wstring(argv[1]) == L"--child") {
            const std::wstring mode(argv[2]); return combined_child(std::string(mode.begin(), mode.end()), fs::path(argv[3]));
        }
        check(argc == 2, "usage: p21_combined <new result folder>");
        const fs::path root(argv[1]); check(fs::create_directory(root), "new combined result folder");
        wchar_t self[MAX_PATH]; check(GetModuleFileNameW(nullptr, self, MAX_PATH) > 0, "own executable known");
        for (const std::string mode : {"resolved", "release-within", "writer-stuck", "provider-stuck", "ordinary-provider-stuck", "release-stuck"}) {
            const auto r = supervise(self, mode, root / mode);
            if (mode == "resolved" || mode == "release-within") check(r.code == 0 && !r.forced && r.state == "resolved", "combined resolved host normal exit");
            else if (mode == "ordinary-provider-stuck") check(r.code == p21::ForcedExitCode && !r.forced && r.state == "unresolved", "combined ordinary unresolved forced exit");
            else check(r.forced && r.state == "unresolved", "combined unresolved host stays parked for its supervisor");
        }
        std::cout << "PASS " << checks << " combined real-provider/recorder checks; sealed readback follows\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
