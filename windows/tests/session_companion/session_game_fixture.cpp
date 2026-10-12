// The device-free stand-in for OutRunDesktop.exe in the companion's fixtures (test_companion.ps1): the SAME intake
// (session::take) and recorder (OrdinaryRecorder) as the game, the same request-bound stop and receipt rules as
// shim_input.cpp, fed synthetic consecutive rows at about 60 Hz instead of a race. No window, no DirectInput, no game
// data. Like the owner closing the game, it ends only when <exe dir>\fixture-close.txt appears (its first line is the
// exit code), never because the companion asks. <exe dir>\fixture-options.txt (one per line, optional):
//   ignore-request          never takes the request (the companion's admission bound)
//   seconds=<s>             the recorder's limit (the limit stop)
//   crash-after-saved=<c>   ends with code c without the exit path once the recording is saved (an abnormal exit)
#include "ordinary_recording.hpp"
#include <windows.h>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string>
using namespace outrun::desktop;
namespace fs=std::filesystem;
namespace {
std::string slurp(const fs::path& p){std::ifstream f(p,std::ios::binary);return std::string((std::istreambuf_iterator<char>(f)),{});}
fs::path exe_dir(){std::wstring p(32768,L'\0');p.resize(GetModuleFileNameW(nullptr,p.data(),32768));return fs::path(p).parent_path();}
bool line(const std::string& text,const std::string& want){
    std::size_t at=0;
    while(at<=text.size()){
        auto end=text.find('\n',at);if(end==std::string::npos)end=text.size();
        auto l=text.substr(at,end-at);while(!l.empty()&&(l.back()=='\r'||l.back()==' '))l.pop_back();
        if(l==want)return true;
        at=end+1;
    }
    return false;
}
std::string option(const std::string& options,const std::string& key){
    const auto at=options.find(key+"=");if(at==std::string::npos)return {};
    auto end=options.find_first_of("\r\n",at);return options.substr(at+key.size()+1,(end==std::string::npos?options.size():end)-at-key.size()-1);
}
}
int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const auto dir=exe_dir();
    const auto options=slurp(dir/"fixture-options.txt");
    const bool shim=line(slurp(dir/"OutRunDesktop.ini"),"Backend = Shim");
    OrdinaryRecorder rec;
    if(!line(options,"ignore-request")){
        const auto t=session::take(session::request_path(),dir,shim,std::uint64_t(std::time(nullptr)));
        std::printf("fixture: intake %d %s\n",int(t.kind),t.why.c_str());
        if(t.kind==session::Intake::Kind::Exit)return 4;
        if(t.kind==session::Intake::Kind::Record){
            OrdinaryRecorder::Arm a;a.request=t.request;a.profile="[Controls]\nsteer=axis:X\n";a.exe_sha256=t.exe_sha256;
            a.adapter_version="session companion fixture";
            if(const auto s=option(options,"seconds");!s.empty())a.seconds=std::stod(s);
            rec=OrdinaryRecorder(a);
            dbce::shim::DeviceReport d;d.identity.name="Fixture Wheel";d.identity.dev="{00000000-0000-0000-0000-0000000000f1}";
            d.identity.prod="{00000000-0000-0000-0000-0000000000f2}";d.opened=true;d.epoch=1;d.status="open";
            rec.set_devices({d});
            rec.publish_status(true);
        }
    }
    const auto crash=option(options,"crash-after-saved");
    for(std::uint64_t tick=1;;++tick){
        if(rec.recording()){
            Snapshot s;s.frame.seq=tick;s.frame.tick_us=std::int64_t(tick)*16667;s.frame.action[0].value=0.25f;
            s.frame.action[0].status=dbce::shim::Status::Live;s.signals.frame_seq=tick;s.signals.mode=16;s.signals.in_gameplay=true;
            s.view.mode=16;s.view.car=true;s.has_consumed=true;s.consumed.connected=true;
            rec.push(s);
            if(rec.at_limit())rec.stop("limit");
        }
        if(tick%30==0&&rec.recording()){   // the request-bound stop, as shim_input.cpp stop_requested
            auto id=slurp(rec.request()->result/L"stop.request");
            while(!id.empty()&&(id.back()=='\r'||id.back()=='\n'||id.back()==' '))id.pop_back();
            if(id==rec.request()->request_id)rec.stop("stop-request");
        }
        if(tick%30==0&&rec.state()==OrdinaryRecorder::State::Complete&&!rec.saved()&&rec.observe_receipt())std::printf("fixture: saved\n");
        rec.publish_status();
        if(!crash.empty()&&rec.saved()){std::printf("fixture: abnormal end %s\n",crash.c_str());TerminateProcess(GetCurrentProcess(),UINT(std::stoul(crash)));}
        if(tick%15==0&&fs::exists(dir/"fixture-close.txt")){
            const auto code=std::stoi("0"+slurp(dir/"fixture-close.txt"));
            if(rec.recording())rec.stop("exit");
            if(rec.armed()){
                (void)rec.wait_saved(dbce::shim::hosting::Clock::now()+std::chrono::seconds(65));
                if(rec.state()==OrdinaryRecorder::State::Complete)(void)rec.observe_receipt();
                rec.publish_status();
            }
            std::printf("fixture: closed (%d)\n",code);
            return code;
        }
        Sleep(16);
    }
}
