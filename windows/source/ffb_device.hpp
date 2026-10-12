#pragma once
// OutRun force feedback, F4: the software force stream (ffb_live.hpp) delivered to the steering wheel through the
// wheel toolkit's WheelFfb.dll (vendor/wheelffb), loaded at run time from the game folder. Off unless [FFB] Output =
// wheel. The gates, in order; the first that fails keeps the wheel silent:
//   load    Output = wheel; not DBCE_FFB_MUTE=1 (STD-035 item 8: the library stays unloaded); not a latched (test)
//           process; not the shim input backend (it holds the wheel's handle, and a second open of a force device is
//           never made: INPUT-SHIM.md section 6; its force lease is not built yet); [Controls] steer names a device
//           instance (dev=); the DLL is 0.9.0 or newer and has every export used here. No library state is created
//           before the open.
//   open    in the race and in front (an exclusive handle is not acquired behind another window, and the game never
//           takes the focus), with the input layer reading that same instance as the steering device; the instance is
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
    bool shim{};               // [Input] Backend = Shim: the shared input shim reads (and owns) the wheel
    std::string steer_dev;     // [Controls] steer dev= ("{...}"); empty when steer names no device
};
// Why the library may not be loaded ("" when it may).
std::string load_refusal(const Config&);
inline constexpr const char* ShimRefusal="the shared input shim reads the steering wheel ([Input] Backend = Shim), and wheel "
    "force needs the shim's force lease (INPUT-SHIM.md section 6), which is not built yet; the wheel is never opened twice";
// "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}", any case; nothing else.
bool parse_guid(const std::string&,GUID&);
enum class Phase { Off, Waiting, Open, Released, Stopped };
const char* phase_name(Phase);
// One input sample of the game.
struct Sample {
    bool race{};               // mode 16
    bool front{};              // the game window is this process's and the foreground window
    void* hwnd{};
    std::string steer_dev;     // the input layer's connected steering device ("" none)
    std::uint64_t ms{};        // a monotonic clock
};
class Delivery {
public:
    // Once per process, after the load: the exports and the version; no device and no library state. False: why().
    bool start(const WheelFfbApi&,const Config&);
    // Once per input sample; `command` (DirectInput units, already x FFBGlobalStrength) is used in the race only.
    void frame(const Sample&,std::int32_t command);
    // PanicStop when opened, FreeDirectInput when the library holds any state; the library is not called again.
    void shutdown();
    Phase phase()const{return phase_;}
    const std::string& why()const{return why_;}
    bool silent()const{return silent_;}         // the last command the wheel accepted was zero
    struct Counts { std::uint32_t sent{},zeroes{},recoveries{},transient{},keepalive{}; };
    const Counts& counts()const{return counts_;}
private:
    bool same_device(const std::string&)const;
    void open_device(const Sample&);
    bool zero(const Sample&);
    bool refused(const Sample&);
    void release(const std::string& why);
    void quiesce();
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
};
}
