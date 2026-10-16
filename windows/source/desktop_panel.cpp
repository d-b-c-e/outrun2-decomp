// The F6 panel (desktop_panel.hpp): the toolkit panel's frame sequence on our own D3D11 device, as the toolkit's
// overlay runs it (dbce_overlay_d3d11.cpp render11 / drawFrame), called from the renderer's present instead of a hook.
#include "desktop_panel.hpp"
#include "desktop_keyboard.hpp"
#include "desktop_settings.hpp"
#include "hud_toggle.hpp"
#include "ffb_live.hpp"
#include "ffb_player_model.hpp"
#include "native_window.hpp"
#include "telemetry_forza.hpp"
#include "shim_input.hpp"
#include "triple_screens.hpp"
#include "wheel_input.hpp"
#include "display_fit.hpp"
#include "renderer.hpp"
#include "platform/native_runtime.hpp"
#include "platform/music_stream.hpp"
#include "platform/frontend_keyboard.hpp"
#include "platform/pc_screen.hpp"
#include "platform/runtime_input.hpp"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "dbce_panel.h"
#include "dbce_panel_input.h"
#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <cstdio>
#include <cstring>
#include <string>
namespace outrun::desktop {
namespace {
// Opt-in, passive ordinary-play route observation. One complete TSV line per
// transition (plus the first snapshot); no input, owner state or UI mutation.
struct FrontendRouteTrace {
    HANDLE file{INVALID_HANDLE_VALUE};
    std::string previous;
    unsigned long long sequence{};
    bool initialized{},failed{};
    ~FrontendRouteTrace(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
    void observe(const platform::NativeRuntimeContext* rt){
        if(!rt||failed)return;
        const char* path=std::getenv("OR2_FRONTEND_STATE_TRACE");
        if(!path||!*path)return;
        if(!initialized){
            initialized=true;
            file=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
            if(file==INVALID_HANDLE_VALUE){failed=true;std::printf("frontend route trace: CREATE_NEW failed (%lu)\n",GetLastError());return;}
            constexpr char header[]="seq\tpid\trun\tms\tmode\tstartStage\teditorState\titem\tfocused\tready\taccepted\tselected\tnameLen\tdoneMoves\tsaves\tloaded\tchildState\tmenuIndex\tchoiceResult\n";
            DWORD written=0;
            if(!WriteFile(file,header,DWORD(sizeof header-1),&written,nullptr)||written!=sizeof header-1){failed=true;return;}
        }
        auto* owner=rt->event_function36.license_owners;
        int state=-1,item=-1,selected=-1,name_len=0,focused=0,ready=0,accepted=0;
        std::string moves="-";
        if(owner){
            const auto editor=driving::Bytes(owner->storage(24),owner->size(24));
            state=int(editor.u32(0x38));item=int(editor.u32(0x3c));
            if(const auto* widget=owner->widgets(24)){
                const auto& keyboard=widget->keyboard_state();
                focused=int(keyboard.focused);ready=int(keyboard.ready_seen);accepted=int(keyboard.accepted);
                selected=int(keyboard.selected);
                if(keyboard.focused){
                    const auto bytes=editor.sub(0xa78,platform::PcKeyboardBytes);
                    name_len=int(platform::keyboard_name_468770(bytes).size());
                    if(keyboard.ready_seen){moves=platform::keyboard_path_to_done(keyboard.selected,(bytes.u32(0x708)&2u)!=0u);}
                }
            }
        }
        const std::string snapshot=std::to_string(rt->mode_state.current)+'\t'+
            std::to_string(rt->start_mode.stage)+'\t'+std::to_string(state)+'\t'+std::to_string(item)+'\t'+
            std::to_string(focused)+'\t'+std::to_string(ready)+'\t'+std::to_string(accepted)+'\t'+
            std::to_string(selected)+'\t'+std::to_string(name_len)+'\t'+moves+'\t'+
            std::to_string(owner?owner->save_calls():0u)+'\t'+
            std::to_string(rt->event_function36.frontend_profiles.loaded_count)+'\t'+
            std::to_string(rt->event_function36.state2_selector_child_state)+'\t'+
            std::to_string(rt->event_function36.frontend_menu_index)+'\t'+
            std::to_string(rt->event_function36.state2_selector_last_result);
        if(sequence&&snapshot==previous)return;
        previous=snapshot;
        const char* run=std::getenv("OR2_FRONTEND_RUN_ID");
        const std::string line=std::to_string(++sequence)+'\t'+std::to_string(GetCurrentProcessId())+'\t'+
            (run&&*run?run:"-")+'\t'+std::to_string(GetTickCount64())+'\t'+snapshot+'\n';
        DWORD written=0;
        if(!WriteFile(file,line.data(),DWORD(line.size()),&written,nullptr)||written!=line.size()){
            failed=true;std::printf("frontend route trace: WriteFile failed (%lu)\n",GetLastError());
        }
    }
};
FrontendRouteTrace route_trace;
struct Panel {
    bool configured{},latched{},input{},backend{},was_open{},playback{};
    bool force_library{},force_muted{};   // WheelFfb.dll beside the game; DBCE_FFB_MUTE in this process
    ffb::player::Model player_model{ffb::player::Model::Legacy};
    ffb::producer2::Strengths applied_strengths{};
    std::string model_refusal;
    bool wav_decoder{},flac_decoder{};   // optional music decoders in this build, queried once at startup
    xbox_runtime::InputFilter inner{};
    ID3D11Device* device{};
    std::string ini,dev,name;
    LARGE_INTEGER freq{},last{};
};
Panel p;
// UILetterboxing, outside Panel (desktop_panel_configure resets that): the race state the game thread saw last
// (input_filter), for the present overlay; and the context's ClearView interface (D3D11.1), queried once per context.
struct Letterbox {
    std::atomic<bool> in_race{false};
    ID3D11DeviceContext* context_for{};ID3D11DeviceContext1* context1{};bool missing_logged{};
};
Letterbox lb;
// The values the pages show, read once from the settings at start; each change saves its one key at once (write_key)
// and applies at the next game start (the owner's choice is never half-applied to a running game).
struct Values {
    int triple{},projection{},latency{2};   // projection: [Triple] Views, 0 Planar, 1 Angled
    int ui_scaling{2},letterbox{1},letterbox_start{1};   // [Graphics] UIScalingMode (read only) / UILetterboxing
    bool lens_flare_fix{true},shading_fix{true},particle_fix{true},bunki_fix{true};
    std::string shadow_opacity{"1"};   // raw setting: arbitrary custom values stay intact until an explicit preset edit
    bool screen_edge_cull{true},screen_edge_cull_start{true};   // [Graphics] ScreenEdgeCullFix
    int anisotropic{},reflection{1},draw_ahead{},draw_behind{};   // reflection: index of 64..4096, default 128
    bool transparency{},full_vehicle_detail{},no_stage_cull{},depth_precision{};   // existing default-off Graphics flags
    bool auto_resolution{true},auto_resolution_start{true};   // [Window] AutoDetectResolution
    int projection_start{};   // [Triple] Views as read at start: the pending note compares with it, not with the status's
                              // request, which also counts the OR2_TRIPLE_VIEWS developer switch (nene LOW 2)
    bool telemetry{true};int layout{};
    int global{100},spring{},damper{},weight{},grip{},transfer{},wall{},gear{},road{},slip{},idle{};bool invert{};
    int selected_steering{50},selected_shift{50};   // [FFBProducer2] integer percentages for the next start
    int sensitivity{100},dead_zone{};
    bool manual_default{},manual_default_start{};   // [Misc] DefaultManualTransmission
    bool allow_horn{true},allow_horn_start{true};   // [Audio] AllowHorn
    std::string hud_key,hud_key_start;   // [Misc] HudToggleKey, empty means Off
    bool allow_wav{},allow_wav_start{},allow_flac{},allow_flac_start{};   // [Audio], both default off
    bool license_character{},menu_miles{true},hide_signin{};   // existing Misc/Bugfixes menu flags, next start only
    bool full_pedal_fix{true},pegasus_sound_fix{true};   // existing default-on Bugfixes, next start only
    bool highway_sets{};   // [Misc] RandomHighwayAnimSets, existing default-off startup policy
    int holly_course2{3};   // [Misc] EnableHollyCourse2, existing next-start availability policy
    bool hidef_characters{true},jp_clarissa{};   // existing Graphics/Misc character appearance, next start only
    bool output_next{};   // [FFB] Output = wheel in the file (the wheel output at the next start)
};
Values v;
int pct(float f){return int(std::lround(double(f)*100.0));}
std::string fraction(int percent){char t[16];std::snprintf(t,sizeof t,"%.2f",percent/100.0);return t;}
// The toolkit's own lines already start with "panel:"; ours get the prefix here.
void log_line(const char* line){std::printf(std::strncmp(line,"panel:",6)==0?"%s\n":"panel: %s\n",line);std::fflush(stdout);}
// A failed save's note, shown again at the next drawn frame: the toolkit's own Stop FFB path notes success after the
// callback returns, which would hide a failure.
std::string pending_failure,stop_error;
// One key into OutRunDesktop.ini (the file Wheelkit reads); never in a test run. False: not saved (the caller puts
// its control back to the saved value, so the page shows only what the next start will use).
bool save(const char* section,const char* key,const std::string& value){
    if(p.latched){pending_failure="a test run saves nothing";dbce::panel::NoteSaved(false,pending_failure.c_str());return false;}
    std::string error;
    const bool ok=write_key(p.ini,section,key,value,error);
    if(ok)pending_failure.clear();else pending_failure="not saved: "+error;
    dbce::panel::NoteSaved(ok,ok?"applies at the next game start":pending_failure.c_str());
    std::printf("panel: [%s] %s = %s %s\n",section,key,value.c_str(),ok?"saved":pending_failure.c_str());std::fflush(stdout);
    return ok;
}
// A control's change saved, or put back to the value it had (the saved one).
template<class T,class S> void keep(T* value,T before,S saved){if(!saved())*value=before;}
bool percent_key(const char* label,int* value,int max,int def,const char* key,const char* help=nullptr){
    const int before=*value;
    if(!dbce::panel::Percent(label,value,0,max,def,help))return false;
    keep(value,before,[&]{return save("FFB",key,fraction(*value));});return true;
}
void graphics_flag(const char* label,bool* value,const char* key,const char* help,const char* section="Graphics"){
    const bool before=*value;
    if(dbce::panel::OffOn(label,value,help))
        keep(value,before,[&]{return save(section,key,*value?"true":"false");});
}
void graphics_slider(const char* label,int* value,int max,const char* key,const char* format,const char* help){
    const int before=*value;
    if(dbce::panel::Slider(label,value,0,max,0,format,help))
        keep(value,before,[&]{return save("Graphics",key,std::to_string(*value));});
}
void page_controls(bool advanced){
    // The backend this process uses: [Input] Backend = Shim runs the shared input shim and never configures wheel_input.
    const auto shim=shim_input_status_text();
    dbce::panel::Status("Input",shim.empty()?"Native (the build's own wheel input)":"Shim (the shared input shim, [Input] Backend = Shim)");
    dbce::panel::Status("Controls",(shim.empty()?wheel_input_status_text():shim).c_str());
    dbce::panel::Text("Bindings are set in Wheelkit ([Controls] of the settings file).");
    {const int before=v.sensitivity;
     if(dbce::panel::Slider("Steering sensitivity",&v.sensitivity,10,300,100,"%d%%"))
         keep(&v.sensitivity,before,[&]{return save("Wheel","SteeringSensitivity",fraction(v.sensitivity));});}
    {const int before=v.dead_zone;
     if(dbce::panel::Percent("Steering dead zone",&v.dead_zone,0,90,0))
         keep(&v.dead_zone,before,[&]{return save("Wheel","SteeringDeadZone",fraction(v.dead_zone));});}
    // [Misc] DefaultManualTransmission (pc_screen.hpp), read at start, so it applies at the next start.
    {const bool before=v.manual_default;
     if(dbce::panel::OffOn("Manual transmission by default",&v.manual_default,
         "At the next start, the transmission menu opens on Manual instead of Automatic, as Tweaks' "
         "DefaultManualTransmission. Its opening animation still shows Automatic briefly."))
         keep(&v.manual_default,before,[&]{return save("Misc","DefaultManualTransmission",v.manual_default?"true":"false");});}
    if(v.manual_default!=v.manual_default_start)dbce::panel::Text("The choice applies at the next game start.");
    {const bool before=v.allow_horn;
     if(dbce::panel::OffOn("Horn",&v.allow_horn,
         "Allows the car horn during a race. Applies at the next game start."))
         keep(&v.allow_horn,before,[&]{return save("Audio","AllowHorn",v.allow_horn?"true":"false");});}
    if(v.allow_horn!=v.allow_horn_start)dbce::panel::Text("The horn choice applies at the next game start.");
    {
        const auto& keys=hud_toggle_setting_choices();
        std::vector<const char*> labels;labels.reserve(keys.size()+1);
        for(const auto& key:keys)labels.push_back(key.empty()?"Off":key.c_str());
        int selected=hud_toggle_setting_index(v.hud_key);
        if(selected<0){
            selected=int(keys.size());labels.push_back("Keep saved value (unsupported)");
            dbce::panel::Status("Saved HUD key",v.hud_key.c_str(),dbce::panel::Tone::Warning);
        }
        if(dbce::panel::Choice("HUD toggle key",labels.data(),int(labels.size()),&selected,
            "Shows or hides the race HUD when this key is released. Applies at the next game start. "
            "Off clears the key. Letters and digits depend on the keyboard layout. F6, F8 and F11 are reserved. "
            "The key still reaches the game; toggling is disabled during recording, playback and tests.")){
            if(selected>=0&&selected<int(keys.size())&&save("Misc","HudToggleKey",keys[std::size_t(selected)]))
                v.hud_key=keys[std::size_t(selected)];
        }
        if(v.hud_key!=v.hud_key_start)dbce::panel::Text("The HUD key choice applies at the next game start.");
    }
    if(advanced){
        dbce::panel::Status("Music decoding",p.wav_decoder?(p.flac_decoder?"WAV and FLAC available":"WAV available; FLAC unavailable"):
            (p.flac_decoder?"WAV unavailable; FLAC available":"WAV and FLAC unavailable in this build"),
            p.wav_decoder&&p.flac_decoder?dbce::panel::Tone::Normal:dbce::panel::Tone::Warning);
        {const bool before=v.allow_wav;
         if(dbce::panel::OffOn("WAV music replacements",&v.allow_wav,
             "Experimental. Uses a same-name .wav file in Sound when no enabled .flac replacement exists. "
             "Requires WAV decoding in this build. A selected replacement that cannot be read does not fall back to Ogg. "
             "Applies at the next game start."))
             keep(&v.allow_wav,before,[&]{return save("Audio","AllowWAV",v.allow_wav?"true":"false");});}
        {const bool before=v.allow_flac;
         if(dbce::panel::OffOn("FLAC music replacements",&v.allow_flac,
             "Experimental. Uses a same-name .flac file in Sound before an enabled WAV replacement. "
             "Requires FLAC decoding in this build. A selected replacement that cannot be read does not fall back to WAV or Ogg. "
             "Applies at the next game start."))
             keep(&v.allow_flac,before,[&]{return save("Audio","AllowFLAC",v.allow_flac?"true":"false");});}
        if(v.allow_wav!=v.allow_wav_start||v.allow_flac!=v.allow_flac_start)
            dbce::panel::Text("The music replacement choices apply at the next game start.");
        dbce::panel::Heading("Menus and license");
        {const bool before=v.license_character;
         if(dbce::panel::OffOn("License character selection",&v.license_character,
             "Off by default. Allows the character entry in the license editor. Applies at the next game start."))
             keep(&v.license_character,before,[&]{return save("Misc","AllowCharacterSelection",v.license_character?"true":"false");});}
        {const bool before=v.hide_signin;
         if(dbce::panel::OffOn("Hide sign-in text",&v.hide_signin,
             "Off by default. Hides the menu sign-in status box, text and SIGN-IN help-bar entry. Also hides the menu's "
             "OutRun Miles line without changing its saved choice. Applies at the next game start."))
             keep(&v.hide_signin,before,[&]{return save("Bugfixes","HideOnlineSigninText",v.hide_signin?"true":"false");});}
        {const bool before=v.menu_miles;
         if(dbce::panel::OffOn("OutRun Miles on the menu",&v.menu_miles,
             "On by default. Shows the active license's OutRun Miles in the menu's Not Signed In status. Hidden while "
             "Hide sign-in text is On. Applies at the next game start."))
             keep(&v.menu_miles,before,[&]{return save("Misc","ShowOutRunMilesOnMenu",v.menu_miles?"true":"false");});}
        if(v.hide_signin&&v.menu_miles)
            dbce::panel::Status("Menu Miles at next start","hidden with sign-in text; the saved Miles choice is kept");
        dbce::panel::Text("Menu and license choices apply at the next game start.");
        {const int before=v.holly_course2;
         static const char* modes[]={"Off","Unlock with Holly 1","Unlock at Holly 1 grade A","Show and unlock at Holly 1 grade A"};
         if(dbce::panel::Choice("Holly MIX 2 availability",modes,4,&v.holly_course2,
             "Default: show and unlock when Holly 1 reaches grade A. Unlock at grade A shows the tile earlier, locked. "
             "All modes require valid MIX 2 course data; missing or malformed data stays locked. "
             "Does not change license progress. Applies at the next game start."))
             keep(&v.holly_course2,before,[&]{return save("Misc","EnableHollyCourse2",std::to_string(v.holly_course2));});}
        dbce::panel::Heading("Compatibility fixes");
        {const bool before=v.full_pedal_fix;
         if(dbce::panel::OffOn("Full pedal press check fix",&v.full_pedal_fix,
             "On (default): treats nearly full throttle or brake (250-254 out of 255) as a full press for checks such as "
             "backfire. Calibration, stored input and recordings are unchanged. Off restores the original 255-only checks. "
             "Applies at the next game start."))
             keep(&v.full_pedal_fix,before,[&]{return save("Bugfixes","FixFullPedalChecks",v.full_pedal_fix?"true":"false");});}
        {const bool before=v.pegasus_sound_fix;
         if(dbce::panel::OffOn("Pegasus sound loop fix",&v.pegasus_sound_fix,
             "On (default): fixes Pegasus hoofbeats continuing after the event sound is stopped. "
             "Off keeps the original sound-stop behavior. Applies at the next game start."))
             keep(&v.pegasus_sound_fix,before,[&]{return save("Bugfixes","FixPegasusClopping",v.pegasus_sound_fix?"true":"false");});}
        dbce::panel::Heading("Highway animations");
        {const bool before=v.highway_sets;
         if(dbce::panel::OffOn("Random highway animation sets",&v.highway_sets,
             "Experimental; Off by default. Mixes highway animation sets from other game modes; animations still require "
             "their event conditions. Overrides the separate row-only option while On, without changing its saved choice. "
             "Applies at the next game start."))
             keep(&v.highway_sets,before,[&]{return save("Misc","RandomHighwayAnimSets",v.highway_sets?"true":"false");});}
        dbce::panel::Heading("Character appearance");
        {const bool before=v.hidef_characters;
         if(dbce::panel::OffOn("High definition characters",&v.hidef_characters,
             "On (default): uses the high definition Alberto, Jennifer and Clarissa character sets during gameplay. "
             "Off uses their standard sets. Applies at the next game start."))
             keep(&v.hidef_characters,before,[&]{return save("Graphics","UseHiDefCharacters",v.hidef_characters?"true":"false");});}
        {const bool before=v.jp_clarissa;
         if(dbce::panel::OffOn("Japanese Clarissa model",&v.jp_clarissa,
             "Off by default. Uses Clarissa's Japanese model in OutRun 2 SP arcade mode and its ending. Works with "
             "either character detail choice. Applies at the next game start."))
             keep(&v.jp_clarissa,before,[&]{return save("Misc","RestoreJPClarissa",v.jp_clarissa?"true":"false");});}
        dbce::panel::Text("Character appearance choices apply at the next game start.");
    }
}
void page_ffb(bool advanced){
    const auto status=ffb_status_text();
    dbce::panel::Status("Wheel output",status.c_str(),ffb_output_running()?dbce::panel::Tone::Good:dbce::panel::Tone::Normal);
    if(!stop_error.empty())dbce::panel::Status("Stop FFB",("the output stopped, but turning it off for the next start failed: "+stop_error).c_str(),
                                                 dbce::panel::Tone::Error);
    // The saved setting for the NEXT start ([FFB] Output = wheel | none), apart from the output above. It is never applied
    // live: On cannot start torque in this session, and Off does not stop a running output (Stop FFB and F8 do). The
    // delivery's own gates (a race, the game in front, the steering wheel, DBCE_FFB_MUTE) still decide when it runs.
    {const bool before=v.output_next;
     if(v.output_next||p.force_library){
         if(dbce::panel::OffOn("Wheel output at the next start",&v.output_next,
                "Saved for the next start of the game. On: force on the steering wheel during a race, with the game in "
                "front. Nothing changes in this session."))
             keep(&v.output_next,before,[&]{return save("FFB","Output",v.output_next?"wheel":"none");});
     }else dbce::panel::Status("Wheel output at the next start","off: turning it on needs the force add-on (WheelFfb.dll beside the game)");}
    if(ffb_output_running()&&!v.output_next)
        dbce::panel::Status("This session","the output still runs until the game closes; Stop FFB or F8 stops it now",dbce::panel::Tone::Warning);
    else if(v.output_next&&!ffb_output_running())
        dbce::panel::Status("This session",p.force_muted?"not running: this process is force-muted (DBCE_FFB_MUTE)":"not running in this session; see Wheel output above",
                            dbce::panel::Tone::Normal);
    dbce::panel::Text("Stop FFB and F8 turn the output off now and at the next start.");
    if(p.player_model!=ffb::player::Model::Legacy){
        if(p.player_model==ffb::player::Model::Producer2){
            dbce::panel::Status("Player model","outrun-producer@2 selected; the legacy FFB sliders do not tune it");
            const auto active="steering "+std::to_string(p.applied_strengths.steering_percent)+"%, shift "+
                std::to_string(p.applied_strengths.shift_percent)+"%";
            dbce::panel::Status("Model in this session",active.c_str());
            {const int before=v.selected_steering;
             if(dbce::panel::Percent("Steering strength",&v.selected_steering,0,100,50,
                 "Scales the selected model's provisional speed-and-steer centering estimate. Saved for the next game start."))
                 keep(&v.selected_steering,before,[&]{return save("FFBProducer2","SteeringStrengthPercent",std::to_string(v.selected_steering));});}
            {const int before=v.selected_shift;
             if(dbce::panel::Percent("Shift strength",&v.selected_shift,0,100,50,
                 "Scales the selected model's gear-change cue independently of steering. Saved for the next game start."))
                 keep(&v.selected_shift,before,[&]{return save("FFBProducer2","ShiftStrengthPercent",std::to_string(v.selected_shift));});}
            if(v.selected_steering!=p.applied_strengths.steering_percent||v.selected_shift!=p.applied_strengths.shift_percent)
                dbce::panel::Text("The saved strengths apply at the next game start; this session keeps the strengths shown above.");
        }else{
            const auto state="settings refused: "+p.model_refusal+"; correct the model settings before force can run";
            dbce::panel::Status("Player model",state.c_str(),dbce::panel::Tone::Warning);
        }
        return;
    }
    const ffb::LegacySettings d;   // the mod's compiled defaults, for each slider's Default
    percent_key("Strength",&v.global,100,pct(d.global_strength),"FFBGlobalStrength");
    if(!advanced)return;
    percent_key("Centering",&v.spring,200,pct(d.spring),"FFBSpringStrength");
    percent_key("Damping",&v.damper,200,pct(d.damper),"FFBDamperStrength");
    percent_key("Cornering load",&v.weight,200,pct(d.steering_weight),"FFBSteeringWeight");
    percent_key("Grip loss",&v.grip,100,pct(d.grip_loss),"FFBGripLoss");
    percent_key("Weight transfer",&v.transfer,200,pct(d.weight_transfer),"FFBWeightTransfer");
    percent_key("Wall impact",&v.wall,200,pct(d.wall_impact),"FFBWallImpact");
    percent_key("Gear shift",&v.gear,200,pct(d.gear_shift),"FFBGearShift");
    percent_key("Road texture",&v.road,200,pct(d.road_texture),"FFBRoadTexture");
    percent_key("Tire slip",&v.slip,200,pct(d.tire_slip),"FFBTireSlip");
    percent_key("Engine idle",&v.idle,100,pct(d.engine_idle),"FFBEngineIdle");
    {const bool before=v.invert;
     if(dbce::panel::OffOn("Invert force",&v.invert))keep(&v.invert,before,[&]{return save("FFB","FFBInvertForce",v.invert?"true":"false");});}
}
void page_cameras(bool advanced){
    static const char* const modes[]{"Off","Surround","Separate monitors"};
    {const int before=v.triple;
     if(dbce::panel::Choice("Triple screens",modes,3,&v.triple,"Off: one 16:9 view. Surround: a Surround desktop's wide window. "
         "Separate monitors: one window over three equal side-by-side monitors. Projection, below, chooses what they show."))
         keep(&v.triple,before,[&]{return save("Triple","Screens",triple_screens_name(TripleScreens(v.triple)));});}
    const auto& st=triple_screens_status();
    char now[320];
    if(st.applied==TripleProjection::angled)
        std::snprintf(now,sizeof now,"%s, %s, three projected views (experimental)",triple_screens_name(st.mode),st.where.c_str());
    else if(st.aspect>0.f)std::snprintf(now,sizeof now,"%s, %s, one wide view %.2f:1",triple_screens_name(st.mode),st.where.c_str(),double(st.aspect));
    else std::snprintf(now,sizeof now,"%s, %s, one 16:9 view",triple_screens_name(st.mode),st.where.c_str());
    dbce::panel::Status("Now",now);
    // [Triple] Views, below the selector and only when it is not Off (STD-022 options sit there; sana 35626 wording).
    if(v.triple!=int(TripleScreens::off)){
        static const char* const projections[]{"One wide view","Three projected views (experimental)"};
        const int before=v.projection;
        if(dbce::panel::Choice("Projection",projections,2,&v.projection,"One wide view: one picture across the screens, as Tweaks. "
            "Three projected views: each side screen shows its own angle, for the preset rig (32-inch 16:9 panels, sides at "
            "70 degrees, 8 mm gaps, the eye matched to the game's field of view), not yet your measured rig."))
            keep(&v.projection,before,[&]{return save("Triple","Views",triple_projection_name(TripleProjection(v.projection)));});
        if(st.requested==TripleProjection::angled&&st.applied!=TripleProjection::angled)
            dbce::panel::Status("Projection now",(std::string(st.aspect>0.f?"one wide view":"one 16:9 view")+" (three projected views not applied: "+
                                st.projection_note+")").c_str(),dbce::panel::Tone::Warning);
    }
    if(v.triple!=int(st.mode)||v.projection!=v.projection_start)dbce::panel::Text("The choice applies at the next game start.");
    if(!advanced)return;
    // As Wheelkit's Display page words it (sana 90841): 0 is the plain presentation mode, not the least-lag choice.
    static const char* const latency[]{"0 (plain)","1","2 (default)","3"};
    const int before=v.latency;
    if(dbce::panel::Choice("Frame latency",latency,4,&v.latency,
           "Frames that may wait for the display: 1 queues fewer frames, 2 is the default, 3 allows more; "
           "0 uses the plain presentation mode."))
        keep(&v.latency,before,[&]{return save("Window","FrameLatency",std::to_string(v.latency));});
    // [Graphics] UILetterboxing (the Tweaks key; the index is the value written), read at start, so it applies at the
    // next start as the rows above. It acts only with UIScalingMode 1 or 2 (pc_screen.hpp); UIScalingMode itself has
    // no row here yet.
    {static const char* const boxing[]{"Off","Outside the race (default)","Always"};
     const int before=v.letterbox;
     if(dbce::panel::Choice("Letterboxing",boxing,3,&v.letterbox,"Black bars beside the centered 4:3 at the next start, as Tweaks' "
         "UILetterboxing: outside the race (menus, loading), or always."))
         keep(&v.letterbox,before,[&]{return save("Graphics","UILetterboxing",std::to_string(v.letterbox));});}
    if(v.letterbox>0&&v.ui_scaling==0)dbce::panel::Status("Letterboxing now","inactive: [Graphics] UIScalingMode is 0 (stretched)");
    if(v.letterbox!=v.letterbox_start)dbce::panel::Text("The choice applies at the next game start.");
    {const bool before=v.screen_edge_cull;
     if(dbce::panel::OffOn("Widescreen object visibility",&v.screen_edge_cull,
         "On (default): cones, course objects and gates use the wider screen bounds. Off: these objects use the original "
         "4:3 visibility bounds. Both choices behave the same at 4:3. Applies at the next game start."))
         keep(&v.screen_edge_cull,before,[&]{return save("Graphics","ScreenEdgeCullFix",v.screen_edge_cull?"true":"false");});}
    if(v.screen_edge_cull!=v.screen_edge_cull_start)dbce::panel::Text("The choice applies at the next game start.");
    // [Window] AutoDetectResolution (renderer.hpp g_auto_detect_height), read at start, so it applies at the next start.
    {const bool before=v.auto_resolution;
     if(dbce::panel::OffOn("Resolution from the display",&v.auto_resolution,
         "While the game's own Settings hold no resolution, the next start renders at the supported height nearest the "
         "display's instead of the fixed 1440 default, as Tweaks' AutoDetectResolution. A resolution chosen in Settings "
         "always wins."))
         keep(&v.auto_resolution,before,[&]{return save("Window","AutoDetectResolution",v.auto_resolution?"true":"false");});}
    if(v.auto_resolution!=v.auto_resolution_start)dbce::panel::Text("The choice applies at the next game start.");
    dbce::panel::Heading("Graphics detail");
    graphics_slider("Anisotropic filtering",&v.anisotropic,16,"AnisotropicFiltering","%d",
        "0 (default) and 1 keep the game's filtering. 2-16 sharpen textures viewed at an angle; point-filtered textures stay unchanged.");
    {static const char* const edges[]{"64","128 (default)","256","512","1024","2048","4096"};
     static const int pixels[]{64,128,256,512,1024,2048,4096};
     const int before=v.reflection;
     if(dbce::panel::Choice("Car reflection resolution",edges,7,&v.reflection,
         "Pixels per side of the car reflection cube. Higher values use more GPU memory and rendering time."))
         keep(&v.reflection,before,[&]{return save("Graphics","ReflectionResolution",std::to_string(pixels[v.reflection]));});}
    graphics_flag("Transparency supersampling",&v.transparency,"TransparencySupersampling",
        "Off by default. Uses alpha-to-coverage for alpha-tested edges only while MSAA is enabled in the game's Settings.");
    graphics_flag("Full-detail other cars",&v.full_vehicle_detail,"DisableVehicleLODs",
        "Off by default. On keeps other cars at their full-detail model even in the distance; this can increase GPU work.");
    graphics_flag("Draw stage nodes outside the view",&v.no_stage_cull,"DisableStageCulling",
        "Off by default. On disables stage culling and can substantially increase GPU work.");
    graphics_slider("Draw distance ahead",&v.draw_ahead,1024,"DrawDistanceIncrease","%d sections",
        "0 keeps the game's distance for planar views. Adds track sections ahead; existing lods.ini exclusions apply. More sections increase GPU work.");
    graphics_slider("Draw distance behind",&v.draw_behind,256,"DrawDistanceBehind","%d sections",
        "0 keeps the game's distance for planar views. Adds track sections behind; some models look wrong from behind. More sections increase GPU work.");
    dbce::panel::Text("Three projected views use 64 sections ahead and behind where the saved value is 0; their developer overrides still apply.");
    graphics_flag("Depth precision fix",&v.depth_precision,"FixZBufferPrecision",
        "Off by default. Adjusts the race camera's near plane by view to reduce distant surface flicker; it can clip very close geometry.");
    dbce::panel::Heading("Graphics compatibility");
    graphics_flag("Lens flare asset fix",&v.lens_flare_fix,"FixLensFlarePath",
        "On (default): uses the shipped lens-flare offsets when available, with the original asset fallback. Off uses only the original asset path. Applies at the next game start.","Bugfixes");
    graphics_flag("Double-sided shading fix",&v.shading_fix,"FixIncorrectShading",
        "On (default): fixes lighting on affected double-sided materials. Off keeps the original shader mapping. Applies at the next game start.","Bugfixes");
    graphics_flag("Particle rendering fix",&v.particle_fix,"FixParticleRendering",
        "On (default): fixes particle size and placement, and the Metropolis fireworks object. Off keeps the original particle behavior. Applies at the next game start.","Bugfixes");
    graphics_flag("Right-side junction animation fix",&v.bunki_fix,"FixRightSideBunkiAnimations",
        "On (default): fixes animation visibility on right-side junction models. Off keeps their original visibility behavior. Applies at the next game start.","Bugfixes");
    {
        static const char* const values[]{"0","0.25","0.5","0.75","1","1.5","2"};
        static const char* const labels[]{"Off (0%)","25%","50%","75%","100% (default)","150%","200%","Keep saved value"};
        int selected=7;
        for(int i=0;i<7;++i)if(v.shadow_opacity==values[i])selected=i;
        if(v.shadow_opacity=="1.0"||v.shadow_opacity=="1.00")selected=4;   // common default spellings; no rewrite
        if(selected==7)dbce::panel::Status("Saved base shadow opacity",v.shadow_opacity.c_str());
        if(dbce::panel::Choice("Car base shadow opacity",labels,8,&selected,
            "Flat car-shadow strength: 100% is the default, 0% disables it, and values above 100% strengthen it. Keep saved value preserves custom values and spellings, including values above 200%; the native setting is not capped at 200%. Applies at the next game start.")){
            if(selected>=0&&selected<7&&save("Graphics","CarBaseShadowOpacity",values[selected]))
                v.shadow_opacity=values[selected];
        }
    }
    dbce::panel::Text("Graphics detail changes apply at the next game start.");
}
void page_telemetry(bool advanced){
    {const bool before=v.telemetry;
     if(dbce::panel::OffOn("Telemetry",&v.telemetry,"Forza Data Out to 127.0.0.1 and the OutRun 2006 mod's shared memory (SimHub, dashboards)."))
         keep(&v.telemetry,before,[&]{return save("Telemetry","Enabled",v.telemetry?"true":"false");});}
    if(advanced){
        static const char* const layouts[]{"Forza Horizon (324 bytes)","Forza Motorsport 7 (311 bytes)"};
        const int before=v.layout;
        if(dbce::panel::Choice("Packet layout",layouts,2,&v.layout))
            keep(&v.layout,before,[&]{return save("Telemetry","Layout",v.layout?"FM7":"Horizon");});
    }
    dbce::panel::Text("Changes apply at the next game start.");
}
void draw_page(dbce::panel::Page page,bool advanced){
    if(!pending_failure.empty()){dbce::panel::NoteSaved(false,pending_failure.c_str());pending_failure.clear();}   // after the toolkit's own note
    switch(page){
    case dbce::panel::PAGE_CONTROLS:page_controls(advanced);break;
    case dbce::panel::PAGE_FFB:page_ffb(advanced);break;
    case dbce::panel::PAGE_CAMERAS:page_cameras(advanced);break;
    case dbce::panel::PAGE_TELEMETRY:page_telemetry(advanced);break;
    case dbce::panel::PAGE_HELP:
        dbce::panel::Status("Settings file",p.latched?"none in a test run (nothing is saved)":p.ini.c_str());
        dbce::panel::Text("Controls are bound in Wheelkit.");
        break;
    default:break;
    }
}
// Stop FFB (the header command and F8, STD-004): the wheel released for this session and [FFB] Output = none saved; the
// next-start state changes only when the save succeeded, else the FFB page says what failed.
void stop_ffb(){
    ffb_stop_output();
    if(p.latched)return;
    if(save("FFB","Output","none")){v.output_next=false;stop_error.clear();}
    else stop_error=pending_failure;
}
bool triples_on(){const auto& st=triple_screens_status();return st.mode!=TripleScreens::off&&st.aspect>0.f;}
// The frame-rate readout's display suffix (STD-023, romi 31510): what the span shows, never "triples on".
// The present thread only (the readout and its log line).
const char* display_text(){
    static char text[96];
    const auto& st=triple_screens_status();
    if(st.mode==TripleScreens::off||!(st.aspect>0.f))std::snprintf(text,sizeof text,"one 16:9 view");
    else if(st.applied==TripleProjection::angled)std::snprintf(text,sizeof text,"%s span, three projected views (experimental)",triple_screens_name(st.mode));
    else std::snprintf(text,sizeof text,"%s span, one wide view %.2f:1",triple_screens_name(st.mode),double(st.aspect));
    return text;
}
// The REC badge (Play with Recording): top right of the center 16:9 view (the middle screen of a span), above the
// game and the panel. Red while recording or failed, amber while saving or verifying, green once saved.
void draw_badge(const std::string& text,int w,int h){
    auto* list=ImGui::GetForegroundDrawList();
    const float s=dbce::panel::Scale(),pad=8.0f*s;
    const float wide=float(h)*16.0f/9.0f,view=float(w)<wide?float(w):wide,right=(float(w)+view)*0.5f;
    const bool failed=text.rfind("REC failed",0)==0,saved=text=="REC saved",recording=!failed&&!saved&&text.rfind("REC saving",0)!=0&&
        text.rfind("REC complete",0)!=0;
    const ImU32 tone=failed||recording?IM_COL32(235,64,52,255):saved?IM_COL32(80,200,120,255):IM_COL32(240,180,60,255);
    const ImVec2 size=ImGui::CalcTextSize(text.c_str());
    const float dot=recording?size.y*0.6f:0.0f;
    const ImVec2 at(right-size.x-dot-pad*4.0f,pad*3.0f);
    list->AddRectFilled(ImVec2(at.x-pad,at.y-pad*0.5f),ImVec2(at.x+dot+size.x+pad,at.y+size.y+pad*0.5f),IM_COL32(0,0,0,170),4.0f*s);
    if(recording)list->AddCircleFilled(ImVec2(at.x+dot*0.4f,at.y+size.y*0.5f),dot*0.35f,tone);
    list->AddText(ImVec2(at.x+dot,at.y),tone,text.c_str());
}
// UILetterboxing (pc_screen.hpp): black outside the centred 4:3, on the back buffer before the badge and the panel.
// ClearView takes no pipeline state, so the renderer's bindings are untouched. Without ID3D11DeviceContext1 no bars
// are drawn, and the log says so once.
void letterbox(ID3D11DeviceContext* ctx,ID3D11RenderTargetView* view,unsigned width,unsigned height){
    using namespace xbox_runtime;
    if(!platform::pc_letterbox_now(platform::g_pc_ui_letterboxing,g_ui_scaling_mode,lb.in_race.load(std::memory_order_relaxed)))return;
    const auto ui=ui_scaling_rect(g_ui_scaling_mode,g_widescreen,platform::g_pc_wide_aspect,double(width)/double(height));
    if(!ui.on||ui.aspect!=4./3.)return;   // the 2D is not in the centred 4:3 (4:3 output, or UIScalingMode 0)
    const auto bars=platform::pc_letterbox_bars(std::int32_t(width),std::int32_t(height));
    if(!bars.any())return;
    if(ctx!=lb.context_for){
        if(lb.context1){lb.context1->Release();lb.context1=nullptr;}
        lb.context_for=ctx;
        if(FAILED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(&lb.context1))))lb.context1=nullptr;
        if(!lb.context1&&!lb.missing_logged){lb.missing_logged=true;
            log_line("UILetterboxing: the D3D11 context has no ID3D11DeviceContext1 (ClearView); no bars are drawn");}
    }
    if(!lb.context1)return;
    D3D11_RECT r[2]{};UINT n=0;
    if(bars.left_end>0)r[n++]=D3D11_RECT{0,0,LONG(bars.left_end),LONG(height)};
    if(bars.right_begin<bars.width)r[n++]=D3D11_RECT{LONG(bars.right_begin),0,LONG(bars.width),LONG(height)};
    const FLOAT black[4]{0.f,0.f,0.f,1.f};
    lb.context1->ClearView(view,black,r,n);
}
void release_backend(){
    if(p.backend){ImGui_ImplDX11_Shutdown();p.backend=false;}
    p.device=nullptr;
}
// The present overlay: every presented frame. Closed: the frame-rate note and the dev-channel poll only.
void overlay(void* device,void* context,void* view,unsigned width,unsigned height){
    dbce::panel::NoteFrame();
    // Playback: the panel never opens (sana 37135): no dev channel, no F6 hook; only the PLAYBACK badge is drawn.
    if(!p.playback)dbce::panel::PollDev();
    if(!p.input&&!p.playback){
        if(const HWND hwnd=static_cast<HWND>(xbox_runtime::native_window().hwnd)){
            p.input=dbce::panel::AttachInput(hwnd);
            log_line(p.input?"F6 input attached to the game window":"F6 input hook FAILED; the dev channel still opens it");
        }
    }
    // UILetterboxing's bars first, every frame, so the badge and the panel are drawn over them.
    if(context&&view&&width&&height)letterbox(static_cast<ID3D11DeviceContext*>(context),static_cast<ID3D11RenderTargetView*>(view),width,height);
    // Closed, a recording session still draws its REC badge (shim_input_recording_badge); nothing else is drawn.
    const bool open=!p.playback&&dbce::panel::IsOpen();
    const auto badge=shim_input_recording_badge();
    if((!open&&badge.empty())||!device||!context||!view||!width||!height)return;
    auto* dev=static_cast<ID3D11Device*>(device);auto* ctx=static_cast<ID3D11DeviceContext*>(context);
    if(dev!=p.device){   // the first open frame (the device lives for the process)
        release_backend();
        if(!ImGui::GetCurrentContext()){
            ImGui::CreateContext();
            ImGuiIO& io=ImGui::GetIO();
            io.IniFilename=nullptr;io.LogFilename=nullptr;   // ImGui writes no files; the panel keeps [Panel] itself
            io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NoMouseCursorChange;
            io.MouseDrawCursor=true;   // the game hides the system cursor; the panel draws its own while open
        }
        if(!p.freq.QuadPart)QueryPerformanceFrequency(&p.freq);   // also when a context already existed (romi 30736)
        p.backend=ImGui_ImplDX11_Init(dev,ctx);
        if(!p.backend){log_line("ImGui's DX11 backend could not start; the panel is not drawn");return;}
        p.device=dev;
    }
    const int w=int(width),h=int(height);
    if(dbce::panel::Prepare(w,h)){dbce::panel::BuildFonts();ImGui_ImplDX11_InvalidateDeviceObjects();}
    ImGui_ImplDX11_NewFrame();
    ImGuiIO& io=ImGui::GetIO();
    io.DisplaySize=ImVec2(float(w),float(h));
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    io.DeltaTime=p.last.QuadPart?float(double(now.QuadPart-p.last.QuadPart)/double(p.freq.QuadPart)):1.0f/60.0f;
    if(io.DeltaTime<=0.0f||io.DeltaTime>1.0f)io.DeltaTime=1.0f/60.0f;
    p.last=now;
    io.MouseDrawCursor=open;   // the panel's own cursor only while it is open (a badge frame draws none)
    dbce::panel::DrainInput(w,h);   // closed: nothing queued; it keeps the panel's open / close transitions
    ImGui::NewFrame();
    if(open)dbce::panel::Draw(w,h);
    if(!badge.empty())draw_badge(badge,w,h);
    ImGui::Render();
    // On the back buffer, then the renderer's targets back (the DX11 backend restores the rest of its state itself).
    ID3D11RenderTargetView* saved[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};ID3D11DepthStencilView* depth=nullptr;
    ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,saved,&depth);
    auto* target=static_cast<ID3D11RenderTargetView*>(view);
    ctx->OMSetRenderTargets(1,&target,nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,saved,depth);
    for(auto* v:saved)if(v)v->Release();
    if(depth)depth->Release();
}
}

void desktop_panel_configure(const Ini& settings,bool latched,const std::string& run_folder,bool playback){
    p=Panel{};
    p.latched=latched;p.playback=playback;
    {const auto selected=ffb::player::select(settings);p.player_model=selected.model;
     p.applied_strengths=selected.strengths;p.model_refusal=selected.refusal;}
    p.ini=settings.path();
    const auto slash=p.ini.find_last_of("\\/");
    const std::string folder=latched?run_folder:(slash==std::string::npos?std::string("."):p.ini.substr(0,slash));
    if(!folder.empty()&&!playback)p.dev=folder+"\\OutRunDesktop.panel-dev.txt";   // none for a latched run without its folder, nor in playback
    p.name="OutRun 2006 desktop (decomp)";
    // The pages' values as the settings file holds them now (the same readers the game uses at start).
    v=Values{};
    v.selected_steering=p.applied_strengths.steering_percent;
    v.selected_shift=p.applied_strengths.shift_percent;
    v.triple=int(parse_triple_screens(settings.text("Triple","Screens")).value_or(TripleScreens::off));
    v.projection=v.projection_start=int(parse_triple_projection(settings.text("Triple","Views")).value_or(TripleProjection::planar));
    {const auto t=settings.text("Window","FrameLatency");if(t.size()==1&&t[0]>='0'&&t[0]<='3')v.latency=t[0]-'0';}
    {std::string note;v.ui_scaling=int(xbox_runtime::parse_ui_scaling_mode(settings.text("Graphics","UIScalingMode"),note));}
    {const int b=settings.integer("Graphics","UILetterboxing",1);v.letterbox=v.letterbox_start=b<0?0:b>2?2:b;}   // as window_desktop.cpp
    v.screen_edge_cull=v.screen_edge_cull_start=settings.flag("Graphics","ScreenEdgeCullFix",true);   // as window_desktop.cpp
    // Match the startup consumers' accepted ranges and defaults without changing the active renderer.
    {const auto t=settings.text("Graphics","AnisotropicFiltering");
     if(!t.empty()&&t.size()<=2u&&std::all_of(t.begin(),t.end(),[](char c){return c>='0'&&c<='9';})){
         const auto level=unsigned(std::stoul(t));if(level<=16u)v.anisotropic=int(level);}}
    {const auto t=settings.text("Graphics","ReflectionResolution");
     if(!t.empty()&&t.size()<=4u&&std::all_of(t.begin(),t.end(),[](char c){return c>='0'&&c<='9';})){
         const auto edge=unsigned(std::stoul(t));
         if(edge>=64u&&edge<=4096u&&(edge&(edge-1u))==0u){
             v.reflection=0;for(auto n=edge;n>64u;n>>=1u)++v.reflection;}}}
    {auto sections=[&](const char* key,int max){
         const auto t=settings.text("Graphics",key);if(t.empty())return 0;
         char* end=nullptr;const long n=std::strtol(t.c_str(),&end,10);
         return end==t.c_str()||*end||n<0||n>max?0:int(n);};
     v.draw_ahead=sections("DrawDistanceIncrease",1024);v.draw_behind=sections("DrawDistanceBehind",256);}
    v.transparency=settings.flag("Graphics","TransparencySupersampling",false);
    v.full_vehicle_detail=settings.flag("Graphics","DisableVehicleLODs",false);
    v.no_stage_cull=settings.flag("Graphics","DisableStageCulling",false);
    v.depth_precision=settings.flag("Graphics","FixZBufferPrecision",false);
    v.lens_flare_fix=settings.flag("Bugfixes","FixLensFlarePath",true);
    v.shading_fix=settings.flag("Bugfixes","FixIncorrectShading",true);
    v.particle_fix=settings.flag("Bugfixes","FixParticleRendering",true);
    v.bunki_fix=settings.flag("Bugfixes","FixRightSideBunkiAnimations",true);
    v.shadow_opacity=settings.text("Graphics","CarBaseShadowOpacity","1");
    v.manual_default=v.manual_default_start=settings.flag("Misc","DefaultManualTransmission",false);   // as window_desktop.cpp
    v.allow_horn=v.allow_horn_start=settings.flag("Audio","AllowHorn",true);   // as window_desktop.cpp
    v.hud_key=v.hud_key_start=settings.text("Misc","HudToggleKey");   // no reconfiguration of the running toggle
    v.allow_wav=v.allow_wav_start=settings.flag("Audio","AllowWAV",false);   // same startup-only policy as window_desktop.cpp
    v.allow_flac=v.allow_flac_start=settings.flag("Audio","AllowFLAC",false);
    v.license_character=settings.flag("Misc","AllowCharacterSelection",false);
    v.menu_miles=settings.flag("Misc","ShowOutRunMilesOnMenu",true);
    v.hide_signin=settings.flag("Bugfixes","HideOnlineSigninText",false);
    v.hidef_characters=settings.flag("Graphics","UseHiDefCharacters",true);
    v.jp_clarissa=settings.flag("Misc","RestoreJPClarissa",false);
    v.full_pedal_fix=settings.flag("Bugfixes","FixFullPedalChecks",true);
    v.pegasus_sound_fix=settings.flag("Bugfixes","FixPegasusClopping",true);
    v.highway_sets=settings.flag("Misc","RandomHighwayAnimSets",false);   // identical to the startup consumer
    v.holly_course2=settings.integer("Misc","EnableHollyCourse2",3);
    if(v.holly_course2<0||v.holly_course2>3)v.holly_course2=3;   // identical to the startup consumer
    p.wav_decoder=platform::music_replacement_decoder_available(platform::MusicFileFormat::Wav);
    p.flac_decoder=platform::music_replacement_decoder_available(platform::MusicFileFormat::Flac);
    v.auto_resolution=v.auto_resolution_start=settings.flag("Window","AutoDetectResolution",true);    // as window_desktop.cpp
    {std::string notes;const auto t=telemetry_settings(settings,nullptr,nullptr);v.telemetry=t.enabled;v.layout=t.layout==dbce::forza::FORZA_FM7_DASH_311?1:0;}
    if(p.player_model==ffb::player::Model::Legacy){
        std::string notes,source;const auto s=ffb_settings(settings,notes,source);
        v.global=pct(s.global_strength);v.spring=pct(s.spring);v.damper=pct(s.damper);v.weight=pct(s.steering_weight);v.grip=pct(s.grip_loss);
        v.transfer=pct(s.weight_transfer);v.wall=pct(s.wall_impact);v.gear=pct(s.gear_shift);v.road=pct(s.road_texture);v.slip=pct(s.tire_slip);
        v.idle=pct(s.engine_idle);v.invert=s.invert;
    }
    {const auto w=wheel_steering(settings);v.sensitivity=pct(w.sensitivity);v.dead_zone=pct(w.dead_zone);}
    {std::string notes;const auto o=ffb_output_config(settings,false,std::getenv("DBCE_FFB_MUTE"),notes);
     v.output_next=o.output;p.force_muted=o.muted;}
    p.force_library=ffb_library_present();   // the force add-on beside the game: a file check, never a load
    dbce::panel::Config c;
    c.modName=p.name.c_str();
    c.modRepositoryUrl="https://github.com/d-b-c-e/outrun2-decomp";
    c.wheelkitRepositoryUrl="https://github.com/d-b-c-e/dbce-wheelkit";
    c.iniPath=latched?nullptr:p.ini.c_str();   // a test process saves nothing
    for(const auto page:{dbce::panel::PAGE_CONTROLS,dbce::panel::PAGE_FFB,dbce::panel::PAGE_CAMERAS,dbce::panel::PAGE_TELEMETRY})c.pages[page]=true;
    c.drawPage=draw_page;
    c.ffbOn=ffb_output_running;c.stopFfb=stop_ffb;   // Stop FFB / F8 (STD-004)
    c.log=log_line;
    c.devFile=p.dev.empty()?nullptr:p.dev.c_str();
    c.triplesOn=triples_on;
    c.displayText=display_text;   // replaces the triples on/off suffix
    dbce::panel::Configure(c);
    xbox_runtime::present_overlay()=overlay;
    p.configured=true;
    if(playback)std::printf("panel: F6 is off in a playback process (the PLAYBACK badge only)\n");
    else std::printf("panel: F6 settings panel ready (Controls, FFB, Cameras, Telemetry, Help; %s; dev channel %s)\n",
        latched?"a test run: nothing saved":"saves to the settings file",p.dev.empty()?"off":p.dev.c_str());
    std::fflush(stdout);
}

bool desktop_panel_open(){return p.configured&&!p.playback&&dbce::panel::IsOpen();}

void desktop_panel_neutral_input(platform::NativeInputState& out){
    const bool connected=out.connected,layer=out.pc_input_layer;
    out=platform::NativeInputState{};
    out.connected=connected;out.pc_input_layer=layer;
}

namespace {
void input_filter(platform::NativeInputState& out,const platform::NativeRuntimeContext* rt){
    if(p.inner)p.inner(out,rt);
    if(!p.latched&&!p.playback)route_trace.observe(rt);
    // UILetterboxing: the race state for the present overlay (78026C, and START's stage 8367A8).
    if(rt)lb.in_race.store(platform::pc_letterbox_in_race(rt->mode_state.current,rt->start_mode.stage),std::memory_order_relaxed);
    const bool open=desktop_panel_open();
    if(open!=p.was_open){desktop_keys_clear();p.was_open=open;}
    if(open)desktop_panel_neutral_input(out);
}
}

void desktop_panel_wrap_input_filter(){
    if(!p.configured)return;
    auto& filt=xbox_runtime::input_filter();
    if(filt==input_filter)return;
    p.inner=filt;filt=input_filter;
}

void desktop_panel_shutdown(){
    if(!p.configured)return;
    xbox_runtime::present_overlay()=nullptr;
    release_backend();
    if(lb.context1){lb.context1->Release();lb.context1=nullptr;}
    lb.context_for=nullptr;
    if(ImGui::GetCurrentContext())ImGui::DestroyContext();
    p.configured=false;
}
}
