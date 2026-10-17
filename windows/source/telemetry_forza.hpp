#pragma once
// Forza "Data Out" telemetry for the desktop build (dbce toolkit STD-016 / STD-009 / STD-033), with the OutRun 2006 mod's
// shared-memory block (dbce-mods-outrun-2006 src/telemetry.hpp, the SimHub plugin's source), so a dashboard, SimHub or
// a bass shaker reads the decomp as it reads the mod.
//   UDP: the toolkit encoder (vendor/telemetry/forza_packet.h) to 127.0.0.1:[Telemetry] Port (8000), the Forza Horizon
//        324-byte layout by default (STD-016); [Telemetry] Layout = FM7 sends the mod's 311-byte FM7 Dash instead.
//   Shared memory: OutRun2006TelemetryData (76 bytes, version 1) under [Telemetry] SharedMemName (OutRun2006Telemetry).
// On by default ([Telemetry] Enabled, or the mod's Enable). Never in a latched (test) process, and not with
// OR2_TELEMETRY=0 (unattended runs: SimHub can turn telemetry into forces or motion). OR2_TELEMETRY_PORT=N overrides the
// port for a run's own listener. One frame per input tick, after the input filter (telemetry_wrap_input_filter).
#include "desktop_settings.hpp"
#include "forza_packet.h"
#include <cstdint>
#include <string>
namespace outrun::platform { struct NativeInputState; struct NativeRuntimeContext; }
namespace outrun::desktop {

struct TelemetrySettings {
    bool enabled{true};
    std::uint16_t port{8000};
    dbce::forza::Layout layout{dbce::forza::FORZA_HORIZON_324};
    std::string shared_name{"OutRun2006Telemetry"};    // empty: no shared memory
    bool game_rpm{};                                    // [Telemetry] EngineRpm = Game (not a mod key; Synthetic default)
    std::string notes;                                  // why a value was refused, for runtime.log
};
// [Telemetry] Enabled / Enable, Port (1..65535), Layout (Horizon or FM7), SharedMemName, EngineRpm (Synthetic or Game);
// then the OR2_TELEMETRY and OR2_TELEMETRY_PORT overrides (env_off / env_port: their values, or null when unset).
TelemetrySettings telemetry_settings(const Ini& settings,const char* env_off,const char* env_port);

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
// The mod's IsInGameplay: START 13, GAME 16, SMPAUSEMENU 18, GOAL 19, TIMEUP 20.
bool telemetry_in_gameplay(std::uint32_t mode);
// One race packet. Speed follows the HUD (STD-009: +0x1F8 km/h / 3.6); a non-finite or negative HUD value sends nothing
// (returns 0), never a confident zero. RPM is synthesised as the mod does by default; game_rpm sends the HUD
// tachometer's RPM with the car's idle and redline (telemetry_game_rpm) when they are valid. Gear 0 is sent as 1
// (Forza shows 0 as reverse). Steering is the game's consumed 7D6810 channel 0 (-127..127), not car +0x1D0
// (speed delta). Accel / brake 0..255 are consumed 7D6810 channels 1/2 in GAME; all three inputs are neutral outside
// GAME. Physical lateral acceleration is unavailable, so Forza accX stays zero rather than using road-edge distances.
int telemetry_packet(const TelemetryCar&,bool race_on,std::int32_t consumed_steer,std::uint8_t accel,std::uint8_t brake,std::uint32_t ms,
                     dbce::forza::Layout,std::uint8_t* out,int size,bool game_rpm=false);
// Outside gameplay: IsRaceOn 0, the same engine constants, gear 1 (a fully zero packet can wedge SimHub).
int telemetry_parked_packet(std::uint32_t ms,dbce::forza::Layout,std::uint8_t* out,int size);
constexpr float TelemetryMaxRpm=8500.0f,TelemetryIdleRpm=900.0f;

#pragma pack(push,1)
struct OutRun2006TelemetryData {        // dbce-mods-outrun-2006 src/telemetry.hpp, version 1, byte for byte
    std::uint32_t version,packetId;
    float speed,steeringAngle,lateralG1,lateralG2,impactForce;
    std::uint32_t gear,prevGear,stateFlags,carFlags;
    std::uint32_t surfaceType[4];
    float vibrationLeft,vibrationRight;   // the decomp has no pad rumble: 0
    std::uint32_t gameMode;
    std::uint8_t isInGameplay;
    std::uint8_t _pad[3];
};
#pragma pack(pop)
static_assert(sizeof(OutRun2006TelemetryData)==76,"the mod's shared-memory block is 76 bytes");
void telemetry_shared(const TelemetryCar&,std::uint32_t mode,bool gameplay,std::uint32_t packet_id,OutRun2006TelemetryData&);

// The process side. configure: opens the socket and the mapping (nothing when off or latched) and logs the state.
void telemetry_configure(const Ini& settings,bool latched);
// Installs the telemetry frame after the current input filter (call after the filter is chosen). No-op when off.
void telemetry_wrap_input_filter();
// Three parked frames to the destination (it stops showing a live race), then closes everything.
void telemetry_shutdown();
}
