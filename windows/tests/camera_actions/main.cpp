// Real canonical Core/codecs and production ABI2 writer; CPU/fake provider only.
#include "frame_channels.hpp"
#include "observer_domain.hpp"
#include "legacy_frame.hpp"
#include "shim_fake_provider.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>
#include <stdexcept>
namespace sh=dbce::shim;namespace rec=sh::recorder;namespace ob=sh::observe;
unsigned checks;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F> void refuses(F f){bool refused=false;try{f();}catch(const std::exception&){refused=true;}check(refused,"malformed extension refused");}
std::vector<char> bytes(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);check(bool(in),"fixture opened");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
rec::Channels channels(const std::filesystem::path& path){std::ifstream in(path);check(bool(in),"recording opened");rec::Channels c;std::string key;double value;while(in>>key>>value)check(c.emplace(key,value).second,"unique channel");check(in.eof(),"recording parsed fully");return c;}
int main(int argc,char** argv){try{
    check(argc==2,"fixture directory required");const std::filesystem::path dir=argv[1];
    const char* legacy[]{"steer","throttle","brake","clutch","handbrake","shiftUp","shiftDown","neutral","reverse","confirm","back","start","select","navUp","navDown","navLeft","navRight","camera","lookBack","reset","horn","modMenu","muteFfb","gear1","gear2","gear3","gear4","gear5","gear6","gear7","gear8"};
    const char* camera[]{"cameraForward","cameraBack","cameraUp","cameraDown","cameraLeft","cameraRight","cameraTiltDown","cameraTiltUp","cameraZoomIn","cameraZoomOut","cameraReset"};
    check(sh::ActionCount==42&&sh::LegacyActionCount==31,"append-only sizes");
    for(int i=0;i<31;++i)check(sh::action_index(legacy[i])==i,"legacy index unchanged");
    for(int i=0;i<11;++i)check(sh::action_index(camera[i])==i+31&&sh::is_digital(i+31),"canonical camera index/digital shape");
    for(unsigned row=0;row<3;++row){
        const auto base=dir/("frame"+std::to_string(row));const auto original=channels(base.string()+".channels");const auto f=rec::decode(original);
        check(rec::encode(f,0,31)==original,"v2 channel bytes retain every value at playback");
        for(int i=31;i<42;++i)check(f.action[i].value==0&&f.action[i].status==sh::Status::Unbound&&f.action[i].sample==-1&&!f.action[i].injected,"whole absent extension zero-filled");
        const auto golden=bytes(base.string()+".abi2");dbce_shim_frame actual{};actual.struct_size=sizeof actual;sh::legacy_abi2_frame(f,actual);
        check(golden.size()==sizeof actual&&std::memcmp(golden.data(),&actual,sizeof actual)==0,"actual legacy ABI2 serializer bytes unchanged from baseline");
        auto extended=f;extended.down|=1ull<<41;extended.up|=1ull<<31;extended.action[41].value=1;extended.action[41].status=sh::Status::TestNeutral;
        sh::legacy_abi2_frame(extended,actual);check(std::memcmp(golden.data(),&actual,sizeof actual)==0,"ABI2 high edges/actions cannot overflow facade");
        const auto current=rec::encode(extended);check(rec::encode(rec::decode(current))==current,"v3 recording round-trip");
        for(const char* field:{"value","status","sample","injected"})for(const char* id:camera){auto partial=current;partial.erase("action."+std::string(id)+"."+field);refuses([&]{(void)rec::decode(partial);});}
        auto stray=original;stray["action.cameraForward.unknown"]=0;refuses([&]{(void)rec::decode(stray);});
        auto old_edges=original;rec::u64(old_edges,"down",1ull<<31);refuses([&]{(void)rec::decode(old_edges);});
        const auto raw=bytes(base.string()+".observer2");ob::Payload old{};check(raw.size()<=sizeof old&&raw.size()%4==0,"bounded observer fixture");std::memcpy(old.data(),raw.data(),raw.size());
        ob::Document d;check(ob::decode(old,d),"v2 observer accepted");check(d.down==f.down&&d.up==f.up&&d.frame==f.seq,"v2 observer identity/edges retained");
        for(int i=0;i<31;++i)check(d.actions[i].value==f.action[i].value&&d.actions[i].status==f.action[i].status&&d.actions[i].sample==f.action[i].sample&&d.actions[i].injected==f.action[i].injected,"v2 observer action retained");
        for(int i=31;i<42;++i)check(d.actions[i].value==0&&d.actions[i].status==sh::Status::Unbound,"observer absent camera neutral");
        d.down|=1ull<<41;d.actions[41]=extended.action[41];ob::Payload next{};check(ob::encode(d,next)&&next[1]==3,"v3 observer emitted");ob::Document round;check(ob::decode(next,round)&&round.down==d.down&&round.actions[41].value==1,"v3 observer round-trip");
        old[1]=3;check(!ob::decode(old,round),"partial v3 observer refused");old[1]=2;old[2]-=1;check(!ob::decode(old,round),"truncated v2 observer refused");
    }
    ob::Document maximum;maximum.identity.process=1;maximum.identity.process_start=1;maximum.identity.profile_devices=unsigned(ob::MaxSamples);maximum.identity.nonce[0]=maximum.identity.executable[0]=maximum.identity.build[0]=maximum.identity.profile[0]=1;
    maximum.frame=1000;maximum.tick_us=1;maximum.boundary_us=2;maximum.event_seq=sh::EventRingSize;maximum.sample_count=unsigned(ob::MaxSamples);maximum.signal_count=unsigned(ob::MaxSignals);maximum.event_count=unsigned(sh::EventRingSize);
    for(unsigned i=0;i<maximum.sample_count;++i){auto& s=maximum.samples[i];s.device=i;s.epoch=1;s.seq=1;s.read_start_us=1;s.read_end_us=2;s.poll=s.state=sh::Call::Ok;s.valid=true;}
    for(unsigned i=0;i<maximum.signal_count;++i){auto& s=maximum.signals[i];s.id=i+1;s.frame=1000;s.us=2;}
    for(unsigned i=0;i<maximum.event_count;++i){auto& e=maximum.events[i];e.seq=i+1;e.device=-1;}
    ob::Payload maximal{};check(ob::encode(maximum,maximal),"maximum v3 observation fits transport");ob::Document full;check(ob::decode(maximal,full)&&full.sample_count==maximum.sample_count,"maximum v3 round-trip");
    sh::fake::Provider provider;sh::fake::Device device;const std::string instance="{d71b8350-61b7-11f1-8001-444553540000}",product="{0006346e-0000-0000-0000-504944564944}";
    device.listed={instance,product,"Scripted camera controls"};device.caps=sh::fake::wheel_caps();const auto slot=provider.add(device);
    std::string profile="[Controls]\nSchema=1\nProfile=Cameras\nRevision=1\nTransmission=Sequential\n";
    for(int i=0;i<11;++i)profile+=std::string(camera[i])+"=button "+std::to_string(i)+" dev="+instance+" prod="+product+"\n";
    sh::Core core(provider,sh::CoreConfig{});core.set_profile(sh::read_profile(profile));const auto tick=[&]()->const sh::Frame&{core.service_devices();provider.advance(10000);return core.tick();};
    for(int i=0;i<4;++i)(void)tick();
    for(int i=0;i<11;++i){provider.at(slot).state.buttons[i]=0x80;const auto& down=tick();check(down.action[i+31].status==sh::Status::Live&&down.action[i+31].value==1&&(down.down&(1ull<<(i+31))),"actual Core consumes camera button at high index");provider.at(slot).state.buttons[i]=0;const auto& up=tick();check((up.up&(1ull<<(i+31)))!=0,"actual Core preserves high release bit");}
    std::cout<<"camera contract compatibility: "<<checks<<" checks\n";return 0;
}catch(const std::exception& e){std::cerr<<"camera compatibility: "<<e.what()<<"\n";return 1;}}
