// Extension 4 (dbce_shim_candidate.h): the pure Candidate over the scripted provider (no threads: the test runs the
// worker pass itself), then the C ABI over fake COM devices with an all-unbound and a bound profile: nothing listed or
// opened before an explicit request, the bound-instance refusal, the candidate's raw reads in extension 3's structs, no
// action and no Core frame sample from it, extension 3 unchanged, release, loss, stop, and the refusals (latched, bridge).
// With a path argument it also loads the built DLL and checks the six exports. No device, game or force.
#include "dbce_shim_internal.hpp"
#include "dbce_shim_bridge.h"
#include "dbce_shim_candidate.h"
#include "ordinary_only.hpp"
#include "candidate.hpp"
#include "dinput_provider.hpp"
#include "shim_fake_com.hpp"
#include "shim_fake_provider.hpp"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace sh = dbce::shim;
int checks = 0;
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
const std::string W = "{d71b8350-61b7-11f1-8001-444553540000}", WP = "{0006346e-0000-0000-0000-504944564944}";
const std::string W2 = "{d71b8351-61b7-11f1-8001-444553540000}", WP2 = "{0006346f-0000-0000-0000-504944564944}";
const std::string W3 = "{d71b8352-61b7-11f1-8001-444553540000}", WP3 = "{00063470-0000-0000-0000-504944564944}";
const std::string Header = "[Controls]\nSchema = 1\nProfile = Rig\nRevision = r1\nTransmission = Sequential\n";
const std::string Unbound = Header;
const std::string Bound = Header + "steer = axis 0 dev=" + W + " prod=" + WP + " range=0..65535 rest=32768 travel=+1\n";
int idx(const char* id) { for (int i = 0; i < DBCE_SHIM_ACTIONS; ++i) if (std::strcmp(dbce_shim_action_name(i), id) == 0) return i; return -1; }

// ---------------------------------------------------------------- pure Candidate
sh::fake::Device fake_device(const std::string& dev, const std::string& prod, const char* name) {
    sh::fake::Device d; d.listed.dev = dev; d.listed.prod = prod; d.listed.name = name; d.caps = sh::fake::wheel_caps();
    d.listed.interface_path = "\\\\?\\hid#" + std::string(name);
    return d;
}
void tick(sh::Candidate& c, sh::fake::Provider& p, std::uint64_t& seq, bool observe = true) {
    c.tick(p, ++seq, p.now_us());
    if (observe) c.observed(seq);
}

void test_pure() {
    sh::fake::Provider p;
    const auto bound = p.add(fake_device(W, WP, "Bound Wheel")), wheel = p.add(fake_device(W2, WP2, "New Wheel"));
    const auto other = p.add(fake_device(W3, WP3, "Pedals"));
    p.at(wheel).state.axes[0] = 4321; p.at(wheel).state.buttons[7] = 0x80; p.at(wheel).state.povs[0] = 18000;
    sh::Candidate c({W});
    std::uint64_t seq = 0;
    for (int i = 0; i < 5; ++i) { tick(c, p, seq); check(!c.service(p), "an idle worker pass makes no provider call"); }
    check(p.enumerations == 0 && p.at(wheel).opens == 0 && p.at(wheel).polls == 0 && c.view().state == sh::CandidateState::None,
          "nothing is listed, opened or read before an explicit request");

    c.request_list(); tick(c, p, seq);
    check(c.listing().pending && c.listing().seq == 0, "a requested listing is pending until the worker runs");
    check(c.service(p) && p.enumerations == 1, "the worker makes one listing");
    tick(c, p, seq);
    check(!c.listing().pending && c.listing().seq == 1 && c.listing().devices.size() == 3 && c.bound(W) && !c.bound(W2),
          "the listing reaches the owner after a tick, with the bound instance marked");
    check(!c.service(p) && p.enumerations == 1, "one request, one listing");

    std::string why;
    check(!c.select(W, WP, why) && why.find("binds") != std::string::npos, "a profile-bound instance is refused");
    check(!c.select("{D71B8351-61B7-11F1-8001-444553540000}", WP2, why) && !c.select("d71b8351", WP2, why) && !c.select(W2, "", why),
          "upper-case, unbraced or empty GUID text is refused");
    check(c.view().state == sh::CandidateState::None && p.at(wheel).opens == 0, "a refused select changes nothing");

    check(c.select(W2, WP2, why) && c.view().state == sh::CandidateState::Opening, "a select queues an open");
    tick(c, p, seq);
    check(c.view().state == sh::CandidateState::Opening && c.view().read.frame_seq == 0 && p.at(wheel).opens == 0, "the open waits for the worker");
    check(c.service(p) && p.at(wheel).opens == 1 && p.enumerations == 2, "the worker lists, then opens the one selection");
    tick(c, p, seq);
    const auto& v = c.view();
    check(v.state == sh::CandidateState::Open && v.opened && v.epoch == 1 && v.identity.dev == W2 && v.identity.name == "New Wheel" &&
          v.identity.interface_path == "\\\\?\\hid#New Wheel" && v.caps.axis[0] && v.caps.range_ok[5], "open: identity, path and caps read back");
    check(v.read.frame_seq == seq && v.read.valid && v.read.device_seq == 1 && v.read.epoch == 1 && v.read.physical.axes[0] == 4321 &&
          v.read.physical.buttons[7] == 0x80 && v.read.physical.povs[0] == 18000 && p.at(wheel).polls == 1 && p.at(wheel).state_reads == 1,
          "the open candidate's first read is in the same tick, raw");
    tick(c, p, seq);
    check(c.view().read.device_seq == 2 && p.at(wheel).polls == 2, "one read per tick");
    check(p.at(bound).opens == 0 && p.at(other).opens == 0, "only the selection is opened");

    // The explicit lifetime: observed frames keep it; two seconds of unobserved ticks release it.
    for (int i = 0; i < 6; ++i) { p.advance(500000); tick(c, p, seq); }
    check(c.view().state == sh::CandidateState::Open, "a getter every frame keeps the candidate open");
    for (int i = 0; i < 4; ++i) { p.advance(500000); tick(c, p, seq, false); }
    check(c.view().state == sh::CandidateState::Open, "2 s unobserved is still within the bound");
    p.advance(500000); tick(c, p, seq, false);
    check(c.view().state == sh::CandidateState::None && !c.view().opened && c.view().reason.find("no capture getter") != std::string::npos,
          "past 2 s with no getter the candidate is released");
    check(c.service(p) && p.at(wheel).closes == 1, "the worker closes the released handle");

    // Replacement and release.
    check(c.select(W2, WP2, why), "select again"); c.service(p); tick(c, p, seq);
    check(c.view().state == sh::CandidateState::Open && c.view().epoch == 2 && p.at(wheel).opens == 2, "a new open is a new epoch");
    check(c.select(W3, WP3, why) && c.view().state == sh::CandidateState::Opening, "selecting another device replaces it");
    c.service(p); tick(c, p, seq);
    check(p.at(wheel).closes == 2 && p.at(other).opens == 1 && c.view().identity.dev == W3 && c.view().state == sh::CandidateState::Open,
          "the replaced handle is closed before the new one opens");
    c.release("cancel"); tick(c, p, seq);
    check(c.view().state == sh::CandidateState::None && c.view().reason == "cancel" && c.view().read.frame_seq == 0, "release: no read in later frames");
    c.service(p);
    check(p.at(other).closes == 1, "the released handle is closed");

    // A release before the worker runs cancels the open; one after the worker opened closes its late result.
    check(c.select(W2, WP2, why), "select"); c.release("cancel early"); c.service(p);
    check(p.at(wheel).opens == 2, "a cancelled open is never made");
    check(c.select(W2, WP2, why), "select"); c.service(p); c.release("cancel late"); tick(c, p, seq); c.service(p);
    check(p.at(wheel).opens == 3 && p.at(wheel).closes == 3 && c.view().state == sh::CandidateState::None, "a late open of a cancelled selection is closed");

    // Loss ends the epoch: no reacquisition for a candidate.
    check(c.select(W2, WP2, why), "select"); c.service(p); tick(c, p, seq);
    { std::lock_guard<std::mutex> l(p.m); p.at(wheel).script.push_back({sh::Call::Gone, sh::Call::NotCalled}); }
    tick(c, p, seq);
    check(c.view().state == sh::CandidateState::Lost && c.view().read.ended && !c.view().read.valid && c.view().read.poll == sh::Call::Gone &&
          c.view().read.state == sh::Call::NotCalled && c.view().epoch == 4, "a gone read ends the epoch: lost, select again");
    c.service(p);
    check(p.at(wheel).closes == 4 && p.at(wheel).reacquires == 0, "the lost handle is closed, never reacquired");
    tick(c, p, seq);
    check(c.view().read.frame_seq == 0, "a lost candidate is not read");

    // Pending reads past the limit end the epoch, as in Core.
    check(c.select(W2, WP2, why), "select"); c.service(p); tick(c, p, seq);
    { std::lock_guard<std::mutex> l(p.m); for (int i = 0; i < 4; ++i) p.at(wheel).script.push_back({sh::Call::Ok, sh::Call::Pending}); }
    tick(c, p, seq);
    check(c.view().state == sh::CandidateState::Open && !c.view().read.valid && !c.view().read.ended, "a pending read keeps the epoch");
    p.advance(600000); tick(c, p, seq);
    check(c.view().state == sh::CandidateState::Open, "pending within the limit");
    p.advance(600000); tick(c, p, seq);
    check(c.view().state == sh::CandidateState::Lost && c.view().read.ended && c.view().read.state == sh::Call::Pending,
          "pending past 1 s ends the epoch");
    c.service(p);

    // The worker's own refusals.
    { std::lock_guard<std::mutex> l(p.m); p.at(other).attached = false; }
    check(c.select(W3, WP3, why), "select a detached device"); c.service(p); tick(c, p, seq);
    check(c.view().state == sh::CandidateState::Refused && c.view().reason == "not attached", "a detached device is refused at the open");
    { std::lock_guard<std::mutex> l(p.m); p.at(other).attached = true; }
    check(c.select(W3, WP2, why), "select with the wrong product"); c.service(p); tick(c, p, seq);
    check(c.view().state == sh::CandidateState::Refused && c.view().reason.find("is product") != std::string::npos, "a product mismatch is refused");
    { std::lock_guard<std::mutex> l(p.m); p.at(other).opened = p.at(other).listed; p.at(other).opened.prod = WP2; }
    const int closes = p.at(other).closes;
    check(c.select(W3, WP3, why), "select a device whose identity changes"); c.service(p); tick(c, p, seq);
    check(c.view().state == sh::CandidateState::Refused && p.at(other).closes == closes + 1, "an identity change during the open is closed and refused");
    check(p.misuse == 0, "no read of a closed handle and no close during a read");
}

// ---------------------------------------------------------------- the C ABI over fake COM
std::filesystem::path scratch() {
    static const auto dir = std::filesystem::temp_directory_path() /
        ("dbce-shim-candidate-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(dir); return dir;
}
fakecom::DeviceScript script(const std::string& instance, const std::string& product, const wchar_t* name) {
    fakecom::DeviceScript d;
    check(sh::guid_parse(instance, d.instance) && sh::guid_parse(product, d.product), "fixture guids");
    d.name = name; d.buttons = 128; d.state.lX = 32768;
    return d;
}
struct Cand {
    int32_t r = 0;
    dbce_shim_snapshot_header h{};
    dbce_shim_snapshot_device d{};
    dbce_shim_snapshot_sample s{};
};
Cand take(void* handle, uint64_t seq) {
    Cand o;
    o.h.struct_size = sizeof o.h; o.h.device_struct_size = sizeof o.d; o.h.sample_struct_size = sizeof o.s;
    o.r = dbce_shim_candidate_snapshot(handle, seq, &o.h, &o.d, 1, &o.s, 1);
    return o;
}
std::string report(void* h) {
    std::vector<char> text(8192); int32_t needed = 0;
    check(dbce_shim_candidate_report(h, text.data(), int32_t(text.size()), &needed) == DBCE_SHIM_OK && needed > 0 && needed <= int32_t(text.size()), "report");
    return text.data();
}
int32_t snapshot3_devices(void* h, uint64_t seq) {
    dbce_shim_snapshot_header hdr{}; hdr.struct_size = sizeof hdr; hdr.device_struct_size = sizeof(dbce_shim_snapshot_device);
    hdr.sample_struct_size = sizeof(dbce_shim_snapshot_sample);
    dbce_shim_snapshot_device rows[4]{}; dbce_shim_snapshot_sample reads[4]{};
    check(dbce_shim_snapshot(h, seq, &hdr, rows, 4, reads, 4) == DBCE_SHIM_OK, "extension 3 still answers");
    return int32_t(hdr.device_count);
}
template <class Done> Cand tick_until(void* h, dbce_shim_frame& f, Done done, int limit, const std::string& what) {
    for (int n = 0; n < limit; ++n) {
        check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, what + ": tick");
        auto o = take(h, f.seq);
        check(o.r == DBCE_SHIM_OK, what + ": candidate snapshot");
        if (done(o)) return o;
        Sleep(5);
    }
    throw std::runtime_error(what + ": not reached in " + std::to_string(limit) + " ticks");
}
void* open_handle(fakecom::Input* in, const std::string& profile, const char* folder) {
    const auto dir = scratch() / folder; std::filesystem::create_directories(dir);
    char why[256] = {}; void* h = nullptr;
    check(dbce_shim_open_with_input(in, dir.wstring().c_str(), "candnonce1", profile.data(), int32_t(profile.size()), nullptr, nullptr, &h, why, 256) == DBCE_SHIM_OK && h,
          std::string("open: ") + why);
    return h;
}

void test_layout() {
    check(dbce_shim_candidate_abi_version() == 1 && dbce_shim_abi_version() == 2 && dbce_shim_frame_size() == 296 && dbce_shim_snapshot_abi_version() == 1,
          "extension 4 version 1; base ABI 2, its frame and extension 3 unchanged");
}

void test_unbound() {
    auto* in = new fakecom::Input;   // fake storage lives to process exit, like the host's own provider
    const auto slot = in->add(script(W, WP, L"MOZA R12 Base"));
    in->scripts[slot].interface_path = L"\\\\?\\hid#candidate";
    void* h = open_handle(in, Unbound, "unbound");
    dbce_shim_frame f{}; f.struct_size = sizeof f;
    for (int n = 0; n < 40; ++n) { check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, "unbound tick"); Sleep(5); }
    auto none = take(h, f.seq);
    check(none.r == DBCE_SHIM_OK && none.h.abi == 1 && none.h.frame_seq == f.seq && none.h.device_count == 0 && none.h.sample_count == 0,
          "no candidate: zero rows");
    check(in->enumerations == 0 && in->counters.creates == 0 && snapshot3_devices(h, f.seq) == 0,
          "an all-unbound profile lists and opens nothing until asked; extension 3 has no rows");
    check(take(h, f.seq - 1).r == DBCE_SHIM_STALE && take(h, f.seq + 1).r == DBCE_SHIM_STALE, "another frame is stale");

    check(dbce_shim_candidate_list(h) == DBCE_SHIM_OK, "list");
    std::string text;
    for (int n = 0; n < 400 && (text = report(h)).find("\"seq\":1") == std::string::npos; ++n) { dbce_shim_tick(h, &f); Sleep(5); }
    check(text.find("\"seq\":1,\"pending\":false") != std::string::npos && text.find("\"instance\":\"" + W + "\"") != std::string::npos &&
          text.find("\"name\":\"MOZA R12 Base\",\"bound\":false") != std::string::npos && text.find("\"state\":\"none\"") != std::string::npos,
          "one listing, the attached wheel unbound, no candidate: " + text);
    check(in->enumerations == 1 && in->counters.creates == 0, "listing opens nothing");

    char why[256] = {};
    check(dbce_shim_candidate_select(h, "{D71B8350-61B7-11F1-8001-444553540000}", WP.c_str(), why, 256) == DBCE_SHIM_BAD_ARGUMENT &&
          dbce_shim_candidate_select(h, nullptr, WP.c_str(), why, 256) == DBCE_SHIM_BAD_ARGUMENT, "malformed or missing GUID text");
    int32_t elsewhere = 0;
    std::thread([&] { elsewhere = dbce_shim_candidate_select(h, W.c_str(), WP.c_str(), nullptr, 0); }).join();
    check(elsewhere == DBCE_SHIM_WRONG_THREAD, "owner thread only");
    { std::lock_guard<std::mutex> l(in->m); auto& st = in->scripts[slot].state; st.lX = 11111; st.lRz = 22222; st.rgbButtons[9] = 0x80; st.rgdwPOV[0] = 27000; }
    check(dbce_shim_candidate_select(h, W.c_str(), WP.c_str(), why, 256) == DBCE_SHIM_OK && why[0] == 0, "select the wheel");
    const auto o = tick_until(h, f, [](const Cand& c) { return c.h.sample_count == 1 && c.s.valid; }, 400, "the candidate opens");
    check(o.h.device_count == 1 && o.d.struct_size == sizeof o.d && o.d.index == 0 && o.d.epoch == 1 &&
          o.d.flags == (DBCE_SHIM_SNAPSHOT_OPENED | DBCE_SHIM_SNAPSHOT_PATH) && W == o.d.instance && WP == o.d.product &&
          o.d.axis_mask == 0x25 && o.d.range_mask == 0x25 && o.d.pov_mask == 1 && o.d.maximum[0] == 65535, "the row: open, path, caps");
    check(o.s.index == 0 && o.s.epoch == 1 && o.s.stale == 0 && o.s.ended == 0 && o.s.poll_call == DBCE_SHIM_CALL_OK &&
          o.s.state_call == DBCE_SHIM_CALL_OK && o.s.axes[0] == 11111 && o.s.axes[5] == 22222 && o.s.buttons[9] == 0x80 && o.s.pov[0] == 27000 &&
          o.h.tick_us <= o.s.read_start_us && o.s.read_end_us == o.h.boundary_us && o.h.fresh_us == 100000, "the read: raw, fresh, this frame");
    check(f.sample_count == 0 && f.action[idx("steer")].status == std::uint8_t(sh::Status::Unbound) && f.action[idx("steer")].value == 0 &&
          snapshot3_devices(h, f.seq) == 0, "no Core sample, no action and no extension 3 row from the candidate");
    text = report(h);
    check(text.find("\"state\":\"open\"") != std::string::npos && text.find("\"interfacePath\":\"\\\\\\\\?\\\\hid#candidate\"") != std::string::npos &&
          text.find("\"epoch\":1,\"opened\":true") != std::string::npos, "the report shows the open candidate: " + text);
    const auto seq = o.s.device_seq;
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK && take(h, f.seq).s.device_seq == seq + 1, "one candidate read per tick");
    const int polls = in->counters.polls;
    for (int n = 0; n < 10; ++n) check(take(h, f.seq).r == DBCE_SHIM_OK, "repeated candidate snapshot");
    check(in->counters.polls == polls, "the getter makes no device call");

    check(dbce_shim_candidate_release(h) == DBCE_SHIM_OK, "release");
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, "a tick after the release");
    auto released = take(h, f.seq);
    check(released.r == DBCE_SHIM_OK && released.h.device_count == 0 && released.h.sample_count == 0, "released: no row, no read");
    check(dbce_shim_candidate_release(h) == DBCE_SHIM_OK, "a second release is harmless");

    // Unplugged: the read ends the epoch; the candidate is lost until selected again.
    check(dbce_shim_candidate_select(h, W.c_str(), WP.c_str(), why, 256) == DBCE_SHIM_OK, "select again");
    const auto again = tick_until(h, f, [](const Cand& c) { return c.h.sample_count == 1 && c.s.valid; }, 400, "reopened");
    check(again.d.epoch == 2, "a new open is a new epoch");
    { std::lock_guard<std::mutex> l(in->m); in->scripts[slot].reads.push_back({DIERR_UNPLUGGED, DI_OK}); in->attached[slot] = false; }
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, "the unplugged frame");
    const auto gone = take(h, f.seq);
    check(gone.h.device_count == 1 && gone.h.sample_count == 1 && gone.s.ended == 1 && gone.s.valid == 0 && gone.s.poll_call == DBCE_SHIM_CALL_GONE &&
          gone.d.flags == 0 && gone.d.epoch == 2, "unplugged: ended, no longer open");
    check(report(h).find("\"state\":\"lost\"") != std::string::npos, "the report says lost");
    { std::lock_guard<std::mutex> l(in->m); in->attached[slot] = true; }
    for (int n = 0; n < 40; ++n) { check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, "a lost frame"); Sleep(5); }
    check(take(h, f.seq).h.sample_count == 0 && report(h).find("\"state\":\"lost\"") != std::string::npos, "a lost candidate is never reopened by itself");

    // Stop with a candidate open: the existing provider shutdown releases it.
    check(dbce_shim_candidate_select(h, W.c_str(), WP.c_str(), why, 256) == DBCE_SHIM_OK, "select before the stop");
    (void)tick_until(h, f, [](const Cand& c) { return c.h.sample_count == 1 && c.s.valid; }, 400, "open before the stop");
    char stopped[512] = {};
    check(dbce_shim_stop(h, stopped, sizeof stopped, nullptr) == DBCE_SHIM_OK && std::string(stopped) == "resolved", std::string("the stop resolves: ") + stopped);
    check(take(h, f.seq).r == DBCE_SHIM_STOPPED && dbce_shim_candidate_list(h) == DBCE_SHIM_STOPPED, "a spent handle reports stopped");
    check(in->counters.force_calls == 0 && in->counters.after_release == 0 && in->refs == 0, "no force call, no use after release, DirectInput released");
}

void test_bound() {
    auto* in = new fakecom::Input;
    const auto wheel = in->add(script(W, WP, L"MOZA R12 Base")), pedals = in->add(script(W2, WP2, L"Pedals"));
    in->scripts[wheel].interface_path = L"\\\\?\\hid#wheel";
    void* h = open_handle(in, Bound, "bound");
    dbce_shim_frame f{}; f.struct_size = sizeof f;
    bool live = false;
    for (int n = 0; n < 400 && !live; ++n) { check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, "tick"); live = f.action[idx("steer")].status == uint8_t(sh::Status::Live); if (!live) Sleep(5); }
    check(live && f.sample_count == 1 && snapshot3_devices(h, f.seq) == 1, "the bound wheel is Live in Core and extension 3");
    char why[256] = {};
    check(dbce_shim_candidate_select(h, W.c_str(), WP.c_str(), why, 256) == DBCE_SHIM_REFUSED && std::string(why).find("binds") != std::string::npos,
          std::string("the bound wheel is refused: ") + why);
    check(dbce_shim_candidate_list(h) == DBCE_SHIM_OK, "list");
    std::string text;
    for (int n = 0; n < 400 && (text = report(h)).find("\"seq\":1") == std::string::npos; ++n) { dbce_shim_tick(h, &f); Sleep(5); }
    check(text.find("\"name\":\"MOZA R12 Base\",\"bound\":true") != std::string::npos && text.find("\"name\":\"Pedals\",\"bound\":false") != std::string::npos,
          "the listing marks the bound wheel: " + text);
    check(dbce_shim_candidate_select(h, W2.c_str(), WP2.c_str(), why, 256) == DBCE_SHIM_OK, "select the pedals");
    const auto o = tick_until(h, f, [](const Cand& c) { return c.h.sample_count == 1 && c.s.valid; }, 400, "the pedals open");
    check(W2 == o.d.instance && f.sample_count == 1 && f.action[idx("steer")].status == uint8_t(sh::Status::Live) && snapshot3_devices(h, f.seq) == 1,
          "the candidate beside a bound device: Core's frame and extension 3 keep exactly the profile's device");
    (void)pedals;
    char stopped[512] = {};
    check(dbce_shim_stop(h, stopped, sizeof stopped, nullptr) == DBCE_SHIM_OK && std::string(stopped) == "resolved", "the bound stop resolves");
    check(in->counters.force_calls == 0 && in->refs == 0, "no force call; DirectInput released");
}

#if !DBCE_SHIM_ORDINARY_ONLY
void test_refusals() {
    const auto dir = scratch() / "latched"; std::filesystem::create_directories(dir);
    const auto session = dir / "inject.on", batch = dir / "inject.txt";
    const auto expires = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() + 600;
    { std::ofstream s(session, std::ios::binary); s << "nonce=candnonce1\nexpires=" << expires << "\n"; }
    static std::wstring folder; folder = dir.wstring();
    dbce_shim_open_args a{}; a.struct_size = sizeof a; a.flags = DBCE_SHIM_LATCHED; a.folder = folder.c_str(); a.nonce = "candnonce1";
    a.profile = Bound.data(); a.profile_len = int32_t(Bound.size()); a.adapter_version = "candidate test adapter";
    const auto sw = session.wstring(), bw = batch.wstring();
    a.session_file = sw.c_str(); a.batch_file = bw.c_str(); a.fences_verified = 1; a.force_absent = 1;
    auto* in = new fakecom::Input; in->add(script(W, WP, L"MOZA R12 Base")); in->add(script(W2, WP2, L"Pedals"));
    char why[256] = {}; void* h = nullptr;
    check(dbce_shim_open2_with_input(in, &a, &h, why, 256) == DBCE_SHIM_OK && h, std::string("latched open: ") + why);
    dbce_shim_frame f{}; f.struct_size = sizeof f;
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK && f.latched, "a latched frame");
    check(dbce_shim_candidate_list(h) == DBCE_SHIM_REFUSED && dbce_shim_candidate_select(h, W2.c_str(), WP2.c_str(), why, 256) == DBCE_SHIM_REFUSED &&
          dbce_shim_candidate_release(h) == DBCE_SHIM_REFUSED && take(h, f.seq).r == DBCE_SHIM_REFUSED, "a latched handle refuses every candidate call");
    char stopped[512] = {};
    check(dbce_shim_stop(h, stopped, sizeof stopped, nullptr) == DBCE_SHIM_OK, std::string("the latched stop resolves: ") + stopped);

    const auto bdir = scratch() / "bridge"; std::filesystem::create_directories(bdir);
    static std::wstring bfolder; bfolder = bdir.wstring();
    dbce_shim_open_args b{}; b.struct_size = sizeof b; b.folder = bfolder.c_str(); b.nonce = "candnonce1";
    b.profile = Bound.data(); b.profile_len = int32_t(Bound.size()); b.adapter_version = "candidate test adapter"; h = nullptr;
    check(dbce_shim_bridge_open(&b, &h, why, 256) == DBCE_SHIM_OK && h, std::string("bridge open: ") + why);
    check(dbce_shim_candidate_list(h) == DBCE_SHIM_REFUSED && take(h, 1).r == DBCE_SHIM_REFUSED, "a bridge handle refuses");
    check(dbce_shim_stop(h, stopped, sizeof stopped, nullptr) == DBCE_SHIM_OK, "the bridge stop resolves");
}
#endif

void test_dll(const std::wstring& path) {
    HMODULE m = LoadLibraryW(path.c_str()); check(m != nullptr, "the built DLL loads");
    for (const char* name : {"dbce_shim_candidate_abi_version", "dbce_shim_candidate_list", "dbce_shim_candidate_select",
                             "dbce_shim_candidate_release", "dbce_shim_candidate_report", "dbce_shim_candidate_snapshot"})
        check(GetProcAddress(m, name) != nullptr, std::string("export ") + name);
    auto version = reinterpret_cast<int32_t (*)()>(reinterpret_cast<void (*)()>(GetProcAddress(m, "dbce_shim_candidate_abi_version")));
    check(version() == 1, "the DLL's extension 4 agrees with this build");
}
}

int main(int argc, char** argv) {
    try {
        test_pure(); test_layout(); test_unbound(); test_bound();
#if !DBCE_SHIM_ORDINARY_ONLY
        test_refusals();
#endif
        if (argc == 2) test_dll(std::filesystem::path(argv[1]).wstring());
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << "\n"; return 1; }
    std::error_code ec; std::filesystem::remove_all(scratch(), ec);
    std::cout << "shim candidate: " << checks << " checks passed\n";
    return 0;
}
