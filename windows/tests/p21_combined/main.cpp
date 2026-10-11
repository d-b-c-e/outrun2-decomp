// Offline composition: the SAME reviewed provider host owns the production desktop recorder (ShimRecorder) in the
// desktop layout: the host reports in the run folder, the recorder creates the capture inside it.
// Reuse the prior fixture's codec, writer and identity-bound supervisor verbatim.
// Omit only its entry points; never rename identifiers in included SDK headers.
#define P21_RECORDER_INTEGRATION_NO_ENTRY_POINTS
#include "p21_recorder_integration/integration.cpp"
#undef P21_RECORDER_INTEGRATION_NO_ENTRY_POINTS
#include "p21_provider_host/host.hpp"
#include "shim_fake_com.hpp"
#include "outrun_recorder.hpp"   // the production desktop recorder (R1b): no fixture recorder

using CombinedHost = p21k::BasicProviderHost<outrun::desktop::ShimRecorder>;
static const p21k::Budget CombinedBudget{1000, 200, 200, 200};

static int combined_child(const std::string& mode, const fs::path& dir) {
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
    // discovery (Astra 16741): rows from the very first tick, the shifter attached late, then lost and reacquired
    // mid-capture, so the sample count goes 0/1 -> 2 -> 1 -> 2 inside one fixed-schema capture.
    const bool discovery = mode == "discovery";
    if (discovery) { std::lock_guard<std::mutex> lock(input->m); input->attached[si] = false; }
    // The fence's run nonce form (letters/digits); the finalizer binds the host report to it through the capture metadata.
    const std::string run_nonce = "p21combined" + std::to_string(GetCurrentProcessId());
    auto barrier = std::make_shared<Barrier>(); barrier->resume = mode != "writer-stuck";   // a blocked writer never returns
    outrun::desktop::CaptureSettings settings;
    settings.folder = dir; settings.profile = fixture_profile(); settings.run_nonce = run_nonce;
    settings.adapter_version = "p21_combined fixture"; settings.before_write = [barrier] { barrier->hold(); };
    // Ordinary physical reads over FAKE COM, no injection admission or real device.
    auto host = std::make_unique<CombinedHost>(dir.parent_path().wstring(), run_nonce, input, fixture_profile(), false,
        std::make_unique<outrun::desktop::ShimRecorder>(settings), outrun_capability());
    host->recorder().set_devices(host->device_reports());   // before the first row, as the desktop does: one slot per device
    Adapter adapter("Sequential");
    std::vector<Snapshot> original;
    auto record = [&](const Frame& frame, const GameView& view) {
        NativeInputState sink{}; adapter.apply(frame, view, sink);
        original.push_back({frame, adapter.signals(), sink, view});
        host->recorder().push(original.back());
    };
    auto both_valid = [](const Frame& f) { return f.samples.size() == 2 && f.samples[0].valid && f.samples[1].valid; };
    bool ready = false;
    for (int n = 0; n < 400 && !ready; ++n) {
        const Frame f = host->tick();
        ready = both_valid(f);
        if (discovery) record(f, GameView{});   // every tick: rows are consecutive frames
        if (discovery && n == 10) { std::lock_guard<std::mutex> lock(input->m); input->attached[si] = true; }
        if (!ready) Sleep(5);
    }
    check(ready, "both devices opened and sampled through the real provider");
    if (discovery) check(!original.empty() && original.front().frame.samples.size() < 2, "rows recorded before every device was open");
    const auto base = original.size();
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
            if (discovery && t == 9) input->scripts[si].reads.push_back({DI_OK, DIERR_INPUTLOST});   // lost mid-capture
        }
        GameView view; view.mode = t >= 11 && t <= 12 ? 3 : 16; view.car = t != 13; view.gear = 2;
        view.speed = float(t * 2); view.consumed_steer = t - 8; view.consumed_accel = t * 10;
        record(host->tick(), view);
    }
    if (discovery) {   // until the provider has reacquired the shifter, still one row per tick
        bool back = false;
        for (int n = 0; n < 400 && !back; ++n) { const Frame f = host->tick(); back = both_valid(f); record(f, GameView{}); if (!back) Sleep(5); }
        bool dropped = false;
        for (std::size_t i = base + 10; i < original.size(); ++i) dropped = dropped || original[i].frame.samples.size() < 2;
        check(back && dropped, "the lost shifter left the frame and came back inside the same capture");
    }
    check(host->recorder().pushed() == original.size() && host->recorder().refusal().empty(), "production recorder accepted every real-provider frame (" +
        std::to_string(host->recorder().pushed()) + "/" + std::to_string(original.size()) + " " + host->recorder().refusal() + ")");
    check(original[base + 4].frame.samples[1].poll == Call::Pending && original[base + 6].frame.samples[1].state == Call::Pending,
        "real provider Pending survives into the recorder's original snapshots");
    check(original[base + 5].frame.action[action_index("shiftUp")].status == Status::Rearming,
        "real provider held button rearms after Pending");
    // Independent producer-side snapshots, outside the capture seal, compare every recorded numeric field later.
    auto rows = dbce::session::jsonStream();
    for (const auto& s : original) {
        const auto c = channels(s, 2); rows << "ROW " << c.size() << '\n';   // the recorder's two device slots
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
    host->recorder().set_devices(host->device_reports());   // the desktop's final device table, before the stop
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
            check(rows.size() >= 16 && rows == producer, "recorded bytes preserve every real-provider producer channel");
            for (const auto& row : rows) check(row.size() == 382, "real provider has the complete two-device schema");
            Adapter adapter("Sequential");
            for (const auto& row : rows) {
                const auto f = decode(frame_channels(row));
                NativeInputState sink{}; adapter.apply(f, decoded_view(row), sink);
                same_signals(adapter.signals(), decoded_signals(row));
            }
            std::cout << "PASS real-provider recording and action replay: " << rows.size() << " frames, " << checks << " checks\n";
            return 0;
        }
        if (argc == 4 && std::wstring(argv[1]) == L"--child") {
            const std::wstring mode(argv[2]); return combined_child(std::string(mode.begin(), mode.end()), fs::path(argv[3]));
        }
        check(argc == 2, "usage: p21_combined <new result folder>");
        const fs::path root(argv[1]); check(fs::create_directory(root), "new combined result folder");
        wchar_t self[MAX_PATH]; check(GetModuleFileNameW(nullptr, self, MAX_PATH) > 0, "own executable known");
        for (const std::string mode : {"resolved", "discovery", "release-within", "writer-stuck", "provider-stuck", "ordinary-provider-stuck", "release-stuck"}) {
            const auto r = supervise(self, mode, root / mode, true);
            if (mode == "resolved" || mode == "discovery" || mode == "release-within") check(r.code == 0 && !r.forced && r.state == "resolved", "combined resolved host normal exit");
            else if (mode == "ordinary-provider-stuck") check(r.code == p21::ForcedExitCode && !r.forced && r.state == "unresolved", "combined ordinary unresolved forced exit");
            else check(r.forced && r.state == "unresolved", "combined unresolved host stays parked for its supervisor");
        }
        std::cout << "PASS " << checks << " combined real-provider/recorder checks; sealed readback follows\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
