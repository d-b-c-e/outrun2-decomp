// P21 host + actual P20 recorder integration proof (windows/docs/P21-SHUTDOWN.md r2.1). The host's stop() drives the
// reviewed worker (recorder_lifecycle_spike/worker.hpp) and the real native session writer with actual Core/OutRun
// adapter frames from fake devices, composed exactly as the reviewed queued-frames fixture does (its native.cpp is
// compiled here unchanged, main renamed). Each mode runs as a child process ended or observed by the parent, which acts
// as the identity-bound supervisor. No device, game, installation or launch.
//   p21_recorder_integration <new result folder>                 parent: runs every mode, writes exit.json per capture
//   p21_recorder_integration --child <mode> <capture folder>     one host lifetime
#include "queued_frames_spike/frame_codec.hpp"   // renames its own included main; #pragma once
#define main queued_frames_main
#include "queued_frames_spike/native.cpp"
#undef main
#include "p21_host_spike/host.hpp"
#include <optional>

namespace p21 = p21_spike;
static const p21::Budget Budget{1000, 200, 200};   // the production budgets (P21-SHUTDOWN.md D3)

static unsigned long long creation_time(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return 0;
    return (static_cast<unsigned long long>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}

// The host for one capture: the input (producer) thread owns stop(); every phase is bounded and attempted.
class IntegratedHost {
    fs::path dir_;
    std::shared_ptr<rs::Identity> id_;
    std::shared_ptr<Barrier> barrier_ = std::make_shared<Barrier>();
    std::vector<std::string> names_;
    std::uint64_t first_{}, last_{};
    std::int64_t origin_{};
    std::unique_ptr<rs::Controller> controller_;
    std::optional<rs::Producer> producer_;
    p21::Worker provider_, control_;
    p21::FakeLeases leases_;
    HANDLE report_ = INVALID_HANDLE_VALUE;
    std::thread::id input_ = std::this_thread::get_id();
    bool stopped_ = false; p21::Report cache_;
    void write_report(const char* state, const p21::Report* r, rs::Outcome outcome) {
        if (report_ == INVALID_HANDLE_VALUE) return;
        std::string t = std::string("{\"state\":\"") + state + "\",\"nonce\":\"" + id_->nonce + "\",\"pid\":" + std::to_string(GetCurrentProcessId()) +
            ",\"creationTime\":" + std::to_string(creation_time(GetCurrentProcess()));
        if (r) {
            t += ",\"recorderOutcome\":" + std::to_string(int(outcome)) + ",\"recorderJoined\":" + (r->recorder_joined ? "true" : "false") +
                 ",\"elapsedUs\":" + std::to_string(r->elapsed_us) + ",\"unresolved\":[";
            for (size_t i = 0; i < r->unresolved.size(); ++i) t += (i ? ",\"" : "\"") + r->unresolved[i] + "\"";
            t += "]";
        }
        t += "}";
        SetFilePointer(report_, 0, nullptr, FILE_BEGIN); DWORD n = 0;
        WriteFile(report_, t.data(), DWORD(t.size()), &n, nullptr); SetEndOfFile(report_);
    }
public:
    IntegratedHost(fs::path dir, bool writer_blocks, std::shared_ptr<p21::Gate> provider_gate)
        : dir_(std::move(dir)), provider_("provider", provider_gate), control_("control", std::make_shared<p21::Gate>()) {
        leases_.in_use = provider_gate;   // a device in flight inside the provider's blocked call
        check(fs::create_directory(dir_), "new capture directory");
        const auto expected = capture(); names_ = schema(channels(expected[0])); preflight(names_, 62.5, .24);   // reviewed bounds, unchanged
        first_ = expected[0].frame.seq; origin_ = expected[0].frame.tick_us;
        id_ = std::make_shared<rs::Identity>();         // this thread is the producer; the writer refuses to run here
        barrier_->resume = !writer_blocks;              // a blocked writer never returns from its first write
        controller_ = std::make_unique<rs::Controller>(16, std::uint32_t(names_.size()), 16 * sizeof(rs::Packet),
            std::make_unique<rs::QueuedSink>(dir_ / "source.jsonl", names_, id_, origin_, fixture_metadata(), dbce::session::Limits{},
                [b = barrier_] { b->hold(); }), first_);
        producer_.emplace(controller_->producer());
        report_ = CreateFileW((dir_ / "host-shutdown.json").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(report_ != INVALID_HANDLE_VALUE, "host report opened at start");
    }
    IntegratedHost(const IntegratedHost&) = delete; IntegratedHost& operator=(const IntegratedHost&) = delete;
    // The game loop: actual Core/Adapter frames, copied into the owned ring on the input thread.
    void run_game() {
        capture([&](const Snapshot& s) { check(producer_->push(packet(s, names_)), "actual frame queued"); last_ = s.frame.seq; });
    }
    p21::Report stop() {
        if (stopped_) return cache_;
        stopped_ = true;
        const auto t0 = p21::Clock::now();
        p21::Report r;
        write_report("stopping", nullptr, rs::Outcome::Open);
        if (std::this_thread::get_id() != input_) r.unresolved.push_back("stop off the input thread");
        // The seal binds this report to the supervisor's observation by pid and creation time; without them it cannot.
        if (GetCurrentProcessId() == 0 || creation_time(GetCurrentProcess()) == 0) r.unresolved.push_back("host identity unavailable");
        // Phase 4: the producer (this thread) ends the tape it produced, then a bounded drain and the thread's own join.
        producer_->finish(last_);
        const auto deadline = p21::Clock::now() + std::chrono::milliseconds(Budget.recorder_ms);
        const auto drained = controller_->drain(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - p21::Clock::now()));
        r.recorder_joined = controller_->join_until(deadline);
        const bool eligible = drained.may_seal() && drained.capture_counts_final && !drained.first_rejected && drained.wrong_producer == 0;
        if (!eligible) r.unresolved.push_back("recorder: outcome " + std::to_string(int(drained.outcome)));
        if (!r.recorder_joined) r.unresolved.push_back("recorder: worker not joined");
        if (eligible && r.recorder_joined) {
            // Only from the joined worker's final, quiescent report: the reviewed member writers, unchanged.
            report(dir_, drained, *id_, first_, origin_);
            write_new(dir_ / "profile.ini", fixture_profile()); write_new(dir_ / "bundle.meta", Contract);
            auto b = dbce::session::jsonStream(); b << names_.size() << '\t' << row_bound(names_) << '\t' << sizeof(rs::Packet) * 16 << '\n';
            write_new(dir_ / "budget.tsv", b.str());
        }
        leases_.shutdown(p21::Clock::now() + std::chrono::milliseconds(Budget.leases_ms));   // phase 5
        if (leases_.leaked) r.unresolved.push_back("lease leaked");
        provider_.request_stop(); control_.request_stop();                                  // phase 6
        const auto workers = p21::Clock::now() + std::chrono::milliseconds(Budget.workers_ms);
        for (p21::Worker* w : {&provider_, &control_}) if (!w->join_until(workers)) r.unresolved.push_back("worker " + w->name());
        r.resolved = r.unresolved.empty();
        r.elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(p21::Clock::now() - t0).count();
        write_report(r.resolved ? "resolved" : "unresolved", &r, drained.outcome);       // phase 8
        cache_ = r; return r;
    }
    [[noreturn]] void park_for_supervisor() { HANDLE never = CreateEventW(nullptr, TRUE, FALSE, nullptr); for (;;) WaitForSingleObject(never, INFINITE); }
    [[noreturn]] void forced_exit() { TerminateProcess(GetCurrentProcess(), p21::ForcedExitCode); for (;;) Sleep(INFINITE); }
    ~IntegratedHost() { if (report_ != INVALID_HANDLE_VALUE) CloseHandle(report_); }   // never stops, never waits (D9)
};

#ifndef P21_RECORDER_INTEGRATION_NO_ENTRY_POINTS
static int child(const std::string& mode, const fs::path& capture_dir) {
    const bool latched = mode.find("ordinary") == std::string::npos;
    auto provider_gate = std::make_shared<p21::Gate>();
    if (mode.find("provider-stuck") != std::string::npos) provider_gate->close();
    auto host = std::make_unique<IntegratedHost>(capture_dir, mode == "writer-stuck", provider_gate);
    if (mode.find("provider-stuck") != std::string::npos) check(provider_gate->wait_entered(1000), "provider inside its blocking call");
    host->run_game();                    // or2_xbox_main
    const auto r = host->stop();         // the game loop returned
    if (!r.resolved) { if (latched) host->park_for_supervisor(); host->forced_exit(); }
    host.reset();
    return 0;                            // normal CRT teardown: the recorder and every worker joined
}
#endif

struct Observed { DWORD code = 0; bool forced = false; std::string state; };
static std::string read_text(const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static std::string state_of(const fs::path& capture_dir) {
    const auto t = read_text(capture_dir / "host-shutdown.json"); const auto k = t.find("\"state\":\"");
    return k == std::string::npos ? std::string() : t.substr(k + 9, t.find('"', k + 9) - k - 9);
}
static Observed supervise(const std::wstring& self, const std::string& mode, const fs::path& run_dir) {
    Observed o; check(fs::create_directory(run_dir), "new run folder");
    const auto capture_dir = run_dir / "capture";
    std::wstring cmd = L"\"" + self + L"\" --child " + std::wstring(mode.begin(), mode.end()) + L" \"" + capture_dir.wstring() + L"\"";
    STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    check(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi) != 0, "spawn child");
    // Handles close on every path. A proof that fails with the child still alive requests its end (best effort, never
    // waits). That cannot guarantee the child is gone, e.g. with I/O still pending (Astra 12222). What this path does
    // guarantee: the proof fails and no exit observation is written.
    struct Owned {
        PROCESS_INFORMATION& p; bool observed = false;
        ~Owned() { if (!observed && WaitForSingleObject(p.hProcess, 0) == WAIT_TIMEOUT) TerminateProcess(p.hProcess, 0xDBCE0F0E); CloseHandle(p.hThread); CloseHandle(p.hProcess); }
    } owned{pi};
    // The identity binding: this handle's process. A failed query is never a usable identity (Astra 12080).
    const auto identity = creation_time(pi.hProcess);
    check(pi.dwProcessId != 0 && identity != 0, "supervisor holds the child's nonzero identity");
    const auto bound = p21::Clock::now() + std::chrono::milliseconds(Budget.recorder_ms + Budget.leases_ms + Budget.workers_ms + 2000 + 2000);
    // A failed wait throws (no observation). Only failures count, so the check total does not depend on poll timing.
    auto wait = [&](DWORD ms) { const DWORD w = WaitForSingleObject(pi.hProcess, ms); if (w != WAIT_OBJECT_0 && w != WAIT_TIMEOUT) check(false, "supervisor wait did not fail"); return w == WAIT_OBJECT_0; };
    for (;;) {
        if (wait(20)) break;
        const auto st = state_of(capture_dir);
        if (st == "unresolved" || p21::Clock::now() >= bound) {
            if (st == "unresolved") Sleep(300);      // a parked child must not have exited on its own meanwhile
            // Whether a forced end is needed depends on timing, so only its failure counts (a deterministic check total).
            if (!wait(0)) { if (!TerminateProcess(pi.hProcess, 0xDBCE0F0F)) check(false, "supervisor ended the child"); o.forced = true; }
            // Bounded: termination can wait on pending I/O. Still alive means no observation, and the proof fails.
            if (!wait(5000)) check(false, "child ended within the bound after its forced end");
            break;
        }
    }
    check(GetExitCodeProcess(pi.hProcess, &o.code) != 0 && o.code != STILL_ACTIVE, "supervisor observed the exit code");
    o.state = state_of(capture_dir);
    owned.observed = true;
    if (fs::exists(capture_dir)) {   // written only after an exit this supervisor actually observed
        std::ofstream e(capture_dir / "exit.json", std::ios::binary);
        e << "{\"pid\":" << pi.dwProcessId << ",\"creationTime\":" << identity << ",\"exitCode\":" << o.code << ",\"forced\":" << (o.forced ? "true" : "false") << "}";
        e.close(); check(bool(e), "exit observation written");
    }
    return o;
}

#ifndef P21_RECORDER_INTEGRATION_NO_ENTRY_POINTS
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 4 && std::wstring(argv[1]) == L"--child") {
            std::string mode; for (const wchar_t* p = argv[2]; *p; ++p) mode.push_back(static_cast<char>(*p & 0x7F));
            return child(mode, fs::path(argv[3]));
        }
        check(argc == 2, "usage: p21_recorder_integration <new result folder>");
        const fs::path root(argv[1]); check(fs::create_directory(root), "new result folder");
        wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
        auto r = supervise(self, "resolved", root / "resolved");
        check(r.code == 0 && !r.forced && r.state == "resolved", "resolved host returns through normal CRT teardown with exit 0");
        r = supervise(self, "writer-stuck", root / "writer-stuck");
        check(r.forced && r.state == "unresolved", "a writer that never returns: unresolved, parked, ended only by the supervisor");
        r = supervise(self, "provider-stuck", root / "provider-stuck");
        check(r.forced && r.state == "unresolved", "a complete recorder beside a stuck provider: unresolved, parked, ended by the supervisor");
        r = supervise(self, "ordinary-provider-stuck", root / "ordinary-provider-stuck");
        check(r.code == p21::ForcedExitCode && !r.forced && r.state == "unresolved", "an ordinary host ends itself, labelled forced");
        std::cout << "PASS " << checks << " supervised host/recorder checks (actual frames, real writer); capture verdicts follow from the finalizer\n";
        std::cout.flush();
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
#endif
