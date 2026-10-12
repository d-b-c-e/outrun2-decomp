// OutRun's own force model, "outrun-legacy@1" (ffb_outrun.hpp): dbce-mods-outrun-2006 src/ffb_calculation.inl
// CalculateSignals, its legacy branch, term for term and in the same order (float arithmetic as written there), so the
// mod's fixture golden (tools/tests/data/signal-legacy-v1.expected) reproduces byte for byte. Comments name the mod's
// own reasons where they explain a constant.
#include "ffb_outrun.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
namespace outrun::desktop::ffb {
namespace {
constexpr int WarmupThreshold=30;   // ~0.5 s at 60 Hz: ramp in over the first frames (garbage early telemetry)
template<class T> T at(const std::uint8_t* car,std::size_t off){T v;std::memcpy(&v,car+off,sizeof v);return v;}
}

CarSignals car_signals(const std::uint8_t* car,std::size_t bytes){
    CarSignals c;
    if(!car||bytes<0x26cu)return c;
    c.state_8=at<std::uint32_t>(car,0x8);c.pedal_34=std::uint32_t(std::max(0,at<std::int32_t>(car,0x34)));
    c.speed_1c4=at<float>(car,0x1c4);c.steer_1d0=at<float>(car,0x1d0);c.steer_rate_1d4=at<float>(car,0x1d4);
    c.gear_208=at<std::uint32_t>(car,0x208);c.lateral_264=at<float>(car,0x264);c.lateral_268=at<float>(car,0x268);
    return c;
}

std::string validate(const LegacySettings& s){
    struct V{const char* key;float v;float lo,hi;};
    const V all[]{{"FFBGlobalStrength",s.global_strength,0,1},{"FFBSpringStrength",s.spring,0,10},{"FFBDamperStrength",s.damper,0,10},
                  {"FFBSteeringWeight",s.steering_weight,0,10},{"FFBWallImpact",s.wall_impact,0,10},{"FFBRumbleStrip",s.rumble_strip,0,10},
                  {"FFBGearShift",s.gear_shift,0,10},{"FFBRoadTexture",s.road_texture,0,10},{"FFBTireSlip",s.tire_slip,0,10},
                  {"FFBGripLoss",s.grip_loss,0,10},{"FFBWeightTransfer",s.weight_transfer,0,10},{"FFBLateralDeadzone",s.lateral_deadzone,0,100},
                  {"FFBEngineIdle",s.engine_idle,0,10}};
    for(const auto& x:all)if(!std::isfinite(x.v)||x.v<x.lo||x.v>x.hi)
        return std::string(x.key)+" = "+std::to_string(x.v)+" outside "+std::to_string(x.lo)+".."+std::to_string(x.hi);
    return {};
}

float LegacyForce::strength_scale()const{return std::isfinite(s_.global_strength)?std::clamp(s_.global_strength,0.0f,1.0f):0.0f;}

void LegacyForce::reset(){
    prev_gear_=prev_collision_flags_=0;prev_speed_=smoothed_lateral_=0;
    std::fill(std::begin(speed_history_),std::end(speed_history_),0.0f);
    std::fill(std::begin(lat_history_),std::end(lat_history_),0.0f);
    speed_history_idx_=lat_history_idx_=0;
    crash_impulse_timer_=gear_shift_timer_=splash_timer_=warmup_frames_=0;update_counter_=0;
    crash_impulse_force_=splash_amp_=road_phase_=slip_phase_=0;
    prev_constant_=prev_struct_=0;
}

void LegacyForce::calculate(const CarSignals& in,float roughness,std::uint32_t water,Sink& sink){
    // Guards the mod does not have (its fixture refuses such input): a non-finite signal is 0 and the roughness stays
    // in the LUT's 0..1, so NaN never reaches tanh or the integer casts. Valid input is untouched (the golden holds).
    // Signals are also held within the mod fixture's own input bound (|v| <= 1000, speed >= 0), so no product overflows.
    if(!invalid_.empty()){++refused_;return;}
    auto fin=[](float v){return std::isfinite(v)?std::clamp(v,-1000.0f,1000.0f):0.0f;};
    CarSignals car=in;
    car.speed_1c4=std::max(0.0f,fin(car.speed_1c4));car.steer_1d0=fin(car.steer_1d0);car.steer_rate_1d4=fin(car.steer_rate_1d4);
    car.lateral_264=fin(car.lateral_264);car.lateral_268=fin(car.lateral_268);
    roughness=std::isfinite(roughness)?std::clamp(roughness,0.0f,1.0f):0.0f;
    float warmupScale=1.0f;
    if(warmup_frames_<WarmupThreshold){
        warmup_frames_++;
        warmupScale=static_cast<float>(warmup_frames_)/static_cast<float>(WarmupThreshold);
    }
    const float recreateScale=1.0f;   // the post-recreation ramp-in lives in the device layer
    // Constant force every frame; periodic envelopes every 4th frame (~15 Hz).
    update_counter_++;
    const bool updateEnvelopes=(update_counter_%4==0);

    const float speed=car.speed_1c4;                       // normalized speed (0.0 - ~1.0+)
    const float speedNorm=std::clamp(speed,0.0f,1.0f);
    const std::uint32_t stateFlags=car.state_8;
    const float lateralForce1=car.lateral_264;
    const float lateralForce2=car.lateral_268;
    const std::uint32_t curGear=car.gear_208;
    const float steer=car.steer_1d0;
    const float steerRate=car.steer_rate_1d4;

    // Signal conditioning: dual-rate EMA on the lateral slide (fast attack, faster decay).
    {
        const float lateralCombined=(lateralForce1+lateralForce2);
        const float alpha=(std::abs(lateralCombined)>std::abs(smoothed_lateral_))?0.25f:0.20f;
        smoothed_lateral_=alpha*lateralCombined+(1.0f-alpha)*smoothed_lateral_;
    }
    // Pre-crash lateral history: the collision response corrupts the lateral signal at impact.
    lat_history_[lat_history_idx_%16]=smoothed_lateral_;
    lat_history_idx_++;
    // Subtractive deadzone on the road-load term only.
    float latDz=0.0f;
    {
        const float mag=std::abs(smoothed_lateral_)-s_.lateral_deadzone;
        if(mag>0.0f)latDz=(smoothed_lateral_>0.0f)?mag:-mag;
    }
    const float latNorm=std::clamp(latDz/24.0f,-1.0f,1.0f);
    const float driftAmt=std::clamp((std::abs(smoothed_lateral_)-12.0f)/12.0f,0.0f,1.0f);
    const float gripFactor=1.0f-s_.grip_loss*driftAmt;   // the wheel lightens as grip is lost
    speed_history_[speed_history_idx_%8]=speed;
    speed_history_idx_++;

    // Crash: a 3% speed loss over 6 frames at speed is a wall hit; push away from the pre-crash lateral side.
    if(crash_impulse_timer_<=0&&speed_history_idx_>6){
        const float oldSpeed=speed_history_[(speed_history_idx_-6)%8];
        const float windowDelta=oldSpeed-speed;
        if(windowDelta>0.03f&&speed>0.1f){
            const float latPre=(lat_history_idx_>8)?lat_history_[(lat_history_idx_-8)%16]:smoothed_lateral_;
            const float impactDir=(latPre>=0.0f)?-1.0f:1.0f;
            crash_impulse_force_=impactDir*1.5f*s_.wall_impact;
            crash_impulse_timer_=90;   // 10 frames of jolt, then cooldown (no re-trigger)
            smoothed_lateral_=0.0f;    // no pinned weight after the impulse
        }
    }
    // Also the contact flag's rising edge (state +8 bit 0x1000).
    {
        const bool collisionActive=(stateFlags&0x1000)!=0;
        const bool wasColliding=(prev_collision_flags_&0x1000)!=0;
        if(collisionActive&&!wasColliding&&crash_impulse_timer_<=0){
            const float latPre=(lat_history_idx_>8)?lat_history_[(lat_history_idx_-8)%16]:smoothed_lateral_;
            const float flagDir=(latPre>=0.0f)?-1.0f:1.0f;
            crash_impulse_force_=flagDir*1.2f*s_.wall_impact;
            crash_impulse_timer_=90;
            smoothed_lateral_=0.0f;
        }
    }

    // Vibration envelopes: the game's own road formula (roughness x speed), the water splash, slip / idle chatter.
    float roadAmp=roughness*speedNorm*s_.road_texture;
    const float roadFreq=25.0f+12.0f*speedNorm;
    if(water&&roughness>0.7f&&speed>0.95f&&splash_timer_<=0){
        splash_amp_=(roughness-0.7f)*speed*0.75f;
        splash_timer_=9;   // ~150 ms
    }
    if(splash_timer_>0){
        roadAmp=std::max(roadAmp,splash_amp_);
        splash_timer_--;
    }
    float slipAmp=0.0f;
    float slipFreq=40.0f;
    if(driftAmt>0.15f&&speed>0.1f){
        slipAmp=driftAmt*s_.tire_slip;
        slipFreq=40.0f-12.0f*driftAmt;
    }else if(speed<0.05f&&car.pedal_34>0){
        const float throttleNorm=std::clamp(static_cast<float>(car.pedal_34)/255.0f,0.0f,1.0f);
        slipAmp=s_.engine_idle*throttleNorm;
        slipFreq=15.0f+7.0f*throttleNorm;
    }

    // Constant force, centre-out: a virtual spring on steering position, damped by the steering derivative; the
    // lateral road load is secondary and lightens with the slide; weight transfer modulates; events pulse on top.
    {
        const float speedCurve=std::clamp(speed/0.25f,0.0f,1.0f)*(0.35f+0.65f*speedNorm);
        const float F_spring=-steer*s_.spring*speedCurve;
        constexpr float STEER_RATE_SCALE=20.0f;
        const float F_damper=-steerRate*STEER_RATE_SCALE*s_.damper*(0.4f+0.6f*speedNorm);
        const float F_lat=latNorm*speedNorm*s_.steering_weight*gripFactor;
        float loadMod=1.0f;
        if(speed_history_idx_>6){
            const float longAccel=(speed-speed_history_[(speed_history_idx_-6)%8])*10.0f;
            loadMod=1.0f+std::clamp(-longAccel*s_.weight_transfer,-0.20f,0.30f);
        }
        float F_struct=0.0f;
        if(crash_impulse_timer_<=80)F_struct=(F_spring+F_lat)*loadMod+F_damper;   // no stacking under the jolt
        float F_events=0.0f;
        if(crash_impulse_timer_>0){
            if(crash_impulse_timer_>80){
                float envelope;
                if(crash_impulse_timer_>85)envelope=1.0f;
                else envelope=float(crash_impulse_timer_-80)/5.0f;
                F_events+=crash_impulse_force_*envelope;
            }
            crash_impulse_timer_--;
        }
        // Gear shift: a symmetric double pulse (+K then -K), which nets to zero laterally.
        if(curGear!=prev_gear_&&prev_gear_!=0&&gear_shift_timer_<=0)gear_shift_timer_=6;
        if(gear_shift_timer_>0){
            const float thunk=0.2f*s_.gear_shift*((gear_shift_timer_>3)?1.0f:-1.0f);
            F_events+=thunk;
            gear_shift_timer_--;
        }
        float totalForce=F_struct+F_events;
        if(s_.invert)totalForce=-totalForce;
        totalForce*=warmupScale*recreateScale;
        if(!std::isfinite(totalForce)){totalForce=0.0f;++nonfinite_;}   // never NaN into tanh or an integer cast
        const float compressed=std::tanh(totalForce);   // soft saturation
        // Slew limit on the structural force (DD wheels); the crash and shift pulses bypass it.
        std::int32_t structMag=std::int32_t(compressed*10000.0f);
        const std::int32_t slewDelta=structMag-prev_struct_;
        constexpr std::int32_t maxSlew=600;
        const bool bypassSlew=(crash_impulse_timer_>80)||(gear_shift_timer_>0);
        if(std::abs(slewDelta)>maxSlew&&!bypassSlew)structMag=prev_struct_+((slewDelta>0)?maxSlew:-maxSlew);
        prev_struct_=structMag;
        // CF-fallback vibration after the compressor (only without hardware periodics), capped at 15 Hz.
        float vib=0.0f;
        if(!periodics_){
            if(roadAmp>0.005f){
                const float f=std::min(roadFreq,15.0f);
                road_phase_=std::fmod(road_phase_+f/60.0f*6.2832f,6.2832f);
                vib+=std::sin(road_phase_)*roadAmp;
            }else road_phase_=0.0f;
            if(slipAmp>0.005f){
                const float f=std::min(slipFreq,15.0f);
                slip_phase_=std::fmod(slip_phase_+f/60.0f*6.2832f,6.2832f);
                vib+=std::sin(slip_phase_)*slipAmp;
            }else slip_phase_=0.0f;
        }
        if(!std::isfinite(vib)){vib=0.0f;++nonfinite_;}
        vib=std::clamp(vib,-2.0f,2.0f);   // two envelopes of at most 1 each (bounded settings): the cast stays in range
        const std::int32_t diMagnitude=std::clamp(structMag+std::int32_t(vib*10000.0f),std::int32_t(-10000),std::int32_t(10000));
        // Deadband: skip an update that barely changed, except during the jolt.
        const std::int32_t delta=std::abs(diMagnitude-prev_constant_);
        if(delta>15||crash_impulse_timer_>80){
            sink.constant(diMagnitude);
            prev_constant_=diMagnitude;
        }
    }
    // Periodic channel: the hardware-rendered envelopes (~15 Hz).
    if(periodics_&&updateEnvelopes){
        sink.periodic(road_slot_,roadAmp,roadFreq);
        sink.periodic(slip_slot_,slipAmp,slipFreq);
    }
    prev_gear_=curGear;
    prev_collision_flags_=stateFlags;
    prev_speed_=speed;
}

double surface_roughness(std::uint32_t a1,int a2,int stage_unique,std::uint32_t* a3){
    // The mod's sub_1149C0, as decompiled there from the game's Xbox vibration code; constants are the game's floats.
    double result=1.f;
    if(a1>0x400){
        if(a1>0x100000){
            switch(a1){
            case 0x200000u:return 0.80000001;
            case 0x400000u:
                if(a2)return 0.25;
                if(stage_unique==0x12||stage_unique==0x30)return 0.25;
                return 0.89999998;
            case 0x800000u:return 0.5;
            }
        }else{
            if(a1==0x100000)return 0.70999998;
            if(a1>0x2000){
                if(a1==0x8000)return 0.40000001;
            }else if(a1==0x2000||a1==0x800||a1==0x1000){
                return 0.34999999;
            }
        }
        return 0.31;
    }
    if(a1==0x400)return 0.30000001;
    if(a1>0x10){
        if(a1==0x80)return 0.85000002;
        if(a1==0x100)return 0.44999999;
        if(a1!=0x200)return 0.31;
        return 0.34999999;
    }
    if(a1==0x10)return 0.89999998;
    switch(a1){
    case 1u:result=0.0;break;
    case 2u:
        if(a2)return 0.25;
        switch(stage_unique){
        case 0xB:case 0x29:result=0.72999996;*a3|=1u;break;
        case 0xD:case 0x2B:result=0.78999996;*a3|=1u;break;
        case 0xE:case 0x2C:result=0.75999999;*a3|=1u;break;
        default:return 0.25;
        }
        break;
    case 4u:result=0.69999999;break;
    case 8u:return 0.85000002;
    default:return 0.31;
    }
    return result;
}
}
