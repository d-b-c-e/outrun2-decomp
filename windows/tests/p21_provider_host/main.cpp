// P21k: the P21 host lifetime over zero's real Core and DInputProvider on fake COM objects, in supervised child processes.
// Each child is one OutRun wWinMain shape: the provider worker on its own thread, the input thread ticking the core, the
// game loop returning, then stop() on the input thread and the reviewed D4 exit. Astra 12465: a bounded slow driver call
// shows deterministic recovery within the budget; an event-backed call that never returns shows the unresolved park and
// the supervisor's forced end, with nothing released under the call and nothing sealed. Astra 13277: the same for the
// release phase (a device's Unacquire, DirectInput's Release), which now has its own budget. No device, game or launch.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "p21_provider_host/host.hpp"
#include "shim_fake_com.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace dbce::shim;
namespace {
int checks = 0, failures = 0;
void check(bool ok, const std::string& what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what.c_str()); } }
const p21k::Budget Fast{300, 200, 200, 200};   // recorder, leases, release, workers
// A stop returns within its phases' budgets, whatever a driver call does (D3); 250 ms covers scheduling and the report.
const long long StopBoundUs = (Fast.recorder_ms + Fast.leases_ms + Fast.release_ms + Fast.workers_ms + 250) * 1000LL;
bool release_mode(const std::string& mode) { return mode.find("unacquire") != std::string::npos || mode.find("input-release") != std::string::npos; }
const GUID WI = fakecom::make_guid(0xd71b8350u, 0x61b7), WP = fakecom::make_guid(0x0006346eu, 0x0000);

std::string profile() {
    const std::string dev = " dev=" + guid_text(WI) + " prod=" + guid_text(WP);
    return "[Controls]\nSchema = 1\nProfile = P21k\nRevision = r1\nTransmission = Sequential\n"
           "steer = axis 0" + dev + " range=0..65535 rest=32768 travel=+1\nconfirm = button 3" + dev + "\n";
}
std::string narrow(const std::wstring& w) { std::string s; for (wchar_t c : w) s.push_back(c < 128 ? char(c) : '?'); return s; }
std::string read_all(const std::wstring& path) { std::ifstream f(path, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
std::string field(const std::string& json, const std::string& key) {
    const auto k = json.find("\"" + key + "\":"); if (k == std::string::npos) return "";
    auto v = k + key.size() + 3; if (v < json.size() && json[v] == '"') { const auto e = json.find('"', v + 1); return json.substr(v + 1, e - v - 1); }
    auto e = json.find_first_of(",}", v); return json.substr(v, e - v);
}
bool write_text(const std::wstring& path, const std::string& text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD n = 0; const bool ok = WriteFile(h, text.data(), DWORD(text.size()), &n, nullptr) && n == text.size();
    CloseHandle(h); return ok;
}

// ---- children ----
int child(const std::string& mode, const std::wstring& dir) {
    const bool latched = mode.rfind("ordinary", 0) != 0;
    auto* in = new fakecom::Input;   // kept for the process's life: a parked call may still be inside it
    auto script = fakecom::DeviceScript{}; script.instance = WI; script.product = WP; script.name = L"MOZA R12 Base";
    script.state.lX = 49152;
    script.acquire_armed = std::make_shared<std::atomic<bool>>(false);
    script.acquire_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    script.acquire_release = CreateEventW(nullptr, TRUE, FALSE, nullptr);   // never set: a call that waits on it never returns
    if (mode.find("slow-within") != std::string::npos) script.acquire_delay_ms = 120;   // inside the 200 ms lease wait
    else if (mode.find("slow-beyond") != std::string::npos) script.acquire_delay_ms = 600;   // past every budget, returns later
    else script.acquire_delay_ms = -1;
    // The release boundary: the provider's shutdown meets a slow or stuck Unacquire, or a stuck release of DirectInput.
    script.unacquire_armed = std::make_shared<std::atomic<bool>>(false);
    script.unacquire_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    script.unacquire_release = CreateEventW(nullptr, TRUE, FALSE, nullptr);   // never set
    script.unacquire_delay_ms = mode.find("unacquire-within") != std::string::npos ? 100     // inside the 200 ms release budget
                              : mode.find("unacquire-beyond") != std::string::npos ? 1500   // past it (Astra's 1200 ms repro, wider)
                              : -1;
    const auto index = in->add(script);
    in->release_armed = std::make_shared<std::atomic<bool>>(false);
    in->release_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    in->release_release = CreateEventW(nullptr, TRUE, FALSE, nullptr);   // never set
    in->release_delay_ms = -1;
    in->enumerate_armed = std::make_shared<std::atomic<bool>>(mode.find("listing") != std::string::npos);
    in->enumerate_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    in->enumerate_release = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    auto host = std::make_unique<p21k::ProviderHost>(dir, "p21k-" + mode, in, profile(), latched);
    // The game loop: tick on this (input) thread until the wheel is read through the real provider.
    bool live = false;
    for (int i = 0; i < 400 && !live && mode.find("listing") == std::string::npos; ++i) {
        const auto& f = host->tick();
        // A valid sample read through the real provider (a latched process holds its actions test-neutral until admission).
        live = !f.samples.empty() && f.samples[0].valid && f.samples[0].physical.axes[0] == 49152;
        Sleep(5);
    }
    if (mode.find("listing") != std::string::npos) WaitForSingleObject(in->enumerate_entered, 2000);   // the first listing is inside the "driver"
    else if (mode != "resolved" && !release_mode(mode)) {
        // Lose acquisition on the next read; the worker's reacquisition then sits inside Acquire.
        { std::lock_guard<std::mutex> l(in->m); in->scripts[index].reads.push_back({DI_OK, DIERR_INPUTLOST}); }
        script.acquire_armed->store(true);
        for (int i = 0; i < 400 && WaitForSingleObject(script.acquire_entered, 0) != WAIT_OBJECT_0; ++i) { host->tick(); Sleep(5); }
    }
    bool entered = mode == "resolved" || WaitForSingleObject(mode.find("listing") != std::string::npos ? in->enumerate_entered : script.acquire_entered, 0) == WAIT_OBJECT_0;
    if (mode.find("unacquire") != std::string::npos) script.unacquire_armed->store(true);   // the shutdown's release of the idle device
    if (mode.find("input-release") != std::string::npos) in->release_armed->store(true);   // its release of DirectInput

    p21k::Report r;
    if (mode.find("off-thread") != std::string::npos) { std::thread t([&] { r = host->stop(Fast); }); t.join(); }
    else r = host->stop(Fast);    // or2_xbox_main returned
    if (release_mode(mode)) entered = WaitForSingleObject(mode.find("unacquire") != std::string::npos ? script.unacquire_entered : in->release_entered, 0) == WAIT_OBJECT_0;

    // The fake's own accounting, written before the D4 exit so the supervisor can read it from a parked child too.
    const auto& c = in->counters;
    std::ostringstream o;
    o << "{\"live\":" << (live ? "true" : "false") << ",\"entered\":" << (entered ? "true" : "false") << ",\"releases\":" << c.releases.load()
      << ",\"releaseDuringCall\":" << c.release_during_read.load() << ",\"afterRelease\":" << c.after_release.load() << ",\"forceCalls\":" << c.force_calls.load()
      << ",\"inputRefs\":" << in->refs.load() << ",\"passes\":" << host->worker_passes() << "}";
    write_text(dir + L"\\counters.json", o.str());
    if (!r.resolved) { if (latched) host->park_for_supervisor(); host->forced_exit(); }
    host.reset();
    return 0;                     // normal CRT teardown: the worker joined, the provider shut, DirectInput released
}

// ---- the identity-bound supervisor, every wait bounded and every call checked (as the reviewed spike) ----
struct Run { DWORD code = 0; bool forced = false; std::string state, host, counters; };
Run supervise(const std::wstring& self, const std::string& mode, const std::wstring& dir) {
    Run run;
    if (!CreateDirectoryW(dir.c_str(), nullptr)) { run.state = "no folder"; return run; }
    std::wstring cmd = L"\"" + self + L"\" --child " + std::wstring(mode.begin(), mode.end()) + L" \"" + dir + L"\"";
    STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) { run.state = "spawn failed"; return run; }
    const auto deadline = p21k::Clock::now() + std::chrono::milliseconds(Fast.recorder_ms + Fast.leases_ms + Fast.release_ms + Fast.workers_ms + 2000 + 4000);
    bool ok = true, exited = false;
    for (;;) {
        const DWORD w = WaitForSingleObject(pi.hProcess, 20);
        if (w == WAIT_FAILED) { ok = false; break; }
        if (w == WAIT_OBJECT_0) { exited = true; break; }
        const auto st = field(read_all(dir + L"\\host-shutdown.json"), "state");
        if (st == "unresolved" || p21k::Clock::now() >= deadline) {
            if (st == "unresolved") Sleep(300);   // a parked child must not exit on its own meanwhile; its counters are written
            if (WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT) { ok = TerminateProcess(pi.hProcess, 0xDBCE0F0F) != 0; run.forced = true; }
            exited = ok && WaitForSingleObject(pi.hProcess, 5000) == WAIT_OBJECT_0;
            break;
        }
    }
    if (!ok || !exited || !GetExitCodeProcess(pi.hProcess, &run.code) || run.code == STILL_ACTIVE) { run.state = "unobserved"; run.code = STILL_ACTIVE; }
    else { run.host = read_all(dir + L"\\host-shutdown.json"); run.state = field(run.host, "state"); }
    run.counters = read_all(dir + L"\\counters.json");
    if (!exited && WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 0xDBCE0F0E);   // best effort; already unobserved
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return run;
}

// D8, the reviewed seal rule: worker Complete + host resolved + a normal exit, all bound to the capture's nonce.
bool sealed(const std::wstring& dir, const std::string& mode, const Run& run) {
    const auto w = read_all(dir + L"\\worker.tsv");
    p21_spike::WorkerReport wr{!w.empty(), "", ""};
    if (wr.present) { const auto o = w.find("outcome\t"), n = w.find("nonce\t"); wr.outcome = w.substr(o + 8, w.find('\n', o) - o - 8); wr.nonce = w.substr(n + 6, w.find('\n', n) - n - 6); }
    p21_spike::HostReport hr{!run.host.empty(), field(run.host, "state"), field(run.host, "nonce")};
    p21_spike::ExitObservation e{run.state != "unobserved", true, run.forced, run.code};
    return p21_spike::may_seal("p21k-" + mode, wr, hr, e);
}

void expect(const std::wstring& self, const std::wstring& root, const std::string& mode, bool resolved, DWORD code, bool forced, const std::string& why,
            const std::vector<std::string>& causes = {}) {
    const auto dir = root + L"\\" + std::wstring(mode.begin(), mode.end());
    const auto r = supervise(self, mode, dir);
    const auto& h = r.host; const auto& c = r.counters;
    check(r.state == (resolved ? "resolved" : "unresolved") && r.code == code && r.forced == forced,
          "P21k " + mode + ": " + why + " (state " + r.state + ", exit 0x" + [&] { char b[16]; std::snprintf(b, sizeof b, "%lX", r.code); return std::string(b); }() + (r.forced ? ", forced by the supervisor" : "") + ")");
    check(field(c, "releaseDuringCall") == "0" && field(c, "afterRelease") == "0" && field(c, "forceCalls") == "0",
          "P21k " + mode + ": nothing released under a call, nothing used after its release, no force call (" + c + ")");
    check(field(c, "entered") == "true", "P21k " + mode + ": the stop met the call where it was meant to (" + c + ")");
    if (mode == "resolved" || mode.find("slow") != std::string::npos || release_mode(mode)) check(field(c, "live") == "true", "P21k " + mode + ": the wheel was read through the real provider first (a valid sample, the scripted axis)");
    // D3 for every mode: the stop itself returned within its budgets, whatever the driver call did (Astra 13277).
    const auto us = field(h, "elapsedUs");
    check(!us.empty() && std::stoll(us) <= StopBoundUs, "P21k " + mode + ": stop returns within its budgets (" + us + " us, bound " + std::to_string(StopBoundUs) + ")");
    check(sealed(dir, mode, r) == resolved, "P21k " + mode + ": " + (resolved ? "seals: worker Complete, host resolved, normal exit" : "never seals"));
    if (resolved) check(field(h, "inputReleased") == "true" && field(h, "held") == "0", "P21k " + mode + ": devices and DirectInput released (" + h + ")");
    // Each unresolved cause is named on its own: one cause hiding behind another is not proof (the host mutations).
    bool named = true; for (const auto& cause : causes) named = named && h.find(cause) != std::string::npos;
    if (!causes.empty()) check(named, "P21k " + mode + ": names its unresolved causes (" + h + ")");
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring(argv[1]) == L"--child") return child(narrow(argv[2]), argv[3]);
    if (argc != 2) { std::printf("usage: p21_provider_host <new result folder>\n"); return 2; }
    const std::wstring root = argv[1];
    if (!CreateDirectoryW(root.c_str(), nullptr)) { std::printf("the result folder must be new\n"); return 2; }
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
    const DWORD Forced = 0xDBCE0F0F;
    expect(self, root, "resolved", true, 0, false, "the real provider and core shut down within budget; normal CRT teardown, exit 0");
    expect(self, root, "slow-within", true, 0, false, "a 120 ms reacquisition inside the 200 ms lease wait is waited for, then released: recovered");
    const std::vector<std::string> heldCall{"lease leaked: 1 device", "worker provider", "DirectInput not released"};
    expect(self, root, "slow-beyond", false, Forced, true, "a 600 ms reacquisition past every budget: held, worker unjoined, parked; a later return cannot resolve it", heldCall);
    expect(self, root, "never", false, Forced, true, "an event-backed reacquisition that never returns: held, never released under it, parked, ended only by the supervisor", heldCall);
    expect(self, root, "ordinary-never", false, p21_spike::ForcedExitCode, false, "an ordinary process with the same call ends itself, labelled forced (0xDBCE0021)", heldCall);
    expect(self, root, "listing-never", false, Forced, true, "a listing that never returns: DirectInput kept for it, worker unjoined, parked", {"worker provider", "DirectInput not released"});
    expect(self, root, "never-off-thread", false, Forced, true, "stop off the input thread: refused as unresolved, the recorder cancelled, parked", {"stop off the input thread", "recorder: Cancelled"});
    // The release boundary (Astra 13277): the provider's own releases are driver calls too.
    expect(self, root, "unacquire-within", true, 0, false, "a 100 ms Unacquire inside the 200 ms release budget is waited for: released, resolved");
    const std::vector<std::string> stuckRelease{"DirectInput release did not return", "DirectInput not released"};
    expect(self, root, "unacquire-beyond", false, Forced, true, "a 1500 ms Unacquire past the release budget: stop does not wait for it; unresolved, parked", stuckRelease);
    expect(self, root, "unacquire-never", false, Forced, true, "an event-backed Unacquire that never returns: unresolved, parked, ended only by the supervisor", stuckRelease);
    expect(self, root, "ordinary-unacquire-never", false, p21_spike::ForcedExitCode, false, "an ordinary process with the same Unacquire ends itself, labelled forced (0xDBCE0021)", stuckRelease);
    expect(self, root, "input-release-never", false, Forced, true, "a release of DirectInput that never returns: unresolved, parked", stuckRelease);
    std::printf("%s %d P21k checks (%d failed): real Core and DInputProvider over fake COM, supervised children; no device, game or launch.\n",
                failures ? "FAIL" : "PASS", checks, failures);
    std::fflush(stdout);
    return failures ? 1 : 0;
}
