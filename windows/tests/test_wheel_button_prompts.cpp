// Game-free fixtures: startup profile plus accepted backend reports, no device access.
#include "wheel_button_prompts.hpp"
#include <iostream>
#include <stdexcept>

namespace p=outrun::desktop::prompts;
namespace ctl=dbce::controls;
namespace sh=dbce::shim;
namespace {
int checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
const std::string Dev="{d71b8350-61b7-11f1-8001-444553540000}";
const std::string Other="{d71b8351-61b7-11f1-8001-444553540000}";
const std::string Prod="{0006346e-0000-0000-0000-504944564944}";
ctl::Binding button(int index){
    ctl::Binding b;b.kind=ctl::Kind::Button;b.index=index;b.dev=Dev;b.prod=Prod;b.name="Wheel device";return b;
}
struct Fixture {
    ctl::Section active;
    sh::Frame frame;
    std::vector<sh::DeviceReport> reports;
    Fixture(){
        active=ctl::parseSection({"Schema=2","Revision=startup","start="+ctl::format(button(0)),
            "confirm="+ctl::format(button(127)),"back="+ctl::format(button(2)),
            "camera="+ctl::format(button(3)),"shiftUp="+ctl::format(button(4)),"shiftDown="+ctl::format(button(5))},2);
        frame.seq=1;frame.boundary_us=200000;
        sh::DeviceReport d;d.identity={Dev,Prod,"Wheel device",""};d.opened=true;d.epoch=1;
        d.caps.button.set();d.caps.pov.fill(true);reports.push_back(d);
        sh::DeviceSample s;s.epoch=1;s.seq=1;s.valid=true;s.poll=s.state=sh::Call::Ok;s.read_end_us=199000;
        frame.samples.push_back(s);
        for(auto& a:frame.action){a.status=sh::Status::Live;a.sample=0;}
    }
    std::vector<std::string> lines(unsigned mode=3,bool car=false){return p::contextual(active,frame,reports,mode,car);}
};
void labels(){
    check(p::label(button(0))=="Button 1","first button is one based");
    check(p::label(button(127))=="Button 128","last button is one based");
    for(int n:{-1,128})check(p::label(button(n)).empty(),"out of range button hidden");
    auto b=button(0);b.name="Not a button label";
    check(p::label(b)=="Button 1","name is device metadata");
    b.kind=ctl::Kind::Axis;check(p::label(b).empty(),"axis has no button prompt");
    b.kind=ctl::Kind::Hat;b.index=3;
    const char* names[]{"Up","Up-right","Right","Down-right","Down","Down-left","Left","Up-left"};
    for(int i=0;i<8;++i){b.angle=i*4500;check(p::label(b)==std::string("Hat 4 ")+names[i],"hat direction");}
    b.angle=1234;check(p::label(b).find("12.34")!=std::string::npos,"nonstandard angle is exact");
    for(int angle:{-1,36000}){b.angle=angle;check(p::label(b).empty(),"invalid hat angle hidden");}
    b.angle=0;for(int index:{-1,4}){b.index=index;check(p::label(b).empty(),"invalid hat index hidden");}
}
void context(){
    Fixture f;
    check(f.lines()==std::vector<std::string>{"Start: Button 1","Confirm: Button 128","Back: Button 3"},"frontend actions only");
    check(f.lines(16,true)==std::vector<std::string>{"Camera: Button 4","Shift up: Button 5","Shift down: Button 6"},"race actions only");
    check(f.lines(16,false).empty(),"no gameplay advice without car");
    check(f.lines(18,true)==f.lines(),"pause has menu advice");
    auto hat=button(0);hat.kind=ctl::Kind::Hat;hat.angle=9000;
    f.active.bound[0].binding=hat;check(f.lines()[0]=="Start: Hat 1 Right","bound hat label");
    f.reports[0].caps.pov[0]=false;check(f.lines().size()==2,"missing hat object hidden");
}
void availability(){
    for(auto status:{sh::Status::Unbound,sh::Status::Invalid,sh::Status::Unsupported,sh::Status::DeviceMissing,
        sh::Status::ProductMismatch,sh::Status::ObjectMissing,sh::Status::Opening,sh::Status::ReadFailed,
        sh::Status::Stale,sh::Status::Rearming,sh::Status::TestNeutral}){
        Fixture f;f.frame.action[std::size_t(sh::action_index("start"))].status=status;
        check(f.lines().size()==2,"unavailable action hidden");
    }
    for(int kind=0;kind<17;++kind){
        Fixture f;auto& s=f.frame.samples[0];auto& d=f.reports[0];
        switch(kind){
        case 0:d.opened=false;break;case 1:d.epoch=2;break;case 2:d.identity.dev=Other;break;
        case 3:d.identity.prod=Other;break;case 4:s.valid=false;break;case 5:s.stale=true;break;
        case 6:s.read_end_us=99999;break;case 7:s.read_end_us=200001;break;case 8:s.seq=0;break;
        case 9:s.epoch=0;break;case 10:s.poll=sh::Call::Gone;break;case 11:s.state=sh::Call::Pending;break;
        case 12:s.device=1;break;case 13:s.injected.buttons.set(0);break;
        case 14:f.frame.latched=true;break;case 15:f.frame.suppress_native=true;break;case 16:f.frame.seq=0;break;
        }
        check(f.lines().empty(),"unaccepted envelope hidden");
    }
    Fixture f;f.frame.samples[0].read_end_us=100000;check(f.lines().size()==3,"freshness boundary accepted");
    f.reports[0].caps.button.reset(0);check(f.lines().size()==2,"missing button hidden");
    f=Fixture{};f.frame.action[std::size_t(sh::action_index("start"))].sample=99;
    check(f.lines().size()==2,"invalid sample reference hidden");
    f=Fixture{};f.frame.action[std::size_t(sh::action_index("start"))].injected=true;
    check(f.lines().size()==2,"injected action hidden");
    for(auto run:{sh::Run::Latched,sh::Run::Waiting,sh::Run::Completed,sh::Run::Expired,sh::Run::Failed,sh::Run::Inert}){
        f=Fixture{};f.frame.run=run;check(f.lines().empty(),"test and stopped runs hidden");
    }
    f=Fixture{};f.active.error="invalid schema";check(f.lines().empty(),"invalid profile hidden");
    f=Fixture{};f.active.bound.erase(f.active.bound.begin());check(f.lines().size()==2,"unbound omitted");
    f=Fixture{};f.active.bound[0].binding.index=128;check(f.lines().size()==2,"invalid binding omitted");
}
void identity(){
    Fixture f;const auto before=f.lines();
    auto draft=f.active;draft.revision="saved-next-start";draft.bound[0].binding=button(9);
    check(f.lines()==before,"editing and saving another profile cannot change active snapshot");
    check(p::contextual(draft,f.frame,f.reports,3,false)[0]=="Start: Button 10","next startup consumes saved identity");
    f.active.bound[1].binding.dev=Other;
    auto d=f.reports[0];d.identity.dev=Other;f.reports.push_back(d);
    auto s=f.frame.samples[0];s.device=1;f.frame.samples.push_back(s);
    f.frame.action[std::size_t(sh::action_index("confirm"))].sample=1;
    const auto lines=f.lines();
    check(lines[0]=="Start: Wheel device [1] Button 1","first device identified");
    check(lines[1]=="Confirm: Wheel device [2] Button 128","same-name second device distinguished");
}
}
int main(){try{labels();context();availability();identity();std::cout<<checks<<" wheel prompt checks passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
