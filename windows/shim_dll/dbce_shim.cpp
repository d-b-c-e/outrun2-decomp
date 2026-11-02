// dbce_shim.h's implementation: the reviewed ProviderHost (windows/host) over the reviewed Core and DInputProvider
// (windows/shim), with the reviewed control worker, journal and shared frame recorder, behind the C ABI. This file only
// validates, converts, pairs capture rows with their frames and owns handles.
// The x86 build (DBCE_SHIM_ORDINARY_ONLY, shim/ordinary_only.hpp) compiles the ordinary path only: the journal, the
// recorder and the copied-input bridge's open are left out, and their entries refuse.
#ifndef DBCE_SHIM_BUILD
#define DBCE_SHIM_BUILD
#endif
#include "dbce_shim_internal.hpp"
#include "legacy_frame.hpp"
#include "dbce_shim_bridge.h"
#include "dbce_shim_extras.h"
#include "dbce_shim_snapshot.h"
#include "dbce_shim_candidate.h"
#include "bridge_provider.hpp"
#include "json_text.hpp"
#include "ordinary_only.hpp"
#if !DBCE_SHIM_ORDINARY_ONLY
#include "control_journal.hpp"
#include "frame_recorder.hpp"
#include "journal_identity.hpp"
#endif
#include "provider_host.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <deque>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>

namespace {
namespace h = dbce::shim::hosting;
namespace sh = dbce::shim;
#if DBCE_SHIM_ORDINARY_ONLY
constexpr const char* X86Refusal = "x86 build: ordinary dbce_shim_open only; dbce_shim_open2, latched control, capture and the "
                                   "copied-input bridge are x64-only";
#else
namespace c = dbce::shim::control;
namespace rec = dbce::shim::recorder;
constexpr const char* CoreVersion = "input-shim core (dbce_shim ABI 2)";
#endif

static_assert(DBCE_SHIM_ACTIONS == sh::LegacyActionCount, "ABI2 retains the original Core action prefix");
static_assert(sizeof(dbce_shim_action) == 8 && offsetof(dbce_shim_action, status) == 4, "dbce_shim_action layout");
static_assert(sizeof(dbce_shim_frame) == 296 && offsetof(dbce_shim_frame, seq) == 8 && offsetof(dbce_shim_frame, run) == 40 &&
              offsetof(dbce_shim_frame, sample_count) == 44 && offsetof(dbce_shim_frame, action) == 48, "dbce_shim_frame layout");
#if DBCE_SHIM_ORDINARY_ONLY
// x86: twelve 4-byte pointers. dbce_shim_open2 refuses here; the layout is pinned so dbce_shim_open_args_size() stays true.
static_assert(sizeof(dbce_shim_open_args) == 80 && offsetof(dbce_shim_open_args, profile_len) == 8 &&
              offsetof(dbce_shim_open_args, fences_verified) == 12 && offsetof(dbce_shim_open_args, folder) == 16 &&
              offsetof(dbce_shim_open_args, capture_folder) == 44 && offsetof(dbce_shim_open_args, hz) == 64 &&
              offsetof(dbce_shim_open_args, seconds) == 72, "dbce_shim_open_args layout (x86)");
#else
static_assert(sizeof(dbce_shim_open_args) == 128 && offsetof(dbce_shim_open_args, profile_len) == 8 &&
              offsetof(dbce_shim_open_args, fences_verified) == 12 && offsetof(dbce_shim_open_args, folder) == 16 &&
              offsetof(dbce_shim_open_args, capture_folder) == 72 && offsetof(dbce_shim_open_args, hz) == 112 &&
              offsetof(dbce_shim_open_args, seconds) == 120, "dbce_shim_open_args layout");
#endif

#if DBCE_SHIM_ORDINARY_ONLY
// x86: no capture is compiled in, so the recorder phase is complete by construction.
struct DllRecorder {
    void finish() {}
    struct Drained { h::RecOutcome outcome; bool joined; };
    Drained drain(h::Clock::time_point) { return {h::RecOutcome::Complete, true}; }
};
#else
// The host's recorder: the shared frame recorder when the open asked for a capture, otherwise nothing to drain (the
// recorder phase is complete by construction).
struct DllRecorder {
    std::optional<rec::FrameRecorder> frames;
    void set_devices(std::vector<sh::DeviceReport> d) { if (frames) frames->set_devices(std::move(d)); }
    void push(const sh::Frame& f, const rec::Channels& game) { if (frames) frames->push(f, game); }
    void finish() { if (frames) frames->finish(); }
    struct Drained { h::RecOutcome outcome; bool joined; };
    Drained drain(h::Clock::time_point deadline) {
        if (!frames) return {h::RecOutcome::Complete, true};
        const auto d = frames->drain(deadline);
        return {d.outcome, d.joined};
    }
};
#endif
using Host = h::ProviderHost<DllRecorder>;
// An ordinary host's freshness bound: Core's, and the one extension 3's snapshot reports.
constexpr std::int64_t OrdinaryFreshUs = 100'000;

// A handle's shell is never freed (a few bytes per open), so a spent handle stays recognisable; the registry lets a
// call refuse an unknown pointer without reading through it.
struct Shim {
    std::thread::id owner = std::this_thread::get_id();
    std::deque<std::string> reasons;   // the capability's text, alive as long as Core may read it
    std::unique_ptr<Host> host;
    std::shared_ptr<sh::BridgeProvider> bridge;
    std::string profile_info;          // Core's parse of the profile, fixed at the open
    bool stopped = false, latched = false, capture = false;
    std::vector<std::string> game;     // the capture's game channels, "game.<name>", fixed at the open
    // The frame just ticked, waiting for its game values (dbce_shim_record) until the next tick or the stop pushes it.
    struct Pending { bool active = false, recorded = false, valid = false; sh::Frame frame; std::vector<double> values; } pending;
    // Extension 2: a handle opened with a declaration ticks only through dbce_shim_tick_extras.
    bool extras = false;
    sh::ContextMode context_mode = sh::ContextMode::None;
    std::uint32_t context_bits = 0;
    bool have_extras_frame = false;
    dbce_shim_extras_frame last_extras{};   // the last tick's, for the read-only dbce_shim_extras
    // Extension 3: the frame the last successful tick returned. Core's own frame, alive as long as the host and changed
    // only by the next tick on this thread, so the snapshot copies nothing per tick. Null until a tick succeeds.
    const sh::Frame* last_frame = nullptr;
};
std::mutex registry_lock;
std::set<const Shim*> registry;

int32_t text_out(const std::string& s, char* buf, int32_t len, int32_t* needed) {
    if (needed) *needed = int32_t(std::min<std::size_t>(s.size() + 1, INT32_MAX));
    if (!buf) return len == 0 ? DBCE_SHIM_OK : DBCE_SHIM_BAD_ARGUMENT;
    if (len <= 0) return DBCE_SHIM_BAD_ARGUMENT;
    const auto n = std::min<std::size_t>(s.size(), std::size_t(len) - 1);
    std::memcpy(buf, s.data(), n); buf[n] = 0;
    return DBCE_SHIM_OK;
}
int32_t fail(int32_t code, const std::string& why, char* buf, int32_t len) { if (buf && len > 0) text_out(why, buf, len, nullptr); return code; }

int32_t lookup(void* handle, Shim*& s) {
    s = static_cast<Shim*>(handle);
    { std::lock_guard<std::mutex> l(registry_lock); if (!s || !registry.count(s)) return DBCE_SHIM_BAD_ARGUMENT; }
    if (std::this_thread::get_id() != s->owner) return DBCE_SHIM_WRONG_THREAD;
    if (s->stopped) return DBCE_SHIM_STOPPED;
    return DBCE_SHIM_OK;
}

// "action=reason" lines; an unknown action or a line without '=' refuses the open.
bool capability(const char* text, Shim& s, sh::Capability& caps, std::string& why) {
    caps=sh::legacy_abi2_capability();
    if (!text) return true;
    std::istringstream in(text); std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        const auto eq = line.find('=');
        const int i = eq == std::string::npos ? -1 : sh::action_index(line.substr(0, eq));
        if (i < 0) { why = "unsupported: not an \"action=reason\" line of a known action: " + line.substr(0, 80); return false; }
        if(i>=DBCE_SHIM_ACTIONS)continue; // fixed ABI2 camera-unsupported reason stays authoritative
        s.reasons.push_back(line.substr(eq + 1));
        caps.unsupported[std::size_t(i)] = s.reasons.back().c_str();
    }
    return true;
}

#if !DBCE_SHIM_ORDINARY_ONLY
// The capture's game channel names: ASCII session names, unique, not the reserved "valid", at most 256.
bool game_channels(const char* text, std::vector<std::string>& out, std::string& why) {
    out.clear();
    std::istringstream in(text ? text : ""); std::string name; std::set<std::string> seen;
    while (std::getline(in, name)) {
        while (!name.empty() && name.back() == '\r') name.pop_back();
        if (name.empty()) continue;
        if (name == "valid" || !dbce::session::ascii("game." + name, 256, true) || !seen.insert(name).second || out.size() == 256) {
            why = "game_channels: \"" + name.substr(0, 64) + "\" is reserved, not an ASCII name, repeated or past 256 names"; return false;
        }
        out.push_back("game." + name);
    }
    return true;
}
bool game_id(const char* id) {
    if (!id) return false;
    const std::string s(id);
    return !s.empty() && s.size() <= 64 && std::all_of(s.begin(), s.end(), [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-'; });
}
#endif

// The DLL's code must outlive every worker the host may leave running (DInputProvider: "the code module is never
// unloaded"): pin it, so no FreeLibrary can unload it under a thread.
bool pin_module() {
    HMODULE self = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&pin_module), &self) != FALSE;
}

std::string device_json(const std::vector<sh::DeviceReport>& devices) {
    using dbce::shim::json_string;
    std::string j = "[";
    for (std::size_t d = 0; d < devices.size(); ++d) {
        const auto& r = devices[d];
        j += std::string(d ? "," : "") + "{\"index\":" + std::to_string(d) + ",\"instance\":"; json_string(j, r.identity.dev, 128);
        j += ",\"product\":"; json_string(j, r.identity.prod, 128); j += ",\"name\":"; if (!json_string(j, r.identity.name, 1560)) j += "null";
        j += ",\"interfacePath\":"; if (!json_string(j, r.identity.interface_path, 1560)) j += "\"\"";
        j += ",\"epoch\":" + std::to_string(r.epoch) + ",\"opened\":" + (r.opened ? "true" : "false") + ",\"status\":";
        if (!json_string(j, r.status, 1560)) j += "null";
        j += "}";
    }
    return j + "]";
}

#if DBCE_SHIM_ORDINARY_ONLY
void push_pending(Shim&) {}                      // x86: no capture is compiled in
void hold_pending(Shim&, const sh::Frame&) {}
#else
// The pending frame's row: its game values when recorded and valid, otherwise zeros with game.valid=0.
void push_pending(Shim& s) {
    if (!s.capture || !s.pending.active) return;
    rec::Channels game;
    for (std::size_t i = 0; i < s.game.size(); ++i) game[s.game[i]] = s.pending.valid ? s.pending.values[i] : 0.0;
    game["game.valid"] = s.pending.valid ? 1.0 : 0.0;
    s.host->recorder().push(s.pending.frame, game);
    s.pending.active = false;
}
void hold_pending(Shim& s, const sh::Frame& f) {
    if (!s.capture) return;
    s.pending.frame = f; s.pending.active = true; s.pending.recorded = false; s.pending.valid = false;
    s.pending.values.assign(s.game.size(), 0.0);
}
#endif

// One open for both entries: v1's ordinary host and v2's arguments.
int32_t open_impl(IDirectInput8W* input, const dbce_shim_open_args& a, void** out, char* why, int32_t why_len,
                  std::shared_ptr<sh::BridgeProvider> bridge = {}, const sh::Declaration* extras = nullptr) {
    auto refuse = [&](int32_t code, const std::string& text) { if (input) input->Release(); return fail(code, text, why, why_len); };
    try {
        if (!out) return refuse(DBCE_SHIM_BAD_ARGUMENT, "out is null");
        *out = nullptr;
        const bool latched = (a.flags & DBCE_SHIM_LATCHED) != 0, capture = (a.flags & DBCE_SHIM_CAPTURE) != 0;
        if (a.flags & ~std::uint32_t(DBCE_SHIM_LATCHED | DBCE_SHIM_CAPTURE)) return refuse(DBCE_SHIM_BAD_ARGUMENT, "unknown open flags");
#if DBCE_SHIM_ORDINARY_ONLY
        if (latched || capture) return refuse(DBCE_SHIM_REFUSED, X86Refusal);   // unreachable through the ABI: its entries refuse first
#endif
        if ((!input && !bridge) || !a.folder || !a.nonce || !a.profile || a.profile_len < 0 || a.profile_len > (1 << 20))
            return refuse(DBCE_SHIM_BAD_ARGUMENT, "input, folder, nonce and a profile of at most 1 MiB are required");
        const DWORD attributes = GetFileAttributesW(a.folder);
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
            return refuse(DBCE_SHIM_BAD_ARGUMENT, "folder must be an existing folder");
        // Test-mode admission stays tied to the entry point's own verified fences and process-latched mute (Astra 18000).
        if (latched && (!a.session_file || !a.batch_file || a.fences_verified != 1 || a.force_absent != 1))
            return refuse(DBCE_SHIM_REFUSED, "a latched open needs both session files, the caller's verified fences and its process-latched force mute");
        if (extras && (latched || capture))   // declared extras remain ordinary input only
            return refuse(DBCE_SHIM_REFUSED, "extras are ordinary input only: no latched control or capture");
        auto shell = std::make_unique<Shim>();
        shell->bridge = bridge;
        shell->latched = latched; shell->capture = capture;
#if !DBCE_SHIM_ORDINARY_ONLY
        if (capture) {
            std::string text;
            if (!a.capture_folder || !game_id(a.game_id) || !std::isfinite(a.hz) || a.hz <= 0 || !std::isfinite(a.seconds) || a.seconds <= 0)
                return refuse(DBCE_SHIM_BAD_ARGUMENT, "a capture needs its folder, a [a-z0-9.-] game id of 1-64 and a positive rate and duration");
            if (!game_channels(a.game_channels, shell->game, text)) return refuse(DBCE_SHIM_BAD_ARGUMENT, text);
        }
#endif
        if (!pin_module()) return refuse(DBCE_SHIM_REFUSED, "cannot pin the shim module");
        sh::Capability caps; std::string text;
        if (!capability(a.unsupported, *shell, caps, text)) return refuse(DBCE_SHIM_BAD_ARGUMENT, text);
        const std::string bytes(a.profile, std::size_t(a.profile_len)), nonce(a.nonce);
        const std::string adapter = a.adapter_version ? a.adapter_version : "";
        std::set<std::string> instances;   // Core keeps one device per bound instance: the capture's sample slots
        {   // the same parser Core uses (read_profile), reported as read
            const auto parsed = sh::read_profile(bytes);
            shell->profile_info = std::string("ok=") + (parsed.ok ? "1" : "0") + "\nerror=" + parsed.error + "\nprofile=" + parsed.section.profile +
                "\nrevision=" + parsed.section.revision + "\ntransmission=" + parsed.section.transmission + "\nsha256=" + sh::hex(parsed.file_hash) + "\n";
            // Shared rig profiles may include source-only camera bindings. Core's
            // capability-aware plan skips them; they must not reserve capture slots.
            if(parsed.ok)for(const auto& e:parsed.section.bound)
                if(sh::action_index(e.action)<DBCE_SHIM_ACTIONS)instances.insert(e.binding.dev);
        }
        auto recorder = std::make_unique<DllRecorder>();
#if !DBCE_SHIM_ORDINARY_ONLY
        if (capture) {
            if (instances.empty()) return refuse(DBCE_SHIM_BAD_ARGUMENT, "a capture needs a profile that binds a device");
            rec::FrameCaptureSettings cs;
            cs.folder = a.capture_folder; cs.profile = bytes; cs.run_nonce = nonce; cs.adapter_version = adapter;
            cs.game = a.game_id; cs.bundle_meta = rec::generic_bundle_meta(a.game_id); cs.context = a.context ? a.context : "";
            cs.hz = a.hz; cs.seconds = a.seconds;cs.action_count=sh::LegacyActionCount;
            // The worst case (every bound instance a slot) refuses the open rather than the first row.
            rec::Channels probe = rec::encode(sh::Frame{}, instances.size(),sh::LegacyActionCount);
            for (const auto& n : shell->game) probe[n] = 0;
            probe["game.valid"] = 0;
            try { rec::preflight(rec::schema(probe), cs.hz, cs.seconds, cs.limits); }
            catch (const std::exception& e) { return refuse(DBCE_SHIM_BAD_ARGUMENT, std::string("capture preflight: ") + e.what()); }
            recorder->frames.emplace(std::move(cs));
        }
#endif
        // From here the host owns the DirectInput reference, also when its construction throws.
        auto* in = input; input = nullptr;
        if (bridge) shell->host = std::make_unique<Host>(a.folder,nonce,bridge,bytes,latched,std::move(recorder),caps,OrdinaryFreshUs,1,extras);
        else shell->host = std::make_unique<Host>(a.folder, nonce, in, bytes, latched, std::move(recorder), caps, static_cast<HWND>(a.hwnd),
                                                  OrdinaryFreshUs, nullptr, 1, extras);
        if (extras) { shell->extras = true; shell->context_mode = extras->mode; shell->context_bits = extras->context_bits(); }
#if !DBCE_SHIM_ORDINARY_ONLY
        if (capture) shell->host->recorder().set_devices(shell->host->device_reports());   // one sample slot per profile device
        if (latched) {   // the reviewed control worker and journal, in the run folder
            c::JournalLimits limits; limits.cap_bytes = c::LatchedJournalCap;
            auto io = std::make_shared<c::FileIo>(a.session_file, a.batch_file, a.folder, false,
                                                  h::journal_identity(*shell->host, nonce, bytes, CoreVersion, adapter.c_str(), 1,   // the DLL ABI reads controls schema 1
                                                                      bridge ? "external-physical-input@1" : nullptr), limits);
            shell->host->start_control(io, {true, a.fences_verified == 1, a.force_absent == 1, nonce});
        }
#endif
        { std::lock_guard<std::mutex> l(registry_lock); registry.insert(shell.get()); }
        *out = shell.release();
        return DBCE_SHIM_OK;
    } catch (const std::exception& e) { return refuse(DBCE_SHIM_REFUSED, std::string("host refused: ") + e.what()); }
    catch (...) { return refuse(DBCE_SHIM_REFUSED, "host refused"); }
}
dbce_shim_open_args ordinary(const wchar_t* folder, const char* nonce, const char* profile, int32_t profile_len, const char* unsupported, void* hwnd) {
    dbce_shim_open_args a{}; a.struct_size = sizeof a; a.folder = folder; a.nonce = nonce; a.profile = profile; a.profile_len = profile_len;
    a.unsupported = unsupported; a.hwnd = hwnd;
    return a;
}
IDirectInput8W* direct_input() {
    IDirectInput8W* input = nullptr;
    if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void**>(&input), nullptr))) return nullptr;
    return input;
}
}

int32_t dbce_shim_open_with_input(IDirectInput8W* input, const wchar_t* folder, const char* nonce, const char* profile,
                                  int32_t profile_len, const char* unsupported, void* hwnd, void** out, char* why, int32_t why_len) {
    return open_impl(input, ordinary(folder, nonce, profile, profile_len, unsupported, hwnd), out, why, why_len);
}
int32_t dbce_shim_open_extras_with_input(IDirectInput8W* input, const wchar_t* folder, const char* nonce, const char* profile,
                                         int32_t profile_len, const char* unsupported, void* hwnd, const char* declaration,
                                         int32_t declaration_len, void** out, char* why, int32_t why_len) {
    if (!declaration || declaration_len == 0)
        return dbce_shim_open_with_input(input, folder, nonce, profile, profile_len, unsupported, hwnd, out, why, why_len);
    sh::Declaration decl; std::string reason;
    // Bounded before any byte is read: the length decides, and the bytes are copied during the parse.
    if (declaration_len < 0 || declaration_len > int32_t(sh::MaxDeclarationBytes)) reason = "the declaration length is not 1-4096 bytes";
    else if (!sh::parse_declaration(declaration, std::size_t(declaration_len), decl, reason)) reason = "declaration: " + reason;
    if (!reason.empty()) {
        if (input) input->Release();
        if (out) *out = nullptr;
        return fail(DBCE_SHIM_BAD_ARGUMENT, reason, why, why_len);
    }
    return open_impl(input, ordinary(folder, nonce, profile, profile_len, unsupported, hwnd), out, why, why_len, {}, &decl);
}
int32_t dbce_shim_open2_with_input(IDirectInput8W* input, const dbce_shim_open_args* args, void** out, char* why, int32_t why_len) {
#if DBCE_SHIM_ORDINARY_ONLY
    (void)args;   // x86: every open2 refuses, whatever its arguments, and releases the reference it was given
    if (input) input->Release();
    if (out) *out = nullptr;
    return fail(DBCE_SHIM_REFUSED, X86Refusal, why, why_len);
#else
    if (!args || args->struct_size != sizeof(dbce_shim_open_args)) {
        if (input) input->Release();
        if (out) *out = nullptr;
        return fail(DBCE_SHIM_BAD_ARGUMENT, "args is null or not dbce_shim_open_args_size() bytes", why, why_len);
    }
    return open_impl(input, *args, out, why, why_len);
#endif
}

extern "C" {
int32_t dbce_shim_abi_version(void) { return DBCE_SHIM_ABI; }
int32_t dbce_shim_frame_size(void) { return int32_t(sizeof(dbce_shim_frame)); }
int32_t dbce_shim_open_args_size(void) { return int32_t(sizeof(dbce_shim_open_args)); }
const char* dbce_shim_action_name(int32_t index) { return index >= 0 && index < DBCE_SHIM_ACTIONS ? sh::action_name(index) : nullptr; }

int32_t dbce_shim_open(const wchar_t* folder, const char* nonce, const char* profile, int32_t profile_len,
                       const char* unsupported, void* hwnd, void** out, char* why, int32_t why_len) {
    IDirectInput8W* input = direct_input();
    if (!input) { if (out) *out = nullptr; return fail(DBCE_SHIM_REFUSED, "DirectInput8Create failed", why, why_len); }
    return dbce_shim_open_with_input(input, folder, nonce, profile, profile_len, unsupported, hwnd, out, why, why_len);
}
int32_t dbce_shim_open2(const dbce_shim_open_args* args, void** out, char* why, int32_t why_len) {
#if DBCE_SHIM_ORDINARY_ONLY
    return dbce_shim_open2_with_input(nullptr, args, out, why, why_len);   // x86: refuses before any DirectInput is created
#else
    if (!args || args->struct_size != sizeof(dbce_shim_open_args)) return dbce_shim_open2_with_input(nullptr, args, out, why, why_len);
    IDirectInput8W* input = direct_input();
    if (!input) { if (out) *out = nullptr; return fail(DBCE_SHIM_REFUSED, "DirectInput8Create failed", why, why_len); }
    return dbce_shim_open2_with_input(input, args, out, why, why_len);
#endif
}

static int32_t tick_impl(Shim* s, dbce_shim_frame* frame, const std::uint32_t* active_contexts = nullptr) {
    // Refused before Core ticks: nothing changes, so the last frame and its snapshot stay.
    if (!frame || frame->struct_size != sizeof(dbce_shim_frame)) return DBCE_SHIM_BAD_ARGUMENT;
    try {
        push_pending(*s);
        const auto& f = active_contexts ? s->host->tick(*active_contexts) : s->host->tick();
        hold_pending(*s, f);
        sh::legacy_abi2_frame(f,*frame);
        // Extension 4: an open candidate's one read, after Core's. It never fails the ordinary tick.
        if (!s->latched) {
            try { s->host->candidate_tick(f.seq, f.tick_us); }
            catch (...) { try { s->host->candidate().release("released after an internal error"); } catch (...) {} }
        }
        s->last_frame = &f;   // published only once the whole tick succeeded
        return DBCE_SHIM_OK;
    } catch (...) { s->last_frame = nullptr; return DBCE_SHIM_REFUSED; }   // Core may have ticked: no snapshot until the next success
}
int32_t dbce_shim_tick(void* handle, dbce_shim_frame* frame) {
    Shim* s=nullptr;if(const auto r=lookup(handle,s))return r;
    if(s->bridge)return DBCE_SHIM_BAD_ARGUMENT;
    if(s->extras)return DBCE_SHIM_BAD_ARGUMENT;   // an extras handle advances only through dbce_shim_tick_extras
    return tick_impl(s,frame);
}

// ---- extension 2: declared extras
static_assert(sizeof(dbce_shim_extras_frame) == 296 && offsetof(dbce_shim_extras_frame, seq) == 8 &&
              offsetof(dbce_shim_extras_frame, count) == 32 && offsetof(dbce_shim_extras_frame, slot) == 40, "dbce_shim_extras_frame layout");
static_assert(DBCE_SHIM_MAX_EXTRA_SLOTS == sh::MaxExtraSlots && DBCE_SHIM_MAX_DECLARATION == sh::MaxDeclarationBytes &&
              DBCE_SHIM_CONTEXT_INACTIVE == int(sh::Status::ContextInactive), "extension 2 constants are Core's");
int32_t dbce_shim_extras_abi_version(void) { return DBCE_SHIM_EXTRAS_ABI; }
int32_t dbce_shim_extras_frame_size(void) { return int32_t(sizeof(dbce_shim_extras_frame)); }
int32_t dbce_shim_open_extras(const wchar_t* folder, const char* nonce, const char* profile, int32_t profile_len,
                              const char* unsupported, void* hwnd, const char* declaration, int32_t declaration_len,
                              void** out, char* why, int32_t why_len) {
    if (!declaration || declaration_len == 0) return dbce_shim_open(folder, nonce, profile, profile_len, unsupported, hwnd, out, why, why_len);
    // The declaration is checked before any DirectInput is created.
    sh::Declaration probe; std::string reason;
    if (declaration_len < 0 || declaration_len > int32_t(sh::MaxDeclarationBytes)) reason = "the declaration length is not 1-4096 bytes";
    else if (!sh::parse_declaration(declaration, std::size_t(declaration_len), probe, reason)) reason = "declaration: " + reason;
    if (!reason.empty()) { if (out) *out = nullptr; return fail(DBCE_SHIM_BAD_ARGUMENT, reason, why, why_len); }
    IDirectInput8W* input = direct_input();
    if (!input) { if (out) *out = nullptr; return fail(DBCE_SHIM_REFUSED, "DirectInput8Create failed", why, why_len); }
    return dbce_shim_open_extras_with_input(input, folder, nonce, profile, profile_len, unsupported, hwnd, declaration, declaration_len, out, why, why_len);
}
static bool extras_arguments(const Shim* s, uint32_t active_contexts, const dbce_shim_frame* frame, const dbce_shim_extras_frame* extras) {
    return s->extras && frame && frame->struct_size == sizeof(*frame) && extras && extras->struct_size == sizeof(*extras) &&
        (active_contexts & ~s->context_bits) == 0 && (s->context_mode == sh::ContextMode::Observed || active_contexts == 0);
}
static int32_t tick_extras_impl(Shim* s, uint32_t active_contexts, dbce_shim_frame* frame, dbce_shim_extras_frame* extras) {
    // Refuse malformed frames/contexts before advancing Core or source sequences.
    if (!extras_arguments(s, active_contexts, frame, extras)) return DBCE_SHIM_BAD_ARGUMENT;
    const auto r = tick_impl(s, frame, &active_contexts);
    if (r != DBCE_SHIM_OK) return r;
    try {
        const auto& x = s->host->extras_frame();
        dbce_shim_extras_frame f{};
        f.struct_size = sizeof f; f.abi = DBCE_SHIM_EXTRAS_ABI; f.seq = x.seq; f.down = x.down; f.up = x.up;
        f.count = std::uint32_t(std::min<std::size_t>(x.slot.size(), DBCE_SHIM_MAX_EXTRA_SLOTS)); f.contexts = x.contexts;
        for (std::uint32_t i = 0; i < f.count; ++i) {
            f.slot[i].value = x.slot[i].value; f.slot[i].status = std::uint8_t(x.slot[i].status); f.slot[i].injected = x.slot[i].injected;
        }
        s->last_extras = f; s->have_extras_frame = true;
        *extras = f;
        return DBCE_SHIM_OK;
    } catch (...) { s->last_frame = nullptr; return DBCE_SHIM_REFUSED; }   // a refused tick offers no snapshot either
}
int32_t dbce_shim_tick_extras(void* handle, uint32_t active_contexts, dbce_shim_frame* frame, dbce_shim_extras_frame* extras) {
    Shim* s = nullptr;
    if (const auto r = lookup(handle, s)) return r;
    if (s->bridge) return DBCE_SHIM_BAD_ARGUMENT; // copied handles must submit their source batch
    return tick_extras_impl(s, active_contexts, frame, extras);
}
int32_t dbce_shim_extras(void* handle, uint64_t frame_seq, dbce_shim_extras_frame* out) {
    Shim* s = nullptr;
    if (const auto r = lookup(handle, s)) return r;
    if (!s->extras || !out || out->struct_size != sizeof(dbce_shim_extras_frame)) return DBCE_SHIM_BAD_ARGUMENT;
    if (!s->have_extras_frame || s->last_extras.seq != frame_seq) return DBCE_SHIM_STALE;
    *out = s->last_extras;   // read-only: repeated reads are identical, edges included
    return DBCE_SHIM_OK;
}
int32_t dbce_shim_extras_info(void* handle, char* text, int32_t len, int32_t* needed) {
    Shim* s = nullptr;
    if (const auto r = lookup(handle, s)) return r;
    if (!s->extras) return DBCE_SHIM_BAD_ARGUMENT;
    try { return text_out(s->host->extras_report(), text, len, needed); } catch (...) { return DBCE_SHIM_REFUSED; }
}

// ---- extension 3: the accepted raw-input snapshot (ordinary handles)
static_assert(sizeof(dbce_shim_snapshot_header) == 56 && offsetof(dbce_shim_snapshot_header, frame_seq) == 16 &&
              offsetof(dbce_shim_snapshot_header, fresh_us) == 40 && offsetof(dbce_shim_snapshot_header, device_count) == 48,
              "dbce_shim_snapshot_header layout");
static_assert(sizeof(dbce_shim_snapshot_device) == 192 && offsetof(dbce_shim_snapshot_device, flags) == 16 &&
              offsetof(dbce_shim_snapshot_device, button_mask) == 32 && offsetof(dbce_shim_snapshot_device, minimum) == 48 &&
              offsetof(dbce_shim_snapshot_device, maximum) == 80 && offsetof(dbce_shim_snapshot_device, instance) == 112 &&
              offsetof(dbce_shim_snapshot_device, product) == 152, "dbce_shim_snapshot_device layout");
static_assert(sizeof(dbce_shim_snapshot_sample) == 224 && offsetof(dbce_shim_snapshot_sample, device_seq) == 16 &&
              offsetof(dbce_shim_snapshot_sample, poll_call) == 40 && offsetof(dbce_shim_snapshot_sample, ended) == 44 &&
              offsetof(dbce_shim_snapshot_sample, axes) == 48 && offsetof(dbce_shim_snapshot_sample, buttons) == 80 &&
              offsetof(dbce_shim_snapshot_sample, pov) == 208, "dbce_shim_snapshot_sample layout");
static_assert(DBCE_SHIM_CALL_OK == int(sh::Call::Ok) && DBCE_SHIM_CALL_ACQUISITION_LOST == int(sh::Call::AcquisitionLost) &&
              DBCE_SHIM_CALL_GONE == int(sh::Call::Gone) && DBCE_SHIM_CALL_NOT_CALLED == int(sh::Call::NotCalled) &&
              DBCE_SHIM_CALL_PENDING == int(sh::Call::Pending), "extension 3's call values are Core's published Call");
int32_t dbce_shim_snapshot_abi_version(void) { return DBCE_SHIM_SNAPSHOT_ABI; }
int32_t dbce_shim_snapshot_sizes(int32_t* header, int32_t* device, int32_t* sample) {
    if (header) *header = int32_t(sizeof(dbce_shim_snapshot_header));
    if (device) *device = int32_t(sizeof(dbce_shim_snapshot_device));
    if (sample) *sample = int32_t(sizeof(dbce_shim_snapshot_sample));
    return DBCE_SHIM_OK;
}
int32_t dbce_shim_snapshot(void* handle, uint64_t frame_seq, dbce_shim_snapshot_header* header, dbce_shim_snapshot_device* devices,
                           int32_t device_capacity, dbce_shim_snapshot_sample* samples, int32_t sample_capacity) {
    Shim* s = nullptr;
    if (const auto r = lookup(handle, s)) return r;
    if (!header || header->struct_size != sizeof(dbce_shim_snapshot_header) ||
        header->device_struct_size != sizeof(dbce_shim_snapshot_device) || header->sample_struct_size != sizeof(dbce_shim_snapshot_sample) ||
        device_capacity < 0 || sample_capacity < 0 || (device_capacity && !devices) || (sample_capacity && !samples))
        return DBCE_SHIM_BAD_ARGUMENT;
    if (s->latched || s->bridge) return DBCE_SHIM_REFUSED;   // ordinary input only: no injected or copied reads
    if (!s->last_frame || s->last_frame->seq != frame_seq) return DBCE_SHIM_STALE;
    try {
        const auto& f = *s->last_frame;
        // The input thread's device table as this frame left it (only a tick changes it). No device or provider call.
        const auto table = s->host->device_reports();
        if (table.size() > DBCE_SHIM_SNAPSHOT_MAX_DEVICES || f.samples.size() > table.size()) return DBCE_SHIM_REFUSED;   // never truncated
        std::vector<dbce_shim_snapshot_device> rows(table.size());
        for (std::size_t i = 0; i < table.size(); ++i) {
            const auto& r = table[i]; auto& o = rows[i];
            o.struct_size = sizeof o; o.abi = DBCE_SHIM_SNAPSHOT_ABI; o.index = std::uint32_t(i); o.epoch = r.epoch;
            o.flags = (r.opened ? std::uint32_t(DBCE_SHIM_SNAPSHOT_OPENED) : 0u) |
                      (r.opened && !r.identity.interface_path.empty() ? std::uint32_t(DBCE_SHIM_SNAPSHOT_PATH) : 0u);
            if (r.opened) {   // Core zeroes a closed epoch's caps; only an open epoch advertises them
                for (int a = 0; a < 8; ++a) {
                    if (r.caps.axis[std::size_t(a)]) o.axis_mask |= 1u << a;
                    if (r.caps.range_ok[std::size_t(a)]) o.range_mask |= 1u << a;
                    o.minimum[a] = r.caps.min[std::size_t(a)]; o.maximum[a] = r.caps.max[std::size_t(a)];
                }
                for (int b = 0; b < 128; ++b)
                    if (r.caps.button[std::size_t(b)]) o.button_mask[b / 8] = std::uint8_t(o.button_mask[b / 8] | (1u << (b % 8)));
                for (int p = 0; p < 4; ++p) if (r.caps.pov[std::size_t(p)]) o.pov_mask |= 1u << p;
            }
            if (r.identity.dev.size() >= sizeof o.instance || r.identity.prod.size() >= sizeof o.product) return DBCE_SHIM_REFUSED;
            std::memcpy(o.instance, r.identity.dev.data(), r.identity.dev.size());
            std::memcpy(o.product, r.identity.prod.data(), r.identity.prod.size());
        }
        std::vector<dbce_shim_snapshot_sample> reads(f.samples.size());
        for (std::size_t i = 0; i < f.samples.size(); ++i) {
            const auto& x = f.samples[i]; auto& o = reads[i];
            if (x.device >= table.size()) return DBCE_SHIM_REFUSED;
            o.struct_size = sizeof o; o.abi = DBCE_SHIM_SNAPSHOT_ABI; o.index = x.device; o.epoch = x.epoch; o.device_seq = x.seq;
            o.read_start_us = x.read_start_us; o.read_end_us = x.read_end_us;
            o.poll_call = std::uint8_t(x.poll); o.state_call = std::uint8_t(x.state); o.valid = x.valid; o.stale = x.stale;
            // Ended from the table, never from the calls: a pending read past Core's limit ends its epoch while both
            // calls still say Pending. A read only ends the epoch it belongs to, and only a tick reopens one.
            const auto& row = table[x.device];
            o.ended = !(row.opened && row.epoch == x.epoch);
            // physical is the complete state read when valid, otherwise its initial value (axes 0, buttons up, POVs centred).
            std::copy(x.physical.axes, x.physical.axes + 8, o.axes);
            std::copy(x.physical.buttons, x.physical.buttons + 128, o.buttons);
            std::copy(x.physical.povs, x.physical.povs + 4, o.pov);
        }
        header->abi = DBCE_SHIM_SNAPSHOT_ABI; header->frame_seq = f.seq; header->tick_us = f.tick_us; header->boundary_us = f.boundary_us;
        header->fresh_us = OrdinaryFreshUs;
        header->device_count = std::uint32_t(rows.size()); header->sample_count = std::uint32_t(reads.size());
        if (std::size_t(device_capacity) < rows.size() || std::size_t(sample_capacity) < reads.size()) return DBCE_SHIM_BAD_ARGUMENT;
        if (!rows.empty()) std::memcpy(devices, rows.data(), rows.size() * sizeof rows[0]);
        if (!reads.empty()) std::memcpy(samples, reads.data(), reads.size() * sizeof reads[0]);
        return DBCE_SHIM_OK;
    } catch (...) { return DBCE_SHIM_REFUSED; }
}

// ---- extension 4: one explicit capture candidate (ordinary handles)
static int32_t candidate_handle(void* handle, Shim*& s) {
    if (const auto r = lookup(handle, s)) return r;
    return s->latched || s->bridge ? DBCE_SHIM_REFUSED : DBCE_SHIM_OK;   // ordinary input only
}
int32_t dbce_shim_candidate_abi_version(void) { return DBCE_SHIM_CANDIDATE_ABI; }
int32_t dbce_shim_candidate_list(void* handle) {
    Shim* s = nullptr;
    if (const auto r = candidate_handle(handle, s)) return r;
    try { s->host->candidate().request_list(); return DBCE_SHIM_OK; } catch (...) { return DBCE_SHIM_REFUSED; }
}
int32_t dbce_shim_candidate_select(void* handle, const char* instance, const char* product, char* why, int32_t why_len) {
    Shim* s = nullptr;
    if (const auto r = candidate_handle(handle, s)) return fail(r, r == DBCE_SHIM_REFUSED ? "ordinary handles only" : "", why, why_len);
    if (!instance || !product) return fail(DBCE_SHIM_BAD_ARGUMENT, "instance and product are required", why, why_len);
    try {
        const std::string dev(instance), prod(product);
        std::string reason;
        if (s->host->candidate().select(dev, prod, reason)) { if (why && why_len > 0) why[0] = 0; return DBCE_SHIM_OK; }
        return fail(sh::candidate_guid(dev) && sh::candidate_guid(prod) ? DBCE_SHIM_REFUSED : DBCE_SHIM_BAD_ARGUMENT, reason, why, why_len);
    } catch (...) { return DBCE_SHIM_REFUSED; }
}
int32_t dbce_shim_candidate_release(void* handle) {
    Shim* s = nullptr;
    if (const auto r = candidate_handle(handle, s)) return r;
    try { s->host->candidate().release("released by the caller"); return DBCE_SHIM_OK; } catch (...) { return DBCE_SHIM_REFUSED; }
}
int32_t dbce_shim_candidate_report(void* handle, char* text, int32_t len, int32_t* needed) {
    Shim* s = nullptr;
    if (const auto r = candidate_handle(handle, s)) return r;
    try {
        using dbce::shim::json_string;
        const auto& c = s->host->candidate();
        const auto& l = c.listing(); const auto& v = c.view();
        std::string j = "{\"abi\":" + std::to_string(DBCE_SHIM_CANDIDATE_ABI) + ",\"listing\":{\"seq\":" + std::to_string(l.seq) +
            ",\"pending\":" + (l.pending ? "true" : "false") + ",\"devices\":[";
        for (std::size_t i = 0; i < l.devices.size(); ++i) {
            const auto& d = l.devices[i];
            j += std::string(i ? "," : "") + "{\"instance\":"; json_string(j, d.dev, 128);
            j += ",\"product\":"; json_string(j, d.prod, 128); j += ",\"name\":"; if (!json_string(j, d.name, 1560)) j += "null";
            j += std::string(",\"bound\":") + (c.bound(d.dev) ? "true" : "false") + "}";
        }
        j += std::string("]},\"candidate\":{\"state\":\"") + sh::candidate_state_name(v.state) + "\",\"instance\":"; json_string(j, v.identity.dev, 128);
        j += ",\"product\":"; json_string(j, v.identity.prod, 128); j += ",\"name\":"; if (!json_string(j, v.identity.name, 1560)) j += "null";
        j += ",\"interfacePath\":"; if (!json_string(j, v.identity.interface_path, 1560)) j += "\"\"";
        j += ",\"epoch\":" + std::to_string(v.epoch) + ",\"opened\":" + (v.opened ? "true" : "false") + ",\"reason\":";
        if (!json_string(j, v.reason, 1560)) j += "null";
        return text_out(j + "}}", text, len, needed);
    } catch (...) { return DBCE_SHIM_REFUSED; }
}
int32_t dbce_shim_candidate_snapshot(void* handle, uint64_t frame_seq, dbce_shim_snapshot_header* header, dbce_shim_snapshot_device* device,
                                     int32_t device_capacity, dbce_shim_snapshot_sample* sample, int32_t sample_capacity) {
    Shim* s = nullptr;
    if (const auto r = lookup(handle, s)) return r;
    if (!header || header->struct_size != sizeof(dbce_shim_snapshot_header) ||
        header->device_struct_size != sizeof(dbce_shim_snapshot_device) || header->sample_struct_size != sizeof(dbce_shim_snapshot_sample) ||
        device_capacity < 0 || sample_capacity < 0 || (device_capacity && !device) || (sample_capacity && !sample))
        return DBCE_SHIM_BAD_ARGUMENT;
    if (s->latched || s->bridge) return DBCE_SHIM_REFUSED;
    if (!s->last_frame || s->last_frame->seq != frame_seq) return DBCE_SHIM_STALE;
    try {
        auto& c = s->host->candidate();
        if (c.tick_seq() != frame_seq) return DBCE_SHIM_STALE;   // the candidate did not run in this frame
        const auto& v = c.view();
        const bool present = v.state != sh::CandidateState::None;
        dbce_shim_snapshot_device row{}; dbce_shim_snapshot_sample read{};
        if (present) {
            row.struct_size = sizeof row; row.abi = DBCE_SHIM_SNAPSHOT_ABI; row.index = 0; row.epoch = v.epoch;
            row.flags = (v.opened ? std::uint32_t(DBCE_SHIM_SNAPSHOT_OPENED) : 0u) |
                        (v.opened && !v.identity.interface_path.empty() ? std::uint32_t(DBCE_SHIM_SNAPSHOT_PATH) : 0u);
            if (v.opened) {
                for (int a = 0; a < 8; ++a) {
                    if (v.caps.axis[std::size_t(a)]) row.axis_mask |= 1u << a;
                    if (v.caps.range_ok[std::size_t(a)]) row.range_mask |= 1u << a;
                    row.minimum[a] = v.caps.min[std::size_t(a)]; row.maximum[a] = v.caps.max[std::size_t(a)];
                }
                for (int b = 0; b < 128; ++b)
                    if (v.caps.button[std::size_t(b)]) row.button_mask[b / 8] = std::uint8_t(row.button_mask[b / 8] | (1u << (b % 8)));
                for (int p = 0; p < 4; ++p) if (v.caps.pov[std::size_t(p)]) row.pov_mask |= 1u << p;
            }
            if (v.identity.dev.size() >= sizeof row.instance || v.identity.prod.size() >= sizeof row.product) return DBCE_SHIM_REFUSED;
            std::memcpy(row.instance, v.identity.dev.data(), v.identity.dev.size());
            std::memcpy(row.product, v.identity.prod.data(), v.identity.prod.size());
        }
        const auto& x = v.read;
        const bool has_read = present && x.frame_seq == frame_seq;
        if (has_read) {
            read.struct_size = sizeof read; read.abi = DBCE_SHIM_SNAPSHOT_ABI; read.index = 0; read.epoch = x.epoch; read.device_seq = x.device_seq;
            read.read_start_us = x.start_us; read.read_end_us = x.end_us;
            read.poll_call = std::uint8_t(x.poll); read.state_call = std::uint8_t(x.state); read.valid = x.valid; read.stale = 0; read.ended = x.ended;
            std::copy(x.physical.axes, x.physical.axes + 8, read.axes);
            std::copy(x.physical.buttons, x.physical.buttons + 128, read.buttons);
            std::copy(x.physical.povs, x.physical.povs + 4, read.pov);
        }
        header->abi = DBCE_SHIM_SNAPSHOT_ABI; header->frame_seq = frame_seq; header->tick_us = c.tick_us();
        header->boundary_us = has_read ? x.end_us : c.tick_us(); header->fresh_us = OrdinaryFreshUs;
        header->device_count = present ? 1u : 0u; header->sample_count = has_read ? 1u : 0u;
        if (device_capacity < int32_t(header->device_count) || sample_capacity < int32_t(header->sample_count)) return DBCE_SHIM_BAD_ARGUMENT;
        if (present) *device = row;
        if (has_read) *sample = read;
        c.observed(frame_seq);
        return DBCE_SHIM_OK;
    } catch (...) { return DBCE_SHIM_REFUSED; }
}

static_assert(sizeof(dbce_shim_bridge_device_v1)==144 && sizeof(dbce_shim_bridge_sample_v1)==232,"bridge extension ABI");
int32_t dbce_shim_bridge_abi_version(void){return 1;}
int32_t dbce_shim_bridge_device_size(void){return sizeof(dbce_shim_bridge_device_v1);}
int32_t dbce_shim_bridge_sample_size(void){return sizeof(dbce_shim_bridge_sample_v1);}
#if !DBCE_SHIM_ORDINARY_ONLY
static int64_t bridge_clock(){LARGE_INTEGER f{},t{};if(!QueryPerformanceFrequency(&f)||f.QuadPart<=0||!QueryPerformanceCounter(&t))return -1;
    return (t.QuadPart/f.QuadPart)*1000000+(t.QuadPart%f.QuadPart)*1000000/f.QuadPart;}
#endif
static std::string bridge_guid(const uint8_t* bytes){GUID g{};std::memcpy(&g,bytes,sizeof(g));char text[40]{};
    std::snprintf(text,sizeof(text),"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",static_cast<unsigned long>(g.Data1),g.Data2,g.Data3,
        g.Data4[0],g.Data4[1],g.Data4[2],g.Data4[3],g.Data4[4],g.Data4[5],g.Data4[6],g.Data4[7]);return text;}
int32_t dbce_shim_bridge_open(const dbce_shim_open_args* args,void** out,char* why,int32_t why_len){
    if(out)*out=nullptr;
#if DBCE_SHIM_ORDINARY_ONLY
    (void)args;   // x86: the bridge takes open_args and is x64-only; with no bridge handle, its other calls refuse the handle
    return fail(DBCE_SHIM_REFUSED,X86Refusal,why,why_len);
#else
    if(!args || args->struct_size!=sizeof(*args))return fail(DBCE_SHIM_BAD_ARGUMENT,"bridge args size",why,why_len);
    if(args->flags & ~std::uint32_t(DBCE_SHIM_LATCHED | DBCE_SHIM_CAPTURE))return fail(DBCE_SHIM_BAD_ARGUMENT,"unknown bridge open flags",why,why_len);
    if(args->flags & DBCE_SHIM_CAPTURE)return fail(DBCE_SHIM_REFUSED,"bridge capture source format is not qualified",why,why_len);
    // LATCHED reuses open_impl's exact fence/mute checks, control journal and
    // terminal suppression. It does not turn copied physical samples into injection.
    try{return open_impl(nullptr,*args,out,why,why_len,std::make_shared<sh::BridgeProvider>(bridge_clock));}
    catch(...){return fail(DBCE_SHIM_REFUSED,"bridge allocation failed",why,why_len);}
#endif
}
int32_t dbce_shim_bridge_open_extras(const dbce_shim_open_args* args,const char* declaration,int32_t declaration_len,
                                      void** out,char* why,int32_t why_len) {
    if(out)*out=nullptr;
#if DBCE_SHIM_ORDINARY_ONLY
    (void)args;(void)declaration;(void)declaration_len;
    return fail(DBCE_SHIM_REFUSED,X86Refusal,why,why_len);
#else
    if(!args || args->struct_size!=sizeof(*args))return fail(DBCE_SHIM_BAD_ARGUMENT,"bridge args size",why,why_len);
    if(args->flags & ~std::uint32_t(DBCE_SHIM_LATCHED | DBCE_SHIM_CAPTURE))return fail(DBCE_SHIM_BAD_ARGUMENT,"unknown bridge open flags",why,why_len);
    if(args->flags)return fail(DBCE_SHIM_REFUSED,"bridge extras are ordinary input only",why,why_len);
    sh::Declaration decl;std::string reason;
    if(!declaration || declaration_len<=0 || declaration_len>int32_t(sh::MaxDeclarationBytes))
        return fail(DBCE_SHIM_BAD_ARGUMENT,"the declaration length is not 1-4096 bytes",why,why_len);
    if(!sh::parse_declaration(declaration,std::size_t(declaration_len),decl,reason))
        return fail(DBCE_SHIM_BAD_ARGUMENT,"declaration: "+reason,why,why_len);
    try{return open_impl(nullptr,*args,out,why,why_len,std::make_shared<sh::BridgeProvider>(bridge_clock),&decl);}
    catch(...){return fail(DBCE_SHIM_REFUSED,"bridge allocation failed",why,why_len);}
#endif
}
static int32_t bridge_devices_impl(void* handle,const dbce_shim_bridge_device_v1* devices,int32_t count,const wchar_t* const* paths){
    Shim* s=nullptr;if(const auto r=lookup(handle,s))return r;
    if(!s->bridge || count<0 || count>64 || (count && !devices))return DBCE_SHIM_BAD_ARGUMENT;
    try{
        std::vector<sh::BridgeDevice> copied;
        for(int i=0;i<count;++i){const auto& d=devices[i];
            if(d.struct_size!=sizeof(d)||d.abi!=1||(d.axis_mask&~255u)||(d.range_mask&~255u)||(d.pov_mask&~15u)||(d.flags&~1u))return DBCE_SHIM_BAD_ARGUMENT;
            sh::BridgeDevice v;v.generation=d.generation;v.identity={bridge_guid(d.instance),bridge_guid(d.product),"external physical input"};
            if(paths){
                if(!paths[i])return DBCE_SHIM_BAD_ARGUMENT;
                int length=0;while(length<260 && paths[i][length])++length;
                if(length==0 || length==260)return DBCE_SHIM_BAD_ARGUMENT;
                const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,paths[i],length,nullptr,0,nullptr,nullptr);
                if(size<=0)return DBCE_SHIM_BAD_ARGUMENT;
                v.identity.interface_path.resize(size);
                if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,paths[i],length,v.identity.interface_path.data(),size,nullptr,nullptr)!=size)return DBCE_SHIM_BAD_ARGUMENT;
            }
            for(int a=0;a<8;++a){v.caps.axis[a]=(d.axis_mask&(1u<<a))!=0;v.caps.range_ok[a]=(d.range_mask&(1u<<a))!=0;v.caps.min[a]=d.minimum[a];v.caps.max[a]=d.maximum[a];}
            for(int b=0;b<128;++b)v.caps.button[b]=(d.button_mask[b/8]&(1u<<(b%8)))!=0;
            for(int p=0;p<4;++p)v.caps.pov[p]=(d.pov_mask&(1u<<p))!=0;
            copied.push_back(v);
        }
        std::string why;return s->bridge->devices(copied,why)?DBCE_SHIM_OK:DBCE_SHIM_REFUSED;
    }catch(...){return DBCE_SHIM_REFUSED;}
}
int32_t dbce_shim_bridge_devices(void* handle,const dbce_shim_bridge_device_v1* devices,int32_t count){
    return bridge_devices_impl(handle,devices,count,nullptr);
}
int32_t dbce_shim_bridge_devices_with_paths(void* handle,const dbce_shim_bridge_device_v1* devices,int32_t count,const wchar_t* const* paths){
    if(count && !paths)return DBCE_SHIM_BAD_ARGUMENT;
    return bridge_devices_impl(handle,devices,count,paths);
}
static int32_t bridge_tick_impl(Shim* s,const dbce_shim_bridge_sample_v1* samples,int32_t count,dbce_shim_frame* frame,
                                const uint32_t* contexts=nullptr,dbce_shim_extras_frame* extras=nullptr){
    if(!s->bridge || s->extras != (contexts!=nullptr))return DBCE_SHIM_BAD_ARGUMENT;
    if(contexts && !extras_arguments(s,*contexts,frame,extras))return DBCE_SHIM_BAD_ARGUMENT;
    s->bridge->finish_frame();
    if(!frame || frame->struct_size!=sizeof(*frame) || count<0 || count>64 || (count && !samples))return DBCE_SHIM_BAD_ARGUMENT;
    try{
        std::vector<sh::BridgeSample> copied;
        for(int i=0;i<count;++i){const auto& r=samples[i];
            if(r.struct_size!=sizeof(r)||r.abi!=1||r.reserved||r.reserved2||r.valid>1||r.poll_call>4||r.state_call>4||
                (r.valid!=((r.poll_call==0 && r.state_call==0)?1:0)))return DBCE_SHIM_BAD_ARGUMENT;
            sh::BridgeSample v;v.source={r.generation,r.sequence,r.read_start_us,r.read_end_us};
            v.poll=static_cast<sh::Call>(r.poll_call);v.state=static_cast<sh::Call>(r.state_call);
            if(r.valid){std::copy(r.axes,r.axes+8,v.raw.axes);std::copy(r.buttons,r.buttons+128,v.raw.buttons);std::copy(r.pov,r.pov+4,v.raw.povs);}
            copied.push_back(v);
        }
        std::string why;if(!s->bridge->samples(copied,why))return DBCE_SHIM_REFUSED;
        const auto result=contexts?tick_extras_impl(s,*contexts,frame,extras):tick_impl(s,frame);s->bridge->finish_frame();return result;
    }catch(...){s->bridge->finish_frame();return DBCE_SHIM_REFUSED;}
}

int32_t dbce_shim_bridge_tick(void* handle,const dbce_shim_bridge_sample_v1* samples,int32_t count,dbce_shim_frame* frame){
    Shim* s=nullptr;if(const auto r=lookup(handle,s))return r;
    return bridge_tick_impl(s,samples,count,frame);
}
int32_t dbce_shim_bridge_tick_extras(void* handle,const dbce_shim_bridge_sample_v1* samples,int32_t count,uint32_t contexts,
                                   dbce_shim_frame* frame,dbce_shim_extras_frame* extras){
    Shim* s=nullptr;if(const auto r=lookup(handle,s))return r;
    return bridge_tick_impl(s,samples,count,frame,&contexts,extras);
}

int32_t dbce_shim_record(void* handle, uint64_t frame_seq, const double* values, int32_t count) {
    Shim* s = nullptr;
    if (const auto r = lookup(handle, s)) return r;
#if DBCE_SHIM_ORDINARY_ONLY
    (void)frame_seq; (void)values; (void)count;
    return DBCE_SHIM_REFUSED;   // x86: no capture is compiled in
#else
    if (!s->capture) return DBCE_SHIM_BAD_ARGUMENT;
    auto& p = s->pending;
    if (!p.active || p.recorded || frame_seq != p.frame.seq) return DBCE_SHIM_STALE;   // changes nothing: the first call stands
    p.recorded = true;
    if (!values || count != int32_t(s->game.size())) return DBCE_SHIM_BAD_ARGUMENT;      // recorded with game.valid=0
    for (int32_t i = 0; i < count; ++i) if (!std::isfinite(values[i])) return DBCE_SHIM_BAD_ARGUMENT;
    p.values.assign(values, values + count); p.valid = true;
    return DBCE_SHIM_OK;
#endif
}

int32_t dbce_shim_start_report(void* handle, char* text, int32_t len, int32_t* needed) {
    Shim* s = nullptr;
    if (const auto r = lookup(handle, s)) return r;
    try {
        const auto report = s->host->start_report();
        std::string t = report.line;
        for (const auto& n : report.notes) t += "\n" + n;
        return text_out(t, text, len, needed);
    } catch (...) { return DBCE_SHIM_REFUSED; }
}

int32_t dbce_shim_device_reports(void* handle, char* text, int32_t len, int32_t* needed) {
    Shim* s = nullptr;
    if (const auto r = lookup(handle, s)) return r;
    try { return text_out(device_json(s->host->device_reports()), text, len, needed); } catch (...) { return DBCE_SHIM_REFUSED; }
}

int32_t dbce_shim_profile_info(void* handle, char* text, int32_t len, int32_t* needed) {
    Shim* s = nullptr;
    if (const auto r = lookup(handle, s)) return r;
    return text_out(s->profile_info, text, len, needed);
}

int32_t dbce_shim_profile_action_info(const char* profile, int32_t profile_len, int32_t action,
                                    char* text, int32_t len, int32_t* needed) {
    if (needed) *needed = 0;
    if (text && len > 0) text[0] = 0;
    if (!profile || profile_len < 0 || profile_len > int32_t(sh::MaxProfileBytes) || action < 0 || action >= DBCE_SHIM_ACTIONS)
        return DBCE_SHIM_BAD_ARGUMENT;
    try {
        const auto parsed = sh::read_profile(std::string(profile, std::size_t(profile_len)));
        const auto plan = sh::plan_profile(parsed,sh::legacy_abi2_capability());
        std::string status = "invalid", reason;
        const dbce::controls::Binding* binding = nullptr;
        if (!parsed.ok) reason = parsed.error;
        else if (!plan.usable) reason = parsed.section.error;
        else if (!plan.invalid_keys.empty()) reason = "profile contains invalid or conflicting action: " + plan.invalid_keys.front();
        else if (plan.bound_of[std::size_t(action)] < 0) { status = "unbound"; reason = "action is not bound"; }
        else { status = "bound"; binding = &plan.bound[std::size_t(plan.bound_of[std::size_t(action)])].binding; }
        using dbce::shim::json_string;
        std::string result = "{\"schema\":1";
        auto field = [&](const char* key, const std::string& value) {
            result += std::string(",\"") + key + "\":";
            return json_string(result, value, sh::MaxProfileBytes);
        };
        if (!field("action", sh::action_name(action)) || !field("status", status) || !field("reason", reason) ||
            !field("sha256", sh::hex(parsed.file_hash)) || !field("kind", binding ? dbce::controls::kindName(binding->kind) : "") ||
            !field("instance", binding ? binding->dev : "") || !field("product", binding ? binding->prod : "") ||
            !field("name", binding ? binding->name : "")) return DBCE_SHIM_BAD_ARGUMENT;
        return text_out(result + "}", text, len, needed);
    } catch (...) { return DBCE_SHIM_REFUSED; }
}

int32_t dbce_shim_stop(void* handle, char* report, int32_t len, int32_t* needed) {
    Shim* s = nullptr;
    if (const auto r = lookup(handle, s)) return r;
    s->stopped = true;   // spent whatever happens below
    try {
        // P21 order, as OutRunDesktop's shutdown: the capture's device table, the pending row, control admission closed,
        // one final Core tick (recorded unconsumed: the caller never applied it), then the host's bounded stop.
#if !DBCE_SHIM_ORDINARY_ONLY
        if (s->capture) s->host->recorder().set_devices(s->host->device_reports());
#endif
        push_pending(*s);
        if (s->latched) {
            s->host->close_control_admission();
            const auto& f = s->host->tick();
            hold_pending(*s, f);
            push_pending(*s);
        }
        const auto r = s->host->stop();
        std::string t = r.resolved ? "resolved" : "unresolved:";
        for (const auto& u : r.unresolved) t += " " + u + ";";
#if !DBCE_SHIM_ORDINARY_ONLY
        if (s->capture) t += std::string("; capture ") + (r.recorder == h::RecOutcome::Complete ? "complete" : "not complete") +
                             (s->host->recorder().frames->refusal().empty() ? "" : " (" + s->host->recorder().frames->refusal() + ")");
#endif
        if (r.resolved) s->host.reset();     // every worker joined: the host can go
        else (void)s->host.release();        // P21: never torn down under a running worker; alive until process exit
        (void)text_out(t, report, len, needed);   // the resolution is the result even when the report does not fit
        return r.resolved ? DBCE_SHIM_OK : DBCE_SHIM_UNRESOLVED;
    } catch (...) { (void)s->host.release(); return DBCE_SHIM_REFUSED; }
}
}
