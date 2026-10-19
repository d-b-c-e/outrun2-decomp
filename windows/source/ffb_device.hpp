#pragma once
// OutRun force feedback, F4: the software force stream (ffb_live.hpp) delivered to the steering wheel through the
// wheel toolkit's WheelFfb.dll (vendor/wheelffb), loaded at run time from the game folder. Off unless [FFB] Output =
// wheel. The gates, in order; the first that fails keeps the wheel silent:
//   load    Output = wheel; not DBCE_FFB_MUTE=1 (STD-035 item 8: the library stays unloaded); not a latched (test)
//           process; [Controls] steer names a device instance (dev=); the DLL is 0.9.0 or newer and has every export
//           used here (with the shim input backend also the read slots and GetReadSlotCaps, native 0.11.0, which the
//           shim's WheelFfb bridge reads the wheel through). No library state is created before the open.
//   open    in the race and in front (an exclusive handle is not acquired behind another window, and the game never
//           takes the focus), with the input layer reading that same instance as the steering device; its product has
//           a measured force direction (wheel_direction: an unmeasured wheel is released before any device call, never
//           driven with a guessed sign); the instance is
//           one of the library's force-feedback devices; strict selection opens exactly it (never a name or an
//           index); the exit guards install; the hold watchdog is set (SetHoldTimeoutMs); then an accepted zero
//           (SetDeviceForcesXY(0, 0) returns 1) before anything else. One attempt per process.
//   force   in the race and in front, a command goes out when it changes, and a held force again every 100 ms, so
//           the library's 250 ms hold watchdog silences the wheel once the game stops sampling (a stall, a hang).
//   silence only an accepted SetDeviceForcesXY(0, 0) counts (ZeroForces returns nothing, and clears the watchdog's
//           record even when the stop failed). Out of the race or out of front, after a refused command, and after a
//           held force older than the hold: an accepted zero, tried every sample, before the next command; none
//           within 2 s releases the device. Any refusal other than the mod's transient set, or a change or loss of
//           the steering device, releases at once. A release is final for the process.
//   release and shutdown: PanicStop (when opened), then FreeDirectInput whenever the library was given a device
//           selection: it stops the effects, removes the exit guards' window subclass and joins the watchdog, so the
//           module may be unloaded afterwards (and only then).
//   owner   with the shim input backend the wheel's handles have one owner at a time (INPUT-SHIM.md section 6): an
//           Owner (the shim's WheelFfb bridge) closes the input's own handle before the library opens the wheel and
//           reads it through the library while it holds it, and the release hands it back after FreeDirectInput.
//           The hand-over never waits (romi 32201): it starts with the steer live (a valid, fresh sample of that
//           device) in an epoch E0, then the Owner is asked at each race sample in front until its own handle is
//           closed (leaving the race or the front cancels it); only then the open. After it only zeros go out until
//           the steer is live again in a new epoch E1 (the read through the force handle), within 2 s or a release;
//           a later epoch change releases; steering that is not live sends a zero (romi 31707). The library gate
//           (romi 32320): a sample that cannot enter it (the input's worker is inside the library) makes no library
//           call at all, so the game thread never waits for the worker; the hold and the expiry treat it as a missed
//           sample, and the library's own hold watchdog silences the wheel meanwhile. A release that cannot enter it
//           keeps the wheel the library's (retained: no PanicStop or FreeDirectInput, the input does not take the
//           wheel back, the library stays loaded) and is tried again by the next shutdown, which does not wait either.
//           Without an Owner (the native input layer) none of this applies and the input keeps its own non-exclusive
//           handle as before.
// Pure decisions over WheelFfbApi's function pointers: tests/test_ffb_delivery.cpp drives them with a fake library.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"   // the vendored header's static loader helpers
#endif
#include "wheelffb.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include <cstdint>
#include <string>
namespace outrun::desktop::ffbdev {
inline constexpr int MinVersion=900;                // WheelFfb 0.9.0
inline constexpr int HoldMs=250;                    // the library's hold watchdog (it polls every 50 ms)
inline constexpr std::uint64_t KeepAliveMs=100;     // the held value is sent again this often
inline constexpr std::uint64_t SilenceMs=2000;      // an accepted zero is due within this
struct Config {
    bool output{};             // [FFB] Output = wheel
    bool muted{};              // DBCE_FFB_MUTE=1
    bool latched{};            // a test process
    bool shim{};               // [Input] Backend = Shim: the shim owns the wheel's handles; it hands them over (Owner)
    std::string steer_dev;     // [Controls] steer dev= ("{...}"); empty when steer names no device
};
// Why the library may not be loaded ("" when it may).
std::string load_refusal(const Config&);
// "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}", any case; nothing else.
bool parse_guid(const std::string&,GUID&);
// The SetDeviceForcesXY x sign that pushes a steering device's wheel toward positive steer, by its product GUID. The
// force model's output (force_model.h) pushes toward positive steer when positive, and the steer is the device's
// DirectInput X axis, which increases to the right; which x sign does that is a per-wheel fact, measured, never derived
// (toolkit knowledge/DIRECTINPUT-FFB.md). MOZA R12 Base {0006346e-0000-0000-0000-504944564944}: -1. A positive x turns
// it to the left: art-of-sim-rally through this library's encoding (2026-09-02: +Fy, which has the steer's sign,
// centres; a left turn pushes the wheel right) and cruisn-collection (2026-08: a positive SDL level turns it left).
// 0 for any other product, empty or malformed text: not measured.
int wheel_direction(const std::string& product);
enum class Phase { Off, Waiting, Open, Released, Stopped };
const char* phase_name(Phase);
// One input sample of the game.
struct Sample {
    bool race{};               // mode 16
    bool front{};              // the game window is this process's and the foreground window
    void* hwnd{};
    std::string steer_dev;     // the input layer's connected steering device ("" none)
    std::string steer_prod;    // that device's product GUID, as the input layer verified it ("" not known)
    std::uint64_t ms{};        // a monotonic clock
    bool steer_live{};         // with an Owner: the steer action is applied from a valid, fresh sample of that device
    std::uint32_t steer_epoch{};   // with an Owner: that device's input epoch
};
// The input's owner of the wheel's handles when it is not the library's own reader (see "owner" above). Game thread.
class Owner {
public:
    virtual ~Owner()=default;
    // True once the input's own handle of the wheel is closed (the library may open it now); false: not yet, asked
    // again with the next race sample. Never waits.
    virtual bool begin_open()=0;
    virtual void end_open(bool held)=0;   // held: the library holds the wheel; else the hand-over ends (failed or cancelled)
    virtual void begin_release()=0;       // before FreeDirectInput: the input stops reading through the library
    virtual void end_release()=0;         // after it: the input reads the wheel on its own handle again
    virtual bool library_quiet()const=0;  // no call of the input's is inside the library or can start: it may be unloaded
    virtual bool enter_library()=0;       // the library gate, never waited for: false while the input's worker is inside
    virtual void leave_library()=0;
};
class Delivery {
public:
    // Once per process, after the load: the exports and the version; no device and no library state. False: why().
    bool start(const WheelFfbApi&,const Config&);
    // With the shim input backend, before the open (Waiting only): the wheel's owner. Config::shim never opens without.
    void set_owner(Owner*);
    // Once per input sample; `command` (SetDeviceForcesXY's x: DirectInput units, already in the device's direction,
    // wheel_direction) is used in the race only. The open refuses a Sample whose steer_prod has no measured direction.
    void frame(const Sample&,std::int32_t command);
    // PanicStop when opened, FreeDirectInput when the library holds any state; the library is not called again.
    void shutdown();
    Phase phase()const{return phase_;}
    const std::string& why()const{return why_;}
    bool silent()const{return silent_;}         // the last command the wheel accepted was zero
    bool retained()const{return retained_;}     // a release could not enter the library gate: the library keeps the wheel
    struct Counts { std::uint32_t sent{},zeroes{},recoveries{},transient{},keepalive{}; };
    const Counts& counts()const{return counts_;}
private:
    bool same_device(const std::string&)const;
    void open_device(const Sample&);
    bool zero(const Sample&);
    bool refused(const Sample&);
    void release(const std::string& why);
    void quiesce();
    struct Gate;               // the library gate for one sample or one release (owner mode)
    WheelFfbApi api_{};
    Config cfg_{};
    GUID guid_{};
    Phase phase_{Phase::Off};
    std::string why_;
    bool native_{};            // the library was given a device selection: FreeDirectInput is owed
    bool opened_{};            // InitDirectInput succeeded: PanicStop at the end
    bool silent_{true};        // the last accepted command was zero
    bool need_zero_{};         // an accepted zero before the next command
    bool unacked_{};           // a zero is due and not yet accepted, since unacked_ms_
    std::uint64_t unacked_ms_{};
    std::int32_t last_value_{};
    std::uint64_t last_send_ms_{};   // the last accepted command or zero
    Counts counts_{};
    Owner* owner_{};
    bool handing_over_{};      // the hand-over started (E0 recorded); begin_open has not said yes yet
    bool promoting_{};         // begin_open said yes and end_open was not called yet
    bool held_{};              // end_open(true): the owner reads the wheel through the library
    bool in_library_{};        // this delivery holds the library gate now
    bool retained_{};          // see retained()
    bool shared_{};            // the steer is live in the epoch read through the force handle (e1_)
    std::uint32_t e0_{},e1_{};
    std::uint64_t opened_ms_{};
};
}
