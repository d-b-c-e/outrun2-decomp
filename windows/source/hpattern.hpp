#pragma once
// The H-pattern gearbox (R1-F10), moved unchanged out of the old input-shim adapter: OutRun 2006 shifts sequentially
// only, so exactly one held gate sets the target; one pulse, at least one released tick (whatever changes in between:
// the gate, the direction, a reset), then the consumed gear must change before the deadline; bounded retries. The
// observed gear must be a real gear (1-6); anything else is unavailable. Neutral and reverse are not in the game.
#include <cstdint>
#include <string>
namespace outrun::desktop {
class HPattern {
public:
    struct Config { std::int64_t deadline_us=250000; int retries=2; };
    enum class State : std::uint8_t { Idle,Pulse,Release,Wait,Stopped,Ambiguous,Unavailable,AtTarget };
    HPattern()=default;
    explicit HPattern(Config c):cfg_(c){}
    // gates[k] = gate k+1 held. Returns -1 (shift down), 0, or +1 (shift up) for this tick.
    int step(const bool gates[6],bool race,bool car,std::uint32_t gear,std::int64_t now_us);
    // Releases on scene changes and suspends shifting while paused (mode 18).
    int step_mode(const bool gates[6],std::uint32_t mode,bool car,std::uint32_t gear,std::int64_t now_us);
    void reset();
    void released_tick(){pulsed_last_=false;}   // a tick the caller sent no pulse on without calling step()
    State state()const{return state_;}
    const std::string& status()const{return status_;}   // changes only on a state change; log it once per change
    std::uint32_t target()const{return target_;}
private:
    Config cfg_{};
    State state_{State::Idle};
    std::string status_;
    std::uint32_t target_{},gear_at_pulse_{};
    std::uint32_t mode_{};
    int direction_{},tries_{};
    std::int64_t deadline_us_{};
    bool pulsed_last_{};                  // survives reset(): a pulse is always followed by a released tick
    void set(State,const std::string&);
};
}
