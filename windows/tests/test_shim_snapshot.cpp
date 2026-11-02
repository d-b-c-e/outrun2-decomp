// dbce_shim extension 3 (dbce_shim_snapshot.h) through the C ABI over fake COM devices: the layout, the copy of the
// last frame's own reads with zero added device calls, raw values against the fake's script, Core's caps and identity,
// STALE and all-or-nothing buffers, a pending read, a pending read past Core's limit that ends its epoch, an unplug, the
// reopens with new epochs, Core's stale verdict, an extras handle, and the refusals (latched, bridge). With a path argument
// it also loads the built DLL and checks the three exports. No device, game or force.
// Built for x86 (DBCE_SHIM_ORDINARY_ONLY) it runs the ordinary and extras cases; the latched, capture and bridge cases
// are x64-only, as their opens are.
#include "dbce_shim_internal.hpp"
#include "dbce_shim_bridge.h"
#include "dbce_shim_extras.h"
#include "dbce_shim_snapshot.h"
#include "ordinary_only.hpp"
#include "dinput_provider.hpp"
#include "shim_fake_com.hpp"
#include <chrono>
#include <cstddef>
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
int checks = 0;
void check(bool v, const std::string& what) { ++checks; if (!v) throw std::runtime_error(what); }
const std::string W = "{d71b8350-61b7-11f1-8001-444553540000}", WP = "{0006346e-0000-0000-0000-504944564944}";
const std::string W2 = "{d71b8351-61b7-11f1-8001-444553540000}", WP2 = "{0006346f-0000-0000-0000-504944564944}";
const std::string Profile = "[Controls]\nSchema = 1\nProfile = Rig\nRevision = r1\nTransmission = Sequential\n"
    "steer = axis 0 dev=" + W + " prod=" + WP + " range=0..65535 rest=32768 travel=+1\n"
    "throttle = axis 2 dev=" + W + " prod=" + WP + " range=0..65535 rest=0 travel=+1\n"
    "handbrake = button 3 dev=" + W + " prod=" + WP + "\n";
int idx(const char* id) { for (int i = 0; i < DBCE_SHIM_ACTIONS; ++i) if (std::strcmp(dbce_shim_action_name(i), id) == 0) return i; return -1; }
// PID plus the start tick: a PID alone can be reused by a later run.
std::filesystem::path scratch() {
    static const auto dir = std::filesystem::temp_directory_path() /
        ("dbce-shim-snapshot-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(dir); return dir;
}
fakecom::DeviceScript script(const std::string& instance, const std::string& product, const wchar_t* name) {
    fakecom::DeviceScript d;
    check(dbce::shim::guid_parse(instance, d.instance) && dbce::shim::guid_parse(product, d.product), "fixture guids");
    d.name = name; d.buttons = 128; d.state.lX = 32768;
    return d;
}
fakecom::Input* wheel_input(std::size_t& slot) {
    auto* in = new fakecom::Input;   // fake storage lives to process exit, like the host's own provider
    slot = in->add(script(W, WP, L"MOZA R12 Base")); return in;
}

constexpr std::uint8_t Fill = 0xAB;
struct Snap {
    int32_t r = 0;
    dbce_shim_snapshot_header h{};
    std::vector<dbce_shim_snapshot_device> d;
    std::vector<dbce_shim_snapshot_sample> s;
};
// Every output byte starts as Fill, so "nothing written" is checkable.
Snap take(void* handle, uint64_t seq, int32_t devices = 4, int32_t samples = 4) {
    Snap o;
    std::memset(&o.h, Fill, sizeof o.h);
    o.h.struct_size = sizeof o.h; o.h.device_struct_size = sizeof(dbce_shim_snapshot_device); o.h.sample_struct_size = sizeof(dbce_shim_snapshot_sample);
    o.d.resize(std::size_t(devices)); o.s.resize(std::size_t(samples));
    if (!o.d.empty()) std::memset(o.d.data(), Fill, o.d.size() * sizeof o.d[0]);
    if (!o.s.empty()) std::memset(o.s.data(), Fill, o.s.size() * sizeof o.s[0]);
    o.r = dbce_shim_snapshot(handle, seq, &o.h, o.d.empty() ? nullptr : o.d.data(), devices, o.s.empty() ? nullptr : o.s.data(), samples);
    return o;
}
bool filled(const void* p, std::size_t n) { const auto* b = static_cast<const std::uint8_t*>(p); for (std::size_t i = 0; i < n; ++i) if (b[i] != Fill) return false; return true; }
bool header_untouched(const Snap& o) { return filled(&o.h.frame_seq, sizeof o.h - offsetof(dbce_shim_snapshot_header, frame_seq)) && o.h.abi == 0xABABABABu; }
bool arrays_untouched(const Snap& o) {
    return (o.d.empty() || filled(o.d.data(), o.d.size() * sizeof o.d[0])) && (o.s.empty() || filled(o.s.data(), o.s.size() * sizeof o.s[0]));
}
const dbce_shim_snapshot_device* row_of(const Snap& o, const std::string& instance) {
    for (std::uint32_t i = 0; i < o.h.device_count; ++i) if (instance == o.d[i].instance) return &o.d[i];
    return nullptr;
}
const dbce_shim_snapshot_sample* read_of(const Snap& o, std::uint32_t index) {
    for (std::uint32_t i = 0; i < o.h.sample_count; ++i) if (o.s[i].index == index) return &o.s[i];
    return nullptr;
}
bool neutral(const dbce_shim_snapshot_sample& s) {
    for (int a = 0; a < 8; ++a) if (s.axes[a] != 0) return false;
    for (int b = 0; b < 128; ++b) if (s.buttons[b] != 0) return false;
    for (int p = 0; p < 4; ++p) if (s.pov[p] != 0xFFFFFFFFu) return false;
    return true;
}
// Ticks until `done` holds for the frame's snapshot, at most `limit` ticks.
template <class Done> Snap tick_until(void* h, dbce_shim_frame& f, Done done, int limit, const std::string& what) {
    for (int n = 0; n < limit; ++n) {
        check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, what + ": tick");
        auto o = take(h, f.seq);
        check(o.r == DBCE_SHIM_OK, what + ": snapshot");
        if (done(o)) return o;
        Sleep(5);
    }
    throw std::runtime_error(what + ": not reached in " + std::to_string(limit) + " ticks");
}

void test_layout() {
    int32_t hs = 0, ds = 0, ss = 0;
    check(dbce_shim_snapshot_abi_version() == 1 && dbce_shim_snapshot_sizes(&hs, &ds, &ss) == DBCE_SHIM_OK && hs == 56 && ds == 192 && ss == 224 &&
          sizeof(dbce_shim_snapshot_header) == 56 && sizeof(dbce_shim_snapshot_device) == 192 && sizeof(dbce_shim_snapshot_sample) == 224,
          "extension 3 version and sizes (no pointers: the same on x86 and x64)");
    check(dbce_shim_snapshot_sizes(nullptr, nullptr, nullptr) == DBCE_SHIM_OK, "the size query takes null pointers");
    check(dbce_shim_abi_version() == 2 && dbce_shim_frame_size() == 296, "base ABI 2 and its frame unchanged");
}

void test_ordinary() {
    const auto dir = scratch() / "ordinary"; std::filesystem::create_directories(dir);
    std::size_t slot = 0; auto* in = wheel_input(slot); in->scripts[slot].interface_path = L"\\\\?\\hid#snapshot";
    char why[256] = {}; void* h = nullptr;
    check(dbce_shim_open_with_input(in, dir.wstring().c_str(), "snapnonce1", Profile.data(), int32_t(Profile.size()), nullptr, nullptr, &h, why, 256) == DBCE_SHIM_OK && h,
          std::string("open: ") + why);
    auto none = take(h, 0);
    check(none.r == DBCE_SHIM_STALE && header_untouched(none) && arrays_untouched(none), "before the first tick: stale, nothing written");
    none = take(h, 1);
    check(none.r == DBCE_SHIM_STALE && header_untouched(none) && arrays_untouched(none), "no frame 1 yet either");
    dbce_shim_frame f{}; f.struct_size = sizeof f;
    bool live = false;
    for (int n = 0; n < 400 && !live; ++n) { check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, "tick"); live = f.action[idx("steer")].status == uint8_t(dbce::shim::Status::Live); if (!live) Sleep(5); }
    check(live && f.sample_count == 1, "the wheel opens and steer goes Live");

    { std::lock_guard<std::mutex> l(in->m); auto& st = in->scripts[slot].state; st.lX = 12345; st.lZ = 54321; st.rgbButtons[3] = 0x80; st.rgdwPOV[0] = 9000; }
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, "the frame to copy");
    const int polls = in->counters.polls, states = in->counters.state_reads, creates = in->counters.creates, paths = in->counters.path_reads;
    const int listings = in->enumerations, acquires = in->counters.acquires;
    const auto a = take(h, f.seq), b = take(h, f.seq);
    for (int n = 0; n < 20; ++n) check(take(h, f.seq).r == DBCE_SHIM_OK, "a repeated snapshot");
    check(in->counters.polls == polls && in->counters.state_reads == states && in->counters.creates == creates && in->counters.path_reads == paths &&
          in->enumerations == listings && in->counters.acquires == acquires && in->counters.force_calls == 0,
          "22 snapshots: no poll, state read, create, path read, listing, acquire or force call");
    check(a.r == DBCE_SHIM_OK && b.r == DBCE_SHIM_OK && std::memcmp(&a.h, &b.h, sizeof a.h) == 0 &&
          std::memcmp(a.d.data(), b.d.data(), a.d.size() * sizeof a.d[0]) == 0 && std::memcmp(a.s.data(), b.s.data(), a.s.size() * sizeof a.s[0]) == 0,
          "the same seq is the same observation, byte for byte");
    check(a.h.abi == 1 && a.h.frame_seq == f.seq && a.h.device_count == 1 && a.h.sample_count == f.sample_count && a.h.fresh_us == 100000 &&
          a.h.tick_us <= a.h.boundary_us, "the header names its frame, the table, the reads and the freshness bound");
    const auto& row = a.d[0];
    check(row.struct_size == sizeof row && row.abi == 1 && row.index == 0 && row.epoch >= 1 &&
          row.flags == (DBCE_SHIM_SNAPSHOT_OPENED | DBCE_SHIM_SNAPSHOT_PATH) && W == row.instance && WP == row.product,
          "the device row: open, a path, the profile's lower-case braced identity");
    check(row.axis_mask == 0x25 && row.range_mask == 0x25 && row.pov_mask == 1 && row.minimum[0] == 0 && row.maximum[0] == 65535 &&
          row.minimum[5] == 0 && row.maximum[5] == 65535 && row.maximum[1] == 0, "Core's caps: X, Z and Rz with their read-back ranges, one POV");
    bool all_buttons = true; for (int i = 0; i < 16; ++i) all_buttons = all_buttons && row.button_mask[i] == 0xFF;
    check(all_buttons, "all 128 buttons present");
    check(filled(&a.d[1], 3 * sizeof a.d[0]) && filled(&a.s[1], 3 * sizeof a.s[0]), "only the counted elements are written");
    const auto& s = a.s[0];
    check(s.struct_size == sizeof s && s.abi == 1 && s.index == 0 && s.epoch == row.epoch && s.valid == 1 && s.stale == 0 && s.ended == 0 &&
          s.poll_call == DBCE_SHIM_CALL_OK && s.state_call == DBCE_SHIM_CALL_OK && s.read_start_us <= s.read_end_us &&
          s.reserved[0] == 0 && s.reserved[1] == 0 && s.reserved[2] == 0, "the read: complete, fresh, in its epoch");
    check(s.axes[0] == 12345 && s.axes[2] == 54321 && s.buttons[3] == 0x80 && s.buttons[4] == 0 && s.pov[0] == 9000 && s.pov[1] == 0xFFFFFFFFu,
          "raw values exactly as the device returned them (button bytes, POV hundredths)");
    check(f.action[idx("throttle")].value > 0.8f && f.action[idx("throttle")].value < 0.9f, "the frame keeps the normalized value; the snapshot the raw one");

    const auto seq = f.seq, device_seq = s.device_seq;
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, "the next frame");
    auto old = take(h, seq);
    check(old.r == DBCE_SHIM_STALE && header_untouched(old) && arrays_untouched(old), "an earlier frame is stale, nothing written");
    old = take(h, f.seq + 1);
    check(old.r == DBCE_SHIM_STALE && header_untouched(old), "a future frame is stale");
    const auto next = take(h, f.seq);
    check(next.r == DBCE_SHIM_OK && next.s[0].device_seq == device_seq + 1 && next.s[0].epoch == s.epoch, "one new read: device_seq advances by one");

    // Ticks refused before Core ticks change nothing: the prior successful frame's snapshot stays, byte for byte.
    {
        const int reads_before = in->counters.polls;
        dbce_shim_frame shortened{}; shortened.struct_size = sizeof shortened - 8;
        dbce_shim_extras_frame xf{}; xf.struct_size = sizeof xf;
        int32_t elsewhere = 0;
        std::thread([&] { dbce_shim_frame g{}; g.struct_size = sizeof g; elsewhere = dbce_shim_tick(h, &g); }).join();
        check(dbce_shim_tick(h, nullptr) == DBCE_SHIM_BAD_ARGUMENT && dbce_shim_tick(h, &shortened) == DBCE_SHIM_BAD_ARGUMENT &&
              dbce_shim_tick_extras(h, 0, &f, &xf) == DBCE_SHIM_BAD_ARGUMENT && elsewhere == DBCE_SHIM_WRONG_THREAD,
              "a null frame, a short frame, the extras tick on an ordinary handle and another thread are refused");
        const auto same = take(h, f.seq);
        check(same.r == DBCE_SHIM_OK && std::memcmp(&same.h, &next.h, sizeof same.h) == 0 &&
              std::memcmp(same.d.data(), next.d.data(), same.d.size() * sizeof same.d[0]) == 0 &&
              std::memcmp(same.s.data(), next.s.data(), same.s.size() * sizeof same.s[0]) == 0 && in->counters.polls == reads_before,
              "after refused ticks the prior frame's snapshot is byte-identical and no device was read");
    }

    // Arguments and buffers.
    dbce_shim_snapshot_header hdr{}; hdr.struct_size = sizeof hdr; hdr.device_struct_size = sizeof(dbce_shim_snapshot_device); hdr.sample_struct_size = sizeof(dbce_shim_snapshot_sample);
    dbce_shim_snapshot_device dev{}; dbce_shim_snapshot_sample smp{};
    check(dbce_shim_snapshot(h, f.seq, nullptr, &dev, 1, &smp, 1) == DBCE_SHIM_BAD_ARGUMENT, "a null header refuses");
    for (int which = 0; which < 3; ++which) {
        auto wrong = hdr; (which == 0 ? wrong.struct_size : which == 1 ? wrong.device_struct_size : wrong.sample_struct_size) -= 8;
        check(dbce_shim_snapshot(h, f.seq, &wrong, &dev, 1, &smp, 1) == DBCE_SHIM_BAD_ARGUMENT, "a wrong struct size refuses (" + std::to_string(which) + ")");
    }
    check(dbce_shim_snapshot(h, f.seq, &hdr, nullptr, 1, &smp, 1) == DBCE_SHIM_BAD_ARGUMENT && dbce_shim_snapshot(h, f.seq, &hdr, &dev, 1, nullptr, 1) == DBCE_SHIM_BAD_ARGUMENT,
          "a null array with a capacity refuses");
    check(dbce_shim_snapshot(h, f.seq, &hdr, &dev, -1, &smp, 1) == DBCE_SHIM_BAD_ARGUMENT && dbce_shim_snapshot(h, f.seq, &hdr, &dev, 1, &smp, -1) == DBCE_SHIM_BAD_ARGUMENT,
          "a negative capacity refuses");
    for (int which = 0; which < 3; ++which) {
        const auto o = take(h, f.seq, which == 1 ? 1 : 0, which == 0 ? 1 : 0);
        check(o.r == DBCE_SHIM_BAD_ARGUMENT && o.h.abi == 1 && o.h.frame_seq == f.seq && o.h.device_count == 1 && o.h.sample_count == 1 && arrays_untouched(o),
              "a short capacity: the counts are given, no array is written (" + std::to_string(which) + ")");
    }
    int32_t other = 0; std::thread([&] { other = take(h, f.seq).r; }).join();
    check(other == DBCE_SHIM_WRONG_THREAD, "another thread refuses");

    // A pending read: no values, the epoch stays open. The fake writes its state even on failure; none of it is exported.
    { std::lock_guard<std::mutex> l(in->m); in->scripts[slot].fill_on_failure = true; in->scripts[slot].reads.push_back({DI_OK, E_PENDING}); }
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, "the pending frame");
    auto p = take(h, f.seq);
    check(p.r == DBCE_SHIM_OK && p.h.sample_count == 1 && p.s[0].valid == 0 && p.s[0].poll_call == DBCE_SHIM_CALL_OK && p.s[0].state_call == DBCE_SHIM_CALL_PENDING &&
          p.s[0].ended == 0 && (p.d[0].flags & DBCE_SHIM_SNAPSHOT_OPENED) && p.d[0].epoch == s.epoch && neutral(p.s[0]),
          "pending: invalid, still open, initial values only (no failed-read bytes)");
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, "after the pending read");
    p = take(h, f.seq);
    check(p.r == DBCE_SHIM_OK && p.s[0].valid == 1 && p.s[0].epoch == s.epoch && p.s[0].axes[0] == 12345, "the next read is complete again, same epoch");

    // Pending past Core's limit (1 s): the epoch ends while both calls still report what they read (Pending).
    { std::lock_guard<std::mutex> l(in->m); for (int n = 0; n < 4000; ++n) in->scripts[slot].reads.push_back({DI_OK, E_PENDING}); }
    const auto end = tick_until(h, f, [](const Snap& o) { return o.h.sample_count == 1 && o.s[0].ended == 1; }, 1200, "pending timeout");
    { std::lock_guard<std::mutex> l(in->m); in->scripts[slot].reads.clear(); }
    const auto ended_epoch = end.s[0].epoch;
    check(end.s[0].valid == 0 && end.s[0].state_call == DBCE_SHIM_CALL_PENDING && end.d[0].epoch == ended_epoch && end.d[0].flags == 0 &&
          end.d[0].axis_mask == 0 && end.d[0].maximum[0] == 0 && W == end.d[0].instance && neutral(end.s[0]),
          "the timeout read is ended by the table (closed, caps zero), though its call says Pending");
    const auto reopened = tick_until(h, f, [&](const Snap& o) { return o.h.sample_count == 1 && o.s[0].valid == 1 && o.s[0].epoch > ended_epoch; }, 800,
                                     "reacquired after the timeout");
    check((reopened.d[0].flags & DBCE_SHIM_SNAPSHOT_OPENED) && reopened.d[0].epoch == reopened.s[0].epoch && reopened.s[0].ended == 0 &&
          reopened.s[0].device_seq > end.s[0].device_seq && reopened.d[0].axis_mask == 0x25, "a new epoch: open, caps again, device_seq still rising");

    // Unplugged: the poll classifies Gone, the state read is never made, the epoch ends. Detached, so the worker cannot
    // reopen it before the next frame; attached again, a later listing reopens it.
    { std::lock_guard<std::mutex> l(in->m); in->scripts[slot].reads.push_back({DIERR_UNPLUGGED, DI_OK}); in->attached[slot] = false; }
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, "the unplugged frame");
    const auto gone = take(h, f.seq);
    check(gone.r == DBCE_SHIM_OK && gone.s[0].valid == 0 && gone.s[0].poll_call == DBCE_SHIM_CALL_GONE && gone.s[0].state_call == DBCE_SHIM_CALL_NOT_CALLED &&
          gone.s[0].ended == 1 && gone.d[0].flags == 0 && gone.d[0].epoch == gone.s[0].epoch, "unplugged: ended, no path, no caps");
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, "a frame with the device closed");
    const auto closed = take(h, f.seq);
    check(closed.r == DBCE_SHIM_OK && closed.h.device_count == 1 && closed.h.sample_count == 0 && closed.d[0].flags == 0 && filled(closed.s.data(), sizeof closed.s[0]),
          "a closed device: its row stays, no read");
    { std::lock_guard<std::mutex> l(in->m); in->attached[slot] = true; }
    const auto again = tick_until(h, f, [&](const Snap& o) { return o.h.sample_count == 1 && o.s[0].valid == 1; }, 800, "reopened after the unplug");
    check(again.s[0].epoch > gone.s[0].epoch && (again.d[0].flags & DBCE_SHIM_SNAPSHOT_PATH), "the reopen is a new epoch with its path");

    char stopped[512] = {};
    check(dbce_shim_stop(h, stopped, sizeof stopped, nullptr) == DBCE_SHIM_OK && std::string(stopped) == "resolved", "the ordinary stop resolves");
    check(take(h, f.seq).r == DBCE_SHIM_STOPPED, "a spent handle reports stopped");
    check(in->counters.force_calls == 0 && in->counters.after_release == 0 && in->refs == 0, "no force call, no use after release, DirectInput released");
}

// Two devices: the table order, a range Core could not request, and Core's stale verdict passed through (the first read
// is older than the 100 ms bound by the time the second, slow read ends).
void test_two_devices() {
    const auto dir = scratch() / "two"; std::filesystem::create_directories(dir);
    const std::string profile = "[Controls]\nSchema = 1\nProfile = Rig\nRevision = r1\nTransmission = Sequential\n"
        "steer = axis 0 dev=" + W + " prod=" + WP + " range=0..65535 rest=32768 travel=+1\n"
        "throttle = axis 2 dev=" + W2 + " prod=" + WP2 + " range=0..65535 rest=0 travel=+1\n";
    auto* in = new fakecom::Input;
    in->add(script(W, WP, L"MOZA R12 Base"));
    auto pedals = script(W2, WP2, L"Fake Pedals"); pedals.accept_range = false; pedals.native_min[0] = -512; pedals.native_max[0] = 511;
    pedals.range_unreadable[5] = true; pedals.buttons = 2; pedals.povs = 0;
    const auto second = in->add(pedals);
    char why[256] = {}; void* h = nullptr;
    check(dbce_shim_open_with_input(in, dir.wstring().c_str(), "snapnonce2", profile.data(), int32_t(profile.size()), nullptr, nullptr, &h, why, 256) == DBCE_SHIM_OK && h,
          std::string("two-device open: ") + why);
    dbce_shim_frame f{}; f.struct_size = sizeof f;
    const auto both = tick_until(h, f, [](const Snap& o) { return o.h.sample_count == 2 && o.s[0].valid && o.s[1].valid; }, 400, "both devices open");
    check(both.h.device_count == 2 && both.d[0].index == 0 && both.d[1].index == 1 && W == both.d[0].instance && W2 == both.d[1].instance &&
          both.s[0].index == 0 && both.s[1].index == 1, "the profile's planning order: rows and reads by index");
    const auto* p = row_of(both, W2);
    check(p && p->range_mask == 0x05 && p->axis_mask == 0x25 && p->minimum[0] == -512 && p->maximum[0] == 511 && p->minimum[2] == 0 &&
          p->maximum[2] == 65535 && p->minimum[5] == 0 && p->maximum[5] == 0 && p->button_mask[0] == 0x03 && p->button_mask[1] == 0 &&
          p->pov_mask == 0 && (p->flags & DBCE_SHIM_SNAPSHOT_PATH) == 0,
          "Core's read-back ranges: the request refused, X stays -512..511 and Z 0..65535, Rz unreadable (no range); no path");
    { std::lock_guard<std::mutex> l(in->m); in->scripts[second].state_delay_ms = 150; }
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK, "a frame with a slow second read");
    { std::lock_guard<std::mutex> l(in->m); in->scripts[second].state_delay_ms = 0; }
    const auto slow = take(h, f.seq);
    const auto* wheel = read_of(slow, 0); const auto* pedal = read_of(slow, 1);
    check(slow.r == DBCE_SHIM_OK && wheel && pedal && wheel->valid == 1 && wheel->stale == 1 && pedal->valid == 1 && pedal->stale == 0 &&
          slow.h.boundary_us - wheel->read_end_us > slow.h.fresh_us && f.action[idx("steer")].status == uint8_t(dbce::shim::Status::Stale),
          "Core's stale verdict at boundary_us, matching the frame's Stale steer");
    char stopped[512] = {};
    check(dbce_shim_stop(h, stopped, sizeof stopped, nullptr) == DBCE_SHIM_OK, "the two-device stop resolves");
    check(in->counters.force_calls == 0 && in->refs == 0, "no force call; DirectInput released");
}

// An extras handle is ordinary input: the snapshot of the frame dbce_shim_tick_extras returned.
void test_extras_handle() {
    const auto dir = scratch() / "extras"; std::filesystem::create_directories(dir);
    const std::string decl = "game test\ntest.boost digital\n";
    char hash[65] = {};
    { const auto digest = dbce::shim::hex(dbce::shim::sha256(decl)); std::memcpy(hash, digest.data(), 64); }
    const std::string profile = Profile + "\n[Extras]\nSchema = 1\nGame = test\nDeclaration = " + hash + "\ntest.boost = button 5 dev=" + W + " prod=" + WP + "\n";
    std::size_t slot = 0; auto* in = wheel_input(slot); char why[256] = {}; void* h = nullptr;
    check(dbce_shim_open_extras_with_input(in, dir.wstring().c_str(), "snapnonce3", profile.data(), int32_t(profile.size()), nullptr, nullptr,
                                           decl.data(), int32_t(decl.size()), &h, why, 256) == DBCE_SHIM_OK && h, std::string("extras open: ") + why);
    dbce_shim_frame f{}; f.struct_size = sizeof f; dbce_shim_extras_frame x{}; x.struct_size = sizeof x;
    bool live = false;
    for (int n = 0; n < 400 && !live; ++n) { check(dbce_shim_tick_extras(h, 0, &f, &x) == DBCE_SHIM_OK, "extras tick"); live = f.sample_count == 1 && f.action[idx("steer")].status == uint8_t(dbce::shim::Status::Live); if (!live) Sleep(5); }
    { std::lock_guard<std::mutex> l(in->m); in->scripts[slot].state.rgbButtons[5] = 0x80; }
    check(live && dbce_shim_tick_extras(h, 0, &f, &x) == DBCE_SHIM_OK, "an extras frame");
    const auto o = take(h, f.seq);
    check(o.r == DBCE_SHIM_OK && o.h.frame_seq == x.seq && o.h.sample_count == 1 && o.s[0].buttons[5] == 0x80, "the snapshot of the extras tick's own reads");
    const int reads_before = in->counters.polls;
    check(dbce_shim_tick_extras(h, 2, &f, &x) == DBCE_SHIM_BAD_ARGUMENT && dbce_shim_tick(h, &f) == DBCE_SHIM_BAD_ARGUMENT,
          "a refused context mask and the plain tick on an extras handle are refused before Core ticks");
    const auto kept = take(h, f.seq);
    check(kept.r == DBCE_SHIM_OK && std::memcmp(&kept.h, &o.h, sizeof o.h) == 0 && std::memcmp(kept.d.data(), o.d.data(), o.d.size() * sizeof o.d[0]) == 0 &&
          std::memcmp(kept.s.data(), o.s.data(), o.s.size() * sizeof o.s[0]) == 0 && in->counters.polls == reads_before,
          "the extras handle's prior snapshot stays byte-identical, no device read");
    char stopped[512] = {};
    check(dbce_shim_stop(h, stopped, sizeof stopped, nullptr) == DBCE_SHIM_OK, "the extras stop resolves");
    check(in->counters.force_calls == 0 && in->refs == 0, "no force call; DirectInput released");
}

#if !DBCE_SHIM_ORDINARY_ONLY
dbce_shim_open_args args_for(const std::wstring& folder, uint32_t flags) {
    static std::wstring held; held = folder;
    dbce_shim_open_args a{}; a.struct_size = sizeof a; a.flags = flags; a.folder = held.c_str(); a.nonce = "snapnonce1";
    a.profile = Profile.data(); a.profile_len = int32_t(Profile.size()); a.adapter_version = "snapshot test adapter";
    return a;
}

// CAPTURE without LATCHED is ordinary input: the snapshot reads, and the frame's capture row is still its caller's to record.
void test_capture_handle() {
    const auto dir = scratch() / "capture"; std::filesystem::create_directories(dir);
    auto a = args_for(dir.wstring(), DBCE_SHIM_CAPTURE); const auto cap = (dir / "capture").wstring();
    a.capture_folder = cap.c_str(); a.game_id = "dbce-shim-snapshot"; a.game_channels = "speed\n"; a.context = "snapshot test"; a.hz = 60; a.seconds = 30;
    std::size_t slot = 0; auto* in = wheel_input(slot); char why[256] = {}; void* h = nullptr;
    check(dbce_shim_open2_with_input(in, &a, &h, why, 256) == DBCE_SHIM_OK && h, std::string("capture open: ") + why);
    dbce_shim_frame f{}; f.struct_size = sizeof f;
    const auto o = tick_until(h, f, [](const Snap& s) { return s.h.sample_count == 1 && s.s[0].valid; }, 400, "capture handle opens");
    const double v[1] = {12.5};
    check(o.h.frame_seq == f.seq && dbce_shim_record(h, f.seq, v, 1) == DBCE_SHIM_OK && dbce_shim_record(h, f.seq, v, 1) == DBCE_SHIM_STALE,
          "the snapshot leaves the pending capture row to dbce_shim_record");
    char stopped[512] = {};
    check(dbce_shim_stop(h, stopped, sizeof stopped, nullptr) == DBCE_SHIM_OK && std::string(stopped).find("capture complete") != std::string::npos,
          std::string("the capture completes: ") + stopped);
}

// LATCHED (injection, test sessions) and the copied-input bridge (its own reader) refuse, even for their current frame.
void test_refusals() {
    const auto dir = scratch() / "latched"; std::filesystem::create_directories(dir);
    const auto session = dir / "inject.on", batch = dir / "inject.txt";
    const auto expires = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() + 600;
    { std::ofstream s(session, std::ios::binary); s << "nonce=snapnonce1\nexpires=" << expires << "\n"; }
    auto a = args_for(dir.wstring(), DBCE_SHIM_LATCHED);
    const auto sw = session.wstring(), bw = batch.wstring();
    a.session_file = sw.c_str(); a.batch_file = bw.c_str(); a.fences_verified = 1; a.force_absent = 1;
    std::size_t slot = 0; auto* in = wheel_input(slot); char why[256] = {}; void* h = nullptr;
    check(dbce_shim_open2_with_input(in, &a, &h, why, 256) == DBCE_SHIM_OK && h, std::string("latched open: ") + why);
    dbce_shim_frame f{}; f.struct_size = sizeof f;
    check(dbce_shim_tick(h, &f) == DBCE_SHIM_OK && f.latched, "a latched frame");
    auto o = take(h, f.seq);
    check(o.r == DBCE_SHIM_REFUSED && header_untouched(o) && arrays_untouched(o), "a latched handle refuses, nothing written");
    char stopped[512] = {};
    check(dbce_shim_stop(h, stopped, sizeof stopped, nullptr) == DBCE_SHIM_OK, std::string("the latched stop resolves: ") + stopped);

    const auto bdir = scratch() / "bridge"; std::filesystem::create_directories(bdir);
    auto b = args_for(bdir.wstring(), 0); h = nullptr;
    check(dbce_shim_bridge_open(&b, &h, why, 256) == DBCE_SHIM_OK && h, std::string("bridge open: ") + why);
    dbce_shim_bridge_device_v1 d{}; d.struct_size = sizeof d; d.abi = 1; d.generation = 1; d.axis_mask = d.range_mask = 1; d.maximum[0] = 65535;
    GUID wg{}, pg{}; check(dbce::shim::guid_parse(W, wg) && dbce::shim::guid_parse(WP, pg), "bridge guids");
    std::memcpy(d.instance, &wg, 16); std::memcpy(d.product, &pg, 16);
    check(dbce_shim_bridge_devices(h, &d, 1) == DBCE_SHIM_OK, "bridge descriptors");
    check(dbce_shim_bridge_tick(h, nullptr, 0, &f) == DBCE_SHIM_OK, "a bridge frame");
    o = take(h, f.seq);
    check(o.r == DBCE_SHIM_REFUSED && header_untouched(o) && arrays_untouched(o), "a bridge handle refuses, nothing written");
    check(dbce_shim_stop(h, stopped, sizeof stopped, nullptr) == DBCE_SHIM_OK, "the bridge stop resolves");
}
#endif

// The three exports by their plain names, and their answers.
void test_dll(const std::wstring& path) {
    HMODULE m = LoadLibraryW(path.c_str()); check(m != nullptr, "the built DLL loads");
    for (const char* name : {"dbce_shim_snapshot_abi_version", "dbce_shim_snapshot_sizes", "dbce_shim_snapshot"})
        check(GetProcAddress(m, name) != nullptr, std::string("export ") + name);
    auto proc = [&](const char* n) { return reinterpret_cast<void (*)()>(GetProcAddress(m, n)); };   // the usual FARPROC idiom
    auto version = reinterpret_cast<int32_t (*)()>(proc("dbce_shim_snapshot_abi_version"));
    auto sizes = reinterpret_cast<int32_t (*)(int32_t*, int32_t*, int32_t*)>(proc("dbce_shim_snapshot_sizes"));
    int32_t hs = 0, ds = 0, ss = 0;
    check(version() == 1 && sizes(&hs, &ds, &ss) == DBCE_SHIM_OK && hs == 56 && ds == 192 && ss == 224, "the DLL's extension 3 agrees with this build");
}
}

int main(int argc, char** argv) {
    try {
        test_layout(); test_ordinary(); test_two_devices(); test_extras_handle();
#if !DBCE_SHIM_ORDINARY_ONLY
        test_capture_handle(); test_refusals();
#endif
        if (argc == 2) test_dll(std::filesystem::path(argv[1]).wstring());
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << "\n"; return 1; }
    std::error_code ec; std::filesystem::remove_all(scratch(), ec);
    std::cout << "shim snapshot: " << checks << " checks passed\n";
    return 0;
}
