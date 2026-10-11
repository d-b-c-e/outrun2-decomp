#include "provider_host.hpp"
#include "shim_fake_com.hpp"
#include <filesystem>
#include <iostream>
namespace h=dbce::shim::hosting;
namespace c=dbce::shim::control;
using namespace dbce::shim;
namespace {
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
void CALLBACK tail(void* gate){WaitForSingleObject(static_cast<HANDLE>(gate),INFINITE);}
struct NoCapture {
    void finish(){}
    struct Drained{h::RecOutcome outcome;bool joined;};
    Drained drain(h::Clock::time_point){return {h::RecOutcome::Complete,true};}
};
struct Io final:c::Io {
    const std::string mode,dev;
    HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),gate=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::atomic<int> appends{0},finishes{0},accepted{0},closed_events{0};
    Io(std::string m,std::string d):mode(std::move(m)),dev(std::move(d)){}
    ~Io(){CloseHandle(entered);CloseHandle(gate);}
    c::Read read_session()override{return {c::ReadState::Bytes,"nonce=ControlWorkerFixture\nexpires=2000\n",{}};}
    c::Read read_batch()override{
        if(mode=="read-blocked"||mode=="late-read"){SetEvent(entered);WaitForSingleObject(gate,INFINITE);}
        return {c::ReadState::Bytes,"nonce=ControlWorkerFixture\nbatch=1\ninject raw button 3 dev="+dev+" value=1 ms=5000\n",{}};
    }
    std::uint64_t unix_seconds()override{return 1000;}
    bool append(const c::Journal& j)override{++appends;for(const auto& e:j.events){
        if(e.kind==EventKind::BatchAccepted)++accepted;
        if(e.kind==EventKind::RunChanged&&e.detail=="failed: control admission closed")++closed_events;
    }return mode!="write-failed";}
    bool finish()override{
        ++finishes;
        if(mode=="finish-blocked"){SetEvent(entered);WaitForSingleObject(gate,INFINITE);}
        if(mode=="thread-tail"){
            DWORD index=FlsAlloc(tail);require(index!=FLS_OUT_OF_INDEXES,"FLS fixture slot");require(FlsSetValue(index,gate)!=FALSE,"FLS fixture tail installed");
        }
        return mode!="finish-failed";
    }
};
int child(const std::string& mode,const std::wstring& folder){
    require(std::filesystem::create_directory(folder),"fresh child folder");
    const GUID wi=fakecom::make_guid(0xd71b8350u,0x61b7),wp=fakecom::make_guid(0x0006346eu,0);
    auto* in=new fakecom::Input;fakecom::DeviceScript script;script.instance=wi;script.product=wp;script.name=L"Fake wheel";in->add(script);
    const std::string profile="[Controls]\nSchema=1\nconfirm=button 3 dev="+guid_text(wi)+" prod="+guid_text(wp)+"\n";
    auto host=std::make_unique<h::ProviderHost<NoCapture>>(folder,"ControlWorkerFixture",in,profile,true,std::make_unique<NoCapture>());
    auto io=std::make_shared<Io>(mode,guid_text(wi));host->start_control(io,{true,true,true,"ControlWorkerFixture"});
    bool observed=false;
    for(int i=0;i<400;++i){
        const auto& f=host->tick();
        if((mode=="read-blocked"||mode=="late-read")&&WaitForSingleObject(io->entered,0)==WAIT_OBJECT_0){observed=true;break;}
        if(mode=="write-failed"&&f.run==Run::Failed){observed=true;break;}
        if(f.run==Run::Running&&f.action[std::size_t(action_index("confirm"))].value==1){observed=true;break;}
        Sleep(5);
    }
    require(observed,"control path observed before stop");
    host->close_control_admission();
    if(mode!="missing-final-tick"){
        const auto& neutral=host->tick();
        require(neutral.suppress_native&&neutral.action[std::size_t(action_index("confirm"))].value==0,"final neutral Core frame");
    }
    h::Budget budget;budget.control_ms=100;
    auto report=host->stop(budget);
    require(report.control_present,"control worker reported");
    require(in->counters.force_calls==0&&in->counters.release_during_read==0&&in->counters.after_release==0,"no force or lifetime misuse");
    if(mode=="resolved"){
        require(report.resolved&&report.control_joined&&report.control_complete,"normal worker completion");
        require(io->accepted==1&&io->closed_events==1&&io->finishes==1,"final journal and sink close observed");
        require(host->stop().elapsed_us==report.elapsed_us,"stop receipt immutable");
        host.reset();return 0;
    }
    require(!report.resolved,"fault cannot qualify host");
    if(mode=="missing-final-tick")require(report.control_joined&&report.control_complete&&!report.control_final_tick&&io->closed_events==0,"missing closure tick distinguished from sink completion");
    if(mode=="late-read"){
        SetEvent(io->gate);Sleep(150);
        require(io->accepted==0,"late read cannot activate or log an accepted batch");
        auto late=host->stop();require(!late.control_joined&&!late.resolved&&late.elapsed_us==report.elapsed_us,"late completion never upgrades cached timeout");
    }
    if(mode=="write-failed"||mode=="finish-failed")require(report.control_joined&&!report.control_complete,"joined failed journal distinguished");
    if(mode=="thread-tail")require(!report.control_joined&&report.control_complete,"sink returned but OS thread tail unjoined");
    if(mode=="read-blocked"||mode=="finish-blocked"||mode=="late-read")require(!report.control_joined&&!report.control_complete,"blocked worker incomplete and unjoined");
    host->park_for_supervisor();
}
}
int wmain(int argc,wchar_t** argv){try{require(argc==3,"mode and fresh child folder required");std::wstring wmode=argv[1];return child(std::string(wmode.begin(),wmode.end()),argv[2]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
