#pragma once
// The car state telemetry reports (STD-016 / STD-009), read from the game; the Wheelkit runtime builds and sends the
// Forza packet (wheelkit_runtime.cpp fills its wk_car once per input tick). Field provenance and the remaining live
// checks: docs/TELEMETRY-FIELDS.md.
#include <cstddef>
#include <cstdint>
namespace outrun::desktop {

// The car (event 8, car_select.car_799d18: the PC's EVWORK_CAR) fields the mod reads, plus the game's own engine speed:
// +0x48 the engine RPM (pc_wheel_dynamics.cpp: +0x21C rad/s x 60 / 2 pi) and +0x20C its smoothed follower, which the HUD
// tachometer needle draws (4B8F30, 0..10000 over 180 frames). The car parameters (car_select.parameters, the PC's car
// +0x2B4) give the engine model's idle speed +0x15F8 and its rev limiter threshold +0x1644 (engine_torque), both rad/s:
// 1500 and 8500 RPM for every car in the retail EXE columns (5E3140; the clamp +0x1690 is 12500, past the needle).
struct TelemetryCar {
    float speed_1c4{},speed_delta_1d0{},hud_kmh_1f8{},prior_speed_178{},edge_distance_264{},edge_distance_268{};
    std::uint32_t flags_4{},state_8{},prev_gear_1d8{},gear_208{};
    std::uint32_t surface_24c[4]{};
    std::int32_t engine_rpm_48{},hud_rpm_20c{};
    float idle_15f8{},redline_1644{};                  // rad/s; zero without the parameters
};
// Zeros when the car buffer is too small; the engine limits stay zero without parameters of at least 0x1648 bytes.
TelemetryCar telemetry_car(const std::uint8_t* car,std::size_t bytes,const std::uint8_t* parameters=nullptr,std::size_t parameter_bytes=0);
// [Telemetry] EngineRpm = Game: the HUD tachometer's RPM (+0x20C) between the parameters' idle and redline (x 60 / 2 pi).
// False (synthesis instead) when those limits are missing, not finite or not idle < redline <= 20000 RPM.
bool telemetry_game_rpm(const TelemetryCar&,float& current,float& idle,float& redline);
// The OutRun 2006 mod's synthesis (the default): a gear ratio times the normalized speed (+0x1C4) across idle..max.
float telemetry_synthetic_rpm(const TelemetryCar&);
// The mod's IsInGameplay: START 13, GAME 16, SMPAUSEMENU 18, GOAL 19, TIMEUP 20.
bool telemetry_in_gameplay(std::uint32_t mode);
constexpr float TelemetryMaxRpm=8500.0f,TelemetryIdleRpm=900.0f;
}
