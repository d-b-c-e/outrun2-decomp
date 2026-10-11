#pragma once
// The OutRun 2006 input-shim adapter (windows/docs/INPUT-SHIM.md section 15), slice 2: pure, tested on fakes.
// Interception point: xbox_runtime::input_filter, after the pad adapter (ps5::DualSenseInput) has built the native
// input and before the game consumes it at 453BB0. The adapter turns one shim frame into the game's input:
//   steer    -> record word 0x0E (lround(n * 32767)); the game's 453860 still applies its dead zone and quantisation
//   throttle -> record 0x0D, brake -> 0x0C (lround(n * 255)), max-merged with the pad (an unrelated device)
//   buttons  -> buttons_04 bits (race, held), menu edges and frontend features (menus, down edges)
//   H-pattern gates -> shift pulses toward the gate, observed on the car's gear word (a monotonic state machine)
// In a latched (test) process the pad's gameplay contribution is suppressed before the consumer, keeping only the
// stop path (the pad's exit combination), and what was suppressed is reported.
#include "shim.hpp"
#include "platform/runtime_input.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace outrun::desktop {
namespace shim=dbce::shim;

// Which contract actions OutRun 2006 takes, and why not the others (section 15).
shim::Capability outrun_capability();

// What the integration glue reads from the runtime each tick (section 15, state signals).
struct GameView {
    std::uint32_t mode{};                 // mode_state.current (16 / 18: race)
    bool car{};                           // the player car event exists
    std::uint32_t gear{};                 // car +208 (1..top), valid when car
    float speed{};                        // car +1C4 (game units)
    std::int32_t consumed_steer{},consumed_accel{},consumed_brake{};   // 7D6810 after 453860: the previous tick's
};

// The H-pattern gearbox (R1-F10): exactly one held gate sets the target; one pulse, at least one released tick, then
// the consumed gear must change before the deadline; bounded retries; neutral and reverse are not in the game.
class HPattern {
public:
    struct Config { std::int64_t deadline_us=250000; int retries=2; };
    enum class State : std::uint8_t { Idle,Pulse,Release,Wait,Stopped,Ambiguous,Unavailable,AtTarget };
    HPattern()=default;
    explicit HPattern(Config c):cfg_(c){}
    // gates[k] = gate k+1 held. Returns -1 (shift down), 0, or +1 (shift up) for this tick.
    int step(const bool gates[6],bool race,bool car,std::uint32_t gear,std::int64_t now_us);
    void reset();
    State state()const{return state_;}
    const std::string& status()const{return status_;}   // changes only on a state change; log it once per change
    std::uint32_t target()const{return target_;}
private:
    Config cfg_{};
    State state_{State::Idle};
    std::string status_;
    std::uint32_t target_{},gear_at_pulse_{};
    int direction_{},tries_{};
    std::int64_t deadline_us_{};
    void set(State,const std::string&);
};

// Signals for the observer and recorder (section 8 item 7), each tagged with the frame it belongs to.
struct Signals {
    std::uint64_t frame_seq{};
    std::uint32_t mode{};
    bool in_gameplay{};
    float speed{};
    std::uint32_t gear{};
    std::int32_t consumed_steer{},consumed_accel{},consumed_brake{};   // offset: the previous tick
    std::int32_t record_steer{},record_accel{},record_brake{};         // offset: this tick
    std::uint32_t record_buttons{};
    std::uint32_t native_suppressed{};    // native gameplay fields that were non-neutral and suppressed this tick
    HPattern::State gearbox{};
};

class Adapter {
public:
    explicit Adapter(std::string transmission,HPattern::Config gearbox={});
    // Applies a frame to `out`, which holds the pad adapter's native input for this tick.
    void apply(const shim::Frame&,const GameView&,platform::NativeInputState& out);
    // The final neutral frame at stop (section 6, shutdown step 1): the owned contribution released, the pad kept
    // unless the process is latched.
    void apply_neutral(bool latched,platform::NativeInputState& out);
    const Signals& signals()const{return signals_;}
    const HPattern& gearbox()const{return gearbox_;}
private:
    bool hpattern_;
    HPattern gearbox_;
    std::uint32_t last_mode_{};
    std::uint32_t pulse_prev_{};          // shift bits the gearbox held last tick (its own edges)
    Signals signals_;
};

// Pre-consumer exclusion for the WGI pad path (R1-F3, P25): a WGI gamepad whose USB identity matches an owned
// device's product GUID (Data1 = PID << 16 | VID) is dropped before the pad adapter. Returns the index of the pad to
// read, or -1 when none is left.
struct PadIdentity { std::uint16_t vid{},pid{}; std::string name; };
int select_pad(const std::vector<PadIdentity>& pads,const std::vector<std::string>& owned_products);
}
