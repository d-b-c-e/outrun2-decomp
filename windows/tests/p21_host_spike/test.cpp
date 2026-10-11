// P21 offline proof (P21-SHUTDOWN.md r2, section 6). In-process rows P21a-d, h, i; supervised child processes P21e, f.
// Usage: p21_host_spike <empty result folder>       (the parent; it runs itself as the children)
//        p21_host_spike --child <mode> <folder>
#include "host.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

using namespace p21_spike;
static int checks = 0, failures = 0;
static void check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } }
static std::string read_all(const std::wstring& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static std::string ascii(const std::wstring& w) { std::string s; for (wchar_t ch : w) s.push_back(static_cast<char>(ch & 0x7F)); return s; }
static std::wstring sub(const std::wstring& root, const wchar_t* name) { std::wstring d = root + L"\\" + name; CreateDirectoryW(d.c_str(), nullptr); return d; }
static long long ms_since(Clock::time_point t) { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count(); }
static const Budget Fast{150, 100, 100};   // the production budgets are 1000/200/200 ms; the rows scale them down

static HostReport host_report(const std::wstring& dir) {
    HostReport h; const auto t = read_all(dir + L"\\host-shutdown.json"); if (t.empty()) return h;
    h.present = true; auto field = [&](const char* k) { auto p = t.find(std::string("\"") + k + "\":\""); if (p == std::string::npos) return std::string(); p += std::strlen(k) + 4; return t.substr(p, t.find('"', p) - p); };
    h.state = field("state"); h.nonce = field("nonce"); return h;
}
static WorkerReport worker_report(const std::wstring& dir) {
    WorkerReport w; const auto t = read_all(dir + L"\\worker.tsv"); if (t.empty()) return w;
    w.present = true; auto field = [&](const char* k) { auto p = t.find(std::string(k) + "\t"); if (p == std::string::npos) return std::string(); p += std::strlen(k) + 1; return t.substr(p, t.find('\n', p) - p); };
    w.outcome = field("outcome"); w.nonce = field("nonce"); return w;
}

static void in_process(const std::wstring& root) {
    {   // P21a: order and budgets, everything resolves
        HostConfig c; c.folder = sub(root, L"a"); c.nonce = "a1";
        Host h(c); Sleep(20);
        const auto t0 = Clock::now(); const auto r = h.stop(Fast);
        check(r.resolved && r.recorder == RecOutcome::Complete && r.recorder_joined && r.unresolved.empty(), "P21a: a quiet host resolves with a complete, joined recorder");
        check(ms_since(t0) < 100, "P21a: nothing waits when nothing blocks");
        const auto hr = host_report(c.folder); const auto wr = worker_report(c.folder);
        check(hr.state == "resolved" && hr.nonce == "a1" && wr.outcome == "Complete" && wr.nonce == "a1", "P21a: host and worker reports name the capture");
        const auto again = h.stop(Fast);
        check(again.resolved && again.elapsed_us == r.elapsed_us, "P21a: a second stop returns the first report");
    }
    {   // P21b: a provider worker blocked in a driver call; its device is in flight
        HostConfig c; c.folder = sub(root, L"b"); c.nonce = "b1"; c.lease_gate = c.provider_gate;
        auto keep = std::shared_ptr<WorkerState>();
        {
            c.provider_gate->close();
            Host h(c); check(c.provider_gate->wait_entered(500), "P21b: the provider is inside its blocking call");
            keep = h.provider_state();
            const auto t0 = Clock::now(); const auto r = h.stop(Fast);
            const auto took = ms_since(t0);
            bool worker = false, lease = false; for (auto& u : r.unresolved) { worker |= u == "worker provider"; lease |= u == "lease leaked"; }
            check(!r.resolved && worker && lease, "P21b: a stuck provider is unresolved and its device leaked, never released under it");
            check(took >= Fast.leases_ms + Fast.workers_ms - 20 && took < Fast.recorder_ms + Fast.leases_ms + Fast.workers_ms + 150, "P21b: the stop stays within its budgets");
            check(host_report(c.folder).state == "unresolved", "P21b: the host report says unresolved");
        }   // the host is destroyed while the provider is still blocked
        c.provider_gate->release();
        std::unique_lock<std::mutex> l(keep->m);
        check(keep->cv.wait_for(l, std::chrono::seconds(2), [&] { return keep->done; }), "P21b: released later, the worker ends on its own state after the host is gone");
    }
    {   // P21c: the recorder blocked in stop/close/hash
        HostConfig c; c.folder = sub(root, L"c"); c.nonce = "c1";
        c.recorder_gate->close();
        Host h(c);
        const auto r = h.stop(Fast);
        bool rec = false; for (auto& u : r.unresolved) rec |= u == "recorder: TimedOut";
        check(!r.resolved && r.recorder == RecOutcome::TimedOut && rec, "P21c: a stuck recorder times out and leaves the host unresolved");
        auto rs = h.recorder_state();
        c.recorder_gate->release();
        { std::unique_lock<std::mutex> l(rs->m); rs->cv.wait_for(l, std::chrono::seconds(2), [&] { return rs->worker_done; }); }
        check(rs->outcome.load() == RecOutcome::TimedOut && worker_report(c.folder).outcome == "TimedOut", "P21c: a late finish never becomes Complete");
        check(host_report(c.folder).state == "unresolved", "P21c: and the host report stays unresolved");
    }
    {   // P21c2 (Astra 11867): Complete, then the worker blocks writing its own report
        HostConfig c; c.folder = sub(root, L"c2"); c.nonce = "c2";
        c.report_gate->close();
        Host h(c);
        const auto r = h.stop(Fast);
        bool joined = false; for (auto& u : r.unresolved) joined |= u == "recorder: worker not joined";
        check(!r.resolved && !r.recorder_joined && joined, "P21c2: a Complete outcome whose worker has not returned is not a resolved recorder");
        check(host_report(c.folder).state == "unresolved", "P21c2: the host report says unresolved before the worker report exists");
        auto rs = h.recorder_state();
        c.report_gate->release();
        { std::unique_lock<std::mutex> l(rs->m); rs->cv.wait_for(l, std::chrono::seconds(2), [&] { return rs->worker_done; }); }
        const auto w = worker_report(c.folder);
        check(w.outcome == "Complete" && !may_seal("c2", w, host_report(c.folder), ExitObservation{true, true, false, 0}),
              "P21c2: the late Complete worker report still cannot seal beside the unresolved host report");
    }
    {   // P21d: no supervisor finish
        HostConfig c; c.folder = sub(root, L"d"); c.nonce = "d1";
        Host h(c); Report r;
        std::thread other([&] { r = h.stop(Fast); }); other.join();
        check(!r.resolved && r.recorder == RecOutcome::Cancelled, "P21d: a stop off the producer thread cancels the recorder, never completes it");
    }
    {   // P21i: destroyed without stop, with a worker blocked
        HostConfig c; c.folder = sub(root, L"i"); c.nonce = "i1";
        c.control_gate->close();
        auto h = std::make_unique<Host>(c); c.control_gate->wait_entered(500);
        const auto t0 = Clock::now(); h.reset();
        check(ms_since(t0) < 50, "P21i: the destructor never stops and never waits");
        const auto hr = host_report(c.folder);
        check(hr.state != "resolved", "P21i: without a stop the shutdown file never reads resolved");
        c.control_gate->release();
    }
    {   // P21h: the seal needs all three facts, bound to one nonce
        const WorkerReport wOk{true, "Complete", "n"}, wBad{true, "TimedOut", "n"}, wSwap{true, "Complete", "other"};
        const HostReport hOk{true, "resolved", "n"}, hBad{true, "unresolved", "n"}, hSwap{true, "resolved", "other"};
        const ExitObservation eOk{true, true, false, 0}, eForced{true, true, true, 0}, eCode{true, true, false, 1}, eOther{true, false, false, 0};
        int sealed = 0;
        for (int m = 0; m < 8; ++m) sealed += may_seal("n", (m & 1) ? wOk : wBad, (m & 2) ? hOk : hBad, (m & 4) ? eOk : eForced) ? 1 : 0;
        check(sealed == 1 && may_seal("n", wOk, hOk, eOk), "P21h: of eight combinations only worker Complete + host resolved + normal exit seals");
        check(!may_seal("n", wSwap, hOk, eOk) && !may_seal("n", wOk, hSwap, eOk), "P21h: a report from another capture refuses");
        check(!may_seal("n", wOk, hOk, eCode) && !may_seal("n", wOk, hOk, eOther) && !may_seal("n", wOk, HostReport{}, eOk), "P21h: a nonzero exit, another process or a missing host report refuses");
    }
}

// ---- children: the OutRun wWinMain shape, one exit path each ----
static Host* g_exit_host = nullptr; static bool g_exit_latched = false;
static void exit_hook() {   // D6: exit() inside the game
    if (!g_exit_host) return;
    const auto r = g_exit_host->stop(Fast);
    if (!r.resolved) { if (g_exit_latched) g_exit_host->park_for_supervisor(); g_exit_host->forced_exit(); }
}
static int child(const std::string& mode, const std::wstring& dir) {
    HostConfig c; c.folder = dir; c.nonce = "child-" + mode;
    const bool stuck = mode.find("stuck") != std::string::npos, latched = mode.find("latched") != std::string::npos;
    if (stuck) { c.lease_gate = c.provider_gate; c.provider_gate->close(); }
    auto host = std::make_unique<Host>(c);
    if (stuck) c.provider_gate->wait_entered(500);
    if (mode.rfind("exit", 0) == 0) {   // the game calls exit(7) from inside its loop
        g_exit_host = host.get(); g_exit_latched = latched; std::atexit(exit_hook);
        std::exit(7);
    }
    const auto r = host->stop(Fast);    // or2_xbox_main returned
    if (!r.resolved) { if (latched) host->park_for_supervisor(); host->forced_exit(); }
    host.reset();
    return 0;                            // normal CRT teardown with no shim thread
}

struct Run { DWORD code = 0; bool forced_by_supervisor = false; std::string state; };
static Run supervise(const std::wstring& self, const std::string& mode, const std::wstring& dir) {
    std::wstring cmd = L"\"" + self + L"\" --child " + std::wstring(mode.begin(), mode.end()) + L" \"" + dir + L"\"";
    STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    Run run;
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) { run.state = "spawn failed"; return run; }
    // The supervisor owns the identity: this handle. Bound: the stop budget plus 2 s (question 4).
    const auto deadline = Clock::now() + std::chrono::milliseconds(Fast.recorder_ms + Fast.leases_ms + Fast.workers_ms + 2000);
    for (;;) {
        if (WaitForSingleObject(pi.hProcess, 20) == WAIT_OBJECT_0) break;
        const auto st = host_report(dir).state;
        if (st == "unresolved" || Clock::now() >= deadline) {
            Sleep(st == "unresolved" ? 300 : 0);      // a parked child must not have exited on its own meanwhile
            if (WaitForSingleObject(pi.hProcess, 0) != WAIT_OBJECT_0) { TerminateProcess(pi.hProcess, 0xDBCE0F0F); run.forced_by_supervisor = true; }
            WaitForSingleObject(pi.hProcess, INFINITE); break;
        }
    }
    GetExitCodeProcess(pi.hProcess, &run.code); run.state = host_report(dir).state;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return run;
}
static void supervised(const std::wstring& self, const std::wstring& root) {
    auto r = supervise(self, "resolved", sub(root, L"e-resolved"));
    check(r.code == 0 && !r.forced_by_supervisor && r.state == "resolved", "P21e: a resolved child returns through normal CRT teardown, exit 0");
    r = supervise(self, "ordinary-stuck", sub(root, L"e-ordinary"));
    check(r.code == ForcedExitCode && !r.forced_by_supervisor && r.state == "unresolved", "P21e: an ordinary child with a stuck worker ends itself, labelled forced (0xDBCE0021)");
    r = supervise(self, "latched-stuck", sub(root, L"e-latched"));
    check(r.forced_by_supervisor && r.code == 0xDBCE0F0F && r.state == "unresolved", "P21e: a latched child parks; only the supervisor ends it, by its own handle");
    r = supervise(self, "exit-resolved", sub(root, L"f-resolved"));
    check(r.code == 7 && !r.forced_by_supervisor && r.state == "resolved", "P21f: exit() inside the game stops the host once, then exit continues");
    r = supervise(self, "exit-latched-stuck", sub(root, L"f-latched"));
    check(r.forced_by_supervisor && r.state == "unresolved", "P21f: exit() with a stuck worker in a latched process parks for the supervisor");
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring(argv[1]) == L"--child") return child(ascii(argv[2]), argv[3]);
    if (argc != 2) { std::printf("usage: p21_host_spike <empty result folder>\n"); return 2; }
    const std::wstring root = argv[1];
    if (!CreateDirectoryW(root.c_str(), nullptr)) { std::printf("the result folder must be new\n"); return 2; }
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
    in_process(root);
    supervised(self, root);
    std::printf("%s %d P21 host checks (%d failed); offline fakes only, no device, game or launch.\n", failures ? "FAIL" : "PASS", checks, failures);
    std::fflush(stdout);
    // The in-process rows leave detached fake workers blocked by design; the proof ends without CRT teardown beside them.
    TerminateProcess(GetCurrentProcess(), failures ? 1 : 0);
    return failures ? 1 : 0;
}
