#include "wheel_button_prompts.hpp"
#include <algorithm>

namespace outrun::desktop::prompts {
namespace ctl=dbce::controls;
namespace sh=dbce::shim;
std::string label(const ctl::Binding& b){
    if(b.kind==ctl::Kind::Button&&b.index>=0&&b.index<128)return "Button "+std::to_string(b.index+1);
    if(b.kind!=ctl::Kind::Hat||b.index<0||b.index>=4||b.angle<0||b.angle>=36000)return {};
    static constexpr const char* directions[]{"Up","Up-right","Right","Down-right","Down","Down-left","Left","Up-left"};
    // Exact contract angles matter: do not advertise a nearby direction for a nonstandard binding.
    return "Hat "+std::to_string(b.index+1)+" "+(b.angle%4500==0?directions[b.angle/4500]:std::to_string(b.angle/100.0)+" deg");
}
std::vector<std::string> contextual(const ctl::Section& active,const sh::Frame& frame,
    const std::vector<sh::DeviceReport>& reports,std::uint32_t mode,bool car){
    std::vector<std::string> out;
    if(!active.error.empty()||!frame.seq||frame.latched||frame.suppress_native||
       (frame.run!=sh::Run::None&&frame.run!=sh::Run::Running))return out;
    const bool race=mode==16u;
    if(race&&!car)return out;
    // Pause is a menu; no camera/shift advice while the car cannot respond.
    static constexpr const char* menu[]{"start","confirm","back"};
    static constexpr const char* drive[]{"camera","shiftUp","shiftDown"};
    static constexpr const char* menu_text[]{"Start","Confirm","Back"};
    static constexpr const char* drive_text[]{"Camera","Shift up","Shift down"};
    std::vector<std::string> devices;
    for(const auto& entry:active.bound)if(std::find(devices.begin(),devices.end(),entry.binding.dev)==devices.end())devices.push_back(entry.binding.dev);
    for(int i=0;i<3;++i){
        const auto* action=race?drive[i]:menu[i];
        const auto* b=ctl::find(active,action);
        if(!b)continue;
        auto text=label(*b);if(text.empty())continue;
        const auto& a=frame.action[std::size_t(sh::action_index(action))];
        if(a.status!=sh::Status::Live||a.injected||a.sample<0||std::size_t(a.sample)>=frame.samples.size())continue;
        const auto& s=frame.samples[std::size_t(a.sample)];
        if(s.device>=reports.size()||!s.seq||!s.epoch||!s.valid||s.stale||s.injected.any()||
           s.poll!=sh::Call::Ok||s.state!=sh::Call::Ok||s.read_end_us<=0||s.read_end_us>frame.boundary_us||
           frame.boundary_us-s.read_end_us>100'000)continue;
        const auto& d=reports[s.device];
        if(!d.opened||d.epoch!=s.epoch||d.identity.dev!=b->dev||d.identity.prod!=b->prod||
           (b->kind==ctl::Kind::Button?!d.caps.button[std::size_t(b->index)]:!d.caps.pov[std::size_t(b->index)]))continue;
        if(devices.size()>1){
            auto name=d.identity.name.empty()?b->name:d.identity.name;
            if(name.size()>24){std::size_t n=24;while(n&&(static_cast<unsigned char>(name[n])&0xc0)==0x80)--n;name=name.substr(0,n)+"...";}
            const auto number=std::find(devices.begin(),devices.end(),b->dev)-devices.begin()+1;
            text=(name.empty()?"Device":name)+" ["+std::to_string(number)+"] "+text;
        }
        out.push_back(std::string(race?drive_text[i]:menu_text[i])+": "+text);
    }
    return out;
}
}
