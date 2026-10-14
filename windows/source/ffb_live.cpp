// OutRun force feedback, F2 + F4 (ffb_live.hpp): the software force stream computed in the game, and its delivery to
// the wheel when [FFB] Output = wheel (ffb_device.hpp; off by default).
#include "ffb_live.hpp"
#include "ffb_player_model.hpp"
#include "ffb_shadow_live.hpp"
#include "native_window.hpp"
#include "wheel_input.hpp"
#include "shim_input.hpp"
#include "desktop_panel.hpp"
#include "platform/native_runtime.hpp"
#include "platform/runtime_input.hpp"
#include "platform/race_area_runtime.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
namespace outrun::desktop {
namespace {
template<class T> T at(const std::uint8_t* car,std::size_t off){T v;std::memcpy(&v,car+off,sizeof v);return v;}
struct Row:ffb::Sink {
    bool sent{};
    void constant(std::int32_t)override{sent=true;}
    void periodic(int,float,float)override{}   // the CF fallback (no device): never called
};
struct Live {
    bool on{};
    bool selected{},row_ready{},selected_gate_open{};
    std::int32_t selected_intent{};
    std::unique_ptr<ffb::LegacyForce> model;
    std::FILE* trace{};
    std::string trace_path,source;
    xbox_runtime::InputFilter inner{};
    RaceEpochs epochs;
    bool output{};              // F4: the delivery started (WheelFfb.dll loaded, gates set)
    std::string off;            // why there is no wheel output (the log line)
    bool stopped{};             // Stop FFB / F8 ended it this session
    bool shim{};                // [Input] Backend = Shim: the shim owns the wheel and hands it over (ffbdev::Owner)
    bool requested{};           // [FFB] Output = wheel
    ffbdev::Owner* owner{};     // the shim's hand-over (WheelFfb bridge): the library stays loaded until it is quiet
    bool retained{};            // a stop could not enter the library gate: tried again at the exit (never waited for)
    WheelFfbApi api{};
    ffbdev::Delivery delivery;
    std::uint32_t race_ticks{},sent{},invalid_rows{};
    std::int32_t min_held{},max_held{};
};
Live f;
// Raw-input validity bits: the inputs as read, before the model's own guards (1C4, 1D0, 1D4, 264, 268 finite and
// within the mod fixture's |v| <= 1000, speed >= 0; then the roughness within 0..1).
unsigned validity(const ffb::CarSignals& c,float roughness){
    auto ok=[](float v){return std::isfinite(v)&&std::fabs(v)<=1000.0f;};
    unsigned bad=0;
    if(!ok(c.speed_1c4)||c.speed_1c4<0.0f)bad|=1u;
    if(!ok(c.steer_1d0))bad|=2u;
    if(!ok(c.steer_rate_1d4))bad|=4u;
    if(!ok(c.lateral_264))bad|=8u;
    if(!ok(c.lateral_268))bad|=16u;
    if(!std::isfinite(roughness)||roughness<0.0f||roughness>1.0f)bad|=32u;
    return bad;
}
// The game window is this process's and the foreground window (the exclusive handle needs it; the game never takes it).
bool own_front(void* hwnd){
    const HWND h=static_cast<HWND>(hwnd);
    if(!h||GetForegroundWindow()!=h)return false;
    DWORD pid=0;GetWindowThreadProcessId(h,&pid);
    return pid==GetCurrentProcessId();
}
// One sample to the wheel (outside the race too: the delivery zeroes there).
void deliver(bool race,std::int32_t intent){
    ffbdev::Sample s;
    // An open F6 panel counts as outside the race: the gates zero the wheel this sample (the panel's own filter runs
    // after this one in the same tick, and the open state changes only in the message pump between ticks).
    s.race=race&&!desktop_panel_open();s.hwnd=xbox_runtime::native_window().hwnd;s.front=ffb_front_check()(s.hwnd);
    if(f.shim){
        // The shim backend: the steer binding's instance, live from a valid, fresh sample in its epoch (romi 31707).
        const auto st=shim_input_steer();
        s.steer_dev=st.dev;s.steer_live=st.live;s.steer_epoch=st.epoch;
    }else s.steer_dev=wheel_input_steer_device();
    s.ms=GetTickCount64();
    const auto before=f.delivery.phase();
    f.delivery.frame(s,intent);
    if(f.delivery.phase()!=before){
        std::printf("ffb: wheel %s%s%s\n",ffbdev::phase_name(f.delivery.phase()),f.delivery.why().empty()?"":": ",f.delivery.why().c_str());
        std::fflush(stdout);
    }
}
// The model for one race tick; false outside the race. `intent` is the command for the wheel (held x global strength).
bool compute(const platform::NativeRuntimeContext* rt,std::int32_t& intent){
    const std::uint32_t mode=rt?rt->mode_state.current:0u;
    if(!f.epochs.step(mode))return false;
    if(f.epochs.reset_now)f.model->reset();
    const auto& b=rt->event_function36.car_select.car_799d18;
    const auto car=ffb::car_signals(b.data(),b.size());
    // The surface: the game's LUT over the four tyres (the mod's Surface: max roughness) with the player's stage
    // (GetStageUniqueNum(GetNowStageNum(8)) in the mod; the race area's 44DC50 read here, -1 when not yet known).
    const std::int32_t stage=platform::native_race_area_player_stage_unique(*rt);
    float roughness=0.0f;std::uint32_t water=0;
    if(b.size()>=0x260u){
        const int load_coli=int(at<std::uint32_t>(b.data(),0x5c));
        for(unsigned i=0;i<4;++i)roughness=std::max(roughness,float(ffb::surface_roughness(at<std::uint32_t>(b.data(),0x24c+i*4u),load_coli,stage,&water)));
    }
    const unsigned bad=validity(car,roughness);
    Row row;
    f.model->calculate(car,roughness,water,row);
    ++f.race_ticks;
    const std::int32_t held=f.model->last_constant();          // what the device would be holding (the deadband)
    const float scale=f.model->strength_scale();               // FFBGlobalStrength, the final delivery scale
    intent=std::int32_t(std::clamp(float(held)*scale,-10000.0f,10000.0f));
    if(row.sent)++f.sent;
    if(bad)++f.invalid_rows;
    f.min_held=f.race_ticks==1?held:std::min(f.min_held,held);
    f.max_held=f.race_ticks==1?held:std::max(f.max_held,held);
    if(f.trace)std::fprintf(f.trace,"%u,%u,%u,%u,%lu,%.9g,%.9g,%.9g,%.9g,%.9g,%u,%u,%u,%.9g,%u,%d,%u,%d,%d,%.9g,%d\n",
        mode,f.epochs.epoch,f.race_ticks,rt->frame_state.frame_counter_95af0c,GetTickCount(),
        double(car.speed_1c4),double(car.steer_1d0),double(car.steer_rate_1d4),double(car.lateral_264),double(car.lateral_268),
        car.gear_208,car.state_8,car.pedal_34,double(roughness),water,stage,bad,row.sent?1:0,held,double(scale),intent);
    return true;
}
void filter(platform::NativeInputState& out,const platform::NativeRuntimeContext* rt){
    if(f.inner)f.inner(out,rt);
    if(!f.on)return;
    if(f.selected){
        const bool race=rt&&rt->mode_state.current==16u&&rt->event_state.pause_depth==0u;
        const bool allowed=race&&!desktop_panel_open()&&ffb_front_check()(xbox_runtime::native_window().hwnd);
        if(!allowed||!f.row_ready||!f.selected_gate_open)ffb_shadow_invalidate_selected();
        // A callback can arrive after a gated filter tick. Never consume that cached request on gate reopening;
        // the first callback after reopening must prime a fresh producer stream.
        if(!allowed||!f.selected_gate_open){f.row_ready=false;f.selected_intent=0;}
        const auto intent=allowed&&f.row_ready?f.selected_intent:0;
        f.selected_gate_open=allowed;
        f.row_ready=false;f.selected_intent=0;
        if(f.output)deliver(race,intent);
        return;
    }
    std::int32_t intent=0;
    const bool race=compute(rt,intent);
    if(f.output)deliver(race,race?intent:0);
}
// Unloads WheelFfb.dll after the delivery's shutdown, unless the shim's bridge may still have a call inside it (a
// worker preparing the read slot): then it stays loaded for the rest of the process, never freed under a call.
void unload(){
    if(f.owner&&!f.owner->library_quiet()){
        std::printf("ffb: WheelFfb.dll stays loaded: a shim call may still be inside it\n");std::fflush(stdout);return;}
    WheelFfb_Unload(&f.api);
}
// The delivery's shutdown, then the unload; a retained stop (the shim's worker inside the library) unloads nothing and
// is tried again by the next stop (the exit), which does not wait either.
void stop_device(){
    f.delivery.shutdown();
    if(f.delivery.retained()){
        if(!f.retained)std::printf("ffb: wheel output stop retained: %s; tried again at the exit\n",f.delivery.why().c_str());
        std::fflush(stdout);f.retained=true;return;}
    f.retained=false;
    unload();
}
// F4: the wheel output, when every gate allows it (ffb_device.hpp); false leaves WheelFfb.dll unloaded.
bool start_output(const Ini& settings,bool latched,std::string& notes){
    const auto cfg=ffb_output_config(settings,latched,std::getenv("DBCE_FFB_MUTE"),notes);
    const auto off=[](const std::string& why){f.off=why;std::printf("ffb: no wheel output: %s\n",why.c_str());return false;};
    f.shim=cfg.shim;f.requested=cfg.output;
    if(!cfg.output){f.off=ffbdev::load_refusal(cfg);return false;}   // the default: no line of its own
    if(const auto why=ffbdev::load_refusal(cfg);!why.empty())return off(why);
    wchar_t exe[MAX_PATH];const DWORD n=GetModuleFileNameW(nullptr,exe,MAX_PATH);
    std::wstring path=n&&n<MAX_PATH?std::wstring(exe,n):std::wstring();
    const auto slash=path.find_last_of(L"\\/");
    if(slash==std::wstring::npos)return off("the game folder is unknown");
    path=path.substr(0,slash+1)+L"WheelFfb.dll";
    WheelFfb_Load(&f.api,path.c_str());   // a partial resolve is judged by Delivery::start (the exports it uses)
    if(!f.api.module)return off("WheelFfb.dll not found beside the game");
    if(!f.delivery.start(f.api,cfg)){
        WheelFfb_Unload(&f.api);          // start() creates no library state: safe to unload
        return off(f.delivery.why());
    }
    std::printf("ffb: wheel output to the steering device %s (WheelFfb %d); it opens in the race, in front\n",cfg.steer_dev.c_str(),
        f.api.GetWheelFfbVersion());
    return true;
}
}

void ffb_configure(const Ini& settings,bool latched,const std::string& latched_run_folder){
    f=Live{};
    const auto choice=ffb::player::select(settings);
    if(choice.model==ffb::player::Model::Refused){
        std::printf("ffb: player model settings refused (%s); nothing computed, no output\n",choice.refusal.c_str());
        std::fflush(stdout);return;
    }
    if(choice.model==ffb::player::Model::Producer2){
        const auto sink=[](const ffb::shadow::Gate&,const ffb::producer2::Row& row){
            f.selected_intent=0;
            if(row.state==ffb::producer2::State::Computed&&std::isfinite(row.shaped_force))
                f.selected_intent=std::int32_t(std::lround(std::clamp(row.shaped_force,-1.0f,1.0f)*10000.0f));
            f.row_ready=true;
        };
        if(!ffb_shadow_select(choice,sink)){
            std::printf("ffb: producer observer refused; nothing computed, no output\n");std::fflush(stdout);return;
        }
        f.selected=f.on=true;
        std::string notes;
        f.output=start_output(settings,latched,notes);
        if(!notes.empty())std::printf("ffb: %s\n",notes.c_str());
        std::printf("ffb: outrun-producer@2 selected, steering %d%%, shift %d%%; output %s\n",
            choice.strengths.steering_percent,choice.strengths.shift_percent,f.output?"WheelFfb (gated)":"none");
        std::fflush(stdout);return;
    }
    std::string notes,source;
    const auto s=ffb_settings(settings,notes,source);
    std::string path;
    if(latched)path=latched_run_folder.empty()?std::string():latched_run_folder+"\\ffb-trace.csv";   // beside the capture
    else if(const char* trace=std::getenv("OR2_FFB_TRACE");trace&&*trace)path=trace;
    // The model's settings are checked before anything loads: refused settings compute nothing and send nothing.
    auto model=std::make_unique<ffb::LegacyForce>(s);
    if(!model->invalid().empty()){
        if(!notes.empty())std::printf("ffb: %s\n",notes.c_str());
        std::printf("ffb: settings refused (%s); nothing computed, no output\n",model->invalid().c_str());std::fflush(stdout);return;}
    f.output=start_output(settings,latched,notes);
    if(!notes.empty())std::printf("ffb: %s\n",notes.c_str());
    if(path.empty()&&!f.output){
        std::printf("ffb: no force output; the model is computed only with OR2_FFB_TRACE, in a latched run, or for [FFB] Output = wheel\n");
        std::fflush(stdout);return;}
    f.model=std::move(model);
    f.model->set_periodics(false,0,1);   // the CF fallback, as the mod without hardware periodics (F4 sends the constant only)
    f.trace_path=path;f.source=source;
    if(path.empty()){
        f.on=true;
        std::printf("ffb: outrun-legacy@1 to the wheel (%s), no trace\n",source.c_str());std::fflush(stdout);return;}
    f.trace=std::fopen(path.c_str(),"w");
    if(!f.trace){
        std::printf("ffb: trace %s not writable; nothing computed, no output\n",path.c_str());std::fflush(stdout);
        if(f.output){f.delivery.shutdown();WheelFfb_Unload(&f.api);f.output=false;}
        f.model.reset();return;}
    std::fprintf(f.trace,"# dbce.outrun-decomp.ffb-trace,4,model=outrun-legacy@1,output=%s,periodics=fallback,stage=race area 44DC50 (-1 unknown),"
        "settings=%s,seam=input sample (the previous tick's car),process=%s\n",f.output?"wheel (WheelFfb, gated)":"none (no device)",
        source.c_str(),latched?"latched":"ordinary");
    std::fprintf(f.trace,"# FFBGlobalStrength=%.9g FFBSpringStrength=%.9g FFBDamperStrength=%.9g FFBSteeringWeight=%.9g FFBWallImpact=%.9g "
        "FFBGearShift=%.9g FFBRoadTexture=%.9g FFBTireSlip=%.9g FFBGripLoss=%.9g FFBWeightTransfer=%.9g FFBLateralDeadzone=%.9g "
        "FFBEngineIdle=%.9g FFBInvertForce=%d\n",double(s.global_strength),double(s.spring),double(s.damper),double(s.steering_weight),
        double(s.wall_impact),double(s.gear_shift),double(s.road_texture),double(s.tire_slip),double(s.grip_loss),double(s.weight_transfer),
        double(s.lateral_deadzone),double(s.engine_idle),s.invert?1:0);
    std::fprintf(f.trace,"# invalid bits: 1 speed, 2 steer, 4 steer rate, 8 lateral 264, 16 lateral 268, 32 roughness (inputs as read, "
        "before the model's guards); held = the last command (the deadband holds it); intent = held x FFBGlobalStrength\n");
    std::fprintf(f.trace,"mode,epoch,race_tick,frame_95af0c,ms,speed_1c4,steer_1d0,steer_rate_1d4,lateral_264,lateral_268,gear_208,state_8,"
        "pedal_34,roughness,water,stage,invalid,sent,held,global_strength,intent\n");
    f.on=true;
    std::printf("ffb: outrun-legacy@1 software force stream to %s (%s); output %s, periodics fallback, stage from the race area\n",
        path.c_str(),source.c_str(),f.output?"to the wheel (gated)":"none (no device)");
    std::fflush(stdout);
}

void ffb_wrap_input_filter(){
    if(!f.on)return;
    auto& filt=xbox_runtime::input_filter();
    if(filt==filter)return;
    f.inner=filt;filt=filter;
}

void ffb_shutdown(){
    if(f.model)std::printf("ffb: %u race ticks in %u race(s), %u commands sent, held %d..%d, %u rows with invalid input, %u refused, "
        "%u non-finite; trace %s\n",f.race_ticks,f.epochs.epoch,f.sent,f.min_held,f.max_held,f.invalid_rows,f.model->refused_ticks(),
        f.model->nonfinite_ticks(),f.trace_path.c_str());
    if(f.trace){std::fclose(f.trace);f.trace=nullptr;}
    if(f.output||f.retained){
        // Quiesce first (PanicStop, FreeDirectInput: effects stopped, the window subclass removed, the watchdog joined),
        // then unload: never the other way round.
        stop_device();
        const auto& c=f.delivery.counts();
        const std::string why=f.delivery.why().empty()?std::string():" ("+f.delivery.why()+")";
        std::printf("ffb: wheel output %s%s; %u commands (%u keep-alive), %u accepted zeros, %u recoveries, %u transient refusals\n",
            ffbdev::phase_name(f.delivery.phase()),why.c_str(),c.sent,c.keepalive,c.zeroes,c.recoveries,c.transient);
        f.output=false;
    }
    std::fflush(stdout);
    f.on=false;
    if(f.selected)ffb_shadow_shutdown();
}

FfbFrontCheck& ffb_front_check(){static FfbFrontCheck check=own_front;return check;}

std::string ffb_status_text(){
    // The player's words; runtime.log keeps the gates' full reasons.
    if(f.retained)return "stopping: a shim call is still inside WheelFfb; the library's watchdog silences the wheel, and the stop completes at the exit";
    if(!f.output){
        if(f.stopped)return "off (stopped)";
        if(!f.shim||!f.requested||f.off.empty())return "off";
        // The shim backend: why the output did not start, and the steering wheel as the shim reads it.
        const auto s=shim_input_steer();
        const std::string wheel=s.live?"live, epoch "+std::to_string(s.epoch):s.opened?"open, not live, epoch "+std::to_string(s.epoch):
                                s.status.empty()?std::string("not known"):s.status;
        return "off: "+f.off+"; steering wheel in the shim: "+wheel;
    }
    switch(f.delivery.phase()){
    case ffbdev::Phase::Waiting:return "on: waits for the race, the game in front and the steering wheel";
    case ffbdev::Phase::Open:return "on";
    case ffbdev::Phase::Released:return "off for this session: "+f.delivery.why();
    default:return "off";
    }
}

bool ffb_output_running(){return f.output;}
bool ffb_library_present(){
    wchar_t exe[MAX_PATH];const DWORD n=GetModuleFileNameW(nullptr,exe,MAX_PATH);
    std::wstring path=n&&n<MAX_PATH?std::wstring(exe,n):std::wstring();
    const auto slash=path.find_last_of(L"\\/");
    if(slash==std::wstring::npos)return false;
    const DWORD a=GetFileAttributesW((path.substr(0,slash+1)+L"WheelFfb.dll").c_str());
    return a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_DIRECTORY);
}

const WheelFfbApi* ffb_shim_wheel_api(){return f.output&&f.shim&&f.delivery.phase()==ffbdev::Phase::Waiting?&f.api:nullptr;}
void ffb_set_shim_owner(ffbdev::Owner* o){if(f.output&&f.shim&&f.delivery.phase()==ffbdev::Phase::Waiting){f.delivery.set_owner(o);f.owner=o;}}

void ffb_stop_output(){
    if(!f.output)return;
    stop_device();           // PanicStop, FreeDirectInput: quiesced before the unload (or retained, see stop_device)
    f.output=false;f.stopped=true;f.off="stopped by Stop FFB / F8";
    std::printf("ffb: wheel output %s by Stop FFB (F8); the software force stream continues\n",f.retained?"stop pending":"stopped");
    std::fflush(stdout);
}
}
