// dbce_shim.h's implementation: the reviewed ProviderHost (windows/host) over the reviewed Core and DInputProvider
// (windows/shim), unchanged, behind the C ABI. This file only validates, converts and owns handles.
#define DBCE_SHIM_BUILD
#include "dbce_shim_internal.hpp"
#include "control_journal.hpp"
#include "provider_host.hpp"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <deque>
#include <mutex>
#include <set>
#include <sstream>

namespace {
namespace h = dbce::shim::hosting;
namespace sh = dbce::shim;

static_assert(DBCE_SHIM_ACTIONS == sh::ActionCount, "the ABI's action count is Core's");
static_assert(sizeof(dbce_shim_action) == 8 && offsetof(dbce_shim_action, status) == 4, "dbce_shim_action layout");
static_assert(sizeof(dbce_shim_frame) == 296 && offsetof(dbce_shim_frame, seq) == 8 && offsetof(dbce_shim_frame, run) == 40 &&
              offsetof(dbce_shim_frame, sample_count) == 44 && offsetof(dbce_shim_frame, action) == 48, "dbce_shim_frame layout");

// Version 1 records nothing: no worker to drain, so the recorder phase is complete by construction.
struct NoCapture {
    void finish() {}
    struct Drained { h::RecOutcome outcome; bool joined; };
    Drained drain(h::Clock::time_point) { return {h::RecOutcome::Complete, true}; }
};
using Host = h::ProviderHost<NoCapture>;

// A handle's shell is never freed (a few bytes per open), so a spent handle stays recognisable; the registry lets a
// call refuse an unknown pointer without reading through it.
struct Shim {
    std::thread::id owner = std::this_thread::get_id();
    std::deque<std::string> reasons;   // the capability's text, alive as long as Core may read it
    std::unique_ptr<Host> host;
    bool stopped = false;
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
bool capability(const char* text, Shim& s, sh::Capability& c, std::string& why) {
    if (!text) return true;
    std::istringstream in(text); std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        const auto eq = line.find('=');
        const int i = eq == std::string::npos ? -1 : sh::action_index(line.substr(0, eq));
        if (i < 0) { why = "unsupported: not an \"action=reason\" line of a known action: " + line.substr(0, 80); return false; }
        s.reasons.push_back(line.substr(eq + 1));
        c.unsupported[std::size_t(i)] = s.reasons.back().c_str();
    }
    return true;
}

// The DLL's code must outlive every worker the host may leave running (DInputProvider: "the code module is never
// unloaded"): pin it, so no FreeLibrary can unload it under a thread.
bool pin_module() {
    HMODULE self = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&pin_module), &self) != FALSE;
}

std::string device_json(const std::vector<sh::DeviceReport>& devices) {
    using dbce::shim::control::journal_detail::json_string;
    std::string j = "[";
    for (std::size_t d = 0; d < devices.size(); ++d) {
        const auto& r = devices[d];
        j += std::string(d ? "," : "") + "{\"index\":" + std::to_string(d) + ",\"instance\":"; json_string(j, r.identity.dev, 128);
        j += ",\"product\":"; json_string(j, r.identity.prod, 128); j += ",\"name\":"; if (!json_string(j, r.identity.name, 1560)) j += "null";
        j += ",\"epoch\":" + std::to_string(r.epoch) + ",\"opened\":" + (r.opened ? "true" : "false") + ",\"status\":";
        if (!json_string(j, r.status, 1560)) j += "null";
        j += "}";
    }
    return j + "]";
}
}

int32_t dbce_shim_open_with_input(IDirectInput8W* input, const wchar_t* folder, const char* nonce, const char* profile,
                                  int32_t profile_len, const char* unsupported, void* hwnd, void** out, char* why, int32_t why_len) {
    auto refuse = [&](int32_t code, const std::string& text) { if (input) input->Release(); return fail(code, text, why, why_len); };
    try {
        if (!out) return refuse(DBCE_SHIM_BAD_ARGUMENT, "out is null");
        *out = nullptr;
        if (!input || !folder || !nonce || !profile || profile_len < 0 || profile_len > (1 << 20))
            return refuse(DBCE_SHIM_BAD_ARGUMENT, "input, folder, nonce and a profile of at most 1 MiB are required");
        const DWORD attributes = GetFileAttributesW(folder);
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
            return refuse(DBCE_SHIM_BAD_ARGUMENT, "folder must be an existing folder");
        if (!pin_module()) return refuse(DBCE_SHIM_REFUSED, "cannot pin the shim module");
        auto shell = std::make_unique<Shim>();
        sh::Capability caps; std::string text;
        if (!capability(unsupported, *shell, caps, text)) return refuse(DBCE_SHIM_BAD_ARGUMENT, text);
        // From here the host owns the DirectInput reference, also when its construction throws.
        auto* in = input; input = nullptr;
        shell->host = std::make_unique<Host>(folder, nonce, in, std::string(profile, std::size_t(profile_len)), false,
                                             std::make_unique<NoCapture>(), caps, static_cast<HWND>(hwnd));
        { std::lock_guard<std::mutex> l(registry_lock); registry.insert(shell.get()); }
        *out = shell.release();
        return DBCE_SHIM_OK;
    } catch (const std::exception& e) { return refuse(DBCE_SHIM_REFUSED, std::string("host refused: ") + e.what()); }
    catch (...) { return refuse(DBCE_SHIM_REFUSED, "host refused"); }
}

extern "C" {
int32_t dbce_shim_abi_version(void) { return DBCE_SHIM_ABI; }
int32_t dbce_shim_frame_size(void) { return int32_t(sizeof(dbce_shim_frame)); }
const char* dbce_shim_action_name(int32_t index) { return index >= 0 && index < sh::ActionCount ? sh::action_name(index) : nullptr; }

int32_t dbce_shim_open(const wchar_t* folder, const char* nonce, const char* profile, int32_t profile_len,
                       const char* unsupported, void* hwnd, void** out, char* why, int32_t why_len) {
    IDirectInput8W* input = nullptr;
    if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void**>(&input), nullptr)) || !input) {
        if (out) *out = nullptr;
        return fail(DBCE_SHIM_REFUSED, "DirectInput8Create failed", why, why_len);
    }
    return dbce_shim_open_with_input(input, folder, nonce, profile, profile_len, unsupported, hwnd, out, why, why_len);
}

int32_t dbce_shim_tick(void* handle, dbce_shim_frame* frame) {
    Shim* s = nullptr;
    if (const auto r = lookup(handle, s)) return r;
    if (!frame || frame->struct_size != sizeof(dbce_shim_frame)) return DBCE_SHIM_BAD_ARGUMENT;
    try {
        const auto& f = s->host->tick();
        frame->abi = DBCE_SHIM_ABI; frame->seq = f.seq; frame->tick_us = f.tick_us; frame->down = f.down; frame->up = f.up;
        frame->run = std::uint8_t(f.run); frame->latched = f.latched; frame->suppress_native = f.suppress_native; frame->reserved0 = 0;
        frame->sample_count = std::uint32_t(f.samples.size());
        for (int i = 0; i < sh::ActionCount; ++i) {
            const auto& a = f.action[std::size_t(i)]; auto& o = frame->action[i];
            o.value = a.value; o.status = std::uint8_t(a.status); o.injected = a.injected; o.reserved[0] = o.reserved[1] = 0;
        }
        return DBCE_SHIM_OK;
    } catch (...) { return DBCE_SHIM_REFUSED; }
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

int32_t dbce_shim_stop(void* handle, char* report, int32_t len, int32_t* needed) {
    Shim* s = nullptr;
    if (const auto r = lookup(handle, s)) return r;
    s->stopped = true;   // spent whatever happens below
    try {
        const auto r = s->host->stop();
        std::string t = r.resolved ? "resolved" : "unresolved:";
        for (const auto& u : r.unresolved) t += " " + u + ";";
        if (r.resolved) s->host.reset();     // every worker joined: the host can go
        else (void)s->host.release();        // P21: never torn down under a running worker; alive until process exit
        (void)text_out(t, report, len, needed);   // the resolution is the result even when the report does not fit
        return r.resolved ? DBCE_SHIM_OK : DBCE_SHIM_UNRESOLVED;
    } catch (...) { (void)s->host.release(); return DBCE_SHIM_REFUSED; }
}
}
