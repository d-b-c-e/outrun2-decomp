#include "camera_adjustment.hpp"
#include "hud_toggle.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace outrun::desktop::camera {
namespace {
constexpr const char* Actions[]{"Forward","Back","Up","Down","Left","Right","Look down","Look up","Widen FOV","Narrow FOV","Reset active view"};
constexpr const char* Fields[]{"Right","Up","Forward","Tilt","Fov"};
std::array<float,5> parts(const driving::MountedCameraPose& p){return {p.right,p.up,p.forward,p.tilt,p.fov};}
driving::MountedCameraPose pose(const std::array<float,5>& p){return {p[0],p[1],p[2],p[3],p[4]};}
bool range(float x,float lo,float hi){return std::isfinite(x)&&x>=lo&&x<=hi;}
}
const std::vector<Key>& keys(){
    // These positions are outside the native keyboard configuration's scan_to_vk set.
    static const std::vector<Key> list{{0x48,"Numpad 8"},{0x50,"Numpad 2"},{0x49,"Numpad 9"},{0x51,"Numpad 3"},
        {0x4b,"Numpad 4"},{0x4d,"Numpad 6"},{0x47,"Numpad 7"},{0x4f,"Numpad 1"},{0x4e,"Numpad +"},{0x4a,"Numpad -"},{0x4c,"Numpad 5"},
        {0x3b,"F1"},{0x3c,"F2"},{0x3d,"F3"},{0x3e,"F4"},{0x3f,"F5"},{0x41,"F7"},{0x43,"F9"},{0x44,"F10"},{0x58,"F12"},
        {0x0c,"-"},{0x0d,"="},{0x1a,"["},{0x1b,"]"},{0x27,";"},{0x28,"'"},{0x29,"`"},{0x2b,"Backslash"},{0x33,","},{0x34,"."},{0x35,"/"}};
    return list;
}
const char* key_name(unsigned scan){if(!scan)return "Unbound";for(const auto& k:keys())if(k.scan==scan)return k.name;return "Unsupported";}
const char* action_name(unsigned action){return action<Count?Actions[action]:"Unknown";}
Model& model(){static Model m;return m;}
std::recursive_mutex& mutex(){static std::recursive_mutex m;return m;}
bool Model::valid(const Config& c,std::string& why)const{
    for(const auto& p:c.views){const auto a=parts(p);for(unsigned i=0;i<5;++i)
        if(!range(a[i],i<3?-2.f:i==3?-30.f:-60.f,i<3?2.f:i==3?30.f:60.f)){why="Pose is outside finite camera bounds";return false;}}
    if(!range(c.move_step,0.001f,0.2f)||!range(c.tilt_step,0.1f,10.f)||!range(c.fov_step,0.1f,10.f)){
        why="Steps must be finite: move 0.001..0.2 m, tilt/FOV 0.1..10 degrees";return false;}
    std::vector<unsigned> seen;
    for(auto scan:c.bindings){if(!scan)continue;
        if(std::string(key_name(scan))=="Unsupported"||std::find(reserved.begin(),reserved.end(),scan)!=reserved.end()){
            why="Reserved, game/HUD assignment or unsupported key; choose an offered key";return false;}
        if(std::find(seen.begin(),seen.end(),scan)!=seen.end()){why="Conflict with another camera adjustment assignment";return false;}
        seen.push_back(scan);
    }
    return true;
}
void Model::load(const Ini& ini,bool allowed){
    *this=Model{};enabled=allowed;path=ini.path();
    // HUD keys share this keyboard; reserve every scan represented by the saved name.
    const auto hud=parse_hud_toggle_key(ini.text("Misc","HudToggleKey"),nullptr);
    reserved=hud.keys;
    Config candidate;
    for(unsigned v=0;v<2;++v){
        auto a=parts(candidate.views[v]);
        for(unsigned f=0;f<5;++f)a[f]=ini.number("CameraAdjustment",std::string(v?"InCar":"Bumper")+Fields[f],a[f]);
        candidate.views[v]=pose(a);
    }
    candidate.move_step=ini.number("CameraAdjustment","MoveStep",candidate.move_step);
    candidate.tilt_step=ini.number("CameraAdjustment","TiltStep",candidate.tilt_step);
    candidate.fov_step=ini.number("CameraAdjustment","FovStep",candidate.fov_step);
    for(unsigned a=0;a<Count;++a)candidate.bindings[a]=unsigned(ini.integer("CameraAdjustment","Key"+std::to_string(a),int(candidate.bindings[a])));
    // Recover only invalid saved values; one corrupt pose must not discard custom bindings or the other view.
    bool recovered=false;
    for(auto& p:candidate.views){auto a=parts(p);for(unsigned i=0;i<5;++i)
        if(!range(a[i],i<3?-2.f:i==3?-30.f:-60.f,i<3?2.f:i==3?30.f:60.f)){a[i]=0;recovered=true;}
        p=pose(a);
    }
    if(!range(candidate.move_step,0.001f,0.2f)){candidate.move_step=.02f;recovered=true;}
    if(!range(candidate.tilt_step,0.1f,10.f)){candidate.tilt_step=1.f;recovered=true;}
    if(!range(candidate.fov_step,0.1f,10.f)){candidate.fov_step=2.f;recovered=true;}
    std::vector<unsigned> seen;
    for(auto& scan:candidate.bindings){if(!scan)continue;
        if(std::string(key_name(scan))=="Unsupported"||std::find(reserved.begin(),reserved.end(),scan)!=reserved.end()||
           std::find(seen.begin(),seen.end(),scan)!=seen.end()){scan=0;recovered=true;}
        else seen.push_back(scan);
    }
    effective=candidate;
    if(recovered)message="Invalid saved camera values defaulted; conflicting keys unbound. Other settings retained.";
    saved_=effective;
}
bool Model::propose(const Config& c){
    if(!enabled){message="Camera adjustments are disabled in tests, recording and playback";return false;}
    if(proposal){message="Retry or Cancel the pending save first";return false;}
    if(!valid(c,message))return false;
    proposal=c;return retry();
}
bool Model::retry(){
    if(!proposal)return true;
    if(!valid(*proposal,message))return false;
    SettingsEdits edits;
    for(unsigned v=0;v<2;++v){const auto a=parts(proposal->views[v]);for(unsigned f=0;f<5;++f)
        edits.push_back({std::string(v?"InCar":"Bumper")+Fields[f],std::to_string(a[f])});}
    edits.push_back({"MoveStep",std::to_string(proposal->move_step)});
    edits.push_back({"TiltStep",std::to_string(proposal->tilt_step)});
    edits.push_back({"FovStep",std::to_string(proposal->fov_step)});
    for(unsigned a=0;a<Count;++a)edits.push_back({"Key"+std::to_string(a),std::to_string(proposal->bindings[a])});
    if(!write_keys(path,"CameraAdjustment",edits,message)){message="Save failed: "+message+". Retry or Cancel; bindings retained";return false;}
    effective=saved_=*proposal;proposal.reset();dirty_=false;wait_=true;message="Camera settings saved";return true;
}
void Model::cancel_proposal(){proposal.reset();effective=saved_;dirty_=false;wait_=true;message="Save cancelled; saved camera values retained";}
void Model::edit_pose(unsigned view,const driving::MountedCameraPose& p,double now){
    if(view>=2||proposal||!enabled)return;
    auto c=effective;c.views[view]=p;
    if(!valid(c,message))return;
    effective=c;dirty_=true;last_edit_=now;wait_=true;
}
void Model::flush(double now,bool force){
    if(enabled&&dirty_&&!proposal&&(force||now-last_edit_>=0.75)){proposal=effective;retry();}
}
void Model::begin_capture(unsigned a,double now){if(a>=Count||proposal||!enabled)return;capture_=int(a);capture_until_=now+10;capture_wait_=true;capture_key_=0;wait_=true;message="Release keys, then press one key (10 second timeout); Cancel retains the assignment";}
void Model::cancel_capture(const char* why){if(capture_<0)return;capture_=-1;wait_=true;message=why;}
void Model::capture(const std::vector<unsigned>& pressed,bool any_down,bool chord,double now){
    if(capture_<0)return;
    if(now>=capture_until_){cancel_capture("Binding timed out; previous assignment retained");return;}
    if(capture_wait_){if(!any_down)capture_wait_=false;return;}
    if(chord||pressed.size()>1||(!pressed.empty()&&capture_key_)){
        cancel_capture("Chords are unsupported; previous assignment retained");return;
    }
    if(!pressed.empty()){
        auto c=effective;c.bindings[unsigned(capture_)]=pressed[0];
        if(!valid(c,message)){capture_=-1;wait_=true;return;}
        capture_key_=pressed[0];message="Release the key to save; Cancel retains the previous assignment";return;
    }
    if(capture_key_&&!any_down){
        auto c=effective;c.bindings[unsigned(capture_)]=capture_key_;capture_=-1;wait_=true;propose(c);
    }
}
driving::MountedCameraPose Model::tick(int view,bool eligible,const KeyStates& down,bool physical_down,double now,std::uint32_t clears,bool blocked){
    if(!std::isfinite(now))return {};
    const bool gap=last_tick_<0||now-last_tick_>0.15||now<last_tick_;last_tick_=now;
    const bool changed=active_view!=view;active_view=eligible&&view>=0&&view<2?view:-1;
    if(blocked||!enabled||!eligible||view<0||view>=2||capturing()||proposal||clears!=clears_||gap||changed){wait_=true;previous_.fill(false);next_.fill(0);}
    clears_=clears;
    if(!enabled||!eligible||view<0||view>=2)return {};
    if(wait_){if(!physical_down)wait_=false;return effective.views[unsigned(view)];}
    auto& p=effective.views[unsigned(view)];
    for(unsigned a=0;a<Count;++a){const bool held=effective.bindings[a]&&down[effective.bindings[a]];
        const bool fire=held&&(!previous_[a]||(a!=10&&now>=next_[a]));
        if(fire){
            next_[a]=now+(previous_[a]?0.08:0.3); // one repeat per tick; no catch-up after a stall
            switch(a){case 0:p.forward+=effective.move_step;break;case 1:p.forward-=effective.move_step;break;
            case 2:p.up+=effective.move_step;break;case 3:p.up-=effective.move_step;break;
            case 4:p.right-=effective.move_step;break;case 5:p.right+=effective.move_step;break;
            case 6:p.tilt-=effective.tilt_step;break;case 7:p.tilt+=effective.tilt_step;break;
            case 8:p.fov+=effective.fov_step;break;case 9:p.fov-=effective.fov_step;break;case 10:p={};break;}
            p.right=std::clamp(p.right,-2.f,2.f);p.up=std::clamp(p.up,-2.f,2.f);p.forward=std::clamp(p.forward,-2.f,2.f);
            p.tilt=std::clamp(p.tilt,-30.f,30.f);p.fov=std::clamp(p.fov,-60.f,60.f);
            dirty_=true;last_edit_=now;feedback_until=now+2;
        }
        previous_[a]=held;
    }
    return p;
}
std::string values(int view,const driving::MountedCameraPose& p){
    char text[220];std::snprintf(text,sizeof text,"%s: right %+.2f m, up %+.2f m, forward %+.2f m, tilt %+.1f deg, FOV %+.1f deg",
        view==0?"Bumper":"In-car",double(p.right),double(p.up),double(p.forward),double(p.tilt),double(p.fov));return text;
}
}
