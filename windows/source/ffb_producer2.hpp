#pragma once
// Pure, default-off software preview of outrun-producer@2. No runtime hook, configuration reader or device API.
// A caller must attest that OperationInput ran for this car on source_tick; raw car bytes alone cannot prove freshness.
// The offline fixture samples the previous completed tick. The optional shadow samples just after car control returns.
#include "../vendor/native/forcemodel/force_model.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace outrun::desktop::ffb::producer2 {
constexpr const char* ModelId = "outrun-producer@2";
constexpr const char* TraceId = "dbce.outrun-producer-ffb-trace@2";
// Independent STD-027 gains. 50 is the fixed preview reference, not a legacy-key migration.
struct Strengths { int steering_percent{50},shift_percent{50}; };

struct Frame {
    std::uint32_t mode{};             // GAME is 16; menu, pause and goal are neutral
    std::uint32_t race_epoch{};       // explicit lifecycle token, nonzero in GAME
    std::uint32_t car_generation{};   // changes on respawn/car replacement, nonzero when proven
    std::uint64_t source_tick{};      // caller-witnessed completed sample ordinal, consecutive within identity
    bool producer_fresh{};            // OperationInput ran for this car on source_tick
    const std::uint8_t* car{};
    std::size_t car_bytes{};
    float dt_seconds{};              // elapsed game tick, (0, 0.1] seconds
};

enum class State { Neutral, Primed, Computed };
enum class Reason { None, OutsideGame, Unproven, ShortCar, StaleSteer, InvalidSteer, InvalidSpeed,
                    InvalidGear, InvalidTime, DiscontinuousTick, NewEpochOrCar, Rejected };

struct Row {
    State state{State::Neutral};
    Reason reason{Reason::OutsideGame};
    std::uint32_t epoch{},car_generation{};
    std::uint64_t source_tick{};
    float dt_seconds{};              // supplied game-tick duration; trace records it for replay
    std::int16_t steer_202{};         // signed game-consumed command << 8, not a wheel angle
    float hud_kmh_1f8{};             // HUD metric base, converted to m/s below
    std::uint32_t gear_208{};
    dbce::force::Inputs request{};   // only has_steer and speed_mps are populated; gear_shift is an event
    float model_force{},shaped_force{};
    bool model_event{};
};

// A fixed preview profile. It is not the owner's Tweaks tune and cannot select physical output.
dbce::force::ModelSettings preview_model_settings();
dbce::force::ModelSettings model_settings(const Strengths& strengths);
dbce::force::ShaperSettings preview_shaper_settings();
const char* reason_name(Reason reason);
std::string trace_header();
std::string trace_csv(const Row& row);

class Stream {
public:
    explicit Stream(const Strengths& strengths={});
    Row step(const Frame& frame);
    void reset();
private:
    dbce::force::Model model_;
    dbce::force::Shaper shaper_;
    std::uint32_t epoch_{},generation_{},gear_{};
    std::uint64_t tick_{};
    bool armed_{};
};
}
