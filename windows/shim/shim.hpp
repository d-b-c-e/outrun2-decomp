#pragma once
// Input shim core, slice 1 (windows/docs/INPUT-SHIM.md r3.1): the profile reader, session and batch checks, device
// epochs and rearm, the resolver with statuses and provenance, and the test-run state machine. Pure C++17: no Windows
// headers. The real DirectInput provider, the observer transport, the recorder, the profile writer and the game
// adapter are later slices.
//
// Threads (section 4): the provider worker calls Core::service_devices; the control worker calls request_admission,
// refuse_admission, offer_batch and the drains; the game input thread calls tick. Nothing here touches a file.
#include "dbce_controls.hpp"
#include "dbce_inject.hpp"
#include "sha256.hpp"
#include <array>
#include <bitset>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
namespace dbce::shim {
namespace ctl=dbce::controls;

// ---- actions (the contract's ids, in ctl::kActions order)
constexpr int ActionCount=31;
int action_index(const std::string& id);   // -1 when unknown
const char* action_name(int index);
bool is_digital(int index);

// ---- raw device state: the DIJOYSTATE2 objects the contract binds
struct RawState {
    std::int32_t axes[8]{};                                   // in the device's read-back range
    std::uint8_t buttons[128]{};                              // DirectInput bytes: down when 0x80 is set
    std::uint32_t povs[4]{0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu};   // centred
};
// What an open device has, read back after the open (section 6: by offset, never by counting).
struct ObjectCaps {
    std::array<bool,8> axis{};
    std::array<bool,8> range_ok{};                            // range readable after the 0..65535 request
    std::array<std::int32_t,8> min{},max{};
    std::bitset<128> button;
    std::array<bool,4> pov{};
};
struct DeviceInfo {
    std::string dev,prod,name;                            // GUID text, lower case
    // Exact UTF-8 DIPROP_GUIDANDPATH from the opened handle, never a name/listing guess. Optional: empty when unavailable.
    std::string interface_path{};
};

// ---- the device provider: the single owner of device handles (section 6). The core never opens a device itself.
// One API call, classified where it was made. The numeric values are part of the observer and recorder contract:
// append only, never renumber (slice 3 review). Pending (E_PENDING, data not yet available) is transient: the device
// stays open and is retried, within CoreConfig::pending_limit_us.
enum class Call : std::uint8_t { Ok=0,AcquisitionLost=1,Gone=2,NotCalled=3,Pending=4 };
static_assert(int(Call::Ok)==0&&int(Call::AcquisitionLost)==1&&int(Call::Gone)==2&&int(Call::NotCalled)==3&&int(Call::Pending)==4,
              "Call values are a published contract");
using Handle=std::uint64_t;                                   // provider-defined; 0 = none
struct OpenResult { bool ok{}; std::string why; DeviceInfo opened; ObjectCaps caps; Handle handle{}; };
class Provider {
public:
    virtual ~Provider()=default;
    // Provider worker.
    virtual std::vector<DeviceInfo> enumerate()=0;           // attached now
    virtual OpenResult open(const DeviceInfo& listed)=0;      // `opened` is the identity read back from the new handle
    virtual Call reacquire(Handle)=0;                         // the same verified handle
    virtual void close(Handle)=0;
    // Game input thread. poll() never fills state; state() fills `out` only when it returns Ok.
    virtual Call poll(Handle)=0;
    virtual Call state(Handle,RawState& out)=0;
    // Any thread: monotonic microseconds.
    virtual std::int64_t now_us()=0;
};

// ---- statuses (section 7)
enum class Status : std::uint8_t {
    Unbound,Invalid,Unsupported,
    DeviceMissing,ProductMismatch,ObjectMissing,RangeUnqualified,Opening,ReadPending,ReadFailed,Stale,   // unavailable
    Rearming,Live,TestNeutral                                                                       // applied
};
const char* status_name(Status);
bool is_unavailable(Status);
bool is_applied(Status);

// ---- the profile file (section 5): strict reader over the file's bytes
struct Profile {
    bool ok{};                    // structurally valid: [Controls] found once, no duplicate keys, UTF-8, bounded
    std::string error;            // why not
    ctl::Section section;         // the contract's parse of [Controls] (its own Schema / invalid handling)
    Sha256 file_hash{};
};
// `reads`: the highest controls schema the caller's readers all apply (STD-037 item 4). 1, the default, is what the DLL
// ABI (dbce_shim.cpp) and every existing caller use; 2 only for a consumer whose input, force-target and shaping readers
// all opt in. ok=1 means the bytes parsed; whether the section applies is section.error.
Profile read_profile(const std::string& bytes,int reads=1);
constexpr std::size_t MaxProfileBytes=64*1024;

// ---- the session file (section 9): exactly nonce= and expires=, nothing else
struct Session { std::string nonce; std::uint64_t expires_unix{}; };
bool parse_session(const std::string& bytes,std::uint64_t now_unix,Session& out,std::string& why);
bool strict_utf8(const std::string& bytes);
inline std::string bounded_diagnostic(const std::string& text,std::size_t limit=512){
    if(!strict_utf8(text))return "diagnostic contained non-UTF-8 bytes";
    if(text.size()<=limit)return text;
    auto end=limit;
    while(end>0&&(static_cast<unsigned char>(text[end])&0xc0)==0x80)--end;
    return text.substr(0,end);
}

// ---- batches (section 9)
struct Command {
    std::string dev;              // device instance
    ctl::Kind kind{};
    int index{},angle{-1};
    long raw{};                   // in [bind_min, bind_max] for axes; 1 / 0 buttons; angle / -1 hats
    long bind_min{},bind_max{};
    int ms{};
    std::string action;           // "" for inject raw
};
struct Generation {
    std::uint64_t n{};
    Sha256 hash{};
    std::vector<Command> commands;
    bool end{};                   // the batch's only command is "end"
    bool used_raw{},used_action{};
    std::int64_t accepted_us{};
};
struct OwnedDevice { std::string dev; ObjectCaps caps; };
struct BatchContext {
    std::string nonce;
    bool have_last{};
    std::uint64_t last_n{};
    Sha256 last_hash{};
    const ctl::Section* profile{};
    std::vector<OwnedDevice> owned;   // the admitted workload's devices and their caps
};
enum class BatchVerdict : std::uint8_t { Accepted,Duplicate,Refused };
struct BatchResult { BatchVerdict verdict{}; std::string why; Generation generation; };
BatchResult check_batch(const std::string& bytes,const BatchContext&);
constexpr std::size_t MaxBatchBytes=4096;
constexpr int MaxBatchCommands=32,MaxRunningObjects=16;

// ---- the frame (section 7)
struct InjectionMark {
    std::uint64_t batch{};
    std::uint8_t hash[8]{};
    std::uint8_t axes{};
    std::bitset<128> buttons;
    std::uint8_t povs{};
    bool any()const{return axes||povs||buttons.any();}
};
struct DeviceSample {
    std::uint32_t device{},epoch{};
    std::uint64_t seq{};
    std::int64_t read_start_us{},read_end_us{};
    Call poll{Call::NotCalled},state{Call::NotCalled};
    bool valid{};
    bool stale{};                 // valid, but older than the freshness bound at the frame's decision time
    RawState physical;            // as read
    RawState delivered;           // after injection (equal to physical when nothing was injected)
    InjectionMark injected;
};
struct ActionValue {
    float value{};                // steer -1..1, pedal 0..1, digital 0 / 1; rest unless live or injected
    Status status{Status::Unbound};
    std::int16_t sample{-1};      // index into Frame::samples, -1 none
    bool injected{};
};
enum class Run : std::uint8_t { None,Latched,Waiting,Running,Completed,Expired,Failed,Inert };
const char* run_name(Run);
bool run_terminal(Run);
struct Frame {
    std::uint64_t seq{};
    std::int64_t tick_us{};       // when tick() began
    std::int64_t boundary_us{};   // re-read after every device read: admission, expiry, activation and freshness use it
    std::uint64_t event_seq{};    // the last event's sequence when this frame was built
    Run run{Run::None};
    bool latched{},suppress_native{};
    std::vector<DeviceSample> samples;
    std::array<ActionValue,ActionCount> action{};
    std::uint64_t down{},up{};    // digital edges against the previous frame, bit = action index
};

// ---- events (section 9): one writer (the input thread); the control worker's events are handed to it
// Numeric values are part of the observer and recorder contract: append only.
enum class EventKind : std::uint8_t {
    EpochBegin=0,EpochEnd=1,BatchAccepted=2,BatchDuplicate=3,BatchRefused=4,BatchActivated=5,BatchSuperseded=6,ObjectExpired=7,
    RunChanged=8,ReadPending=9
};
static_assert(int(EventKind::RunChanged)==8&&int(EventKind::ReadPending)==9,"EventKind values are a published contract");
const char* event_name(EventKind);
struct Event {
    std::uint64_t seq{};
    std::int64_t us{};            // when it happened (acceptance: the control worker's time)
    EventKind kind{};
    std::int32_t device{-1};
    std::uint32_t epoch{};
    std::uint64_t batch{};
    std::string detail;
};
constexpr std::size_t EventRingSize=64;

// ---- status changes for the structured status (section 12): coalesced per key, bounded
struct StatusChange {
    std::int64_t us{};
    enum class Of : std::uint8_t { Action,Device,Run } of{};
    int index{};
    std::string status,reason;
    // Input-thread decision-time identity, never looked up by a later drain.
    // Joins the immutable profile device table; -1/0 means no device/epoch.
    std::int32_t device{-1};
    std::uint32_t epoch{};
};
// One consistent cut of both queues and their counters, taken under one lock (CONTROL-JOURNAL.md section 4):
// a status for an epoch is never drained ahead of that epoch's EpochBegin, which the input thread emits first.
struct JournalCut {
    std::vector<Event> events;
    std::vector<StatusChange> status;
    std::uint64_t status_dropped{},status_coalesced{},events_dropped{},control_events_dropped{};
};

// ---- the adapter's declaration (section 8, item 1): nullptr = supported, else why not
struct Capability { std::array<const char*,ActionCount> unsupported{}; };

struct CoreConfig {
    bool latched{};               // inject.on existed at process start (decided by the entry point)
    Capability capability;
    std::int64_t fresh_us=100000;             // a sample counts as fresh for this long
    double rest_threshold=0.05;               // throttle / handbrake rearm
    std::int64_t wait_first_us=120000000;     // WAITING: first batch
    std::int64_t admission_us=10000000;       // LATCHED: devices ready for admission
    int reacquire_tries=3;
    std::int64_t pending_limit_us=1000000;    // continuous E_PENDING for longer ends the epoch (reacquisition)
    int max_opens_per_pass=4;
    std::int64_t missing_rescan_us=250000;     // a missing device does not re-enumerate on every worker pass
    std::size_t status_queue=256,event_queue=1024,generation_queue=8,control_event_queue=64;
};

struct StartReport { std::string line; std::vector<std::string> notes; };
// Input-thread snapshot of an adopted device epoch. Identity is the bound identity verified against both the listing
// and the opened handle. No native handle escapes. Caps are empty unless that adopted epoch remains open.
struct DeviceReport {
    DeviceInfo identity;
    std::uint32_t epoch{};
    bool opened{};
    ObjectCaps caps;
    std::string status;
};

class Core {
public:
    Core(Provider&,CoreConfig);
    // Before the workers start. A latched process with a profile that is not usable goes INERT.
    void set_profile(const Profile&);

    // Provider worker: one bounded pass (enumeration when needed, at most max_opens_per_pass opens, reacquisition,
    // closes of epochs the input thread handed back). Returns true when it did anything.
    bool service_devices();

    // Control worker.
    void request_admission(const Session&,std::uint64_t now_unix,bool fences_verified,bool force_absent);
    void refuse_admission(const std::string& why);       // inject.on unreadable or refused, fences, ...
    BatchResult offer_batch(const std::string& bytes);  // checked, then queued for activation
    // Cross-thread control handoff. Never read input-thread run() from a worker.
    struct ControlState { Run run; bool closed; };
    ControlState control_state()const;
    void fail_control(const std::string& why);           // first I/O/journal failure; next tick is terminal neutral
    void close_control();                              // host stop gate; late I/O cannot enqueue or rearm
    std::vector<Event> drain_events();
    std::vector<StatusChange> drain_status();
    JournalCut drain_journal();                          // the control worker's one drain: one lock, one cut
    std::uint64_t status_dropped()const;
    std::uint64_t status_coalesced()const;
    std::uint64_t events_dropped()const;
    std::uint64_t control_events_dropped()const;

    // Game input thread.
    const Frame& tick();

    // Reports (input thread, or after it stopped).
    StartReport start_report()const;
    std::vector<DeviceReport> device_reports()const;
    const std::array<Event,EventRingSize>& ring()const{return ring_;}
    Run run()const{return run_;}
    std::string run_reason()const{return run_reason_;}

private:
    struct Bound { int action{}; ctl::Binding binding; int device{-1}; };
    enum class DevState : std::uint8_t { Missing,ProductMismatch,Opening,Open,ReadFailed };
    struct Device {
        DeviceInfo id;            // immutable after set_profile
        // Under m_: written by the provider worker.
        DevState worker_state{DevState::Missing};
        std::string worker_reason;
        // Input thread only.
        DevState state{DevState::Missing};
        std::string reason;
        Handle handle{};
        std::string interface_path;   // input thread: the adopted handle's path, cleared when its epoch ends
        ObjectCaps caps;
        std::uint32_t epoch{};
        std::uint64_t seq{};
        bool fresh_seen{};        // a valid read in this epoch
        bool pending{};           // in a run of pending reads (an explicit flag: 0 is a valid clock reading)
        std::int64_t pending_since_us{};   // the start of that run, valid while pending
        std::int64_t last_valid_us{};
        int sample{-1};           // this frame's sample index
    };
    struct Opened { int device; Handle handle; ObjectCaps caps; std::uint32_t epoch; std::string interface_path; };
    struct Returned { int device; Handle handle; Call why; std::uint32_t epoch; };
    struct Rearm { bool need_release{},need_rest{}; };
    struct Running { Command cmd; std::int64_t deadline_us{}; std::uint64_t batch{}; Sha256 hash{}; int device{-1}; };

    Provider& p_;
    CoreConfig cfg_;
    // Immutable after set_profile.
    Profile profile_;
    bool usable_{};
    std::vector<Bound> bound_;
    std::array<int,ActionCount> bound_of_{};          // index into bound_, -1 none
    std::array<bool,ActionCount> in_profile_{};       // bound in the profile (supported or not)
    std::array<Status,ActionCount> base_{};           // Unbound / Invalid / Unsupported, or Opening when bound
    std::array<std::string,ActionCount> base_reason_{};
    std::vector<std::string> invalid_keys_;           // every invalid line's key, for the start line
    std::vector<Device> devs_;

    // Cross-thread inboxes, all under m_.
    mutable std::mutex m_;
    std::vector<Opened> opened_;                      // worker -> input
    std::vector<Returned> returned_;                  // input -> worker
    std::deque<Generation> generations_;              // control -> input, first in first out, bounded
    std::vector<Event> pending_events_;               // control -> input, bounded by control_event_queue
    std::uint64_t control_events_dropped_{},duplicates_pending_{},duplicate_last_n_{},refusals_after_failure_{},admission_repeats_{};
    std::int64_t admission_deadline_us_{};            // the session's end on the monotonic clock, latched at the request
    bool admission_requested_{},admission_refused_{},batch_failed_{};
    bool control_closed_{},control_failed_{};
    std::string control_failure_;
    std::string admission_why_,batch_failed_why_;
    Session session_;
    std::uint64_t session_now_unix_{};
    bool fences_verified_{},force_absent_{};
    Run published_run_{Run::None};                    // input -> control
    std::vector<OwnedDevice> workload_snapshot_;      // input -> control at admission
    std::deque<StatusChange> status_q_;               // input -> control, coalesced per key
    std::uint64_t status_dropped_{},status_coalesced_{};
    std::deque<Event> event_q_;                       // input -> control (the log copy of the ring)
    std::uint64_t events_dropped_{};

    // Provider worker only.
    std::vector<std::uint32_t> worker_epochs_;
    std::vector<ObjectCaps> worker_caps_;
    std::vector<std::string> worker_interface_paths_; // same-handle reacquisition only; a new open replaces this
    std::int64_t missing_scan_after_us_{};

    // Control worker only.
    bool have_last_{};
    std::uint64_t last_n_{};
    Sha256 last_hash_{};

    // Input thread only.
    Frame frame_;
    Run run_{Run::None};
    std::string run_reason_;
    std::int64_t start_us_{},admitted_us_{},expires_us_{};
    std::vector<std::pair<int,std::uint32_t>> workload_;   // device, epoch at admission
    std::vector<Running> running_;
    std::array<Rearm,ActionCount> rearm_{};
    std::uint64_t prev_digital_{};
    std::array<Status,ActionCount> last_status_{};
    std::array<std::string,ActionCount> last_reason_{};
    std::array<std::uint32_t,ActionCount> last_status_epoch_{};
    std::vector<std::string> last_dev_status_;
    std::array<Event,EventRingSize> ring_{};
    std::uint64_t event_seq_{};

    void adopt_worker_changes(std::int64_t now);
    void evaluate_admission(std::int64_t now);
    void read_devices(std::int64_t now);
    bool workload_intact(std::string& why)const;
    void activate_generations(std::int64_t now);
    void deliver(std::int64_t now);
    void resolve(std::int64_t now);
    void set_run(Run,const std::string& why,std::int64_t now);
    void emit(EventKind,std::int64_t us,int device,std::uint32_t epoch,std::uint64_t batch,const std::string& detail);
    void emit_event(Event);
    void queue_status(StatusChange);
    void track_status(std::int64_t now);
    bool owned_ready()const;
    int usable_bindings()const;
    void push_control_event(Event);                   // m_ held
    std::string device_status(const Device&)const;
    std::string device_reason(const Device&)const;
};
}
