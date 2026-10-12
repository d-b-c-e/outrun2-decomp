// The [FFB] settings as the game reads them (ffb_live.hpp): pure, so test_ffb_outrun checks the parse.
#include "ffb_live.hpp"
#include <cstdlib>
namespace outrun::desktop {
namespace {
bool number(const std::string& v,float& out){
    if(v.empty())return false;
    char* end=nullptr;const float f=std::strtof(v.c_str(),&end);
    if(!end||*end!='\0')return false;
    out=f;return true;   // range and finiteness: ffb::validate, once, for the whole set
}
}

ffb::LegacySettings ffb_settings(const Ini& settings,std::string& notes){
    auto s=ffb::owner_accepted();
    struct K{const char* key;float* v;};
    const K keys[]{{"FFBGlobalStrength",&s.global_strength},{"FFBSpringStrength",&s.spring},{"FFBDamperStrength",&s.damper},
                   {"FFBSteeringWeight",&s.steering_weight},{"FFBWallImpact",&s.wall_impact},{"FFBRumbleStrip",&s.rumble_strip},
                   {"FFBGearShift",&s.gear_shift},{"FFBRoadTexture",&s.road_texture},{"FFBTireSlip",&s.tire_slip},
                   {"FFBGripLoss",&s.grip_loss},{"FFBWeightTransfer",&s.weight_transfer},{"FFBLateralDeadzone",&s.lateral_deadzone},
                   {"FFBEngineIdle",&s.engine_idle}};
    for(const auto& k:keys){
        const auto text=settings.text("FFB",k.key);
        if(text.empty())continue;
        float v;
        if(number(text,v))*k.v=v;else notes+=std::string("[FFB] ")+k.key+" = "+text.substr(0,24)+" is not a number; the default stays. ";
    }
    s.use_periodic_effects=settings.flag("FFB","FFBUsePeriodicEffects",s.use_periodic_effects);
    s.invert=settings.flag("FFB","FFBInvertForce",s.invert);
    return s;
}

}
