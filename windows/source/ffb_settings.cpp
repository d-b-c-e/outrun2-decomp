// The [FFB] settings and the computation's lifecycle (ffb_live.hpp): pure, so test_ffb_outrun checks them.
#include "ffb_live.hpp"
#include "dbce_controls.hpp"
#include "shim_input.hpp"
#include "controls_store.hpp"
#include <cctype>
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

ffb::LegacySettings ffb_settings(const Ini& settings,std::string& notes,std::string& source){
    ffb::LegacySettings s;   // the mod's compiled defaults (its fixture's "legacy-defaults")
    source="mod-defaults";
    const auto preset=settings.text("FFB","FFBPreset");
    if(preset=="owner-2026-10-08"){s=ffb::owner_accepted();source="preset owner-2026-10-08";}
    else if(!preset.empty())notes+="[FFB] FFBPreset = "+preset.substr(0,32)+" is not a known preset (owner-2026-10-08); the mod's defaults stay. ";
    struct K{const char* key;float* v;};
    const K keys[]{{"FFBGlobalStrength",&s.global_strength},{"FFBSpringStrength",&s.spring},{"FFBDamperStrength",&s.damper},
                   {"FFBSteeringWeight",&s.steering_weight},{"FFBWallImpact",&s.wall_impact},{"FFBRumbleStrip",&s.rumble_strip},
                   {"FFBGearShift",&s.gear_shift},{"FFBRoadTexture",&s.road_texture},{"FFBTireSlip",&s.tire_slip},
                   {"FFBGripLoss",&s.grip_loss},{"FFBWeightTransfer",&s.weight_transfer},{"FFBLateralDeadzone",&s.lateral_deadzone},
                   {"FFBEngineIdle",&s.engine_idle}};
    int overrides=0;
    for(const auto& k:keys){
        const auto text=settings.text("FFB",k.key);
        if(text.empty())continue;
        float v;
        if(number(text,v)){*k.v=v;++overrides;}
        else notes+=std::string("[FFB] ")+k.key+" = "+text.substr(0,24)+" is not a number; the default stays. ";
    }
    s.use_periodic_effects=settings.flag("FFB","FFBUsePeriodicEffects",s.use_periodic_effects);
    s.invert=settings.flag("FFB","FFBInvertForce",s.invert);
    if(overrides)source+=", "+std::to_string(overrides)+" [FFB] key(s)";
    return s;
}

ffbdev::Config ffb_output_config(const Ini& settings,bool latched,const char* mute_env,std::string& notes){
    ffbdev::Config c;
    std::string v=settings.text("FFB","Output");
    for(auto& ch:v)ch=char(std::tolower(static_cast<unsigned char>(ch)));
    if(v=="wheel")c.output=true;
    else if(!v.empty()&&v!="none")notes+="[FFB] Output = "+v.substr(0,24)+" is neither wheel nor none; no force output. ";
    c.muted=mute_env&&*mute_env&&std::string(mute_env)!="0";
    c.latched=latched;
    c.shim=!latched&&shim_backend_selected(settings);   // a latched process never runs the ordinary shim backend
    // The force target reads the section as the input readers do (STD-037 item 4): a schema-2 section keeps its steer device.
    const auto section=dbce::controls::parseSection(settings.section_lines("Controls"),outrun::desktop::ControlsReads);
    for(const auto& e:section.bound)if(e.action=="steer"){c.steer_dev=e.binding.dev;break;}
    return c;
}

bool RaceEpochs::step(std::uint32_t mode){
    reset_now=false;
    if(mode!=16u){
        if(!(mode==18u&&last_mode==16u))last_mode=mode;   // a pause keeps the race's state
        return false;
    }
    if(last_mode!=16u){reset_now=true;++epoch;}
    last_mode=16u;
    return true;
}
}
