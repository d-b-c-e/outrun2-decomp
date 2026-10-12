#pragma once
// OutRun's own force model, model identity "outrun-legacy@1": the OutRun 2006 mod's production calculation
// (dbce-mods-outrun-2006 src/ffb_calculation.inl, CalculateSignals with FFBProfile = legacy), transliterated term for
// term so its own offline fixture's golden output reproduces exactly (tests/test_ffb_outrun.cpp). It is the model the
// owner drives with (his OutRun2006Tweaks.ini: FFBProfile = legacy). OutRun is a pure arcade racer, so STD-025's
// exemption applies: a documented model of its own rather than the toolkit's tyre model.
//
// Pure: the car's signals in, the force calls out through a Sink. No device, no window, no clock of its own. The
// device side (WheelFfb, the muted / default-off gates) is a separate layer.
//
// Not adopted from the mod: its shared-model path (FFBProfile = arcade-outrun@1). There, crashImpulseTimer and
// gearShiftTimer count down only inside the legacy branch (ffb_calculation.inl:313 / 326), so after the first crash
// the shared path re-sends the impact every tick and never re-arms. Reported to the mod; not ported.
#include <cstddef>
#include <cstdint>
#include <string>
namespace outrun::desktop::ffb {

// The mod's [FFB] settings and their compiled defaults (plugin.hpp: the fixture's "legacy-defaults").
struct LegacySettings {
    float global_strength=1.0f;     // FFBGlobalStrength: the output scale, clamped 0..1 (StrengthScale)
    float spring=0.45f;             // FFBSpringStrength
    float damper=0.10f;             // FFBDamperStrength
    float steering_weight=0.55f;    // FFBSteeringWeight
    float wall_impact=1.0f;         // FFBWallImpact
    float rumble_strip=0.35f;       // FFBRumbleStrip (read by the mod's other paths, not by this calculation)
    float gear_shift=0.3f;          // FFBGearShift
    float road_texture=0.6f;        // FFBRoadTexture
    float tire_slip=0.35f;          // FFBTireSlip
    float grip_loss=0.6f;           // FFBGripLoss
    float weight_transfer=0.8f;     // FFBWeightTransfer
    float lateral_deadzone=1.5f;    // FFBLateralDeadzone
    float engine_idle=0.08f;        // FFBEngineIdle
    bool use_periodic_effects=true; // FFBUsePeriodicEffects (the device layer decides whether periodics exist)
    bool invert=false;              // FFBInvertForce
};

// The car fields the calculation reads (the PC's EVWORK_CAR, the decomp's car_select.car_799d18).
struct CarSignals {
    float speed_1c4{},steer_1d0{},steer_rate_1d4{},lateral_264{},lateral_268{};
    std::uint32_t gear_208{},state_8{},pedal_34{};
};
CarSignals car_signals(const std::uint8_t* car,std::size_t bytes);   // zeros when the buffer is too small

class Sink {
public:
    virtual ~Sink()=default;
    virtual void constant(std::int32_t magnitude)=0;        // -10000..10000, before StrengthScale
    virtual void periodic(int slot,float magnitude,float hz)=0;
};

// Settings are checked once (romi 28372): every value finite, strengths within 0..10, the deadzone within 0..100,
// the global strength within 0..1. Empty when valid; otherwise which value and why.
std::string validate(const LegacySettings&);

class LegacyForce {
public:
    explicit LegacyForce(LegacySettings settings={}):s_(settings),invalid_(validate(settings)){}
    const LegacySettings& settings()const{return s_;}
    // Invalid settings: calculate() sends nothing (counted), and invalid() says why.
    const std::string& invalid()const{return invalid_;}
    std::uint32_t refused_ticks()const{return refused_;}
    std::uint32_t nonfinite_ticks()const{return nonfinite_;}   // a non-finite intermediate, sent as 0 (should stay 0)
    // Hardware periodic effects (sine slots) when the device has them; otherwise the vibration is synthesised into
    // the constant force (the CF fallback).
    void set_periodics(bool active,int road_slot,int slip_slot){periodics_=active;road_slot_=road_slot;slip_slot_=slip_slot;}
    void reset();
    // One game tick (60 Hz). roughness / water: the game's surface LUT result for this tick (surface_roughness).
    void calculate(const CarSignals&,float roughness,std::uint32_t water,Sink&);
    std::int32_t last_constant()const{return prev_constant_;}
    float strength_scale()const;   // the mod's StrengthScale: FFBGlobalStrength clamped to 0..1
private:
    LegacySettings s_;
    std::string invalid_;
    std::uint32_t refused_{},nonfinite_{};
    bool periodics_{};int road_slot_{-1},slip_slot_{-1};
    std::uint32_t prev_gear_{},prev_collision_flags_{};
    float prev_speed_{},smoothed_lateral_{};
    float speed_history_[8]{},lat_history_[16]{};
    int speed_history_idx_{},lat_history_idx_{};
    int crash_impulse_timer_{},gear_shift_timer_{},splash_timer_{},warmup_frames_{};
    std::uint32_t update_counter_{};
    float crash_impulse_force_{},splash_amp_{},road_phase_{},slip_phase_{};
    std::int32_t prev_constant_{},prev_struct_{};
};

// The game's surface roughness LUT (the PC's Xbox vibration code; the mod's sub_1149C0), for one tyre's surface mask.
// load_coli: the car's loadColiType; stage_unique: GetStageUniqueNum(GetNowStageNum(8)). Sets *water |= 1 on water.
double surface_roughness(std::uint32_t mask,int load_coli,int stage_unique,std::uint32_t* water);
}
