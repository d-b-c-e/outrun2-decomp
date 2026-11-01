#include "ffb_player_model.hpp"
#include "desktop_settings.hpp"
#include <utility>

namespace outrun::desktop::ffb::player {
namespace {
bool percent(const Ini& ini,const char* key,int& value){
    if(!ini.has_value("FFBProducer2",key))return true;
    const auto text=ini.text("FFBProducer2",key);
    if(text.empty()||text.size()>3u)return false;
    int parsed=0;
    for(const char ch:text){
        if(ch<'0'||ch>'9')return false;
        parsed=parsed*10+(ch-'0');
    }
    if(parsed>100)return false;
    value=parsed;return true;
}
}
Selection select(const Ini& settings){
    Selection s;
    // FFB ships on (owner, 2026-10-31): an absent Model is the wheel model, producer@2.
    const auto id=settings.has_value("FFB","Model")?settings.text("FFB","Model"):std::string(producer2::ModelId);
    if(id==LegacyId)return s;
    if(id!=producer2::ModelId){
        s.model=Model::Refused;
        s.refusal="[FFB] Model must be outrun-legacy@1 or outrun-producer@2";
        return s;
    }
    s.model=Model::Producer2;
    if(!percent(settings,"SteeringStrengthPercent",s.strengths.steering_percent))
        s.refusal="[FFBProducer2] SteeringStrengthPercent must be an integer from 0 to 100";
    else if(!percent(settings,"ShiftStrengthPercent",s.strengths.shift_percent))
        s.refusal="[FFBProducer2] ShiftStrengthPercent must be an integer from 0 to 100";
    if(!s.refusal.empty())s.model=Model::Refused;
    return s;
}
SelectedRequests::SelectedRequests(const Selection& selection,Sink sink){
    if(selection.model!=Model::Producer2||!sink||selection.strengths.steering_percent<0||
       selection.strengths.steering_percent>100||selection.strengths.shift_percent<0||
       selection.strengths.shift_percent>100)return;
    core_=std::make_unique<shadow::Core>([](const std::string&){return true;},selection.strengths,std::move(sink));
}
void SelectedRequests::before_frame(const shadow::Gate& gate) noexcept {if(core_)core_->before_frame(gate);}
void SelectedRequests::observe(const shadow::Sample& sample) noexcept {if(core_)core_->observe(sample);}
bool SelectedRequests::active()const{return core_&&core_->active();}
}
