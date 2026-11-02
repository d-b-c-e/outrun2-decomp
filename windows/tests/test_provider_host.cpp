// The in-process host (host/provider_host.hpp) over fake COM devices, constructed as a consumer constructs it: which
// controls schema its Core applies (STD-037 item 4, `controls_reads`). Calibration 9a run1 found the host reading a
// consumer's Schema 2 profile at reads 1 while the consumer's own readers applied it; a parser-only test cannot see
// that, so these checks read the Core-applied values. Ordinary (the physical wheel) and latched (injected raw values
// through the control worker and inject.txt, as the 9a runtime proof) both take the calibration law at reads 2; the
// default refuses the section. No device, game or force. Built for x86 (DBCE_SHIM_ORDINARY_ONLY) it runs the ordinary
// cases only: the journal and its observer are x64-only.
#include "provider_host.hpp"
#include "ordinary_only.hpp"
#if !DBCE_SHIM_ORDINARY_ONLY
#include "journal_identity.hpp"
#endif
#include "shim_fake_com.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
namespace h = dbce::shim::hosting;
namespace sh = dbce::shim;
#if !DBCE_SHIM_ORDINARY_ONLY
namespace c = dbce::shim::control;
#endif
int checks = 0;
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
const std::string W = "{d71b8350-61b7-11f1-8001-444553540000}", WP = "{0006346e-0000-0000-0000-504944564944}";
const std::string Nonce = "hostnonce1";
// The 9a steer: measured locks 4000 and 60000 around a measured centre of 24000, inside the declared 0..65535.
const std::string Profile = "[Controls]\nSchema = 2\nProfile = Calibrated Rig\nRevision = r2\nTransmission = Sequential\n"
    "steer = axis 0 dev=" + W + " prod=" + WP + " range=0..65535 rest=32768 travel=+1 cal=4000..24000..60000\n"
    "throttle = axis 2 dev=" + W + " prod=" + WP + " range=0..65535 rest=0 travel=+1\n";
// The 9a raw values and the schema-2 two-sided law's steer for each; the schema-1 midpoint law gives another value.
struct Point { long raw; double cal; };
const Point Points[] = {{4000, -1.0}, {14000, -0.5}, {24000, 0.0}, {42000, 0.5}, {60000, 1.0}};
double midpoint(long raw) { return (double(raw) - 32767.5) / 32767.5; }

struct NoRecorder {   // nothing to drain: the recorder phase is complete by construction
    void finish() {}
    struct Drained { h::RecOutcome outcome; bool joined; };
    Drained drain(h::Clock::time_point) { return {h::RecOutcome::Complete, true}; }
};
using Host = h::ProviderHost<NoRecorder>;

std::filesystem::path scratch() {
    static const auto dir = std::filesystem::temp_directory_path() / ("dbce-provider-host-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(dir); return dir;
}
fakecom::Input* wheel_input(std::size_t& slot, long steer) {
    auto* in = new fakecom::Input;   // fake storage lives to process exit, like the host's own provider
    fakecom::DeviceScript d; check(sh::guid_parse(W, d.instance) && sh::guid_parse(WP, d.product), "fixture guids");
    d.name = L"MOZA R12 Base"; d.buttons = 128; d.state.lX = steer;
    slot = in->add(d); return in;
}
// `reads` as the consumer passes it; 0 constructs the host with the default, as the DLL does.
std::unique_ptr<Host> open(const std::filesystem::path& dir, fakecom::Input* in, bool latched, int reads) {
    std::filesystem::create_directories(dir);
    if (!reads) return std::make_unique<Host>(dir.wstring(), Nonce, in, Profile, latched, std::make_unique<NoRecorder>());
    return std::make_unique<Host>(dir.wstring(), Nonce, in, Profile, latched, std::make_unique<NoRecorder>(), sh::Capability{}, nullptr,
                                  100'000, nullptr, reads);
}
const sh::ActionValue& steer(const sh::Frame& f) { return f.action[std::size_t(sh::action_index("steer"))]; }
void set_steer(fakecom::Input* in, std::size_t slot, long raw) { std::lock_guard<std::mutex> l(in->m); in->scripts[slot].state.lX = raw; }
#if !DBCE_SHIM_ORDINARY_ONLY
// Temp file, then a replacing rename: the control worker never reads a half-written batch.
void publish(const std::filesystem::path& to, const std::string& text) {
    const auto tmp = to.wstring() + L".tmp";
    { std::ofstream f(tmp, std::ios::binary); f << text; }
    check(MoveFileExW(tmp.c_str(), to.wstring().c_str(), MOVEFILE_REPLACE_EXISTING) != 0, "publish " + to.string());
}
#endif

void test_fixture() {
    for (const auto& p : Points)
        check(std::fabs(p.cal - midpoint(p.raw)) > 0.05, "raw " + std::to_string(p.raw) + " tells the calibration law from the midpoint law");
}

// Ordinary, reads 2: the physical steer through the host's Core follows the calibration, and the start line applies it.
void test_ordinary_reads2() {
    std::size_t slot = 0; auto* in = wheel_input(slot, 24000);
    auto host = open(scratch() / "ordinary-2", in, false, 2);
    bool live = false;
    for (int n = 0; n < 400 && !live; ++n) { live = steer(host->tick()).status == sh::Status::Live; if (!live) Sleep(5); }
    check(live, "the calibrated steer goes Live at reads 2");
    const auto start = host->start_report().line;
    check(start.find("profile \"Calibrated Rig\" revision r2") != std::string::npos && start.find("applied 2 (steer,throttle)") != std::string::npos,
          "the start line applies the Schema 2 profile: " + start);
    for (const auto& p : Points) {
        set_steer(in, slot, p.raw);
        const auto& a = steer(host->tick());
        check(a.status == sh::Status::Live && !a.injected && std::fabs(a.value - p.cal) < 1e-4,
              "physical raw " + std::to_string(p.raw) + " reads " + std::to_string(a.value) + ", the calibration law's " + std::to_string(p.cal));
    }
#if !DBCE_SHIM_ORDINARY_ONLY
    const auto id = h::journal_identity(*host, Nonce, Profile, "test core", "test adapter", 2);
    check(id->profile_revision == "r2" && id->id.profile == sh::read_profile(Profile, 2).file_hash &&
          id->start_line.find("applied 2 (steer,throttle)") != std::string::npos, "the journal identity reads the profile as the Core does");
#endif
    const auto r = host->stop();
    check(r.resolved, "the ordinary stop resolves");
    check(in->counters.force_calls == 0 && in->refs == 0, "no force call; DirectInput released");
}

// The default (reads 1, the DLL's): the same Schema 2 profile applies nothing, whatever the wheel does.
void test_default_refuses() {
    std::size_t slot = 0; auto* in = wheel_input(slot, 60000);
    auto host = open(scratch() / "ordinary-default", in, false, 0);
    bool live = false;
    for (int n = 0; n < 100 && !live; ++n) { const auto& a = steer(host->tick()); live = a.status == sh::Status::Live || a.value != 0.f; Sleep(1); }
    check(!live && steer(host->tick()).status == sh::Status::Unbound, "at the default reads steer stays unbound and neutral");
    const auto start = host->start_report().line;
    check(start == "controls: nothing applied via shim-ini-controls-1: Schema 2 is not supported (this mod reads 1)", "the default refuses the section: " + start);
    check(host->stop().resolved && in->counters.force_calls == 0, "the refused host stops resolved, no force call");
}

#if !DBCE_SHIM_ORDINARY_ONLY
// Latched, reads 2, the 9a path: inject.on admits the session, each batch holds one raw value on the wheel's axis 0
// (physical full right must not drive), and the Core-applied steer follows the calibration law.
void test_latched_reads2() {
    const auto dir = scratch() / "latched-2"; std::filesystem::create_directories(dir);
    const auto session = dir / "inject.on", batch = dir / "inject.txt";
    const auto expires = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() + 600;
    publish(session, "nonce=" + Nonce + "\nexpires=" + std::to_string(expires) + "\n");
    std::size_t slot = 0; auto* in = wheel_input(slot, 65535);
    auto host = open(dir, in, true, 2);
    c::JournalLimits limits; limits.cap_bytes = c::LatchedJournalCap;
    auto io = std::make_shared<c::FileIo>(session.wstring(), batch.wstring(), dir.wstring(), false,
                                          h::journal_identity(*host, Nonce, Profile, "test core", "test adapter", 2), limits);
    host->start_control(io, {true, true, true, Nonce});
    bool waiting = false;
    for (int n = 0; n < 600 && !waiting; ++n) {
        const auto& f = host->tick(); waiting = f.run == sh::Run::Waiting;
        check(steer(f).value == 0.f, "physical input feeds nothing in a latched process");
        if (!waiting) Sleep(5);
    }
    check(waiting, "the session is admitted and waits for its first batch");
    int number = 0;
    for (const auto& p : Points) {
        publish(batch, "nonce=" + Nonce + "\nbatch=" + std::to_string(++number) + "\ninject raw axis 0 dev=" + W + " value=" + std::to_string(p.raw) + " ms=3000\n");
        bool got = false; float last = 0.f;
        for (int n = 0; n < 600 && !got; ++n) {
            const auto& a = steer(host->tick()); last = a.value;
            got = a.status == sh::Status::Live && a.injected && std::fabs(a.value - p.cal) < 1e-4;
            if (!got) Sleep(5);
        }
        check(got, "injected raw " + std::to_string(p.raw) + " reads the calibration law's " + std::to_string(p.cal) + " (last " + std::to_string(last) + ")");
    }
    host->close_control_admission();
    (void)host->tick();
    const auto r = host->stop();
    std::string why; for (const auto& u : r.unresolved) why += " " + u + ";";
    check(r.resolved, "the latched stop resolves:" + why);
    check(std::filesystem::exists(dir / "controls-journal.jsonl") && in->counters.force_calls == 0, "the journal is in the run folder; no force call");
}
#endif
}

int main() {
    try {
        test_fixture(); test_ordinary_reads2(); test_default_refuses();
#if !DBCE_SHIM_ORDINARY_ONLY
        test_latched_reads2();
#endif
    }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << "\n"; return 1; }
    std::error_code ec; std::filesystem::remove_all(scratch(), ec);
    std::cout << "provider host: " << checks << " checks passed\n";
    return 0;
}
