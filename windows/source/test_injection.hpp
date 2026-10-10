#pragma once
// Test injection (wheel toolkit STD-033 section 6 / STD-035), development only:
// raw device samples for Wheelkit's closed-loop control tests. The protocol is
// the OutRun2006Tweaks fork's (RemapInject), under this build's own folder
// %LOCALAPPDATA%\dbce\outrun2006-decomp\:
//   inject.on   "nonce=<8-64 letters/digits>" and "expires=<unix seconds, UTC>"
//               at most an hour ahead; read once, at startup;
//   inject.txt  first line "nonce=<the session's>", then at most 32 commands
//               (toolkit grammar, vendor/controls/dbce_inject.hpp), at most
//               4096 bytes; each version written after the start is read once
//               (checked at most every 100 ms). A session takes 2000 commands.
// It arms only when the canonical [Controls] applies (Schema 1) and the build
// sends no force: this build has no force feedback at all (when it gains one,
// arming will also require the process-lifetime output mute).
// Samples replace a device read before the bindings resolve, so the game takes
// them through the applied profile exactly as it takes the wheel. They end at
// the session's expiry and whenever their device's read fails.
// Observer: while armed, every input sample publishes ObserveData to
// "Local\DbceOutRunDecompInjectObserve-<nonce>", created by this process or not
// at all.
#include "dbce_controls.hpp"
#include <cstdint>
#include <string>
#include <vector>
struct DIJOYSTATE2;
namespace outrun::desktop::inject {
#pragma pack(push,1)
struct ObserveData {
    std::uint32_t version;       // 1
    std::uint32_t packetId;      // odd while an update is written, even when whole: keep a copy only when the
                                 // same even value is read before and after it
    std::uint32_t tickMs;        // GetTickCount of the update
    std::uint32_t gameMode;      // mode_state.current (16 / 18: race)
    std::uint8_t inGameplay;     // mode 16 or 18
    std::uint8_t pad[3];
    // Raw device samples of the bound steer / throttle / brake after injection, in their binding's range
    // (axes; a button binding reads 0 / 1). -1 when unbound.
    std::int32_t rawSteer,rawThrottle,rawBrake;
    // What this build wrote into the 453BB0 device record: steering word (-32767..32767), accelerator and brake
    // (0..255), the button bits (+4).
    std::int32_t recordSteer,recordAccel,recordBrake;
    std::uint32_t recordButtons;
    // What the game consumed: its 7D6810 channels after 453860 (steering -127..127, accelerator, brake 0..255),
    // as of the previous game frame.
    std::int32_t gameSteer,gameAccel,gameBrake;
    // The player car (event 799D18): speed +1C4, gear +208, the words +34 and +38.
    float speed;
    std::uint32_t gear;
    std::int32_t car34,car38;
};
#pragma pack(pop)
static_assert(sizeof(ObserveData)==76,"ObserveData layout");

// Startup, once the canonical [Controls] is read. Returns the log lines (none when inject.on is absent).
std::vector<std::string> init(const dbce::controls::Section& applied,bool sends_force);
bool armed();
// New commands, when inject.txt changed (at most every 100 ms).
void poll();
// One successful read of `dev` (instance GUID text, lower case) into `state`, axes in 0..65535: running samples
// replace their objects. Returns how many objects carried one; log lines in `log`.
int deliver(const std::string& dev,DIJOYSTATE2& state,int buttons,int povs,std::vector<std::string>& log);
// The device's read failed or it was closed: its running samples end.
void device_gone(const std::string& dev,std::vector<std::string>& log);
// Publishes one observation while armed (fills version, packetId, tickMs). false when not published.
bool observe(ObserveData data,std::vector<std::string>& log);

// Tests: a session text, a command file and delivery on an explicit clock, without files or a mapping.
bool parse_session(const std::string& text,std::uint64_t now_unix,std::string& nonce,std::uint64_t& expires_unix,std::string& why);
bool test_arm(const dbce::controls::Section& applied);
int test_file(const std::string& text,std::vector<std::string>& log);
void test_clock(std::uint64_t now_ms);
}
