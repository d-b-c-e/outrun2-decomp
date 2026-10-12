// dbce_shim.h's implementation: the reviewed ProviderHost (windows/host) over the reviewed Core and DInputProvider
// (windows/shim), with the reviewed control worker, journal and shared frame recorder, behind the C ABI. This file only
// validates, converts, pairs capture rows with their frames and owns handles.
#ifndef DBCE_SHIM_BUILD
#define DBCE_SHIM_BUILD
#endif
#include "dbce_shim_internal.hpp"
#include "dbce_shim_bridge.h"
#include "bridge_provider.hpp"
#include "control_journal.hpp"
#include "frame_recorder.hpp"
#include "journal_identity.hpp"
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
namespace c = dbce::shim::control;
namespace rec = dbce::shim::recorder;
constexpr const char* CoreVersion = "input-shim core (dbce_shim ABI 2)";

static_assert(DBCE_SHIM_ACTIONS == sh::ActionCount, "the ABI's action count is Core's");
static_assert(sizeof(dbce_shim_action) == 8 && offsetof(dbce_shim_action, status) == 4, "dbce_shim_action layout");
static_assert(sizeof(dbce_shim_frame) == 296 && offsetof(dbce_shim_frame, seq) == 8 && offsetof(dbce_shim_frame, run) == 40 &&
              offsetof(dbce_shim_frame, sample_count) == 44 && offsetof(dbce_shim_frame, action) == 48, "dbce_shim_frame layout");
static_assert(sizeof(dbce_shim_open_args) == 128 && offsetof(dbce_shim_open_args, profile_len) == 8 &&
              offsetof(dbce_shim_open_args, fences_verified) == 12 && offsetof(dbce_shim_open_args, folder) == 16 &&
              offsetof(dbce_shim_open_args, capture_folder) == 72 && offsetof(dbce_shim_open_args, hz) == 112 &&
              offsetof(dbce_shim_open_args, seconds) == 120, "dbce_shim_open_args layout");

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
using Host = h::ProviderHost<DllRecorder>;

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
    if (!text) return true;
    std::istringstream in(text); std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        const auto eq = line.find('=');
        const int i = eq == std::string::npos ? -1 : sh::action_index(line.substr(0, eq));
        if (i < 0) { why = "unsupported: not an \"action=reason\" line of a known action: " + line.substr(0, 80); return false; }
        s.reasons.push_back(line.substr(eq + 1));
        caps.unsupported[std::size_t(i)] = s.reasons.back().c_str();
    }
    return true;
}

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
        j += ",\"interfacePath\":"; if (!json_string(j, r.identity.interface_path, 1560)) j += "\"\"";
        j += ",\"epoch\":" + std::to_string(r.epoch) + ",\"opened\":" + (r.opened ? "true" : "false") + ",\"status\":";
        if (!json_string(j, r.status, 1560)) j += "null";
        j += "}";
    }
    return j + "]";
}

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

// One open for both entries: v1's ordinary host and v2's arguments.
int32_t open_impl(IDirectInput8W* input, const dbce_shim_open_args& a, void** out, char* why, int32_t why_len,
                  std::shared_ptr<sh::BridgeProvider> bridge = {}) {
    auto refuse = [&](int32_t code, const std::string& text) { if (input) input->Release(); return fail(code, text, why, why_len); };
    try {
        if (!out) return refuse(DBCE_SHIM_BAD_ARGUMENT, "out is null");
        *out = nullptr;
        const bool latched = (a.flags & DBCE_SHIM_LATCHED) != 0, capture = (a.flags & DBCE_SHIM_CAPTURE) != 0;
        if (a.flags & ~std::uint32_t(DBCE_SHIM_LATCHED | DBCE_SHIM_CAPTURE)) return refuse(DBCE_SHIM_BAD_ARGUMENT, "unknown open flags");
        if ((!input && !bridge) || !a.folder || !a.nonce || !a.profile || a.profile_len < 0 || a.profile_len > (1 << 20))
            return refuse(DBCE_SHIM_BAD_ARGUMENT, "input, folder, nonce and a profile of at most 1 MiB are required");
        const DWORD attributes = GetFileAttributesW(a.folder);
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
            return refuse(DBCE_SHIM_BAD_ARGUMENT, "folder must be an existing folder");
        // Test-mode admission stays tied to the entry point's own verified fences and process-latched mute (Astra 18000).
        if (latched && (!a.session_file || !a.batch_file || a.fences_verified != 1 || a.force_absent != 1))
            return refuse(DBCE_SHIM_REFUSED, "a latched open needs both session files, the caller's verified fences and its process-latched force mute");
        auto shell = std::make_unique<Shim>();
        shell->bridge = bridge;
        shell->latched = latched; shell->capture = capture;
        if (capture) {
            std::string text;
            if (!a.capture_folder || !game_id(a.game_id) || !std::isfinite(a.hz) || a.hz <= 0 || !std::isfinite(a.seconds) || a.seconds <= 0)
                return refuse(DBCE_SHIM_BAD_ARGUMENT, "a capture needs its folder, a [a-z0-9.-] game id of 1-64 and a positive rate and duration");
            if (!game_channels(a.game_channels, shell->game, text)) return refuse(DBCE_SHIM_BAD_ARGUMENT, text);
        }
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
            if (parsed.ok) for (const auto& e : parsed.section.bound) instances.insert(e.binding.dev);
        }
        auto recorder = std::make_unique<DllRecorder>();
        if (capture) {
            if (instances.empty()) return refuse(DBCE_SHIM_BAD_ARGUMENT, "a capture needs a profile that binds a device");
            rec::FrameCaptureSettings cs;
            cs.folder = a.capture_folder; cs.profile = bytes; cs.run_nonce = nonce; cs.adapter_version = adapter;
            cs.game = a.game_id; cs.bundle_meta = rec::generic_bundle_meta(a.game_id); cs.context = a.context ? a.context : "";
            cs.hz = a.hz; cs.seconds = a.seconds;
            // The worst case (every bound instance a slot) refuses the open rather than the first row.
            rec::Channels probe = rec::encode(sh::Frame{}, instances.size());
            for (const auto& n : shell->game) probe[n] = 0;
            probe["game.valid"] = 0;
            try { rec::preflight(rec::schema(probe), cs.hz, cs.seconds, cs.limits); }
            catch (const std::exception& e) { return refuse(DBCE_SHIM_BAD_ARGUMENT, std::string("capture preflight: ") + e.what()); }
            recorder->frames.emplace(std::move(cs));
        }
        // From here the host owns the DirectInput reference, also when its construction throws.
        auto* in = input; input = nullptr;
        if (bridge) shell->host = std::make_unique<Host>(a.folder,nonce,bridge,bytes,latched,std::move(recorder),caps);
        else shell->host = std::make_unique<Host>(a.folder, nonce, in, bytes, latched, std::move(recorder), caps, static_cast<HWND>(a.hwnd));
        if (capture) shell->host->recorder().set_devices(shell->host->device_reports());   // one sample slot per profile device
        if (latched) {   // the reviewed control worker and journal, in the run folder
            c::JournalLimits limits; limits.cap_bytes = c::LatchedJournalCap;
            auto io = std::make_shared<c::FileIo>(a.session_file, a.batch_file, a.folder, false,
                                                  h::journal_identity(*shell->host, nonce, bytes, CoreVersion, adapter.c_str()), limits);
            shell->host->start_control(io, {true, a.fences_verified == 1, a.force_absent == 1, nonce});
        }
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
int32_t dbce_shim_open2_with_input(IDirectInput8W* input, const dbce_shim_open_args* args, void** out, char* why, int32_t why_len) {
    if (!args || args->struct_size != sizeof(dbce_shim_open_args)) {
        if (input) input->Release();
        if (out) *out = nullptr;
        return fail(DBCE_SHIM_BAD_ARGUMENT, "args is null or not dbce_shim_open_args_size() bytes", why, why_len);
    }
    return open_impl(input, *args, out, why, why_len);
}

extern "C" {
int32_t dbce_shim_abi_version(void) { return DBCE_SHIM_ABI; }
int32_t dbce_shim_frame_size(void) { return int32_t(sizeof(dbce_shim_frame)); }
int32_t dbce_shim_open_args_size(void) { return int32_t(sizeof(dbce_shim_open_args)); }
const char* dbce_shim_action_name(int32_t index) { return index >= 0 && index < sh::ActionCount ? sh::action_name(index) : nullptr; }

int32_t dbce_shim_open(const wchar_t* folder, const char* nonce, const char* profile, int32_t profile_len,
                       const char* unsupported, void* hwnd, void** out, char* why, int32_t why_len) {
    IDirectInput8W* input = direct_input();
    if (!input) { if (out) *out = nullptr; return fail(DBCE_SHIM_REFUSED, "DirectInput8Create failed", why, why_len); }
    return dbce_shim_open_with_input(input, folder, nonce, profile, profile_len, unsupported, hwnd, out, why, why_len);
}
int32_t dbce_shim_open2(const dbce_shim_open_args* args, void** out, char* why, int32_t why_len) {
    if (!args || args->struct_size != sizeof(dbce_shim_open_args)) return dbce_shim_open2_with_input(nullptr, args, out, why, why_len);
    IDirectInput8W* input = direct_input();
    if (!input) { if (out) *out = nullptr; return fail(DBCE_SHIM_REFUSED, "DirectInput8Create failed", why, why_len); }
    return dbce_shim_open2_with_input(input, args, out, why, why_len);
}

static int32_t tick_impl(Shim* s, dbce_shim_frame* frame) {
    if (!frame || frame->struct_size != sizeof(dbce_shim_frame)) return DBCE_SHIM_BAD_ARGUMENT;
    try {
        push_pending(*s);
        const auto& f = s->host->tick();
        hold_pending(*s, f);
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
int32_t dbce_shim_tick(void* handle, dbce_shim_frame* frame) {
    Shim* s=nullptr;if(const auto r=lookup(handle,s))return r;
    if(s->bridge)return DBCE_SHIM_BAD_ARGUMENT;
    return tick_impl(s,frame);
}

static_assert(sizeof(dbce_shim_bridge_device_v1)==144 && sizeof(dbce_shim_bridge_sample_v1)==232,"bridge extension ABI");
int32_t dbce_shim_bridge_abi_version(void){return 1;}
int32_t dbce_shim_bridge_device_size(void){return sizeof(dbce_shim_bridge_device_v1);}
int32_t dbce_shim_bridge_sample_size(void){return sizeof(dbce_shim_bridge_sample_v1);}
static int64_t bridge_clock(){LARGE_INTEGER f{},t{};if(!QueryPerformanceFrequency(&f)||f.QuadPart<=0||!QueryPerformanceCounter(&t))return -1;
    return (t.QuadPart/f.QuadPart)*1000000+(t.QuadPart%f.QuadPart)*1000000/f.QuadPart;}
static std::string bridge_guid(const uint8_t* bytes){GUID g{};std::memcpy(&g,bytes,sizeof(g));char text[40]{};
    std::snprintf(text,sizeof(text),"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",static_cast<unsigned long>(g.Data1),g.Data2,g.Data3,
        g.Data4[0],g.Data4[1],g.Data4[2],g.Data4[3],g.Data4[4],g.Data4[5],g.Data4[6],g.Data4[7]);return text;}
int32_t dbce_shim_bridge_open(const dbce_shim_open_args* args,void** out,char* why,int32_t why_len){
    if(out)*out=nullptr;
    if(!args || args->struct_size!=sizeof(*args))return fail(DBCE_SHIM_BAD_ARGUMENT,"bridge args size",why,why_len);
    if(args->flags)return fail(DBCE_SHIM_REFUSED,"bridge control/capture provenance is not qualified",why,why_len);
    try{return open_impl(nullptr,*args,out,why,why_len,std::make_shared<sh::BridgeProvider>(bridge_clock));}
    catch(...){return fail(DBCE_SHIM_REFUSED,"bridge allocation failed",why,why_len);}
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
int32_t dbce_shim_bridge_tick(void* handle,const dbce_shim_bridge_sample_v1* samples,int32_t count,dbce_shim_frame* frame){
    Shim* s=nullptr;if(const auto r=lookup(handle,s))return r;
    if(!s->bridge)return DBCE_SHIM_BAD_ARGUMENT;
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
        const auto result=tick_impl(s,frame);s->bridge->finish_frame();return result;
    }catch(...){s->bridge->finish_frame();return DBCE_SHIM_REFUSED;}
}

int32_t dbce_shim_record(void* handle, uint64_t frame_seq, const double* values, int32_t count) {
    Shim* s = nullptr;
    if (const auto r = lookup(handle, s)) return r;
    if (!s->capture) return DBCE_SHIM_BAD_ARGUMENT;
    auto& p = s->pending;
    if (!p.active || p.recorded || frame_seq != p.frame.seq) return DBCE_SHIM_STALE;   // changes nothing: the first call stands
    p.recorded = true;
    if (!values || count != int32_t(s->game.size())) return DBCE_SHIM_BAD_ARGUMENT;      // recorded with game.valid=0
    for (int32_t i = 0; i < count; ++i) if (!std::isfinite(values[i])) return DBCE_SHIM_BAD_ARGUMENT;
    p.values.assign(values, values + count); p.valid = true;
    return DBCE_SHIM_OK;
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

int32_t dbce_shim_stop(void* handle, char* report, int32_t len, int32_t* needed) {
    Shim* s = nullptr;
    if (const auto r = lookup(handle, s)) return r;
    s->stopped = true;   // spent whatever happens below
    try {
        // P21 order, as OutRunDesktop's shutdown: the capture's device table, the pending row, control admission closed,
        // one final Core tick (recorded unconsumed: the caller never applied it), then the host's bounded stop.
        if (s->capture) s->host->recorder().set_devices(s->host->device_reports());
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
        if (s->capture) t += std::string("; capture ") + (r.recorder == h::RecOutcome::Complete ? "complete" : "not complete") +
                             (s->host->recorder().frames->refusal().empty() ? "" : " (" + s->host->recorder().frames->refusal() + ")");
        if (r.resolved) s->host.reset();     // every worker joined: the host can go
        else (void)s->host.release();        // P21: never torn down under a running worker; alive until process exit
        (void)text_out(t, report, len, needed);   // the resolution is the result even when the report does not fit
        return r.resolved ? DBCE_SHIM_OK : DBCE_SHIM_UNRESOLVED;
    } catch (...) { (void)s->host.release(); return DBCE_SHIM_REFUSED; }
}
}
