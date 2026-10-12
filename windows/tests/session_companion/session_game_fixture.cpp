// The device-free stand-in for OutRunDesktop.exe in the companion's fixtures (test_companion.ps1): the SAME intake
// (session::take) and recorder (OrdinaryRecorder) as the game, the same request-bound stop and receipt rules as
// shim_input.cpp, fed synthetic consecutive rows at about 60 Hz instead of a race. No DirectInput and no game data; one
// hidden window, so a WM_CLOSE to the process's windows (the playback companion's normal close) ends it through its exit
// path. A recording also ends only when <exe dir>\fixture-close.txt appears (its first line is the exit code), never
// because the companion asks. <exe dir>\fixture-options.txt (one per line, optional):
//   ignore-request          never takes the request (the companion's admission bound)
//   seconds=<s>             the recorder's limit (the limit stop)
//   crash-after-saved=<c>   ends with code c without the exit path once the recording is saved (an abnormal exit)
//   still                   a recording's car never moves (not admitted for playback)
//   diverge-at=<row>        playback: the game's speed differs from the recording's at that tape row
//   owner-write             playback: appends to the game folder's options.ini (what a playback must never do)
//   tick-ms=<ms>            the tick's sleep (16 by default; a slow playback for the cancel case)
// Playback (session::Intake::Kind::Playback): the exported tape's rows played one per tick, the game's channels taken from
// the tape itself (so a faithful replay compares equal), then shim_input.hpp's TapeEndRows neutral rows and "tape-end".
#include "ordinary_recording.hpp"
#include "consumed_replay.hpp"
#include <windows.h>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace outrun::desktop;
namespace fs=std::filesystem;
namespace {
constexpr std::uint64_t TapeEndRows=60;   // shim_input.hpp
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
bool close_requested=false;
LRESULT CALLBACK window_proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_CLOSE){close_requested=true;return 0;}   // ended through the exit path, as the game's own close
    return DefWindowProcW(h,m,w,l);
}
double number(const rec::Channels& c,const char* k){const auto i=c.find(k);return i==c.end()?0.0:i->second;}
// A tape row as the game observed it: what it consumed (the replayed words), its view and the car's pose.
Snapshot from_tape(const rec::Channels& c,std::uint64_t tick){
    Snapshot s;s.frame.seq=tick;s.frame.tick_us=std::int64_t(tick)*16667;s.frame.boundary_us=s.frame.tick_us;
    s.signals.frame_seq=tick;s.signals.mode=std::uint32_t(number(c,"gameInput.mode"));
    const auto r=decoded_replay(c);s.has_consumed=true;s.consumed=r.input;s.pc_drive=r.pc_drive;
    s.view.mode=std::uint32_t(number(c,"gameInput.mode"));s.view.car=number(c,"gameInput.car")!=0;s.view.gear=std::uint32_t(number(c,"gameInput.gear"));
    s.view.speed=float(number(c,"gameInput.speed"));
    s.view.consumed_steer=std::int32_t(number(c,"gameInput.consumedSteer"));s.view.consumed_accel=std::int32_t(number(c,"gameInput.consumedAccel"));
    s.view.consumed_brake=std::int32_t(number(c,"gameInput.consumedBrake"));
    auto& p=s.pose;p.valid=number(c,"gameInput.poseValid")==1;
    p.position={float(number(c,"gameInput.posX")),float(number(c,"gameInput.posY")),float(number(c,"gameInput.posZ"))};
    p.vec20={float(number(c,"gameInput.vec20X")),float(number(c,"gameInput.vec20Y")),float(number(c,"gameInput.vec20Z"))};
    for(int i=0;i<9;++i){const std::string k="gameInput.rot"+std::to_string(i/3)+std::to_string(i%3);p.rotation[std::size_t(i)]=float(number(c,k.c_str()));}
    p.model=std::uint32_t(number(c,"gameInput.carModel"));p.stage_key=std::uint32_t(number(c,"gameInput.stageKey"));
    p.course_gate=std::uint32_t(number(c,"gameInput.courseGate"));
    return s;
}
}
int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const auto dir=exe_dir();
    const auto options=slurp(dir/"fixture-options.txt");
    const bool shim=line(slurp(dir/"OutRunDesktop.ini"),"Backend = Shim");
    WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"SessionGameFixture";
    RegisterClassW(&wc);
    (void)CreateWindowExW(0,wc.lpszClassName,L"session game fixture",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);   // never shown
    OrdinaryRecorder rec;
    std::vector<rec::Channels> tape;bool playback=false;
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
        if(t.kind==session::Intake::Kind::Playback){
            playback=true;
            try{rec::read_export(t.request.tape,[&](const rec::Channels& c){tape.push_back(c);});}catch(const std::exception& e){std::printf("fixture: tape %s\n",e.what());}
            OrdinaryRecorder::Arm a;a.request=t.request;a.profile="[Controls]\nsteer=axis:X\n";
            a.exe_sha256=t.exe_sha256;a.adapter_version="session companion fixture";a.seconds=playback_capture_seconds(tape.size(),TapeEndRows);
            rec=OrdinaryRecorder(a);rec.set_devices({playback_no_device()});
            if(tape.empty())rec.fail("the tape cannot be played: no rows");
            rec.publish_status(true);
        }
    }
    const auto crash=option(options,"crash-after-saved");
    const bool still=line(options,"still"),owner_write=line(options,"owner-write");
    const auto diverge=option(options,"diverge-at");const long long diverge_at=diverge.empty()?-1:std::stoll(diverge);
    const auto tick_ms=option(options,"tick-ms");const DWORD sleep_ms=tick_ms.empty()?16:DWORD(std::stoul(tick_ms));
    bool wrote_owner=false;
    for(std::uint64_t tick=1;;++tick){
        for(MSG m;PeekMessageW(&m,nullptr,0,0,PM_REMOVE);){TranslateMessage(&m);DispatchMessageW(&m);}
        if(rec.recording()&&!playback){
            Snapshot s;s.frame.seq=tick;s.frame.tick_us=std::int64_t(tick)*16667;s.frame.action[0].value=0.25f;
            s.frame.action[0].status=dbce::shim::Status::Live;s.signals.frame_seq=tick;s.signals.mode=16;s.signals.in_gameplay=true;
            s.view.mode=16;s.view.car=true;s.has_consumed=true;s.consumed.connected=true;
            // The active car, moving along x unless "still" (a recording admitted for playback needs a moved car).
            s.pose.valid=true;s.pose.position={still?0.f:0.5f*float(tick),0.f,-16.f};s.pose.rotation={1,0,0,0,1,0,0,0,1};s.pose.model=1;
            rec.push(s);
            if(rec.at_limit())rec.stop("limit");
        }
        if(rec.recording()&&playback){
            const auto row=tick-1;
            Snapshot s;
            if(row<tape.size()){s=from_tape(tape[std::size_t(row)],tick);s.tape_row=std::int64_t(row);
                if(std::int64_t(row)==diverge_at)s.view.speed+=1.0f;}
            else{s=from_tape(tape.back(),tick);s.tape_row=-1;s.consumed=outrun::platform::NativeInputState{};s.consumed.connected=true;s.pc_drive=false;}
            rec.push(s);
            if(owner_write&&!wrote_owner&&row==tape.size()/2){wrote_owner=true;std::ofstream f(dir/"options.ini",std::ios::app);f<<"; written during a playback\n";}
            if(row+1>=tape.size()+TapeEndRows)rec.stop("tape-end");
        }
        if(tick%30==0&&rec.recording()&&!playback){   // the request-bound stop, as shim_input.cpp stop_requested
            auto id=slurp(rec.request()->result/L"stop.request");
            while(!id.empty()&&(id.back()=='\r'||id.back()=='\n'||id.back()==' '))id.pop_back();
            if(id==rec.request()->request_id)rec.stop("stop-request");
        }
        if(tick%30==0&&rec.state()==OrdinaryRecorder::State::Complete&&!rec.saved()&&rec.observe_receipt())std::printf("fixture: saved\n");
        rec.publish_status();
        if(!crash.empty()&&rec.saved()){std::printf("fixture: abnormal end %s\n",crash.c_str());TerminateProcess(GetCurrentProcess(),UINT(std::stoul(crash)));}
        if(close_requested||(tick%15==0&&fs::exists(dir/"fixture-close.txt"))){
            const auto code=close_requested?0:std::stoi("0"+slurp(dir/"fixture-close.txt"));
            if(rec.recording())rec.stop("exit");
            if(rec.armed()){
                (void)rec.wait_saved(dbce::shim::hosting::Clock::now()+std::chrono::seconds(65));
                if(rec.state()==OrdinaryRecorder::State::Complete)(void)rec.observe_receipt();
                rec.publish_status();
            }
            std::printf("fixture: closed (%d%s)\n",code,close_requested?", WM_CLOSE":"");
            return code;
        }
        Sleep(sleep_ms);
    }
}
