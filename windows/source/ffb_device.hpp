#pragma once
// OutRun force feedback, F4: the software force stream (ffb_live.hpp) delivered to the steering wheel through the
// wheel toolkit's WheelFfb.dll (vendor/wheelffb), loaded at run time from the game folder. Off unless [FFB] Output =
// wheel. The gates, in order; the first that fails keeps the wheel silent:
//   load    Output = wheel; not DBCE_FFB_MUTE=1 (STD-035 item 8: the library stays unloaded); not a latched (test)
//           process; [Controls] steer names a device instance (dev=); the DLL is 0.9.0 or newer and has every export
//           used here.
//   open    in the race and in front (an exclusive handle is not acquired behind another window, and the game never
//           takes the focus), with the input layer reading that same instance as the steering device; the instance is
//           one of the library's force-feedback devices; strict selection opens exactly it (never a name or an
//           index); then an accepted zero (SetDeviceForcesXY(0, 0) returns 1) before anything else. One attempt per
//           process.
//   force   in the race and in front, every input sample sends the model's command (the held value while its
//           deadband holds), so the library's hold watchdog (SetHoldTimeoutMs) silences the wheel once the game stops
//           sampling (a stall, a hang). Outside the race or out of front: one zero, then nothing; back in, an accepted
//           zero before the next command.
//   errors  a transient refusal (acquisition lost, effect being recreated) zeroes and waits for an accepted zero, at
//           most 2 s of tries in the race and in front; any other refusal, a change or loss of the steering device,
//           or the end of those 2 s releases the device for the rest of the process.
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
inline constexpr int HoldMs=250;                    // the library's hold watchdog
inline constexpr std::uint64_t RecoveryMs=2000;     // tries for an accepted zero after a transient refusal
struct Config {
    bool output{};             // [FFB] Output = wheel
    bool muted{};              // DBCE_FFB_MUTE=1
    bool latched{};            // a test process
    std::string steer_dev;     // [Controls] steer dev= ("{...}"); empty when steer names no device
};
// Why the library may not be loaded ("" when it may).
std::string load_refusal(const Config&);
// "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}", any case; nothing else.
bool parse_guid(const std::string&,GUID&);
enum class Phase { Off, Waiting, Open, Recovering, Released, Stopped };
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
    // After the load: the exports, the version and the hold watchdog; no device is touched. False: why().
    bool start(const WheelFfbApi&,const Config&);
    // Once per input sample; `command` (DirectInput units, already x FFBGlobalStrength) is used in the race only.
    void frame(const Sample&,std::int32_t command);
    // PanicStop and FreeDirectInput when a device was opened; the library is not called again.
    void shutdown();
    Phase phase()const{return phase_;}
    const std::string& why()const{return why_;}
    struct Counts { std::uint32_t sent{},zeroes{},recoveries{},transient{}; };
    const Counts& counts()const{return counts_;}
private:
    bool accept(int result);
    void release(const std::string& why);
    void open(const Sample&);
    void zero_best_effort();
    WheelFfbApi api_{};
    Config cfg_{};
    GUID guid_{};
    Phase phase_{Phase::Off};
    std::string why_;
    bool opened_{};            // InitDirectInput was called: FreeDirectInput is owed
    bool need_zero_{};         // the next command follows an accepted zero
    bool silent_{true};        // the last thing the wheel was told is zero
    bool recovering_clock_{};  // a focused recovery is timing
    std::uint64_t recovery_start_{};
    Counts counts_{};
};
}
