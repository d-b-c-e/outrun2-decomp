// Copied memory only: no DirectInput object, game, install, or force output.
#include "bridge_provider.hpp"
#include "dbce_shim_bridge.h"
#include <windows.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace dbce::shim;
namespace {
int checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
const std::string W="{d71b8350-61b7-11f1-8001-444553540000}",P="{0006346e-0000-0000-0000-504944564944}";
const std::string Profile="[Controls]\nSchema = 1\nProfile = Bridge\nRevision = r1\nTransmission = Sequential\nsteer = axis 0 dev="+W+" prod="+P+" range=0..65535 rest=32768 travel=+1\n";
BridgeDevice device(std::uint64_t generation=1){BridgeDevice d;d.identity={W,P,"copied wheel"};d.generation=generation;d.caps.axis[0]=d.caps.range_ok[0]=true;d.caps.max[0]=65535;return d;}
BridgeSample sample(std::uint64_t sequence,std::int64_t end,std::uint64_t generation=1){BridgeSample s;s.source={generation,sequence,end-10,end};s.poll=s.state=Call::Ok;s.raw.axes[0]=32768;return s;}
void provider(){
    std::int64_t now=1000000;BridgeProvider p([&]{return now;});std::string why;
    auto d=device();d.identity.interface_path="\\\\?\\hid#owned-slot";
    check(p.devices({d},why),"qualified device");auto opened=p.open(d.identity);check(opened.ok,"memory lease");
    check(opened.opened.interface_path==d.identity.interface_path,"copied lease carries exact owned path");
    auto changed=d;changed.identity.interface_path="\\\\?\\hid#another-slot";
    check(!p.devices({changed},why),"path immutable within source generation");
    auto s=sample(1,now-20);check(p.samples({s},why),"first physical sample");s.raw.axes[0]=65535;RawState raw;SourceRead source;
    check(p.poll(opened.handle)==Call::Ok && p.state(opened.handle,raw)==Call::Ok && raw.axes[0]==32768,"input buffers copied");
    check(p.source_read(opened.handle,source)&&source.sequence==1&&source.end_us==now-20,"original provenance");
    check(!p.samples({s},why)&&p.poll(opened.handle)==Call::Pending,"repeat clears delivery");
    check(!p.samples({sample(2,now+1)},why),"future sample refused");
    check(!p.samples({sample(2,now-100)},why),"time regression refused");
    check(!p.samples({sample(2,now),sample(3,now)},why),"duplicate generation refused atomically");
    check(p.samples({sample(2,now)},why),"failed submission did not consume sequence");p.finish_frame();
    check(p.poll(opened.handle)==Call::Pending&&!p.source_read(opened.handle,source),"no cached freshness");
    d.caps.max[0]=100;check(!p.devices({d},why),"caps immutable within generation");
    check(p.devices({device(2)},why)&&p.poll(opened.handle)==Call::Gone,"new generation retires old lease");
    check(!p.devices({device(1)},why),"retired generation cannot return");
    auto next=p.open(device(2).identity);auto failed=sample(1,now,2);failed.poll=Call::AcquisitionLost;failed.state=Call::NotCalled;
    check(p.samples({failed},why)&&p.poll(next.handle)==Call::AcquisitionLost,"failed physical call preserved");
    check(p.reacquire(next.handle)==Call::Gone&&!p.open(device(2).identity).ok,"bridge never reacquires external handle");
    auto r=p.shutdown(0);check(r.input_released&&p.enumerate().empty()&&!p.devices({device(3)},why),"bounded memory shutdown");
}
void core(){
    std::int64_t now=1000000;BridgeProvider p([&]{return now;});std::string why;Core c(p,{});c.set_profile(read_profile(Profile));
    auto d=device();d.identity.interface_path="\\\\?\\hid#core-owned-slot";
    check(p.devices({d},why),"core descriptors");c.service_devices();
    check(p.samples({sample(1,now-200000)},why),"old original source accepted as evidence");
    const auto& old=c.tick();check(old.samples.size()==1&&old.samples[0].stale&&old.action[0].status==Status::Stale,"Core uses original age, not copy time");
    check(c.device_reports()[0].identity.interface_path==d.identity.interface_path,"Core reports copied owned path");
    check(old.samples[0].source_generation==1&&old.samples[0].source_sequence==1&&old.samples[0].read_end_us==now-200000,"Core retains source sequence and epoch");
    check(p.samples({sample(2,now)},why),"fresh physical read");const auto& fresh=c.tick();check(fresh.action[0].status==Status::Live,"fresh centre Live");
    now+=10000;auto right=sample(3,now);right.raw.axes[0]=65535;check(p.samples({right},why)&&c.tick().action[0].value==1.f,"resolver drives endpoint");
    p.finish_frame();check(c.tick().action[0].status==Status::ReadPending&&c.tick().action[0].value==0.f,"omission cannot replay prior axis");
}
void latched_core(){
    std::int64_t now=1000000;BridgeProvider p([&]{return now;});std::string why;
    CoreConfig cfg;cfg.latched=true;Core c(p,cfg);c.set_profile(read_profile(Profile));
    check(p.devices({device()},why),"latched Core descriptors");c.service_devices();
    c.request_admission(Session{"bridgecheck1",1000600},1000000,true,true);
    auto physical=sample(1,now);physical.raw.axes[0]=65535;check(p.samples({physical},why),"copied physical endpoint");
    const auto waiting=c.tick();
    check(waiting.run==Run::Waiting&&waiting.suppress_native&&waiting.action[0].status==Status::TestNeutral&&waiting.action[0].value==0&&!waiting.action[0].injected,"copied physical input stays test-neutral, native suppressed");
    check(waiting.samples.size()==1&&waiting.samples[0].source_generation==1&&waiting.samples[0].source_sequence==1&&waiting.samples[0].read_end_us==now,"latched physical provenance retained separately");
    check(c.offer_batch("nonce=bridgecheck1\nbatch=1\ninject action steer -0.5 ms=3000\n").verdict!=BatchVerdict::Refused,"latched Core batch accepted");
    now+=10000;physical=sample(2,now);physical.raw.axes[0]=65535;check(p.samples({physical},why),"second physical endpoint");
    const auto running=c.tick();
    check(running.run==Run::Running&&running.action[0].injected&&std::fabs(running.action[0].value+.5f)<1e-3f&&running.samples[0].source_sequence==2,"only accepted command is injected, source sequence remains physical");
    check(running.samples[0].physical.axes[0]==65535&&running.samples[0].delivered.axes[0]!=65535&&running.samples[0].injected.axes==1,"injection marks delivered copy without rewriting original physical sample");
    check(c.offer_batch("nonce=bridgecheck1\nbatch=2\nend\n").verdict!=BatchVerdict::Refused,"end accepted");
    now+=10000;physical=sample(3,now);physical.raw.axes[0]=65535;check(p.samples({physical},why),"physical endpoint after end");
    const auto ended=c.tick();check(ended.run==Run::Completed&&ended.suppress_native&&ended.down==0&&std::all_of(ended.action.begin(),ended.action.end(),[](const auto& a){return a.value==0;}),"terminal frame is neutral and native remains suppressed");
    check(c.offer_batch("nonce=bridgecheck1\nbatch=3\ninject action steer 1 ms=3000\n").verdict==BatchVerdict::Refused,"terminal run cannot rearm");
}
std::int64_t clock_us(){LARGE_INTEGER f{},n{};QueryPerformanceFrequency(&f);QueryPerformanceCounter(&n);return n.QuadPart/f.QuadPart*1000000+n.QuadPart%f.QuadPart*1000000/f.QuadPart;}
void abi(){
    check(dbce_shim_abi_version()==2&&dbce_shim_frame_size()==296&&dbce_shim_open_args_size()==128,"base ABI unchanged");
    check(dbce_shim_bridge_abi_version()==1&&dbce_shim_bridge_device_size()==144&&dbce_shim_bridge_sample_size()==232,"extension layout");
    const auto dir=std::filesystem::temp_directory_path()/("dbce-bridge-"+std::to_string(GetCurrentProcessId()));std::filesystem::create_directories(dir);const auto folder=dir.wstring();
    dbce_shim_open_args a{};a.struct_size=sizeof(a);a.folder=folder.c_str();a.nonce="bridgecheck1";a.profile=Profile.data();a.profile_len=int(Profile.size());
    void* h=nullptr;char why[512]{};a.flags=DBCE_SHIM_CAPTURE;
    check(dbce_shim_bridge_open(&a,&h,why,sizeof(why))==DBCE_SHIM_REFUSED&&!h,"capture source format deliberately refused");a.flags=0;
    check(dbce_shim_bridge_open(&a,&h,why,sizeof(why))==DBCE_SHIM_OK&&h,"bridge opens with no native input");
    dbce_shim_bridge_device_v1 d{};d.struct_size=sizeof(d);d.abi=1;d.generation=1;d.axis_mask=d.range_mask=1;d.maximum[0]=65535;
    const GUID wg={0xd71b8350,0x61b7,0x11f1,{0x80,1,0x44,0x45,0x53,0x54,0,0}},pg={0x0006346e,0,0,{0,0,0x50,0x49,0x44,0x56,0x49,0x44}};
    std::memcpy(d.instance,&wg,16);std::memcpy(d.product,&pg,16);
    check(dbce_shim_bridge_devices_with_paths(h,&d,1,nullptr)==DBCE_SHIM_BAD_ARGUMENT,"path list required");
    const wchar_t* bad[]={L""};check(dbce_shim_bridge_devices_with_paths(h,&d,1,bad)==DBCE_SHIM_BAD_ARGUMENT,"empty path refused atomically");
    wchar_t path[]=L"\\\\?\\hid#copied-owned-slot";const wchar_t* paths[]={path};
    check(dbce_shim_bridge_devices_with_paths(h,&d,1,paths)==DBCE_SHIM_OK,"copied ABI descriptors and paths");
    path[8]=L'X';check(dbce_shim_bridge_devices_with_paths(h,&d,1,paths)==DBCE_SHIM_REFUSED,"source path immutable");
    dbce_shim_frame f{};f.struct_size=sizeof(f);check(dbce_shim_tick(h,&f)==DBCE_SHIM_BAD_ARGUMENT,"base tick refuses copied handle");
    dbce_shim_bridge_sample_v1 s{};s.struct_size=sizeof(s);s.abi=1;s.generation=1;s.valid=1;s.axes[0]=32768;
    bool live=false;for(int i=0;i<400&&!live;++i){++s.sequence;s.read_start_us=s.read_end_us=clock_us();check(dbce_shim_bridge_tick(h,&s,1,&f)==DBCE_SHIM_OK,"copied ABI tick");live=f.action[0].status==uint8_t(Status::Live);if(!live)Sleep(5);}
    check(live,"bridge Host reaches Live");
    char reports[4096]{};int needed=0;check(dbce_shim_device_reports(h,reports,sizeof(reports),&needed)==DBCE_SHIM_OK &&
        std::string(reports).find("hid#copied-owned-slot")!=std::string::npos,"owned interfacePath JSON retained copied bytes");
    check(dbce_shim_bridge_tick(h,&s,1,&f)==DBCE_SHIM_REFUSED,"ABI repeat refused");
    int other=0;std::thread([&]{other=dbce_shim_bridge_tick(h,nullptr,0,&f);}).join();check(other==DBCE_SHIM_WRONG_THREAD,"owner thread retained");
    check(dbce_shim_bridge_tick(h,nullptr,0,&f)==DBCE_SHIM_OK&&f.action[0].value==0,"missing next physical read neutral");
    check(dbce_shim_stop(h,why,sizeof(why),nullptr)==DBCE_SHIM_OK,"bridge Host resolved stop");
    check(dbce_shim_bridge_tick(h,nullptr,0,&f)==DBCE_SHIM_STOPPED,"spent bridge handle");
}
void latched_abi(){
    const auto dir=std::filesystem::temp_directory_path()/("dbce-bridge-latched-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    std::filesystem::create_directories(dir);const auto folder=dir.wstring(),session=(dir/"inject.on").wstring(),batch=(dir/"inject.txt").wstring();
    const auto expires=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()+600;
    {std::ofstream file(dir/"inject.on",std::ios::binary);file<<"nonce=bridgecheck1\nexpires="<<expires<<"\n";}
    dbce_shim_open_args a{};a.struct_size=sizeof(a);a.folder=folder.c_str();a.nonce="bridgecheck1";a.profile=Profile.data();a.profile_len=int(Profile.size());a.adapter_version="bridge-latched-test";
    a.session_file=session.c_str();a.batch_file=batch.c_str();a.fences_verified=a.force_absent=1;
    void* h=nullptr;char why[1024]{};
    a.flags=8;check(dbce_shim_bridge_open(&a,&h,why,sizeof(why))==DBCE_SHIM_BAD_ARGUMENT&&!h,"unknown flags refused");
    a.flags=DBCE_SHIM_LATCHED|DBCE_SHIM_CAPTURE;check(dbce_shim_bridge_open(&a,&h,why,sizeof(why))==DBCE_SHIM_REFUSED&&!h,"latched cannot bypass capture format refusal");
    a.flags=DBCE_SHIM_LATCHED;
    for(const auto value:{0,2}){
        a.fences_verified=uint8_t(value);check(dbce_shim_bridge_open(&a,&h,why,sizeof(why))==DBCE_SHIM_REFUSED&&!h,"fences require exactly one");a.fences_verified=1;
        a.force_absent=uint8_t(value);check(dbce_shim_bridge_open(&a,&h,why,sizeof(why))==DBCE_SHIM_REFUSED&&!h,"force absent requires exactly one");a.force_absent=1;
    }
    a.session_file=nullptr;check(dbce_shim_bridge_open(&a,&h,why,sizeof(why))==DBCE_SHIM_REFUSED&&!h,"session path required");a.session_file=session.c_str();
    a.batch_file=nullptr;check(dbce_shim_bridge_open(&a,&h,why,sizeof(why))==DBCE_SHIM_REFUSED&&!h,"batch path required");a.batch_file=batch.c_str();
    check(dbce_shim_bridge_open(&a,&h,why,sizeof(why))==DBCE_SHIM_OK&&h,"latched copied host opens with fences and no DirectInput");
    dbce_shim_bridge_device_v1 d{};d.struct_size=sizeof(d);d.abi=1;d.generation=9;d.axis_mask=d.range_mask=1;d.maximum[0]=65535;
    const GUID wg={0xd71b8350,0x61b7,0x11f1,{0x80,1,0x44,0x45,0x53,0x54,0,0}},pg={0x0006346e,0,0,{0,0,0x50,0x49,0x44,0x56,0x49,0x44}};
    std::memcpy(d.instance,&wg,16);std::memcpy(d.product,&pg,16);check(dbce_shim_bridge_devices(h,&d,1)==DBCE_SHIM_OK,"latched copied metadata");
    dbce_shim_bridge_sample_v1 s{};s.struct_size=sizeof(s);s.abi=1;s.generation=9;s.valid=1;s.axes[0]=65535;
    dbce_shim_frame f{};f.struct_size=sizeof(f);
    auto tick=[&]{++s.sequence;s.read_start_us=s.read_end_us=clock_us();if(dbce_shim_bridge_tick(h,&s,1,&f)!=DBCE_SHIM_OK)throw std::runtime_error("latched copied ABI tick");};
    auto until=[&](Run run){for(int i=0;i<600;++i){tick();if(f.run==uint8_t(run))return true;Sleep(5);}return false;};
    check(until(Run::Waiting)&&f.latched&&f.suppress_native&&f.action[0].value==0&&!f.action[0].injected,"real control worker admission leaves copied physical endpoint neutral");
    {std::ofstream file(dir/"inject.txt",std::ios::binary);file<<"nonce=bridgecheck1\nbatch=1\ninject action steer -0.5 ms=3000\n";}
    check(until(Run::Running)&&f.action[0].injected&&std::fabs(f.action[0].value+.5f)<1e-3f,"real control worker injects accepted batch");
    {std::ofstream file(dir/"next.txt",std::ios::binary);file<<"nonce=bridgecheck1\nbatch=2\nend\n";}
    check(MoveFileExW((dir/"next.txt").c_str(),(dir/"inject.txt").c_str(),MOVEFILE_REPLACE_EXISTING)!=FALSE,"atomic next batch");
    check(until(Run::Completed)&&f.suppress_native&&f.down==0&&std::all_of(std::begin(f.action),std::end(f.action),[](const auto& v){return v.value==0;}),"end produces terminal neutral native-suppressed frame");
    tick();check(f.run==uint8_t(Run::Completed)&&f.suppress_native&&f.action[0].value==0,"physical endpoint cannot return after completion");
    check(dbce_shim_stop(h,why,sizeof(why),nullptr)==DBCE_SHIM_OK,"latched bridge control journal and memory provider resolve stop");
    check(dbce_shim_bridge_tick(h,nullptr,0,&f)==DBCE_SHIM_STOPPED,"latched bridge handle spent");
    std::ifstream journal(dir/"controls-journal.jsonl",std::ios::binary);std::string text((std::istreambuf_iterator<char>(journal)),{});
    check(text.find("\"inputSource\":\"external-physical-input@1\"")!=std::string::npos&&text.find("\"profileSha256\":\""+hex(read_profile(Profile).file_hash)+"\"")!=std::string::npos&&text.find(W)!=std::string::npos,"journal names exact Controls hash/device and versioned external physical source");
    check(text.find("\"complete\":true")!=std::string::npos&&text.find("completed")!=std::string::npos,"complete control journal records native-suppressed terminal run");
    std::cout<<"latched journal: "<<(dir/"controls-journal.jsonl").string()<<"\n";
}
}
int main(){try{provider();core();latched_core();abi();latched_abi();std::cout<<"PASS: "<<checks<<" copied-provider/Core/ABI checks; no device acquisition or force\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
